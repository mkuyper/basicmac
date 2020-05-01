// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
// Copyright (C) 2014-2016 IBM Corporation. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "lmic.h"
#include "aes.h"
#include "peripherals.h"

// RUNTIME STATE
static struct {
    osjob_t* scheduledjobs;
    unsigned int exact;
    union {
        u4_t randwrds[4];
        u1_t randbuf[16];
    } /* anonymous */;
} OS;

void os_init (void* bootarg) {
    memset(&OS, 0x00, sizeof(OS));
    hal_init(bootarg);
#ifndef CFG_noradio
    radio_init(false);
#endif
    LMIC_init();
}

// return next random byte derived from seed buffer
// (buf[0] holds index of next byte to be returned 1-16)

void rng_init (void) {
#ifdef PERIPH_TRNG
    trng_next(OS.randwrds, 4);
#else
    memcpy(OS.randbuf, __TIME__, 8);
    os_getDevEui(OS.randbuf + 8);
#endif
}

u1_t os_getRndU1 (void) {
    u1_t i = OS.randbuf[0];
    switch( i ) {
        case 0:
            rng_init(); // lazy initialization
            // fall-thru
        case 16:
            os_aes(AES_ENC, OS.randbuf, 16); // encrypt seed with any key
            i = 0;
    }
    u1_t v = OS.randbuf[i++];
    OS.randbuf[0] = i;
    return v;
}

bit_t os_cca (u2_t rps, u4_t freq) { //XXX:this belongs into os_radio module
    return 0;  // never grant access
}

u1_t os_getBattLevel (void) {
    return hal_getBattLevel();
}

ostime_t os_getTime () {
    return hal_ticks();
}

osxtime_t os_getXTime () {
    return hal_xticks();
}

osxtime_t os_time2XTime (ostime_t t, osxtime_t context) {
    return context + ((t - (ostime_t) context));
}

// unlink job from queue, return 1 if removed
static int unlinkjob (osjob_t** pnext, osjob_t* job) {
    for( ; *pnext; pnext = &((*pnext)->next)) {
        if(*pnext == job) { // unlink
            *pnext = job->next;
	    if ((job->flags & OSJOB_FLAG_APPROX) == 0) {
		OS.exact -= 1;
	    }
            return 1;
        }
    }
    return 0;
}

// NOTE: since the job queue might begin with jobs which already have a shortly expired deadline, we cannot use
//       the maximum span of ostime to schedule the next job (otherwise it would be queued in first)!
#define XJOBTIME_MAX_DIFF (OSTIME_MAX_DIFF / 2)

// update schedule of extended job
static void extendedjobcb (osxjob_t* xjob) {
    hal_disableIRQs();
    osxtime_t now = os_getXTime();
    if (xjob->deadline - now > XJOBTIME_MAX_DIFF) {
	// schedule intermediate callback
	os_setTimedCallbackEx((osjob_t*) xjob, (ostime_t) (now + XJOBTIME_MAX_DIFF), (osjobcb_t) extendedjobcb, OSJOB_FLAG_APPROX);
    } else {
	// schedule final callback
	os_setTimedCallbackEx((osjob_t*) xjob, (ostime_t) xjob->deadline, xjob->func, OSJOB_FLAG_APPROX);
    }
    hal_enableIRQs();
}

// schedule job far in the future (deadline may exceed max delta of ostime_t 2^31-1 ticks = 65535.99s = 18.2h)
void os_setExtendedTimedCallback (osxjob_t* xjob, osxtime_t xtime, osjobcb_t cb) {
    hal_disableIRQs();
    unlinkjob(&OS.scheduledjobs, (osjob_t*) xjob);
    xjob->func = cb;
    xjob->deadline = xtime;
    extendedjobcb(xjob);
    hal_enableIRQs();
}

// clear scheduled job, return 1 if job was removed
int os_clearCallback (osjob_t* job) {
    hal_disableIRQs();
    int r = unlinkjob(&OS.scheduledjobs, job);
    hal_enableIRQs();
    return r;
}

// schedule timed job
void os_setTimedCallbackEx (osjob_t* job, ostime_t time, osjobcb_t cb, unsigned int flags) {
    osjob_t** pnext;
    hal_disableIRQs();
    // remove if job was already queued
    unlinkjob(&OS.scheduledjobs, job);
    // fill-in job
    ostime_t now = os_getTime();
    if( flags & OSJOB_FLAG_NOW ) {
        time = now;
    } else if ( time - now <= 0 ) {
	flags |= OSJOB_FLAG_NOW;
    }
    job->deadline = time;
    job->func = cb;
    job->next = NULL;
    job->flags = flags;
    if ((flags & OSJOB_FLAG_APPROX) == 0) {
	OS.exact += 1;
    }
    // insert into schedule
    for(pnext=&OS.scheduledjobs; *pnext; pnext=&((*pnext)->next)) {
        if((*pnext)->deadline - time > 0) { // (cmp diff, not abs!)
            // enqueue before next element and stop
            job->next = *pnext;
            break;
        }
    }
    *pnext = job;
    hal_enableIRQs();
}

void os_runstep (void) {
    // check for runnable jobs
    hal_disableIRQs();
    ostime_t now = os_getTime();
    osjob_t* j = OS.scheduledjobs;
    ostime_t deadline;
    if( j ) {
        deadline = j->deadline;
        if( (deadline - now) <= 0 ) {
	    OS.scheduledjobs = j->next; // de-queue
            hal_enableIRQs();

            hal_watchcount(30); // max 60 sec XXX
            j->func(j);
            hal_watchcount(0);
            return;
        }
    } else {
        deadline = now + 0x7fffff00;
    }
    hal_sleep(OS.exact ? HAL_SLEEP_EXACT : HAL_SLEEP_APPROX, deadline);
    hal_enableIRQs();
}

// execute jobs from timer and from run queue
void os_runloop (void) {
    while (1) {
	os_runstep();
    }
}

static u1_t evcatEn = 0xFF;

void os_logEv (uint8_t evcat, uint8_t evid, uint32_t evparam) {
    if( evcat >= EVCAT_MAX && evcat < sizeof(evcatEn)*8 && (evcatEn & (1<<evcat)) == 0 )
        return;
    hal_logEv(evcat, evid, evparam);
}
