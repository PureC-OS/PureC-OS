#include "pkgd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha512.h"

#ifdef PKG_SIG_OPENSSL
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#else
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#endif

#define POINT_LEN 65

static int hex_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int load_key(const char *path, unsigned char point[POINT_LEN])
{
    char *text = read_text_file(path, NULL);
    if (!text)
        return -1;
    size_t n = 0;
    while (text[n] && hex_value((unsigned char)text[n]) >= 0)
        n++;
    int ok = n == POINT_LEN * 2;
    if (ok)
        for (int i = 0; i < POINT_LEN; i++)
            point[i] = (unsigned char)(hex_value((unsigned char)text[i * 2]) * 16 + hex_value((unsigned char)text[i * 2 + 1]));
    free(text);
    return ok && point[0] == 0x04 ? 0 : -1;
}

static int digest_file(const char *path, unsigned char digest[PURE_SHA512_DIGEST_LEN])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    pure_sha512_ctx ctx;
    pure_sha512_init(&ctx);
    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
        pure_sha512_update(&ctx, buf, got);
    int bad = ferror(f);
    fclose(f);
    if (bad)
        return -1;
    pure_sha512_final(&ctx, digest);
    return 0;
}

static int verify_digest(const unsigned char *digest, const unsigned char *sig, size_t sig_len,
                         const unsigned char point[POINT_LEN])
{
#ifdef PKG_SIG_OPENSSL
    EC_KEY *key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!key)
        return 0;
    const EC_GROUP *group = EC_KEY_get0_group(key);
    EC_POINT *pub = EC_POINT_new(group);
    int ok = 0;
    if (pub && EC_POINT_oct2point(group, pub, point, POINT_LEN, NULL) == 1 && EC_KEY_set_public_key(key, pub) == 1)
        ok = ECDSA_verify(0, digest, PURE_SHA512_DIGEST_LEN, sig, (int)sig_len, key) == 1;
    EC_POINT_free(pub);
    EC_KEY_free(key);
    return ok;
#else
    ecc_key key;
    int res = 0;
    if (wc_ecc_init(&key) != 0)
        return 0;
    if (wc_ecc_import_x963_ex(point, POINT_LEN, &key, ECC_SECP256R1) == 0)
        if (wc_ecc_verify_hash(sig, (word32)sig_len, digest, PURE_SHA512_DIGEST_LEN, &res, &key) != 0)
            res = 0;
    wc_ecc_free(&key);
    return res == 1;
#endif
}

static void scan_keys(const char *dir, const unsigned char *digest, const char *sig, size_t sig_len, int *keys,
                      int *valid)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && !*valid) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".pub") != 0)
            continue;
        char path[PKG_PATH_MAX + 300];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        unsigned char point[POINT_LEN];
        if (load_key(path, point) < 0)
            continue;
        (*keys)++;
        if (verify_digest(digest, (const unsigned char *)sig, sig_len, point))
            *valid = 1;
    }
    closedir(d);
}

int sig_verify_file(const char *file, const char *signature_file)
{
    unsigned char digest[PURE_SHA512_DIGEST_LEN];
    if (digest_file(file, digest) < 0)
        return SIG_BAD;
    size_t sig_len = 0;
    char *sig = read_text_file(signature_file, &sig_len);
    if (!sig || sig_len == 0 || sig_len > 128) {
        free(sig);
        return SIG_BAD;
    }
    int keys = 0, valid = 0;
    char target_dir[PKG_PATH_MAX + 64];
    snprintf(target_dir, sizeof(target_dir), "%s", rp("etc/pkg/keys"));
    scan_keys(target_dir, digest, sig, sig_len, &keys, &valid);
    if (!valid && strcmp(target_dir, "/etc/pkg/keys") != 0)
        scan_keys("/etc/pkg/keys", digest, sig, sig_len, &keys, &valid);
    free(sig);
    if (valid)
        return SIG_OK;
    return keys ? SIG_BAD : SIG_NOKEYS;
}
