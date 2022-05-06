# Copyright (C) 2020-2022 Michael Kuyper. All rights reserved.
#
# This file is subject to the terms and conditions defined in file 'LICENSE',
# which is part of this source code package.

from zfwtool import Update
import frag
import random
import struct
import sys

# check usage
if len(sys.argv) != 2:
    print('usage: %s <update-file>' % sys.argv[0])
    exit()

# load update file
updata = open(sys.argv[1], 'rb').read()
up = Update.fromfile(updata)

# short CRC of updated firmware
dst_crc = up.fwcrc & 0xFFFF

# short CRC of referenced firmware in case of delta update or 0
src_crc = struct.unpack_from('<I', up.data)[0] & 0xFFFF if up.uptype == Update.TYPE_LZ4DELTA else 0

# choose fragment size (multiple of 4, fragment data plus 8-byte header must fit LoRaWAN payload size!)
frag_size = 128

# initialize fragment generator
fc = frag.FragCarousel.fromfile(updata, frag_size)

# generate FUOTA downlinks
while True:
    # randomly select non-zero fragment index
    idx = random.randint(1, 65535)
    # generate FUOTA payload (session header plus fragment data)
    payload = struct.pack('<HHHH', src_crc, dst_crc, fc.cct, idx) + fc.chunk(idx)
    # print payload (could be delivered as downlink on port 16 to ex-fuota application)
    print(payload.hex())
