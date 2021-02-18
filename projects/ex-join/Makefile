#TARGET := b_l072z_lrwan1
#TARGET := nucleo_l073rz-sx1276mb1las
#TARGET := nucleo_l073rz-sx1272mbed
#TARGET := nucleo_l073rz-sx1276mb1mas
#TARGET := nucleo_l053r8-sx1276mb1las
#TARGET := nucleo_l053r8-sx1261mbed
#TARGET := nucleo_l053r8-sx1262mbed
TARGET := wrl13990-sx1276mb1las

VARIANTS := eu868 us915 hybrid simul

REGIONS.simul := eu868
TARGET.simul := unicorn


CFLAGS += -Os
CFLAGS += -g
CFLAGS += -Wall -Wno-main

SVCS += app

DEFS += -DDEBUG_RX
DEFS += -DDEBUG_TX

DEFS += -DSVC_FRAG_TEST  # enable proprietary test command in frag service

DEFS += -DSVC_FWMAN_TEST # enable test key function in fwman service
DEFS += -DSVC_FWMAN_PUBKEY=fwman_testkey
DEFS += -DSVC_FWMAN_CURVE=uECC_secp256r1

LMICCFG += eeprom_keys
LMICCFG += eeprom_region
LMICCFG += DEBUG
LMICCFG += extapi

include ../projects.gmk

ifeq (simul,$(VARIANT))
test: build-$(VARIANT)/$(PROJECT).hex $(BL_BUILD)/bootloader.hex
	PYTHONPATH=$${PYTHONPATH}:$(TOPDIR)/unicorn/simul:$(SVCSDIR)/perso \
		   TEST_HEXFILES='$^' \
		   ward $(WARDOPTS)
endif


.PHONY: test apptest fuotatest
