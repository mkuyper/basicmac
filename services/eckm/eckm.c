// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <string.h>

#include "lmic.h"
#include "eefs/eefs.h"
#include "micro-ecc/uECC.h"

#include "eckm.h"

#include "svcdefs.h" // for type-checking hook functions

#define CURVE uECC_secp256r1

// 170eb959c8bb6770-4c85eac0
static const uint8_t UFID_ECKM_CONFIG[12] = {
    0x70, 0x67, 0xbb, 0xc8, 0x59, 0xb9, 0x0e, 0x17, 0xc0, 0xea, 0x85, 0x4c
};

enum {
    F_INIT      = (1 << 0), // key generated
    F_PAIRED    = (1 << 1), // paired with join server
};

typedef struct {
    uint32_t flags;      // flags (F_*)
    uint8_t prikey[32];  // ECC private key
    uint32_t master[4];  // master key (16 bytes)
    uint8_t joineui[8];  // join EUI
} eckm_config;

static struct {
    uint8_t joineui[8];
    uint8_t nwkkey[16];
    uint8_t appkey[16];
} current;

//  src: 32 bytes ->  8 words or NULL
// dest: 64 bytes -> 16 words
static void xor64 (uint32_t* dest, uint32_t x, const uint32_t* src) {
    int i = 0;
    if( src ) {
        for( ; i < 8; i++ ) {
            dest[i] = src[i] ^ x;
        }
    }
    for( ; i < 16; i++ ) {
        dest[i] = x;
    }
}

typedef struct {
    uint32_t key[16];
    union {
        uint32_t hash[8];
        uint8_t msg[32];
    };
} hmac_buf;

// key must be NULL or 8 words
static void hmac (uint32_t* hash, const uint32_t* key, hmac_buf* buf, int msglen) {
    xor64(buf->key, 0x36363636, key);
    sha256(buf->hash, (uint8_t*) buf, sizeof(buf->key) + msglen);
    xor64(buf->key, 0x5c5c5c5c, key);
    sha256(hash, (uint8_t*) buf, sizeof(buf->key) + sizeof(buf->hash));
}

// key is 16 bytes (4 words)
// output is 16 bytes
// infolen must be < 32
static void hkdf (uint8_t* dest, const uint32_t* key, const char* info, int infolen) {
    hmac_buf hmb;
    uint32_t hash[8];

    for( int i = 0; i < 4; i++ ) {
        hmb.hash[i] = key[i];
    }
    hmac(hash, NULL, &hmb, 16);

    ASSERT(infolen < sizeof(hmb.msg));
    memcpy(hmb.msg, info, infolen);
    hmb.msg[infolen] = 0x01;
    hmac(hash, hash, &hmb, infolen + 1);

    memcpy(dest, hash, 16);
}

static uint32_t get_keyid (eckm_config* config) {
    uint32_t hash[8];
    sha256(hash, (uint8_t*) config->prikey, 32);
    return hash[0];
}

static void load (eckm_config* config) {
    if( eefs_read(UFID_ECKM_CONFIG, config, sizeof(eckm_config)) != sizeof(eckm_config) ) {
        config->flags = 0;
    }
}

static void init (void) {
    eckm_config config;
    load(&config);
    if( config.flags & F_PAIRED ) {
        memcpy(current.joineui, config.joineui, 8);
        hkdf(current.nwkkey, config.master, "nwkkey", 6);
        hkdf(current.appkey, config.master, "appkey", 6);
    } else {
        // use EEPROM settings
        memcpy(current.joineui, hal_joineui(), 8);
        memcpy(current.nwkkey, hal_nwkkey(), 16);
        memcpy(current.appkey, hal_appkey(), 16);
    }
#if defined(CFG_DEBUG)
    debug_printf("eckm:   flags = %08x\r\n", config.flags);
    if( config.flags & F_INIT) {
        debug_printf("eckm:   keyid = %08X\r\n", get_keyid(&config));
    }
    debug_printf("eckm: joineui = %E\r\n", current.joineui);
#if defined(CFG_DEBUG_ECKM_KEYS)
    debug_printf("eckm:  nwkkey = %h\r\n", current.nwkkey, 16);
    debug_printf("eckm:  appkey = %h\r\n", current.appkey, 16);
#endif
#endif
}

static int eckm_rand (uint8_t* dest, unsigned int size) {
    while( size-- > 0 ) {
        *dest++ = os_getRndU1();
    }
    return 1;
}

static bool commit (eckm_config* config) {
    if( eefs_save(UFID_ECKM_CONFIG, config, sizeof(eckm_config)) < 0 ) {
        return false;
    } else {
        init();
        return true;
    }
}

bool eckm_initkey (void) {
    eckm_config config = {
        .flags = F_INIT,
    };
    uint8_t pub[64];

    uECC_set_rng(eckm_rand);
    if( uECC_make_key(pub, config.prikey, CURVE()) ) {
        return commit(&config);
    }
    return false;
}

bool eckm_pubkey (uint8_t* pubkey, uint32_t* keyid) {
    eckm_config config;
    load(&config);
    uint8_t pub[64];
    if( (config.flags & F_INIT)
            && uECC_compute_public_key(config.prikey, pub, CURVE()) ) {
        if( pubkey ) {
            memcpy(pubkey, pub, 64);
        }
        if( keyid ) {
            *keyid = get_keyid(&config);
        }
        return true;
    }
    return false;
}

bool eckm_joineui (uint8_t* joineui) {
    eckm_config config;
    load(&config);
    if( (config.flags & F_PAIRED) ) {
        if( joineui ) {
            memcpy(joineui, config.joineui, 8);
        }
        return true;
    }
    return false;
}

bool eckm_pair (const uint8_t* joineui, const uint8_t* pubkey) {
    eckm_config config;
    load(&config);

    uint8_t secret[32];

    if( (config.flags & F_INIT)
            && uECC_valid_public_key(pubkey, CURVE())
            && uECC_shared_secret(pubkey, config.prikey, secret, CURVE()) ) {
        memcpy(config.master, secret, 16);
        memcpy(config.joineui, joineui, 8);
        config.flags |= F_PAIRED;
        return commit(&config);
    }
    return false;
}

void eckm_clear (void) {
    eefs_rm(UFID_ECKM_CONFIG);
    init();
}

void _eckm_init (void) {
    init();
}

const char* _eckm_eefs_fn (const uint8_t* ufid) {
    if( memcmp(ufid, UFID_ECKM_CONFIG, sizeof(UFID_ECKM_CONFIG)) == 0 ) {
        return "ch.mkdata.svc.eckm.config";
    }
    return NULL;
}

void os_getDevEui (u1_t* buf) {
    memcpy(buf, hal_deveui(), 8);
}

void os_getJoinEui (u1_t* buf) {
    memcpy(buf, current.joineui, 8);
}

void os_getNwkKey (u1_t* buf) {
    memcpy(buf, current.nwkkey, 16);
}

void os_getAppKey (u1_t* buf) {
    memcpy(buf, current.appkey, 16);
}
