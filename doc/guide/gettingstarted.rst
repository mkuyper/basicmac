..
   Copyright (C) 2020-2020 Michael Kuyper. All rights reserved.
   .
   This file is subject to the terms and conditions defined in file 'LICENSE',
   which is part of this source code package.


Getting Started
===============

The reference hardware platform for Basic MAC is the `B-L072Z-LRWAN1
LoRa®/Sigfox™ Discovery Kit
<https://www.st.com/en/evaluation-tools/b-l072z-lrwan1.html>`_.

The shell variable ``$MACREPO`` is used below to refer to your local Basic MAC
directory.

Prerequisites
-------------

The required tools to build and work with Basic MAC are listed below. It's
always a good idea to also have a look at the ``.travis.yml`` file in the root
directory of the project; this file controls the continuous integration build
and provides all the information necessary to perform this task.

Toolchain
'''''''''

It is recommended to use a recent Linux distribution as build host. To build a
Basic MAC project, you must have an appropriate cross-compile toolchain
installed.

We use Ubuntu 20.04 (Focal Fossa) with ``gcc-arm-none-eabi`` and
``libnewlib-arm-none-eabi``.

Python 3.8 or newer
'''''''''''''''''''

The build environment uses a number of tools that are written in Python. An
environment with a Python 3.8 interpreter or newer is required.

OpenOCD
'''''''

To load firmware onto actual hardware, a tool such as `OpenOCD
<http://openocd.org/>`_ must be used.

Cloning the Repository
----------------------

Getting the Source Code
'''''''''''''''''''''''

Clone the repository from GitHub, including all submodules:

.. code-block:: console

   $ git clone --recurse-submodules https://github.com/mkuyper/basicmac.git $MAC_REPO

