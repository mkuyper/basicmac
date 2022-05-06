// proprietary FUOTA protocol

#include "lmic.h"
#include "micro-ecc/uECC.h"
#include "fuota/fuota.h"
#include "bootloader.h"
#include "hal.h"

// FUOTA session storage area: last page in flash
#define FUOTA_SESSION_ADDR      (FLASH_END - FLASH_PAGE_SZ)
#define FUOTA_SESSION           ((fuota_session*) FUOTA_SESSION_ADDR)


static int validate (boot_uphdr* fwup, int totalsize) {
#if defined(unicorn)
    // code signing public key used by simulation
    static const unsigned char pubkey[64] = { // testkey.pem
	0xec, 0x70, 0x36, 0xe8, 0xf1, 0xa8, 0xd5, 0x74, 0x4c, 0x9f, 0xd9, 0xfc, 0x34, 0xdf, 0x43, 0xd8,
	0xff, 0x0b, 0xf0, 0x5b, 0xc0, 0xe6, 0x8e, 0xf9, 0x31, 0x40, 0xe8, 0x01, 0x72, 0xfd, 0x06, 0x8e,
	0x36, 0x86, 0x7c, 0x09, 0xa9, 0x28, 0x5e, 0xca, 0x0e, 0x88, 0x67, 0x4a, 0x28, 0x77, 0x34, 0xdc,
	0x04, 0x2e, 0x24, 0x42, 0x02, 0x8a, 0xc8, 0x3a, 0xb3, 0xd1, 0x5d, 0xaf, 0x3d, 0x2f, 0x0f, 0x07,
    };
#else
    // code signing public key used by real application firmware
    static const unsigned char pubkey[64] = { // mykey.pem
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
#endif
    // check size constraints
    if( (uintptr_t) (fwup + 1) > FLASH_END
	|| (fwup->size & 3) != 0
	|| fwup->size > totalsize
	|| (uintptr_t) fwup + totalsize > FLASH_END ) {
	return -1;
    }
    // check crc
    if( crc32((unsigned char*) fwup + 8, (fwup->size - 8) >> 2) != fwup->crc ) {
	return -2;
    }
    // compute hash
    uint32_t hash[8];
    sha256(hash, (unsigned char*) fwup, fwup->size);
    // verify signature(s)
    unsigned char* max = (unsigned char*) fwup + totalsize - sizeof(pubkey);
    for (unsigned char* sig = (unsigned char*) fwup + fwup->size; sig <= max; sig += sizeof(pubkey)) {
	if( uECC_verify(pubkey, (unsigned char*) hash, 32, sig, uECC_secp256r1()) == 1 ) {
	    // OK
	    return 0;
	}
    }
    // no signature verified
    return -3;
}


// downlink format:  (8 + fragsize bytes)
// ----+---------+---------+----------+----------+------------------
// off | 0       |  2      | 4        | 6        | 8
// ----+---------+---------+----------+----------+------------------
// len | 2       |  2      | 2        | 2        | fragsize
// ----+---------+---------+----------+----------+------------------
// val | src-crc | dst-crc | frag-cnt | frag-idx | frag-data...
// ----+---------+---------+----------+----------+------------------
void process_fuota (unsigned char* buf, int len) {
    // 16bit src-crc
    // 16bit dst-crc
    // 16bit chunk-cnt
    // 16bit chunk-idx
    if( len <= 8 ) {
        return;
    }
    hal_fwi fwi;
    hal_fwinfo(&fwi);
    unsigned int src = os_rlsbf2(buf + 0);
    unsigned int dst = os_rlsbf2(buf + 2);
    unsigned int cct = os_rlsbf2(buf + 4);
    unsigned int cid = os_rlsbf2(buf + 6);
    buf += 8;
    len -= 8;
    if( len & 3 ) {
        // invalid chunk size
        return;
    }
    unsigned int cnw = len >> 2;

    // check target and referenced firmware CRCs
    if( dst == (fwi.crc & 0xffff) ) {
        // already have it
        return;
    }
    if( src != 0 && (fwi.crc & 0xffff) != src ) {
        // delta from a different fw
        return;
    }

    // use a combination of referenced firmware CRC and target CRC as session-id
    unsigned int sid = (src << 16) | dst;

    // check if session parameters match, otherwise initialize new session
    if( fuota_check_state(FUOTA_SESSION, sid, cct, cnw) == FUOTA_ERROR ) {
        // need to create a new session
        int dnp = ((cct * cnw * 4)             + (FLASH_PAGE_SZ-1)) / FLASH_PAGE_SZ;
        int mnp = (fuota_matrix_size(cct, cnw) + (FLASH_PAGE_SZ-1)) / FLASH_PAGE_SZ;
        void* data = (void*) (FUOTA_SESSION_ADDR - ((dnp      ) * FLASH_PAGE_SZ));
        void* mtrx = (void*) (FUOTA_SESSION_ADDR - ((dnp + mnp) * FLASH_PAGE_SZ));
        // check for enough space
	extern volatile hal_fwhdr fwhdr; // (XXX should be provided by hal_fwinfo())
	void* fwend = (unsigned char*) &fwhdr + fwhdr.boot.size; // (page-aligend)
	if( mtrx < fwend ) {
	    debug_printf("not enough space for FUOTA data+matrix+state!\r\n");
	    return;
	}
        // erase matrix+data+session pages
        flash_write(mtrx, NULL, (dnp + mnp + 1) * FLASH_PAGE_NW, true);
        // initialize state
        fuota_init(FUOTA_SESSION, mtrx, data, sid, cct, cnw);
    }

    // process received chunk data
    if( fuota_process(FUOTA_SESSION, cid, buf) > FUOTA_MORE ) {
	// try to fully defragment
        void* up = fuota_unpack(FUOTA_SESSION);
        if( up ) {
	    // validate code signature
	    if( validate((boot_uphdr*) up, cct * cnw * 4) != 0 ) {
		debug_printf("firmware update validation failed!\r\n");
		return;
	    }
	    // register firmware for installation at next boot
	    if( !hal_set_update(up) ) {
		debug_str("firmware update registration for bootloader failed!\r\n");
		return;
	    }
	    // OK - reset...
	    debug_str("firmware update registered for installation. rebooting...\r\n");
	    hal_reboot();
	    // (not reached)
        }
    }
}

// uplink format:  (8 bytes)
// ----+--------+----------+----------
// off | 0      | 4        | 6
// ----+--------+----------+----------
// len | 4      | 2        | 2
// ----+--------+----------+----------
// val | fw-crc | done-cnt | frag-cnt
// ----+--------+----------+----------
void report_fuota (unsigned char* msgbuf) {
    hal_fwi fwi;
    hal_fwinfo(&fwi);
    os_wlsbf4(msgbuf + 0, fwi.crc);
    uint32_t chunk_ct, complete_ct;
    if( fuota_state(FUOTA_SESSION, NULL, &chunk_ct, NULL, &complete_ct) < 0 ) {
	chunk_ct = 0;
	complete_ct = 0;
    }
    os_wlsbf2(msgbuf + 4, complete_ct);
    os_wlsbf2(msgbuf + 6, chunk_ct);
    debug_printf("FUOTA progress %d / %d\r\n", complete_ct, chunk_ct);
}
