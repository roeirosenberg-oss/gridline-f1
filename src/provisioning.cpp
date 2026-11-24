#include "provisioning.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include "config.h"
#include "display.h"
#include "log.h"
#include "settings.h"
#include "touch.h"
#include "ui_setup.h"


namespace {

enum ConnState { CS_IDLE, CS_CONNECTING, CS_CONNECTED, CS_FAILED };

WebServer s_web(80);
DNSServer s_dns;
char s_apSsid[24];
char s_apPass[16];
volatile ConnState s_state = CS_IDLE;
char s_trySsid[33];
char s_tryPass[65];
char s_message[160] = "Waiting for your phone";
// snapshot of the last completed scan (SSIDs are not secret)
struct SeenNet { char ssid[33]; int8_t rssi; uint8_t channel; uint8_t bssid[6]; };
SeenNet s_seen[16];
int s_seenCount = 0;
int s_attempt = 0;
uint32_t s_nextBeginAt = 0;
int8_t s_tryChannel = 0;
uint8_t s_tryBssid[6];
uint32_t s_connectStart = 0;
volatile int s_disconnectReason = 0;
bool s_canCancel = false;
uint32_t s_successAt = 0;

const char kPage[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gridline F1 setup</title>
<style>
:root{--bg:#07090d;--panel:#141922;--line:#262c38;--text:#f2f4f7;--dim:#a3acba;--acc:#ff3b30}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:460px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:20px;letter-spacing:.04em;margin:4px 0 2px}h1 b{color:var(--acc)}
p{color:var(--dim);margin:6px 0 14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
.net{display:flex;align-items:center;justify-content:space-between;padding:12px 10px;border-radius:8px;border:1px solid transparent;cursor:pointer}
.net:hover,.net.sel{border-color:var(--acc)}.net small{color:var(--dim)}
label{display:block;color:var(--dim);font-size:14px;margin:12px 0 4px}
input{width:100%;padding:12px;border-radius:8px;border:1px solid var(--line);background:#0c1016;color:var(--text);font-size:16px}
button{width:100%;padding:13px;border:0;border-radius:10px;background:var(--acc);color:#07090d;font-weight:700;font-size:16px;margin-top:14px}
button.sec{background:transparent;border:1px solid var(--line);color:var(--text)}
#msg{min-height:24px;margin-top:12px;font-weight:600}.ok{color:#3ddc97}.bad{color:#ff6b6b}
.row{display:flex;gap:8px;align-items:center}.row input[type=checkbox]{width:auto}
</style></head><body><main>
<h1><b>//</b> GRIDLINE F1</h1>
<p>Choose your home Wi-Fi network. The display supports 2.4&nbsp;GHz networks only.</p>
<div class="card"><div id="list">Scanning for networks…</div>
<p style="font-size:14px;margin:10px 0 0">The list shows only the networks the display itself can receive (2.4&nbsp;GHz).</p>
<button class="sec" id="rescan" type="button">Scan again</button></div>
<form class="card" id="f">
<label for="ssid">Network name</label><input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
<label for="pass">Password</label><input id="pass" name="pass" type="password" maxlength="63" autocomplete="off">
<div class="row"><input type="checkbox" id="show"><label for="show" style="margin:0">Show password</label></div>
<button type="submit">Connect</button><div id="msg"></div></form>
<p>If your phone disconnects during the test, reconnect it to the display's network to see the result.</p>
</main><script>
const $=id=>document.getElementById(id);
function bars(r){return r>-60?'▂▄▆█':r>-70?'▂▄▆':r>-80?'▂▄':'▂'}
function esc(s){return s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function scan(again){
 $('list').textContent='Scanning for networks…';
 for(let i=0;i<20;i++){
  try{const r=await fetch('/scan'+(again&&i==0?'?refresh=1':''));const j=await r.json();
   if(j.state=='done'){
    if(!j.networks.length){$('list').textContent='No networks found. You can type the name below.';return}
    $('list').innerHTML=j.networks.map(n=>`<div class="net" data-s="${esc(n.ssid)}"><span>${esc(n.ssid)}</span><small>${n.secure?'🔒 ':''}${bars(n.rssi)}</small></div>`).join('');
    document.querySelectorAll('.net').forEach(e=>e.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));e.classList.add('sel');$('ssid').value=e.dataset.s;$('pass').focus()});
    return}}catch(e){}
  await new Promise(r=>setTimeout(r,1000))}
 $('list').textContent='Scan failed. You can type the name below.'}
$('rescan').onclick=()=>scan(true);
$('show').onchange=e=>$('pass').type=e.target.checked?'text':'password';
$('f').onsubmit=async e=>{e.preventDefault();const m=$('msg');m.className='';m.textContent='Saving…';
 const p=$('pass').value;if(p.length>0&&p.length<8){m.className='bad';m.textContent='Wi-Fi passwords have at least 8 characters.';return}
 try{const r=await fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:$('ssid').value,pass:p})});
  const j=await r.json();if(!j.ok){m.className='bad';m.textContent=j.message;return}}catch(e){}
 poll()};
