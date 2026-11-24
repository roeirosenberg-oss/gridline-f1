/* Host test of esp_crt_bundle.c (original or Gridline-patched) with real mbedTLS 2.28. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/x509_crt.h>
#include "esp_err.h"
#include "esp_crt_bundle.h"

int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags);

static unsigned char *slurp(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(2); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc(n + 1); fread(b, 1, n, f); b[n] = 0; fclose(f);
  *len = (size_t)n; return b;
}

static int verify(const char *dir, const char *chain, const char *cn) {
  char path[512]; size_t len;
  snprintf(path, sizeof(path), "%s/%s.pem", dir, chain);
  unsigned char *pem = slurp(path, &len);
  mbedtls_x509_crt crt, dummy;
  mbedtls_x509_crt_init(&crt); mbedtls_x509_crt_init(&dummy);
  if (mbedtls_x509_crt_parse(&crt, pem, len + 1) != 0) { printf("parse failed\n"); exit(2); }
  uint32_t flags = 0;
  int ret = mbedtls_x509_crt_verify(&crt, &dummy, NULL, cn, &flags, esp_crt_verify_callback, NULL);
  mbedtls_x509_crt_free(&crt); free(pem);
  return ret == 0 && flags == 0;
}

int main(int argc, char **argv) {
  const char *dir = argv[1];
  size_t blen; unsigned char *bundle = slurp(argv[2], &blen);
  arduino_esp_crt_bundle_set(bundle);
  struct { const char *chain, *cn; } cases[] = {
    {"aws_style_cross", "api.example.test"}, {"aws_style_cross_plus_old", "api.example.test"},
    {"plain_chain", "api.example.test"}, {"ecdsa_chain", "api.example.test"},
    {"plain_chain", "wrong.example.test"}, {"aws_style_cross", "wrong.example.test"},
    {"evil_same_name_other_key", "api.example.test"}, {"untrusted_root_only", "api.example.test"},
    {"self_signed", "api.example.test"},
    {"evil_plus_old_root", "api.example.test"}, {"aws_style_cross_plus_old", "wrong.example.test"},
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    printf("%-26s cn=%-20s -> %s\n", cases[i].chain, cases[i].cn, verify(dir, cases[i].chain, cases[i].cn) ? "ACCEPT" : "REJECT");
  return 0;
}