Required Python Modules
'''''''''''''''''''''''

The Python modules required by the build and simulation tools can be installed using pip:

.. code-block:: console

   $ pip3 install -r $MAC_REPO/basicloader/requirements.txt
   $ pip3 install -r $MAC_REPO/requirements.txt

Build Process
-------------

Building the Bootloaders
''''''''''''''''''''''''

The Basic MAC HAL for STM32 employs a bootloader, *Basic Loader*, to load and
start the actual firmware. This bootloader also applies any firmware updates
and verifies the integrity of the current firmware before calling the
firmware's entrypoint.

To build Basic Loader, change to the ``basicloader`` directory and type make:

.. code-block:: console

   $ cd $MAC_REPO/basicloader
   $ make

The output of the build process for each supported platform is a file named
``bootloader.hex``.

Building a Project
''''''''''''''''''

Projects are built from their respective subfolder in the ``projects``
directory.  A simple example project is the *Join Example* project in
``$MAC_REPO/projects/ex-join``.

Build settings, such as target platform, compile time and configuration options
are specified in the project's *Makefile*.

To build a project, simply change into the project's directory and run make:

.. code-block:: console

   $ cd $MAC_REPO/projects/ex-join
   $ make

Multiple variants of a project can be built, such as versions for different
regions, or platforms. The *Join Example* project builds four variants:
*eu868*, *us915*, *hybrid* (supports both EU868 and US915), and *simul* (for
use with the simulation environment).

The output files of the build process are stored in the project's
``build-<variant>`` folders, with the firmware hex file having the same name as
the project with a *.hex* extension; in this case ``ex-join.hex``.

Loading a Project
-----------------

After a project is built, it can be loaded onto a device. Note that both the
bootloader and the firmware must be loaded, although since the bootloader
rarely changes, it is generally only loaded once.

.. note::

   You may need to install udev rules to grant permissions to regular users for
   accessing the ST-LINKv2 device. You can install these using the tar file
   provided in the Basic MAC repository with following command:

   .. code-block:: console

      $ sudo tar xzvf $MAC_REPO/tools/openocd/stlink-rules.tgz -C /etc/udev/rules.d/

To load the bootloader and the firmware, respectively, use the *loadbl* and *load* make targets:

.. code-block:: console

   $ make loadbl       # if not already done previously
   $ make load

If multiple variants are present, this will load the *default variant*, which
is generally the first variant specified in the project's Makefile.

Personalization
---------------

The HAL for STM32 stores personalization information such as EUIs and keys for
LoRaWAN operation in EEPROM.

If no valid personalization information is found in EEPROM, the HAL will
create a device EUI from the MCU's Unique ID registers, and use a fixed Join
EUI and test key:

- Device EUI: ``FF-FF-FF-AA-xx-xx-xx-xx``
- Join EUI: ``FF-FF-FF-BB-00-00-00-00``
- Device Key: ``404142434445464748494A4B4C4D4E4F``

You can see the device EUI used by your board in the debug output.

Viewing Debug Output
--------------------

On the B-L072Z-LRWAN1, the firmware prints debug information to the UART that
is connected via the ST-LINK to the host computer. On Linux, this device
usually shows up as ``/dev/ttyACM0``. Use a serial terminal application to
connect to that port, using 2000000/8-N-1; for example using miniterm.py:

.. code-block:: console

   $ miniterm.py /dev/ttyACM0 2000000

   --- Miniterm on /dev/ttyACM0  2000000,8,N,1 ---
   --- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---

   ============== DEBUG STARTED ==============
   id: FF-FF-FF-AA-2B-05-01-41 | sn:  | hw: 0x000 | flash: 192K
   bl: v256 | fw: ex-join eu868 0x00000000 0x8B27E203 | boot: normal
   Hello World!
   switching mode: normal
   lwm: JOINING
   lwm: TXSTART
   TX[freq=868.1,sf=7,bw=125,len=23]: 0000000000BBFFFFFF4101052BAAFFFFFFECF2B6412021
   lwm: TXDONE

   ...

Simulation
----------

The *Join Example* has support for running the LoRaWAN Certification test suite
and an application level test suite within the device simulation using the
*test* make target of the *simul* variant:

.. code-block:: console

   $ VARIANT=simul make test
   PYTHONPATH=${PYTHONPATH}:../../unicorn/simul \
   	   TEST_HEXFILES='build-simul/ex-join.hex ../../basicloader/build/boards/simul-unicorn/bootloader.hex' \
   	   ward
   Ward 0.48.0b0, CPython 3.8.0
   Collected 18 tests and 4 fixtures in 1.32 seconds.
   
    PASS  test_exjoin:20: Join
    PASS  test_exjoin:26: Uplink
    PASS  test_exjoin:41: Downlink
    PASS  test_lwcert103eu868:40: 2.1 Device Activation
    PASS  test_lwcert103eu868:45: 2.2 Test Application Functionality
    PASS  test_lwcert103eu868:61: 2.3 Over The Air Activation
    PASS  test_lwcert103eu868:100: 2.4 Packet error rate RX2 default DR
    PASS  test_lwcert103eu868:116: 2.5 Cryptography
    PASS  test_lwcert103eu868:133: 2.6 Downlink Window Timing
    PASS  test_lwcert103eu868:141: 2.7 Frame Sequence Number
    PASS  test_lwcert103eu868:163: 2.8 DevStatusReq MAC Command
    PASS  test_lwcert103eu868:176: 2.9 MAC commands
    PASS  test_lwcert103eu868:194: 2.10 NewChannelReq MAC Command
    PASS  test_lwcert103eu868:236: 2.11 DlChannelReq MAC Command
    PASS  test_lwcert103eu868:318: 2.12 Confirmed Packets
    PASS  test_lwcert103eu868:366: 2.13 RXParamSetupReq MAC Command
    PASS  test_lwcert103eu868:410: 2.14 RXTimingSetupReq Command
    PASS  test_lwcert103eu868:444: 2.15 LinkADRReq MAC Command
   
   SUCCESS in 44.71 seconds [ 18 passed ]
