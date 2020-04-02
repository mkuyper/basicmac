// Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _eckm_h_
#define _eckm_h_

#include <stdint.h>

// Generate a new device key-pair. This will erase any existing pairing.
bool eckm_initkey (void);

// Retrieve the device public key and/or key id.
bool eckm_pubkey (uint8_t* pubkey, uint32_t* keyid);

// Pair device with a join server.
bool eckm_pair (const uint8_t* joineui, const uint8_t* pubkey);

// Retrieve the join EUI.
bool eckm_joineui (uint8_t* joineui);

// Clear all ECKM state (erase key and pairing).
void eckm_clear (void);

#endif