async function poll(){const m=$('msg');
 for(;;){try{const r=await fetch('/status');const j=await r.json();
  if(j.state=='connecting'){m.className='';m.textContent='Connecting to '+j.ssid+'…'}
  else if(j.state=='connected'){m.className='ok';m.textContent='Connected! The display restarts now. You can close this page.';return}
  else if(j.state=='failed'){m.className='bad';m.textContent=j.message;return}}catch(e){}
  await new Promise(r=>setTimeout(r,1000))}}
scan(false);
</script></body></html>)HTML";

void makePassword(char* out) {
  // no 0/O, 1/l/I to avoid confusion when typing from the display
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  uint8_t rnd[8];
  esp_fill_random(rnd, sizeof(rnd));
  for (int i = 0; i < 8; i++) {
    int pos = i < 4 ? i : i + 1;
    out[pos] = alphabet[rnd[i] % (sizeof(alphabet) - 1)];
  }
  out[4] = '-';
  out[9] = 0;
}

void drawStatusBar() {
  SetupView v{s_apSsid, s_apPass, s_message, (uint8_t)s_state, s_canCancel};
  setupDrawStatus(v);
}

void drawSetupScreen() {
  SetupView v{s_apSsid, s_apPass, s_message, (uint8_t)s_state, s_canCancel};
  setupDrawScreen(v);
}

void setMessage(const char* m) {
  if (strcmp(m, s_message) == 0) return;
  strncpy(s_message, m, sizeof(s_message) - 1);
  s_message[sizeof(s_message) - 1] = 0;
  drawStatusBar();
}

void snapshotScan(int n) {
  s_seenCount = 0;
  for (int i = 0; i < n && s_seenCount < 16; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    bool dup = false;
    for (int k = 0; k < s_seenCount; k++)
      if (ss == s_seen[k].ssid) {
        dup = true;
        if (WiFi.RSSI(i) > s_seen[k].rssi) {  // keep the strongest access point
          s_seen[k].rssi = WiFi.RSSI(i);
          s_seen[k].channel = WiFi.channel(i);
          memcpy(s_seen[k].bssid, WiFi.BSSID(i), 6);
        }
      }
    if (dup) continue;
    SeenNet& e = s_seen[s_seenCount++];
    strncpy(e.ssid, ss.c_str(), sizeof(e.ssid) - 1);
    e.ssid[sizeof(e.ssid) - 1] = 0;
    e.rssi = WiFi.RSSI(i);
    e.channel = WiFi.channel(i);
    memcpy(e.bssid, WiFi.BSSID(i), 6);
  }
  LOGI("SETUP", "scan: %d networks visible on 2.4 GHz", s_seenCount);
  for (int k = 0; k < s_seenCount; k++)
    LOGI("SETUP", "  \"%s\" ch %u, %d dBm", s_seen[k].ssid, s_seen[k].channel, s_seen[k].rssi);
}

// Exact match first, then case-insensitive / surrounding-space tolerant match.
const SeenNet* findSeen(const char* ssid) {
  for (int k = 0; k < s_seenCount; k++)
    if (strcmp(s_seen[k].ssid, ssid) == 0) return &s_seen[k];
  for (int k = 0; k < s_seenCount; k++) {
    String a = s_seen[k].ssid, b = ssid;
    a.trim();
    b.trim();
    if (a.equalsIgnoreCase(b)) return &s_seen[k];
  }
  return nullptr;
}

void waitForScan(uint32_t maxMs) {
  uint32_t t0 = millis();
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < maxMs) delay(50);
  int n = WiFi.scanComplete();
  if (n >= 0) snapshotScan(n);
}

void beginAttempt() {
  s_attempt++;
  s_disconnectReason = 0;
  WiFi.disconnect(false, false);
  delay(100);
  if (s_tryChannel > 0) {
    LOGI("SETUP", "attempt %d: \"%s\" on channel %d (password not logged)", s_attempt, s_trySsid, s_tryChannel);
    WiFi.begin(s_trySsid, s_tryPass[0] ? s_tryPass : nullptr, s_tryChannel, s_tryBssid);
  } else {
    LOGI("SETUP", "attempt %d: \"%s\" (not in scan results; password not logged)", s_attempt, s_trySsid);
    WiFi.begin(s_trySsid, s_tryPass[0] ? s_tryPass : nullptr);
  }
}

