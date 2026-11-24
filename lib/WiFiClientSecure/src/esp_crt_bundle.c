// Copyright 2018-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <string.h>
#include <stdio.h>
#include <esp_system.h>
#include <esp32-hal-log.h>
#include "esp_crt_bundle.h"
#include "esp_err.h"

#define BUNDLE_HEADER_OFFSET 2
#define CRT_HEADER_OFFSET 4

/* a dummy certificate so that
 * cacert_ptr passes non-NULL check during handshake */
static mbedtls_x509_crt s_dummy_crt;


typedef struct crt_bundle_t {
    const uint8_t **crts;
    uint16_t num_certs;
    size_t x509_crt_bundle_len;
} crt_bundle_t;

static crt_bundle_t s_crt_bundle;

int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int data, uint32_t *flags);
static int esp_crt_check_signature(mbedtls_x509_crt *child, const uint8_t *pub_key_buf, size_t pub_key_len);


static int esp_crt_check_signature(mbedtls_x509_crt *child, const uint8_t *pub_key_buf, size_t pub_key_len)
{
    int ret = 0;
    mbedtls_x509_crt parent;
    const mbedtls_md_info_t *md_info;
    unsigned char hash[MBEDTLS_MD_MAX_SIZE];

    mbedtls_x509_crt_init(&parent);

    if ( (ret = mbedtls_pk_parse_public_key(&parent.pk, pub_key_buf, pub_key_len) ) != 0) {
        log_e("PK parse failed with error %X", ret);
        goto cleanup;
    }


    // Fast check to avoid expensive computations when not necessary
    if (!mbedtls_pk_can_do(&parent.pk, child->sig_pk)) {
        log_e("Simple compare failed");
        ret = -1;
        goto cleanup;
    }

    md_info = mbedtls_md_info_from_type(child->sig_md);
    if ( (ret = mbedtls_md( md_info, child->tbs.p, child->tbs.len, hash )) != 0 ) {
        log_e("Internal mbedTLS error %X", ret);
        goto cleanup;
    }

    if ( (ret = mbedtls_pk_verify_ext( child->sig_pk, child->sig_opts, &parent.pk,
                                       child->sig_md, hash, mbedtls_md_get_size( md_info ),
                                       child->sig.p, child->sig.len )) != 0 ) {

        log_e("PK verify failed with error %X", ret);
        goto cleanup;
    }
cleanup:
    mbedtls_x509_crt_free(&parent);

    return ret;
}


/* ---------------------------------------------------------------------------
 * Gridline F1 patch (2026-10-24) - see PATCHED.md
 *
 * The original IDF 4.4 callback only checks whether the ISSUER of the topmost
 * certificate sent by the server is in the bundle. Servers that send a
 * cross-signed copy of a trusted root at the top of the chain (e.g. AWS:
 * "Starfield Services Root CA - G2" issued by the removed "Starfield Class 2"
 * root; Google: "GTS Root R4" issued by "GlobalSign Root CA") were therefore
 * rejected although the chain reaches a trusted key.
 *
 * Patched rule, applied only to the certificate mbedTLS marked NOT_TRUSTED:
 *   1. its issuer is a bundled root whose key verifies its signature, or
 *   2. it IS a bundled root: identical subject name AND identical public key
 *      (the signature of the certificate below it has already been verified
 *      by mbedTLS against this key while building the chain), or
 *   3. a certificate further down the verified chain is such a bundled root
 *      (servers that also append an old, removed self-signed root).
 * Anything else is rejected and the top of the chain is logged.
 * Lookups are linear and require exact name length and bytes.
 * ------------------------------------------------------------------------- */
static size_t entry_name_len(const uint8_t *e) { return (size_t)(e[0] << 8 | e[1]); }
static size_t entry_key_len(const uint8_t *e) { return (size_t)(e[2] << 8 | e[3]); }
static const uint8_t *entry_name(const uint8_t *e) { return e + CRT_HEADER_OFFSET; }
static const uint8_t *entry_key(const uint8_t *e) { return e + CRT_HEADER_OFFSET + entry_name_len(e); }

static int name_equals(const uint8_t *e, const mbedtls_x509_buf *raw)
{
    return entry_name_len(e) == raw->len && memcmp(entry_name(e), raw->p, raw->len) == 0;
}

