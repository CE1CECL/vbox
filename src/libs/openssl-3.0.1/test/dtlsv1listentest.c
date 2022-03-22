/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/conf.h>
#include "internal/nelem.h"
#include "testutil.h"

#ifndef OPENSSL_NO_SOCK

/* Just a ClientHello without a cookie */
static const unsigned char clienthello_nocookie[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x3A, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x2E, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x2E, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x00, /* Cookie len */
    0x00, 0x04, /* Ciphersuites len */
    0x00, 0x2f, /* AES128-SHA */
    0x00, 0xff, /* Empty reneg info SCSV */
    0x01, /* Compression methods len */
    0x00, /* Null compression */
    0x00, 0x00 /* Extensions len */
};

/* First fragment of a ClientHello without a cookie */
static const unsigned char clienthello_nocookie_frag[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x30, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x2E, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x24, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x00 /* Cookie len */
};

/* First fragment of a ClientHello which is too short */
static const unsigned char clienthello_nocookie_short[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x2F, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x2E, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x23, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00 /* Session id len */
};

/* Second fragment of a ClientHello */
static const unsigned char clienthello_2ndfrag[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x38, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x2E, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x02, /* Fragment offset */
    0x00, 0x00, 0x2C, /* Fragment length */
    /* Version skipped - sent in first fragment */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x00, /* Cookie len */
    0x00, 0x04, /* Ciphersuites len */
    0x00, 0x2f, /* AES128-SHA */
    0x00, 0xff, /* Empty reneg info SCSV */
    0x01, /* Compression methods len */
    0x00, /* Null compression */
    0x00, 0x00 /* Extensions len */
};

/* A ClientHello with a good cookie */
static const unsigned char clienthello_cookie[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x4E, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x42, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x42, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x14, /* Cookie len */
    0x00, 0x01, 0x02, 0x03, 0x04, 005, 0x06, 007, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, /* Cookie */
    0x00, 0x04, /* Ciphersuites len */
    0x00, 0x2f, /* AES128-SHA */
    0x00, 0xff, /* Empty reneg info SCSV */
    0x01, /* Compression methods len */
    0x00, /* Null compression */
    0x00, 0x00 /* Extensions len */
};

/* A fragmented ClientHello with a good cookie */
static const unsigned char clienthello_cookie_frag[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x44, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x42, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x38, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x14, /* Cookie len */
    0x00, 0x01, 0x02, 0x03, 0x04, 005, 0x06, 007, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 /* Cookie */
};


/* A ClientHello with a bad cookie */
static const unsigned char clienthello_badcookie[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x4E, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x42, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x42, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x14, /* Cookie len */
    0x01, 0x01, 0x02, 0x03, 0x04, 005, 0x06, 007, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, /* Cookie */
    0x00, 0x04, /* Ciphersuites len */
    0x00, 0x2f, /* AES128-SHA */
    0x00, 0xff, /* Empty reneg info SCSV */
    0x01, /* Compression methods len */
    0x00, /* Null compression */
    0x00, 0x00 /* Extensions len */
};

/* A fragmented ClientHello with the fragment boundary mid cookie */
static const unsigned char clienthello_cookie_short[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x43, /* Record Length */
    0x01, /* ClientHello */
    0x00, 0x00, 0x42, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x37, /* Fragment length */
    0xFE, 0xFD, /* DTLSv1.2 */
    0xCA, 0x18, 0x9F, 0x76, 0xEC, 0x57, 0xCE, 0xE5, 0xB3, 0xAB, 0x79, 0x90,
    0xAD, 0xAC, 0x6E, 0xD1, 0x58, 0x35, 0x03, 0x97, 0x16, 0x10, 0x82, 0x56,
    0xD8, 0x55, 0xFF, 0xE1, 0x8A, 0xA3, 0x2E, 0xF6, /* Random */
    0x00, /* Session id len */
    0x14, /* Cookie len */
    0x00, 0x01, 0x02, 0x03, 0x04, 005, 0x06, 007, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12 /* Cookie */
};

/* Bad record - too short */
static const unsigned char record_short[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* Record sequence number */
};

