# CoAP IoT System for Raspberry Pi Pico W

A lightweight CoAP client-server implementation for Raspberry Pi Pico W with file transfer, observable resources, GPIO control, and RFC 8132 FETCH support.

## ✨ Features

- **CoAP Server**: Observable button states, actuator control (LED/buzzer), SD card file operations
- **CoAP Client**: Auto-subscribe to notifications, send commands, receive files with block transfer
- **FETCH Method**: RFC 8132 compliant line-range retrieval from files
- **Reliable Transfer**: Automatic retransmission (4 attempts, exponential backoff), duplicate detection
- **Block-wise Transfer**: 1024-byte blocks for large file transfers
- **Hardware Integration**: WS2812 RGB LED, buzzer, buttons, SD card storage

## 📡 CoAP Methods Supported

| Method | Description | RFC |
|--------|-------------|-----|
| **GET** | Retrieve resources with Block2 support | RFC 7252 |
| **GET + Observe** | Subscribe to real-time notifications | RFC 7641 |
| **PUT** | Control actuators (LED, buzzer) | RFC 7252 |
| **iPATCH** | Append text to files | RFC 8132 |
| **FETCH** | Retrieve specific line ranges from files | RFC 8132 |

## 🔧 Quick Start

### Hardware Setup
- Raspberry Pi Pico W
- WS2812 RGB LED → GPIO 28
- Buzzer → GPIO 18
- 3 push buttons → GPIO 20, 21, 22
- SD card module (SPI)

### Build & Flash

```bash
mkdir build && cd build
cmake ..
make
# Copy coap_server.uf2 or coap_client.uf2 to Pico in BOOTSEL mode
```

### Network Configuration
- **Server IP**: `192.168.137.50` (static)
- **Wi-Fi**: Update `WIFI_SSID` and `WIFI_PASS` in source files
- **CoAP Port**: `5683` (default)

---

## 🖲️ Server Buttons

| Button | GPIO | Action |
|--------|------|--------|
| **Button 1** | GP20 | Send byte notification (`0x42`) |
| **Button 2** | GP21 | Send button state string (`BTN1=?,BTN2=1,BTN3=?`) |
| **Button 3** | GP22 | Send button state string (`BTN1=?,BTN2=?,BTN3=1`) |

All buttons send **Observe notifications** to subscribed clients.

---

## 🎮 Client Buttons

| Button | GPIO | Action |
|--------|------|--------|
| **Button 1** | GP21 | Toggle server LED/Buzzer (PUT `/actuators`) |
| **Button 2** | GP20 | Append line to server file (iPATCH `/file`) |
| **Button 3 (short)** | GP22 | FETCH specific lines from server file (FETCH `/file`) |
| **Button 3 (long)** | GP22 | Request file transfer from server (GET `/file` with Block2) |

**Long press** = hold for >1 second

---

## 📋 CoAP Endpoints

### Server Endpoints

| Endpoint | Methods | Description |
|----------|---------|-------------|
| `/buttons` | GET, GET+Observe | Query or subscribe to button states |
| `/actuators` | GET, PUT | Query or control LED/buzzer |
| `/file` | GET, iPATCH, FETCH | File operations (transfer, append, fetch lines) |

### GET `/file` - File Transfer (Block2)
```bash
# Get text file
GET coap://192.168.137.50:5683/file

# Get image file
GET coap://192.168.137.50:5683/file?type=image
```
- Uses Block2 option for 1024-byte block transfers
- Automatic retransmission on packet loss
- Query parameter `type=image` sends `server.jpg` instead of `server.txt`

### iPATCH `/file` - Append to File
```bash
iPATCH coap://192.168.137.50:5683/file
Payload: "Text to append"
```
- Appends payload to `server.txt`
- Response: `Appended`

### FETCH `/file` - Retrieve Line Range (RFC 8132)
```bash
FETCH coap://192.168.137.50:5683/file
Content-Format: 0 (text/plain)
Payload: "start,end"
```
- **Payload format**: `"start,end"` (inclusive range, zero-indexed)
  - Example: `"0,4"` = lines 0-4 (5 lines)
  - Example: `"10,15"` = lines 10-15 (6 lines)
  - Backward compatible: `"5"` = first 5 lines
- **Content-Format**: Must be `0` (text/plain)
- **Buffer limit**: 1024 bytes (dynamic line limit)
- **Validation**:
  - Rejects negative line numbers
  - Rejects reversed ranges (`end < start`)
  - Gracefully handles EOF (returns partial/empty data)
  - Returns `4.00 Bad Request` if buffer fills before completing request

### PUT `/actuators` - Control Hardware
```bash
PUT coap://192.168.137.50:5683/actuators
Payload: "LED=ON,BUZZER=ON"
```
- Control server LED (GP28) and buzzer (GP18)
- Accepted values: `LED=ON/OFF`, `BUZZER=ON/OFF`
- Can combine: `LED=ON,BUZZER=ON`

### GET+Observe `/buttons` - Subscribe to Notifications
```bash
GET coap://192.168.137.50:5683/buttons
Observe: 0
```
- Real-time notifications when server buttons are pressed
- Automatic retransmission and ACK handling
- Client auto-subscribes on startup

---

## 📂 Test Suite

The project includes a comprehensive testing suite located in the `test/` directory in the project root.

