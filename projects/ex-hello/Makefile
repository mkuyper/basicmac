TARGET := wrl13990-sx1261mbed
#TARGET := b_l072z_lrwan1
#TARGET := nucleo_l073rz-sx1276mb1las
#TARGET := nucleo_l073rz-sx1272mbed
#TARGET := nucleo_l073rz-sx1276mb1mas
#TARGET := nucleo_l053r8-sx1276mb1las
#TARGET := nucleo_l053r8-sx1261mbed
#TARGET := nucleo_l053r8-sx1262mbed

VARIANTS := hybrid


CFLAGS += -Os
CFLAGS += -g
CFLAGS += -Wall -Wno-main

SVCS += app

LMICCFG += eeprom_keys
LMICCFG += eeprom_region
LMICCFG += DEBUG

include ../projects.gmk