// ------------------------------------------------------------------ web handlers
void sendNoCache() {
  s_web.sendHeader("Cache-Control", "no-store");
}

void handleRoot() {
  sendNoCache();
  s_web.send_P(200, "text/html; charset=utf-8", kPage);
}

void redirectToRoot() {
  s_web.sendHeader("Location", String("http://") + GL_AP_IP_STR + "/", true);
  s_web.send(302, "text/plain", "");
}

void handleScan() {
  if (s_web.hasArg("refresh")) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
  }
  int n = WiFi.scanComplete();
  JsonDocument doc;
  if (n >= 0) snapshotScan(n);
  if (n == WIFI_SCAN_RUNNING) {
    doc["state"] = "scanning";
  } else if (n < 0) {
    WiFi.scanNetworks(true, false);
    doc["state"] = "scanning";
  } else {
    doc["state"] = "done";
    JsonArray arr = doc["networks"].to<JsonArray>();
    // de-duplicate by SSID (strongest first)
    int idx[40];
    int m = n < 40 ? n : 40;
    for (int i = 0; i < m; i++) idx[i] = i;
    for (int i = 1; i < m; i++) {
      int k = idx[i], j = i - 1;
      while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(k)) { idx[j + 1] = idx[j]; j--; }
      idx[j + 1] = k;
    }
    int added = 0;
    for (int i = 0; i < m && added < 20; i++) {
      String s = WiFi.SSID(idx[i]);
      if (!s.length()) continue;
      bool dup = false;
      for (JsonObject o : arr) if (s == o["ssid"].as<const char*>()) { dup = true; break; }
      if (dup) continue;
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = s;
      o["rssi"] = WiFi.RSSI(idx[i]);
      o["secure"] = WiFi.encryptionType(idx[i]) != WIFI_AUTH_OPEN;
      added++;
    }
  }
  String out;
  serializeJson(doc, out);
  sendNoCache();
  s_web.send(200, "application/json", out);
}

void handleSave() {
  JsonDocument doc;
  String ssid = s_web.arg("ssid");
  String pass = s_web.arg("pass");
  ssid.trim();
  if (ssid.length() < 1 || ssid.length() > 32) {
    doc["ok"] = false;
    doc["message"] = "Please enter a network name (1-32 characters).";
  } else if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    doc["ok"] = false;
    doc["message"] = "The password must be 8-63 characters (or empty for open networks).";
  } else if (s_state == CS_CONNECTING) {
    doc["ok"] = false;
    doc["message"] = "Already trying to connect - please wait.";
  } else {
    strncpy(s_trySsid, ssid.c_str(), sizeof(s_trySsid) - 1);
    s_trySsid[sizeof(s_trySsid) - 1] = 0;
    strncpy(s_tryPass, pass.c_str(), sizeof(s_tryPass) - 1);
    s_tryPass[sizeof(s_tryPass) - 1] = 0;
    // a running scan makes the connection attempt fail: let it finish first
    waitForScan(8000);
    const SeenNet* seen = findSeen(s_trySsid);
    if (seen && strcmp(seen->ssid, s_trySsid) != 0) {
      LOGI("SETUP", "using exact network name \"%s\" from the scan", seen->ssid);
      strncpy(s_trySsid, seen->ssid, sizeof(s_trySsid) - 1);
    }
    s_tryChannel = seen ? seen->channel : 0;
    if (seen) memcpy(s_tryBssid, seen->bssid, 6);
    s_state = CS_CONNECTING;
    s_connectStart = millis();
    s_attempt = 0;
    s_nextBeginAt = 0;
    beginAttempt();
    char m[64];
    snprintf(m, sizeof(m), "Connecting to %s...", s_trySsid);
    setMessage(m);
    doc["ok"] = true;
  }
  String out;
  serializeJson(doc, out);
  sendNoCache();
  s_web.send(200, "application/json", out);
}

void handleStatus() {
  JsonDocument doc;
  static const char* names[] = {"idle", "connecting", "connected", "failed"};
  doc["state"] = names[s_state];
  doc["ssid"] = s_trySsid;
  doc["message"] = s_message;
  String out;
  serializeJson(doc, out);
  sendNoCache();
  s_web.send(200, "application/json", out);
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_disconnectReason = info.wifi_sta_disconnected.reason;
}

