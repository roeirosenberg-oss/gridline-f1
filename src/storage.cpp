#include "storage.h"
#include <LittleFS.h>
#include <sys/stat.h>
#include "gl_logic.h"
#include "log.h"

static bool s_ok = false;

// LittleFS.exists() logs an error for missing files; stat() is silent.
static bool fileExists(const char* path) {
  char full[40];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return stat(full, &st) == 0;
}

bool storageBegin() {
  // partition label "littlefs" (see partitions_gridline_4mb.csv); formats on first use
  s_ok = LittleFS.begin(true, "/littlefs", 6, "littlefs");
  if (s_ok) {
    LOGI("FS", "LittleFS mounted, %u/%u bytes used", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    // leftovers from an interrupted write are discarded
    for (int ds = 0; ds < 8; ds++) {
      char tmp[24];
      snprintf(tmp, sizeof(tmp), "/c%d.tmp", ds);
      if (fileExists(tmp)) {
        LittleFS.remove(tmp);
        LOGI("FS", "removed stale %s", tmp);
      }
    }
  } else {
    LOGI("FS", "LittleFS mount FAILED - cache disabled");
  }
  return s_ok;
}

static void paths(uint8_t ds, char* fin, char* tmp) {
  snprintf(fin, 24, "/c%u.bin", ds);
  snprintf(tmp, 24, "/c%u.tmp", ds);
}

bool cacheSave(uint8_t dataset, const void* payload, size_t size, int64_t fetchedUtc) {
  if (!s_ok) return false;
  char fin[24], tmp[24];
  paths(dataset, fin, tmp);
  gl::CacheHeader h;
  uint8_t hdrBuf[sizeof(gl::CacheHeader)];
  // encodeCache needs header+payload contiguous; build the header separately
  memset(&h, 0, sizeof(h));
  h.magic = gl::kCacheMagic;
  h.schema = gl::kCacheSchema;
  h.dataset = dataset;
  h.payloadSize = (uint32_t)size;
  h.payloadCrc = gl::crc32(payload, size);
  h.fetchedUtc = fetchedUtc;
  h.headerCrc = gl::crc32(&h, offsetof(gl::CacheHeader, headerCrc));
  memcpy(hdrBuf, &h, sizeof(h));
  File f = LittleFS.open(tmp, "w");
  if (!f) { LOGI("FS", "open %s failed", tmp); return false; }
  size_t w = f.write(hdrBuf, sizeof(hdrBuf));
  w += f.write((const uint8_t*)payload, size);
  f.close();
  if (w != sizeof(hdrBuf) + size) {
    LOGI("FS", "short write %s (%u of %u)", tmp, (unsigned)w, (unsigned)(sizeof(hdrBuf) + size));
    LittleFS.remove(tmp);
    return false;
  }
  if (!LittleFS.rename(tmp, fin)) {
    // some VFS layers refuse to replace: remove and retry
    if (fileExists(fin)) LittleFS.remove(fin);
    if (!LittleFS.rename(tmp, fin)) { LOGI("FS", "rename failed for %s", fin); return false; }
  }
  LOGI("FS", "cache %s saved (%u bytes)", fin, (unsigned)(sizeof(hdrBuf) + size));
  return true;
}

bool cacheLoad(uint8_t dataset, void* payload, size_t size, int64_t& fetchedUtc) {
  if (!s_ok) return false;
  char fin[24], tmp[24];
  paths(dataset, fin, tmp);
  if (!fileExists(fin)) return false;
  File f = LittleFS.open(fin, "r");
  if (!f) return false;
  size_t total = f.size();
  if (total != sizeof(gl::CacheHeader) + size) {
    f.close();
    LOGI("FS", "cache %s has unexpected size %u - ignored", fin, (unsigned)total);
    return false;
  }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) { f.close(); return false; }
  size_t r = f.read(buf, total);
  f.close();
  bool ok = r == total && gl::decodeCache(buf, total, dataset, payload, size, fetchedUtc);
  free(buf);
  LOGI("FS", "cache %s %s", fin, ok ? "restored" : "INVALID - ignored");
  return ok;
}

void storageInfo(size_t& used, size_t& total) {
  used = s_ok ? LittleFS.usedBytes() : 0;
  total = s_ok ? LittleFS.totalBytes() : 0;
}