int gl_crt_bundle_is_anchor(const mbedtls_x509_crt *crt)
{
    if (s_crt_bundle.crts == NULL || crt == NULL) {
        return 0;
    }
    unsigned char der[1200];
    int n = mbedtls_pk_write_pubkey_der((mbedtls_pk_context *)&crt->pk, der, sizeof(der));
    if (n <= 0) {
        return 0;
    }
    const unsigned char *key = der + sizeof(der) - n;
    for (int i = 0; i < s_crt_bundle.num_certs; i++) {
        const uint8_t *e = s_crt_bundle.crts[i];
        if (name_equals(e, &crt->subject_raw) && entry_key_len(e) == (size_t)n &&
            memcmp(entry_key(e), key, (size_t)n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* State for one chain verification. mbedTLS calls the callback for every
 * certificate of the built chain, from the top (highest depth) down to the
 * leaf (depth 0, always the last call). TLS connections are made one at a time
 * from a single task, so static state is sufficient. */
static int s_pending_untrusted_top = 0;   /* top not trusted by rules 1/2 */
static int s_anchor_below = 0;            /* a lower chain certificate is a bundled root */
static char s_top_desc[240];

int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    mbedtls_x509_crt *child = crt;
    int result = 0;

    /* rule 3 bookkeeping: an anchor below an untrusted top makes the chain trusted */
    if (s_pending_untrusted_top && depth > 0 && gl_crt_bundle_is_anchor(child)) {
        s_anchor_below = 1;
    }

    /* It's OK for a trusted cert to have a weak signature hash alg.
       as we already trust this certificate */
    uint32_t flags_filtered = *flags & ~(MBEDTLS_X509_BADCERT_BAD_MD);

    if (flags_filtered == MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
        int trusted = 0;
        if (s_crt_bundle.crts == NULL) {
            log_e("No certificates in bundle");
            result = MBEDTLS_ERR_X509_FATAL_ERROR;
        } else {
            /* 1. issued by a bundled root */
            for (int i = 0; i < s_crt_bundle.num_certs && !trusted; i++) {
                const uint8_t *e = s_crt_bundle.crts[i];
                if (name_equals(e, &child->issuer_raw) &&
                    esp_crt_check_signature(child, entry_key(e), entry_key_len(e)) == 0) {
                    trusted = 1;
                }
            }
            /* 2. cross-signed copy of a bundled root (same subject and same key) */
            if (!trusted && gl_crt_bundle_is_anchor(child)) {
                log_i("Certificate at depth %d is a trusted root (cross-signed copy)", depth);
                trusted = 1;
            }
            if (trusted) {
                *flags = 0;
            } else {
                char subj[96], iss[96];
                mbedtls_x509_dn_gets(subj, sizeof(subj), &child->subject);
                mbedtls_x509_dn_gets(iss, sizeof(iss), &child->issuer);
                snprintf(s_top_desc, sizeof(s_top_desc), "depth %d subject '%s', issuer '%s'", depth, subj, iss);
                if (depth > 0) {
                    /* 3. decide at the leaf whether a lower certificate is a trusted root */
                    s_pending_untrusted_top = 1;
                    s_anchor_below = 0;
                    *flags = 0;
                } else {
                    log_e("Failed to verify certificate: chain top %s not trusted", s_top_desc);
                    result = MBEDTLS_ERR_X509_FATAL_ERROR;
                }
            }
        }
    }

    if (depth == 0 && s_pending_untrusted_top) {
        if (s_anchor_below) {
            log_i("Untrusted chain top (%s) is above a trusted root - accepted", s_top_desc);
        } else {
            log_e("Failed to verify certificate: chain top %s not trusted", s_top_desc);
            *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
            result = MBEDTLS_ERR_X509_FATAL_ERROR;
        }
        s_pending_untrusted_top = 0;
        s_anchor_below = 0;
    }
    return result;
}


/* Initialize the bundle into an array so we can do binary search for certs,
   the bundle generated by the python utility is already presorted by subject name
 */
static esp_err_t esp_crt_bundle_init(const uint8_t *x509_bundle)
{
    s_crt_bundle.num_certs = (x509_bundle[0] << 8) | x509_bundle[1];
    s_crt_bundle.crts = calloc(s_crt_bundle.num_certs, sizeof(x509_bundle));

    if (s_crt_bundle.crts == NULL) {
        log_e("Unable to allocate memory for bundle");
        return ESP_ERR_NO_MEM;
    }

    const uint8_t *cur_crt;
    cur_crt = x509_bundle + BUNDLE_HEADER_OFFSET;

    for (int i = 0; i < s_crt_bundle.num_certs; i++) {
        s_crt_bundle.crts[i] = cur_crt;

        size_t name_len = cur_crt[0] << 8 | cur_crt[1];
        size_t key_len = cur_crt[2] << 8 | cur_crt[3];
        cur_crt = cur_crt + CRT_HEADER_OFFSET + name_len + key_len;
    }

    return ESP_OK;
}

esp_err_t arduino_esp_crt_bundle_attach(void *conf)
{
    esp_err_t ret = ESP_OK;
    // If no bundle has been set by the user then use the bundle embedded in the binary
    if (s_crt_bundle.crts == NULL) {
        log_e("Failed to attach bundle");
        return ret;
    }

    if (conf) {
        /* point to a dummy certificate
         * This is only required so that the
         * cacert_ptr passes non-NULL check during handshake
         */
        mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
        mbedtls_x509_crt_init(&s_dummy_crt);
        mbedtls_ssl_conf_ca_chain(ssl_conf, &s_dummy_crt, NULL);
        mbedtls_ssl_conf_verify(ssl_conf, esp_crt_verify_callback, NULL);
    }

    return ret;
}

void arduino_esp_crt_bundle_detach(mbedtls_ssl_config *conf)
{
    free(s_crt_bundle.crts);
    s_crt_bundle.crts = NULL;
    if (conf) {
        mbedtls_ssl_conf_verify(conf, NULL, NULL);
    }
}

void arduino_esp_crt_bundle_set(const uint8_t *x509_bundle)
{
    // Free any previously used bundle
    free(s_crt_bundle.crts);
    esp_crt_bundle_init(x509_bundle);
}