static const unsigned char verify[] = {
    0x16, /* Handshake */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x00, 0x00, /* Epoch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Record sequence number */
    0x00, 0x23, /* Record Length */
    0x03, /* HelloVerifyRequest */
    0x00, 0x00, 0x17, /* Message length */
    0x00, 0x00, /* Message sequence */
    0x00, 0x00, 0x00, /* Fragment offset */
    0x00, 0x00, 0x17, /* Fragment length */
    0xFE, 0xFF, /* DTLSv1.0 */
    0x14, /* Cookie len */
    0x00, 0x01, 0x02, 0x03, 0x04, 005, 0x06, 007, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 /* Cookie */
};

typedef struct {
    const unsigned char *in;
    unsigned int inlen;
    /*
     * GOOD == positive return value from DTLSv1_listen, no output yet
     * VERIFY == 0 return value, HelloVerifyRequest sent
     * DROP == 0 return value, no output
     */
    enum {GOOD, VERIFY, DROP} outtype;
} tests;

static tests testpackets[9] = {
    { clienthello_nocookie, sizeof(clienthello_nocookie), VERIFY },
    { clienthello_nocookie_frag, sizeof(clienthello_nocookie_frag), VERIFY },
    { clienthello_nocookie_short, sizeof(clienthello_nocookie_short), DROP },
    { clienthello_2ndfrag, sizeof(clienthello_2ndfrag), DROP },
    { clienthello_cookie, sizeof(clienthello_cookie), GOOD },
    { clienthello_cookie_frag, sizeof(clienthello_cookie_frag), GOOD },
    { clienthello_badcookie, sizeof(clienthello_badcookie), VERIFY },
    { clienthello_cookie_short, sizeof(clienthello_cookie_short), DROP },
    { record_short, sizeof(record_short), DROP }
};

# define COOKIE_LEN  20

static int cookie_gen(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len)
{
    unsigned int i;

    for (i = 0; i < COOKIE_LEN; i++, cookie++)
        *cookie = i;
    *cookie_len = COOKIE_LEN;

    return 1;
}

static int cookie_verify(SSL *ssl, const unsigned char *cookie,
                         unsigned int cookie_len)
{
    unsigned int i;

    if (cookie_len != COOKIE_LEN)
        return 0;

    for (i = 0; i < COOKIE_LEN; i++, cookie++) {
        if (*cookie != i)
            return 0;
    }

    return 1;
}

static int dtls_listen_test(int i)
{
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    BIO *outbio = NULL;
    BIO *inbio = NULL;
    BIO_ADDR *peer = NULL;
    tests *tp = &testpackets[i];
    char *data;
    long datalen;
    int ret, success = 0;

    if (!TEST_ptr(ctx = SSL_CTX_new(DTLS_server_method()))
            || !TEST_ptr(peer = BIO_ADDR_new()))
        goto err;
    SSL_CTX_set_cookie_generate_cb(ctx, cookie_gen);
    SSL_CTX_set_cookie_verify_cb(ctx, cookie_verify);

    /* Create an SSL object and set the BIO */
    if (!TEST_ptr(ssl = SSL_new(ctx))
            || !TEST_ptr(outbio = BIO_new(BIO_s_mem())))
        goto err;
    SSL_set0_wbio(ssl, outbio);

    /* Set Non-blocking IO behaviour */
    if (!TEST_ptr(inbio = BIO_new_mem_buf((char *)tp->in, tp->inlen)))
        goto err;
    BIO_set_mem_eof_return(inbio, -1);
    SSL_set0_rbio(ssl, inbio);

    /* Process the incoming packet */
    if (!TEST_int_ge(ret = DTLSv1_listen(ssl, peer), 0))
        goto err;
    datalen = BIO_get_mem_data(outbio, &data);

    if (tp->outtype == VERIFY) {
        if (!TEST_int_eq(ret, 0)
                || !TEST_mem_eq(data, datalen, verify, sizeof(verify)))
            goto err;
    } else if (datalen == 0) {
        if (!TEST_true((ret == 0 && tp->outtype == DROP)
                || (ret == 1 && tp->outtype == GOOD)))
            goto err;
    } else {
        TEST_info("Test %d: unexpected data output", i);
        goto err;
    }
    (void)BIO_reset(outbio);
    inbio = NULL;
    SSL_set0_rbio(ssl, NULL);
    success = 1;

 err:
    /* Also frees up outbio */
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    BIO_free(inbio);
    OPENSSL_free(peer);
    return success;
}
#endif

int setup_tests(void)
{
#ifndef OPENSSL_NO_SOCK
    ADD_ALL_TESTS(dtls_listen_test, (int)OSSL_NELEM(testpackets));
#endif
    return 1;
}
