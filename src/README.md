# Source Code Documentation

Technical documentation for the CoAP client-server implementation.

## Architecture Overview

```
Application Layer (coap_client.c / coap_server.c)
        ↓
CoAP Protocol Layer (cs04_coap/)
        ↓
Hardware Abstraction Layer (cs04_hardware.h)
        ↓
External Libraries (microcoap, FatFS, lwIP)
```

## Directory Structure

### `/src/cs04_coap/`

Shared CoAP protocol utilities used by both client and server.

#### `cs04_coap_packet.c/h`
**Purpose**: High-level CoAP packet building and notification sending

**Key Functions**:
```c
// Send CON notification with Block2 support
uint16_t coap_send_con_notification(
    struct udp_pcb *pcb,
    const ip_addr_t *ip,
    u16_t port,
    const coap_buffer_t *token,
    uint16_t observe_seq,
    const uint8_t *payload,
    size_t payload_len,
    bool is_block_transfer,
    uint32_t block_num,
    bool more_blocks,
    bool is_image
);

// Send CON request (GET/PUT/iPATCH/FETCH)
uint16_t coap_send_con_request(
    struct udp_pcb *pcb,
    const ip_addr_t *ip,
    u16_t port,
    uint8_t method,
    const char *path,
    const coap_buffer_t *token,
    const uint8_t *payload,
    size_t payload_len,
    bool store_for_retransmit
);

// Send ACK/Block ACK responses
void coap_send_ack(...);
void coap_send_block_ack(...);
```

**Features**:
- Automatic message ID generation
- Block2 option encoding for file transfers
- Content-Format option for image detection
- Token management

#### `cs04_coap_reliability.c/h`
**Purpose**: Message retransmission and duplicate detection

**Key Data Structures**:
```c
typedef struct {
    bool active;
    uint16_t msgid;
    ip_addr_t dest_ip;
    u16_t dest_port;
    uint8_t buffer[1024];
    size_t buffer_len;
    uint32_t last_send_time;
    uint8_t retries;
} pending_message_t;

typedef struct {
    uint16_t msgids[16];  // Sliding window
    uint8_t count;
    uint8_t head;
} duplicate_detector_t;
```

**Key Functions**:
```c
// Store message for retransmission
void coap_store_for_retransmit(...);

// Check all pending messages and retransmit if needed
void coap_check_retransmissions(struct udp_pcb *pcb);

// Remove message after receiving ACK
void coap_clear_pending_message(uint16_t msgid);

// Duplicate detection
bool coap_is_duplicate_message(duplicate_detector_t *detector, uint16_t msgid);
void coap_record_message_id(duplicate_detector_t *detector, uint16_t msgid);
```

**Retransmission Policy**:
- Max retries: 4
- Timeout sequence: 2s, 4s, 8s, 16s (exponential backoff)
- Callback on failure: `on_retransmit_failure()`

#### `cs04_hardware.c/h`
**Purpose**: Hardware abstraction for GPIO, SD card, and audio/visual feedback

**Key Functions**:
```c
// Button handling with debouncing
typedef struct {
    uint gpio;
    bool last_state;
} button_t;

void hw_button_init(button_t *btn, uint gpio);
bool hw_button_pressed(button_t *btn);

// Buzzer control
void hw_buzz(uint gpio, uint freq_hz, uint duration_ms);

// SD card initialization
bool hw_sd_init(FATFS *fs);

// WS2812 RGB control wrapper
uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness);
```

**Hardware Debouncing**: 50ms polling interval prevents false button presses

---

### `/src/coap_server.c`

**Purpose**: CoAP server with observable resources and file serving

#### Key Configurations
```c
#define STATIC_IP_ADDR    "192.168.137.50"
#define COAP_SERVER_PORT  5683
#define MAX_SUBSCRIBERS   5
#define BLOCK_SIZE        1024
```

#### Endpoints
```c
const coap_endpoint_t endpoints[] = {
    {COAP_METHOD_GET, handle_get_buttons, &path_buttons, "ct=0;obs"},
    {COAP_METHOD_GET, handle_get_actuators, &path_actuators, "ct=0"},
    {COAP_METHOD_PUT, handle_put_actuators, &path_actuators, "ct=0"},
    {COAP_METHOD_iPATCH, handle_ipatch_file, &path_file, "ct=0"},
    {COAP_METHOD_FETCH, handle_fetch_file, &path_file, "ct=0"},
};
```

#### Subscriber Management
```c
typedef struct {
    bool active;
    ip_addr_t ip;
    u16_t port;
    coap_buffer_t token;
    uint16_t observe_seq;
    uint32_t last_ack_time;
    uint32_t timeout_sessions;
} coap_subscriber_t;
```

**Timeout Policy**:
- No ACK for 3 hours → increment timeout counter
- 3 timeouts → subscriber removed
- Checked every 5 seconds in main loop

#### File Transfer State Machine
```c
typedef struct {
    FIL file;
    uint32_t block_num;
    bool transfer_active;
    bool waiting_for_ack;
    bool is_image;
    char filename[32];
} file_transfer_state_t;
```

**Block Transfer Flow**:
1. Button 3 pressed → `start_file_transfer()`
2. For each subscriber → `send_next_file_block()`
3. Wait for ACK → set `waiting_for_ack = true`
4. On ACK received → increment `block_num`, send next block
5. Last block (more=0) → close file, reset state

#### FETCH Implementation
```c
int handle_fetch_file(...) {
    // Parse requested line count from payload
    int numlines = atoi(payload);

    // Read lines into buffer (max 1024 bytes)
    char fetchbuffer[1024];
    while (linesread < numlines && buffer_not_full) {
        fgets(line, 256, file);
        memcpy(fetchbuffer + bufferpos, line, linelen);
    }

    // Send response
    return coap_make_response(..., fetchbuffer, bufferpos, ...);
}
```

**Buffer Limit**: Stops reading when 1024 bytes reached, even if fewer lines than requested

---

### `/src/coap_client.c`

**Purpose**: CoAP client with auto-subscribe and direct SD card file reception

#### Key Configurations
```c
#define COAP_SERVER_IP    "192.168.137.50"
#define BLOCK_SIZE        1024  // Must match server
#define RECEIVED_FILENAME "fromserver.txt"
#define RECEIVED_IMAGE_FILENAME "fromserver.jpg"
```

#### Auto-Subscribe on Startup
```c
int main() {
    // ... Wi-Fi connection ...
    init_hardware();

    sleep_ms(1000);
    printf("Auto-subscribing to buttons...\n");
    request_subscribe_buttons();

    // Main loop with button polling
}
```

#### Direct SD Card Write
**Optimization**: Writes blocks directly to SD card instead of buffering in RAM

```c
void udp_recv_callback(...) {
    // Detect Block2 transfer
    if (block2opt) {
        uint32_t blocknum = blockval >> 4;

        // Open file on block 0
        if (blocknum == 0 && !fileopen) {
            f_open(&filehandle, filename, FA_WRITE | FA_CREATE_ALWAYS);
            fileopen = true;
        }

        // Write block directly to SD
        f_lseek(&filehandle, blocknum * blocksize);
        f_write(&filehandle, pkt.payload.p, pkt.payload.len, &bw);

        // Send ACK
        coap_send_block_ack(...);

        // Close on last block (more=0)
        if (!more) {
            f_close(&filehandle);
            fileopen = false;
        }
    }
}
```

**Block Validation**:
- Duplicate blocks (same blocknum) → re-ACK and discard
- Gap in sequence (blocknum > expected) → reject without ACK
- Server will retransmit missing blocks

#### Button Request Functions
```c
// GP21 - Toggle LED/buzzer
void request_put_actuators(const char *payload) {
    coap_send_con_request(pcb, server_ip, COAP_SERVER_PORT,
                          COAP_METHOD_PUT, "actuators",
                          &client_token, payload, strlen(payload), true);
}

// GP20 - Append to file
void request_ipatch_file(const char *line) {
    coap_send_con_request(pcb, server_ip, COAP_SERVER_PORT,
                          COAP_METHOD_iPATCH, "file",
                          &client_token, line, strlen(line), true);
}

// GP22 - Fetch lines
void request_fetch_file(int numlines) {
    char payload[16];
    snprintf(payload, sizeof(payload), "%d", numlines);
    coap_send_con_request(pcb, server_ip, COAP_SERVER_PORT,
                          COAP_METHOD_FETCH, "file",
                          &client_token, payload, strlen(payload), true);
}
```

---

## Protocol Details

