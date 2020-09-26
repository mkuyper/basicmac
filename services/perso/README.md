# Basic MAC Device Personalization and Test Mode Specification
*Version 0.1*

This document describes the functionality and protocol implemented by the Basic
MAC Personalization Service (perso).  It is intended to allow a way for devices
to be tested and personalized on a production line via a serial connection.

Design goals of the protocol include robustness, reliability and error
recovery; while enabling a simple and safe implementation on the device.


## Physical Layer

Logic-level asynchronous serial communication with the following settings shall
be used:

- Bit rate: 115,200 bps
- Data bits: 8
- Parity: none
- Stop bits: 1
- Flow control: none

After reset, the device checks for presence of personalization/test equipment
(PTE) by checking the state of a designated I/O line. If this line is driven
high by a PTE, the test and personalization mode is activated; otherwise the
device will resume normal operation.

> **Note:** It is recommended to use the UART RX line as the designated I/O
> line. Since the idle state of a UART line is logic high, a PTE is easily
> detected whenever a serial transceiver is connected to the device.


## Data Link Layer

Transport layer packets shall be individually framed using *Consistent Overhead
Byte Stuffing* ([COBS]). An encoded frame's length shall not exceed 254 bytes.

> **Note:** The transport layer does not allow packets longer than 252 bytes,
> thus a frame containing a valid packet cannot exceed 254 bytes.

The receiver shall silently drop any data that is not properly framed.


## Transport Layer

The protocol employs a strict host-initiated command-response sequence. Command
and response packets are formatted as follows:
```
                +---+---+---+---+---- - -  -  -  -  +---- - - - +---+---+---+---+
Host -> Device: |CMD|  TAG  |LEN| Payload[0-236]... | Pad[0-3]  |    CRC-32     |
                +---+---+---+---+---- - -  -  -  -  +---- - - - +---+---+---+---+

                +---+---+---+---+---- - -  -  -  -  +---- - - - +---+---+---+---+
Device -> Host: |RES|  TAG  |LEN| Payload[0-236]... | Pad[0-3]  |    CRC-32     |
                +---+---+---+---+---- - -  -  -  -  +---- - - - +---+---+---+---+
```
The packet header consists of:

- CMD/RES (1 byte): Command Identifier or Result Code
- TAG (2 bytes): Message Tag;
- LEN (1 byte): Payload Length (0-236).

The meaning of the command identifiers and result codes are defined by the
application layer.

The message tag is used to match the device's response to the host's command.
For every command sent, a different tag shall be used; at least the tag for the
first command in a session shall be chosen randomly. The device's response
shall carry the same message tag as the corresponding command.

A variable length payload from 0 to 236 bytes, defined by the application
layer, may be included. Its length is encoded in the LEN byte of the header.

0 to 3 padding bytes with value 0xFF shall be appended after the payload to
make the packet length a multiple of four.

Finally, a CRC-32 is calculated over the header (CMD/RES, TAG, LEN), the
payload (if any), and the padding bytes (if any); this value is then appended
to the packet.

The receiver shall silently drop any packet with a CRC-32 that does not verify,
carrying an unexpected message tag, or that is otherwise malformed.

After receiving a valid command packet, the device shall reply with a response
packet. The host shall not send any further command packet until it receives a
response packet from the device or has determined that the command or response
packet might have been dropped due to communication failure. As a special case,
the device may respond with a status of *Wait extension*, which indicates that
the command is still bein processed. Upon receipt of a wait extension, the host
shall reset its time-out mechanism and continue waiting for the actual response
code.


## Application Layer

All multi-byte fields shall be represented in little-endian byte order.

### Response Codes

By convention, response codes that indicate sucess have the most-significant
bit cleared; response codes indicating failure have this bit set. No payload
should be included if the response code indicates failure.

Common response codes are summarized in this table:

 Code | Description
------|------------------
`0x00`| Success
`0x80`| Invalid parameter
`0x81`| Internal error
`0xFE`| Wait extension
`0xFF`| Not implemented

> **Note:** Unless otherwise noted, the device shall respond with response code
> `0x00` on success, or an appropriate failure code from the table above on
> error.


### Commands

#### `0x00` NOP
```
                +--+--+--+--+
Host -> Device: |00| tag |00|
                +--+--+--+--+

                +--+--+--+--+
Device -> Host: |7F| tag |00|
                +--+--+--+--+
```
**Description:** The NOP (no operation) command has no effect on the device and
does not change any state.

**Response:** The device shall respond with response code **`0x7F`**.


#### `0x01` Run
```
                +--+--+--+--+
Host -> Device: |02| tag |00|
                +--+--+--+--+

                +--+--+--+--+
Device -> Host: |00| tag |00|
                +--+--+--+--+
```
**Description:** The Run command exits personalization and test mode and causes
the device firmware to resume normal operation.


#### `0x02` Reset
```
                +--+--+--+--+
Host -> Device: |02| tag |00|
                +--+--+--+--+

                +--+--+--+--+
Device -> Host: |00| tag |00|
                +--+--+--+--+
```
**Description:** The Reset command resets the device.


#### `0x90` Read EEPROM Data
```
                +--+--+--+--+--+--+--+
Host -> Device: |04| tag |ln| off |nb|
                +--+--+--+--+--+--+--+

                +--+--+--+--+---  -  -  ---+
Host -> Device: |00| tag |ln|     data     |
                +--+--+--+--+---  -  -  ---+
```
**Description:** This command reads *nb* bytes of data from EEPROM at offset *off*.


#### `0x91` Write EEPROM Data
```
                +--+--+--+--+--+--+---  -  -  ---+
Host -> Device: |04| tag |ln| off |     data     |
                +--+--+--+--+--+--+---  -  -  ---+

                +--+--+--+--+---  -  -  ---+
Host -> Device: |00| tag |ln|     data     |
                +--+--+--+--+---  -  -  ---+
```
**Description:** This command writes *data* to EEPROM at offset *off*.

[COBS]: https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing
