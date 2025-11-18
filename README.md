# CoAP IoT System for Raspberry Pi Pico W

A lightweight CoAP client-server implementation for Raspberry Pi Pico W with file transfer, observable resources, and GPIO control.

## Features

- **CoAP Server**: Observable button states, actuator control (LED/buzzer), SD card file operations
- **CoAP Client**: Auto-subscribe to notifications, send commands, receive files with block transfer
- **Reliable Transfer**: Automatic retransmission, duplicate detection, block-wise file transfer
- **Hardware Integration**: WS2812 RGB LED, buzzer, buttons, SD card storage

## Quick Start

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
- **CoAP Port**: 5683

## Usage

### Server
- **Button 1 (GP20)**: Send byte notification to subscribers
- **Button 2 (GP21)**: Send string notification
- **Button 3 (GP22)**: Start file transfer (text/image toggle)

### Client
- **Button 1 (GP21)**: Toggle LED/buzzer ON/OFF
- **Button 2 (GP20)**: Append line to server file
- **Button 3 (GP22)**: Fetch 5 lines from server

## CoAP Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/buttons` | Observable button states |
| GET/PUT | `/actuators` | Control LED and buzzer |
| iPATCH | `/file` | Append data to server.txt |
| FETCH | `/file` | Read N lines from server.txt |

## Project Structure

```
pico-coap-project/
├── src/                          # Application code
│   ├── cs04_coap/                # Shared CoAP utilities
│   ├── coap_client.c             # Client application
│   └── coap_server.c             # Server application
├── external_libraries/           # Third-party libraries
│   ├── microcoap/                # MIT License
│   └── no-OS-FatFS/              # Apache 2.0 License
├── ws2812.*/lwipopts.h          # Config files (from pico-examples)
└── LICENSE_*.txt                 # License files
```

See `src/README.md` for detailed technical documentation.

## Third-Party Libraries

- **Raspberry Pi Pico SDK** - BSD 3-Clause License
- **microcoap** by Toby Jaffey - MIT License
- **no-OS-FatFS-SD-SPI-RPi-Pico** by carlk3 - Apache 2.0 License

## License

See `LICENSE_BSD.txt`, `LICENSE_MIT.txt`, and `LICENSE_APACHE.txt` for component licenses.