### CoAP Message Types
- **CON**: Confirmable (requires ACK)
- **NON**: Non-confirmable
- **ACK**: Acknowledgment
- **RST**: Reset (not used)

### Custom CoAP Methods
```c
#define COAP_METHOD_iPATCH 0x07  // Custom: append operation
#define COAP_METHOD_FETCH  0x05  // RFC 8132: selective read
```

### Block2 Option Encoding
```
 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+
|  Block Num  |M|  SZX  |
+-+-+-+-+-+-+-+-+

Block Num: Block number (shifted left by 4)
M: More flag (1 = more blocks follow)
SZX: Size exponent (4 = 1024 bytes)
```

### Observe Option
- Value 0: Register for notifications
- Server increments sequence number on each notification
- Client detects stale notifications via sequence

---

## Memory Considerations

### Stack Usage
- **Server scratch buffer**: 1536 bytes (for FETCH responses)
- **Client scratch buffer**: 1024 bytes
- **File transfer buffer**: 1024 bytes (static in function scope)

### Heap Usage
- **lwIP pbufs**: Allocated per packet (freed after processing)
- **Subscriber array**: 5 × ~64 bytes = 320 bytes
- **Pending messages**: 8 × ~1088 bytes = 8.7 KB

### SD Card I/O
- **Direct write**: No intermediate RAM buffer for block transfers
- **FETCH read**: Temporary 1024-byte buffer on stack

---

## Error Handling

### Network Errors
- UDP send failure → logged, no retry (retransmission handles it)
- Parse errors → packet dropped, logged to serial

### File System Errors
- File not found → send 4.04 Not Found
- Write failure → send 5.03 Service Unavailable
- SD card init failure → loop indefinitely until successful

### Timeout Handling
- **Client**: Max 4 retries, then red LED + error callback
- **Server**: Subscriber timeout after 3 × 3-hour periods without ACK

---

## LED Status Codes

### Server
| Color | Pattern | Meaning |
|-------|---------|---------|
| Blue-Green (10,10) | Steady | Idle/ready |
| Green (50,0) | Single | Button pressed / sending |
| Red (50,0,0) | Single | Retransmit failure |

### Client
| Color | Pattern | Meaning |
|-------|---------|---------|
| Blue-Green (10,10) | Steady | Connected/ready |
| Yellow (50,50) | Single | LED/buzzer toggle sent |
| Green (50,0) | Double | Append confirmed |
| Cyan (50,50) | Triple | File transfer complete |
| Red (50,0,0) | Single | Error |

---

## Testing & Debugging

### Enable Tracing
Uncomment in relevant files:
```c
#define TRACE_PRINTF printf
// #define TRACE_PRINTF(fmt, args...)  // Disable
```

### Serial Output
- Server: Lists files on SD card at startup
- Client: Shows token, button presses, block numbers
- Both: Display Wi-Fi connection status, CoAP packet details

### Common Issues

**Client not receiving notifications**:
- Check subscriber array on server (serial output)
- Verify token matches between subscription and notifications
- Ensure NAT doesn't change client port

**File transfer stalls**:
- Check `waiting_for_ack` flag on server
- Verify block numbers increment sequentially
- Look for "Block gap" messages on client

**SD card errors**:
- Reduce SPI baud rate in `hw_config.c`
- Check FAT32 formatting
- Verify power supply (SD cards draw current spikes)

---

## Performance Optimization

### Tuning Parameters

**Retransmission timeout** (`cs04_coap_reliability.h`):
```c
#define INITIAL_TIMEOUT_MS 2000  // Increase for slow networks
```

**SPI baud rate** (`hw_config.c`):
```c
.baud_rate = 12500000,  // Reduce if SD errors occur
```

**Block size** (both client and server):
```c
#define BLOCK_SIZE 1024  // Must match! Use 512 for marginal networks
```

### Profiling Results
- File transfer: ~764 KiB/s write, ~926 KiB/s read (12.5 MHz SPI)
- CoAP round-trip: <50ms on local network
- Button press to notification: <100ms

---

## Future Development

### Planned Features
- Multiple simultaneous file transfers
- DELETE method for file removal
- DTLS security layer
- Power-saving mode (deep sleep between operations)

### Code Improvements
- Refactor endpoint handlers into separate files
- Add unit tests for CoAP packet functions
- Implement CoRE Link Format for `.well-known/core`

---

For build instructions and hardware setup, see root `README.md`.
