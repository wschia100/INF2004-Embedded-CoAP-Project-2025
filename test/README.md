# Pico Unit & Component Test Suite

## Overview
This file (`unit_component_test.c`) serves as the master test runner for the CS04 CoAP project on the Raspberry Pi Pico W. It combines **Unit Tests** (pure logic verification) and **Component Tests** (hardware driver verification) into a single executable to ensure system integrity before deployment.

## Hardware Requirements
* **Raspberry Pi Pico W** (Required for Wi-Fi tests)
* **Micro SD Card Module** (SPI Interface wired to default SPI pins)
* **SD Card** (Formatted FAT32)
* **USB Connection** (For serial output/debugging)

## Dependencies
This test suite relies on the following libraries and project headers:
* **Pico SDK:** `pico_stdlib`, `pico_cyw43_arch`
* **FatFs:** Generic FAT filesystem module (`ff.h`)
* **Project Headers:**
    * `cs04_coap_packet.h` (Packet parsing logic)
    * `cs04_coap_reliability.h` (Duplicate detection logic)
    * `cs04_hardware.h` (LED and SD wrapper functions)

## Configuration
**⚠️ IMPORTANT: Update Wi-Fi Credentials**
Before compiling, you must update lines 13-14 in `unit_component_test.c` with your local network credentials:

```c
// --- WI-FI CREDENTIALS ---
#define TEST_WIFI_SSID "YOUR_WIFI_SSID"
#define TEST_WIFI_PASS "YOUR_WIFI_PASSWORD"
```

## Test Coverage

### Part 1: Unit Tests (Logic)
These tests run immediately and do not require external hardware interaction (other than the CPU).
1.  **Packet Message ID:** Verifies correct extraction of 16-bit IDs from headers.
2.  **Token Matching:** Compares token buffers for equality.
3.  **Block Size Math:** Validates CoAP SZX to Byte conversion (e.g., SZX 0=16, SZX 6=1024).
4.  **Block2 Encoding:** Checks if Block2 options are encoded into bytes correctly.
5.  **Reliability (Basic):** Tests the duplicate message detection logic.
6.  **Reliability (Circular Buffer):** Verifies that the duplicate detector correctly overwrites old IDs when the buffer is full.
7.  **LED Math:** Validates the logic for scaling RGB values by brightness.

### Part 2: Component Tests (Hardware)
These tests verify the physical drivers.
1.  **SD Storage:**
    * Initializes the SD card.
    * Mounts the Filesystem.
    * Creates and writes to a test file (`COMP_TEST.TXT`).
    * Verifies bytes written.
    * Deletes the test file (Cleanup).
2.  **Wi-Fi Driver:**
    * Initializes the CYW43 chip.
    * Attempts to connect to the AP configured in the `#define` macros.
    * Verifies a valid IP address is obtained.

## How to Run
1.  Add `unit_component_test.c` to your `CMakeLists.txt` as an executable. (it is already)
2.  Ensure `pico_cyw43_arch_lwip_threadsafe_background` and your project libraries are linked.
3.  Build the project.
4.  Flash the `.uf2` file to the Pico.
5.  Open a Serial Monitor (e.g., Putty, minicom, or VS Code Serial Monitor).
6.  Reset the Pico.

## Expected Output
On success, the serial monitor will display:

```text
========================================
   PICO UNIT & COMPONENT TESTS (FULL)
========================================

[UNIT] Testing Message ID Extraction...
[PASS] Extract Message ID 0xABCD

[UNIT] Testing Token Matching...
[PASS] Identical tokens match
[PASS] Different tokens do not match

[UNIT] Testing Block Size Math...
[PASS] SZX 0 -> 16 bytes
[PASS] SZX 6 -> 1024 bytes
[PASS] SZX 7 (Max) -> 1024 bytes

[UNIT] Testing Block2 Option Encoding...
[PASS] Short Block2 should be 1 byte
[PASS] Encoded Byte Match
[PASS] Medium Block2 should be 2 bytes
[PASS] Upper Byte Match
[PASS] Lower Byte Match

[UNIT] Testing Duplicate Detection...
[PASS] New ID 123 is not duplicate
[PASS] ID 123 is now duplicate

[UNIT] Testing Circular Buffer Overwrite...
[PASS] Oldest ID (1) present
[PASS] ID 1 overwritten
[PASS] ID 17 present

[UNIT] Testing LED Color Math...
[PASS] Color scaling 50%
[PASS] Brightness 0 should be black

[COMPONENT] Testing SD Card Storage...
initialized!initialized!V2-Version Card
R3/R7: 0x1aa
R3/R7: 0xff8000
R3/R7: 0xc0ff8000
Card Initialized: High Capacity Card
SD card initialized
SDHC/SDXC Card: hc_c_size: 60499
Sectors: 61952000
Capacity:    30250 MB
SD card mounted successfully.
[PASS] SD Mount Success
[PASS] File Write (4 bytes)

[COMPONENT] Testing Wi-Fi Driver...
[PASS] Wi-Fi Chip Init Success
Version: 7.95.49 (2271bb6 CY) CRC: b7a28ef3 Date: Mon 2021-11-29 22:50:27 PST Ucode Ver: 1043.2162 FWID 01-c51d9400
cyw43 loaded ok, mac d8:3a:dd:75:5d:e7
API: 12.2
Data: RaspberryPi.PicoW
Compiler: 1.29.4
ClmImport: 1.47.1
Customization: v5 22/06/24
Creation: 2022-06-24 06:55:08

   (Attempting connection to lomohomo...)
connect status: joining
connect status: no ip
connect status: link up
[PASS] Wi-Fi Connected (Valid IP)

----------------------------------
SUMMARY: 22 Tests Run
RESULT:  ALL PASSED! :)
----------------------------------
```

## Integration Testing
For end-to-end system validation (Client-Server testing using `aiocoap`), please refer to the separate **Integration Test Plan** located in this directory:
> **File:** `integration_testing.md`