void checkConnect() {
  if (s_state != CS_CONNECTING) return;
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    LOGI("SETUP", "connected, IP %s - saving credentials", WiFi.localIP().toString().c_str());
    wifiCredsSave(s_trySsid, s_tryPass);
    memset(s_tryPass, 0, sizeof(s_tryPass));
    s_state = CS_CONNECTED;
    char m[64];
    snprintf(m, sizeof(m), "Connected to %s - restarting", s_trySsid);
    setMessage(m);
    s_successAt = millis();
    return;
  }
  int reason = s_disconnectReason;
  const char* fail = nullptr;
  static char notFound[160];
  if (reason == WIFI_REASON_NO_AP_FOUND) {
    // not found can be transient (channel switch, AP busy): retry a few times first
    if (s_attempt < 4 && millis() - s_connectStart < 30000) {
      if (!s_nextBeginAt) {
        LOGI("SETUP", "attempt %d: network not found (reason 201), retrying", s_attempt);
        s_nextBeginAt = millis() + 1500;
        if (s_attempt == 2) WiFi.scanNetworks(true, false);  // refresh what is visible
      } else if (millis() >= s_nextBeginAt && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        s_nextBeginAt = 0;
        int n = WiFi.scanComplete();
        if (n >= 0) {
          snapshotScan(n);
          const SeenNet* seen = findSeen(s_trySsid);
          s_tryChannel = seen ? seen->channel : 0;
          if (seen) memcpy(s_tryBssid, seen->bssid, 6);
        }
        beginAttempt();
      }
      return;
    }
    int len = snprintf(notFound, sizeof(notFound), "\"%s\" not found on 2.4 GHz.", s_trySsid);
    if (s_seenCount && !findSeen(s_trySsid)) {
      len += snprintf(notFound + len, sizeof(notFound) - len, " Visible:");
      for (int k = 0; k < s_seenCount && k < 4 && len < (int)sizeof(notFound) - 36; k++)
        len += snprintf(notFound + len, sizeof(notFound) - len, "%s %s", k ? "," : "", s_seen[k].ssid);
    }
    fail = notFound;
  } else if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_EXPIRE || reason == 15)
    fail = "Wrong password? Please try again.";
  else if (millis() - s_connectStart > 25000) fail = "Could not connect. Check name and password.";
  if (fail && (millis() - s_connectStart > 3000 || reason == WIFI_REASON_NO_AP_FOUND)) {
    LOGI("SETUP", "connection failed after %d attempt(s), reason %d, %lu ms", s_attempt, reason,
         (unsigned long)(millis() - s_connectStart));
    WiFi.disconnect(false, false);
    memset(s_tryPass, 0, sizeof(s_tryPass));
    s_state = CS_FAILED;
    setMessage(fail);
  }
}

}  // namespace

void provisioningRun(bool canCancel) {
  s_canCancel = canCancel;
  LOGI("SETUP", "entering Wi-Fi setup mode");
  WiFi.persistent(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.onEvent(onWifiEvent);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  snprintf(s_apSsid, sizeof(s_apSsid), GL_AP_PREFIX "%02X%02X", mac[4], mac[5]);
  makePassword(s_apPass);  // RF is on, so esp_fill_random() is a true RNG here
  IPAddress ip, mask(255, 255, 255, 0);
  ip.fromString(GL_AP_IP_STR);
  WiFi.softAPConfig(ip, ip, mask);
  bool apOk = WiFi.softAP(s_apSsid, s_apPass, 6, 0, 4);
  LOGI("SETUP", "access point %s on %s: %s (password shown on screen only)", s_apSsid, GL_AP_IP_STR, apOk ? "up" : "FAILED");
  WiFi.scanNetworks(true, false);
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", ip);
  s_web.on("/", HTTP_GET, handleRoot);
  s_web.on("/scan", HTTP_GET, handleScan);
  s_web.on("/save", HTTP_POST, handleSave);
  s_web.on("/status", HTTP_GET, handleStatus);
  const char* captive[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                           "/ncsi.txt", "/connecttest.txt", "/redirect", "/canonical.html", "/success.txt", "/fwlink"};
  for (const char* c : captive) s_web.on(c, redirectToRoot);
  s_web.onNotFound(redirectToRoot);
  s_web.begin();
  backlightSet(g_settings.brightness < 50 ? 50 : g_settings.brightness);
  drawSetupScreen();
  int lastClients = -1;
  for (;;) {
    s_dns.processNextRequest();
    s_web.handleClient();
    checkConnect();
    if (s_state == CS_IDLE || s_state == CS_FAILED) {
      int clients = WiFi.softAPgetStationNum();
      if (clients != lastClients) {
        lastClients = clients;
        if (s_state == CS_IDLE) setMessage(clients ? "Phone connected - open the page" : "Waiting for your phone");
      }
    }
    if (s_state == CS_CONNECTED && millis() - s_successAt > 4000) {
      s_web.stop();
      delay(100);
      ESP.restart();
    }
    touchPoll();
    int16_t x, y;
    if (s_canCancel && touchTakeTap(x, y) && setupCancelHit(x, y)) {
      LOGI("SETUP", "cancelled by user");
      ESP.restart();
    }
    delay(2);
  }
}
