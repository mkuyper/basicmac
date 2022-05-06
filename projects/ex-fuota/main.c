// Copyright (C) 2020-2022 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "lwmux/lwmux.h"
#include "svcdefs.h"

// from fuota.c
void process_fuota (unsigned char* buf, int len);
void report_fuota (unsigned char* msgbuf);

static osjob_t periodicjob;
static lwm_job periodiclwmjob;

static osjob_t fuotajob;
static lwm_job fuotalwmjob;
static int fuotacnt;

static void nextperiodic (osjob_t* job);
static void nextfuotastatus (osjob_t* job);

//////////////////////////////////////////////////////////////////////
// fuota status uplinks
//////////////////////////////////////////////////////////////////////

static void txfuotadonecb (void) {
    if( --fuotacnt > 0 ) {
	// schedule next fuota status in 10 seconds
	os_setApproxTimedCallback(&fuotajob, os_getTime() + sec2osticks(10), nextfuotastatus);
    }
}

static bool txfuotastatus (lwm_txinfo* txinfo) {
    static unsigned char msgbuf[8];
    report_fuota(msgbuf);
    txinfo->data = msgbuf;
    txinfo->dlen = 8;
    txinfo->port = 16;
    txinfo->txcomplete = txfuotadonecb;
    return true;
}

static void nextfuotastatus (osjob_t* job) {
    lwm_request_send(&fuotalwmjob, 0, txfuotastatus);
}

//////////////////////////////////////////////////////////////////////
// periodic uplinks
//////////////////////////////////////////////////////////////////////

static void txperiodicdonecb (void) {
    // schedule next periodic transmission in 60 seconds
    os_setApproxTimedCallback(&periodicjob, os_getTime() + sec2osticks(60), nextperiodic);
}

static bool txperiodic (lwm_txinfo* txinfo) {
    txinfo->data = (unsigned char*) "hello";
    txinfo->dlen = 5;
    txinfo->port = 15;
    txinfo->txcomplete = txperiodicdonecb;
    return true;
}

static void nextperiodic (osjob_t* job) {
    lwm_request_send(&periodiclwmjob, 0, txperiodic);
}

//////////////////////////////////////////////////////////////////////


// lwm_downlink hook
void app_dl (int port, unsigned char* data, int dlen, unsigned int flags) {
    debug_printf("DL[%d]: %h\r\n", port, data, dlen);

    // check for FUOTA data on dedicated port
    if( port == 16 ) {
	// process fragment
	process_fuota(data, dlen);
	// respond with FUOTA status/progress 3 times to create more downlink opportunities
	fuotacnt = 3;
	nextfuotastatus(&fuotajob);
    }
}

bool app_main (osjob_t* job) {
    debug_printf("Hello World!\r\n");

    // join network
    lwm_setmode(LWM_MODE_NORMAL);

    // initiate first uplink
    nextperiodic(job);

    // indicate that we are running
    return true;
}