* **`test/unit_component_test.c`**: Contains Unit Tests (logic verification) and Component Tests (hardware drivers like SD card and Wi-Fi) to be flashed to the Pico.
* **`test/integration_testing.md`**: The formal Integration Test Plan covering end-to-end system validation.
* **`test/README.md`**: Refer to this file for detailed instructions on how to build, flash, and run the test suite.

---

## 🧪 Testing with Python aiocoap

See **[integration_testing.md](./test/integration_testing.md)** for complete testing guide with command examples.

### Quick Test Commands

```bash
# Get text file
python -m aiocoap.cli.client coap://192.168.137.50:5683/file > received.txt

# Get image file
python -m aiocoap.cli.client coap://192.168.137.50:5683/file?type=image > received.jpg

# Control LED
python -m aiocoap.cli.client -m PUT coap://192.168.137.50:5683/actuators --payload "LED=ON"

# Append to file
python -m aiocoap.cli.client -m iPATCH coap://192.168.137.50:5683/file --payload "Hello from aiocoap"

# Fetch lines 0-4
python -m aiocoap.cli.client -m FETCH coap://192.168.137.50:5683/file --payload "0,4" --content-format 0

# Subscribe to button notifications
python -m aiocoap.cli.client -m GET coap://192.168.137.50:5683/buttons --observe -vvv
```

---

## 🔁 Reliability Features

### Automatic Retransmission
- **Initial timeout**: 2000ms (2 seconds)
- **Max retries**: 4 attempts
- **Backoff strategy**: Exponential (2s → 4s → 8s → 16s → 32s)
- **Total timeout**: ~62 seconds per message

### Duplicate Detection
- Tracks last 16 message IDs
- Automatically re-sends ACK for duplicates
- Prevents duplicate processing

### Error Handling
- **Client timeout**: Red LED + 600Hz buzz (200ms)
- **Server timeout**: Red LED + 800Hz buzz (300ms)
- Visual/audio feedback for all operations

---

## 💡 LED Feedback Guide

| Color | RGB | Meaning |
|-------|-----|---------|
| Purple | `10,0,10` | Startup/Idle (Client) |
| Dim Green | `0,10,0` | Ready (Server) |
| Cyan | `0,10,10` | Subscribed (Client) |
| Yellow | `50,50,0` | PUT request / Button notification |
| Orange | `50,20,0` | iPATCH request |
| Bright Green | `0,50,0` | Success/Data received |
| Cyan Flash | `0,20,50` | FETCH request |
| Red | `50,0,0` | Error/Timeout |

---

## 📁 Project Structure

```
├── src/
│   ├── coap_server.c          # Server implementation
│   ├── coap_client.c          # Client implementation
│   ├── cs04_coap_packet.c     # Packet building helpers
│   ├── cs04_coap_reliability.c # Retransmission logic
│   ├── cs04_hardware.c        # Hardware abstractions
│   └── coap.c/coap.h          # microcoap library
├── include/
│   ├── cs04_coap_packet.h
│   ├── cs04_coap_reliability.h
│   └── cs04_hardware.h
├── test/                      # Test Suite
│   ├── unit_component_test.c  # Unit & Component tests
│   ├── integration_testing.md # Integration test plan
│   └── README.md              # Testing documentation
├── index.html                 # Interactive documentation
├── TESTING.md                 # Complete testing guide
├── CMakeLists.txt
└── README.md
```

---

## 📚 Documentation

- **[./test/integration_testing.md](./test/integration_testing.md)** - Complete testing guide with all CoAP commands and examples
- **[./test/README.md](./test/README.md)** - Complete Unit and Componenting testing guide

---

## 🛠️ Technical Specifications

| Parameter | Value |
|-----------|-------|
| **Platform** | Raspberry Pi Pico W |
| **Protocol** | CoAP over UDP (RFC 7252) |
| **Port** | 5683 (default CoAP port) |
| **Block Size** | 1024 bytes |
| **FETCH Buffer** | 1024 bytes (dynamic line limit) |
| **Max Retries** | 4 attempts with exponential backoff |
| **Observe Support** | RFC 7641 compliant |
| **FETCH Support** | RFC 8132 compliant (with Content-Format validation) |
| **Libraries** | microcoap (modified), Pico SDK, lwIP, FatFs |

---

## 🔐 Security Note

This implementation is designed for educational purposes and local network use. For production deployments, consider:
- Adding DTLS support (CoAPs)
- Implementing authentication/authorization
- Rate limiting and input validation
- Secure credential management

---

## 📄 License

- MIT License - See LICENSE.TXT file for details
- BSD License for RPI pico-examples related files - See LICENSE_BSD.TXT file for details
- APACHE License for no-OS-FatFS-SD-SPI-RPi-Pico - See external_libraries\no-OS-FatFS-SD-SPI-RPi-Pico\LICENSE file for details
- MIT License for microcoap - See external_libraries\microcoap\LICENSE.txt file for details

---

## 🙏 Acknowledgments

- microcoap library by Toby Jaffey
- Pico SDK by Raspberry Pi Foundation
- CoAP specifications (RFC 7252, RFC 7641, RFC 8132)

---

**Last Updated:** November 19, 2025
**Author:** CS04
**Platform:** Raspberry Pi Pico WH
