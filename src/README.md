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
// Send CON request (GET/PUT/iPATCH)
uint16_t coap_send_con_request(
    struct udp_pcb *pcb,
    const ip_addr_t *dest_ip,
    u16_t dest_port,
    coap_method_t method,
    const char *uri_path,
    const coap_buffer_t *token,
    const uint8_t *payload,
    size_t payload_len,
    bool store_for_retransmit
);

// Send FETCH request (RFC 8132 compliant)
uint16_t coap_send_fetch_request(
    struct udp_pcb *pcb,
    const ip_addr_t *dest_ip,
    u16_t dest_port,
    const char *uri_path,
    const coap_buffer_t *token,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t content_format,  // REQUIRED: 0 for text/plain
    bool store_for_retransmit
);

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

// Send ACK response
void coap_send_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const uint8_t *payload,
    size_t payload_len
);

// Send Block2 ACK for file transfers
void coap_send_block_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const coap_option_t *block2_opt
);

// Helper: Generate random message ID
uint16_t coap_generate_msg_id(void);

// Helper: Generate random token
void coap_generate_token(coap_buffer_t *token, uint8_t *token_data, size_t len);

// Helper: Check if tokens match
bool coap_token_matches(const coap_buffer_t *tok1, const coap_buffer_t *tok2);
```

**Design Notes**:
- Separates FETCH requests from generic CON requests for RFC 8132 compliance
- Automatically adds Content-Format and Accept options for FETCH
- Handles retransmission storage via `cs04_coap_reliability.c`
- Supports both text and image Content-Format for Block2 transfers

***

#### `cs04_coap_reliability.c/h`
**Purpose**: Automatic retransmission and duplicate detection

**Key Functions**:
```c
// Store message for retransmission
void coap_store_for_retransmit(
    uint16_t msg_id,
    const ip_addr_t *dest_ip,
    u16_t dest_port,
    const uint8_t *packet,
    size_t packet_len
);

// Clear message from pending queue (on ACK received)
void coap_clear_pending_message(uint16_t msg_id);

// Periodic retransmission check (call from main loop)
void coap_check_retransmissions(struct udp_pcb *pcb);

// Check if message ID is duplicate
bool coap_is_duplicate_message(duplicate_detector_t *detector, uint16_t msg_id);

// Record message ID to prevent duplicate processing
void coap_record_message_id(duplicate_detector_t *detector, uint16_t msg_id);
```

**Retransmission Logic**:
- **Initial timeout**: 2000ms (2 seconds)
- **Max retries**: 4 attempts
- **Backoff**: Exponential (2s → 4s → 8s → 16s → 32s)
- **Total timeout**: ~62 seconds before giving up
- **Queue size**: 10 pending messages maximum
- **Duplicate detection**: Last 16 message IDs tracked

**Data Structures**:
```c
typedef struct {
    bool active;
    uint16_t msg_id;
    ip_addr_t dest_ip;
    u16_t dest_port;
    uint8_t retransmit_count;
    uint32_t next_retry_ms;
    uint8_t packet_buf;
    size_t packet_len;
} pending_message_t;

typedef struct {
    uint16_t recent_msg_ids[RECENT_MSG_HISTORY];
    uint8_t recent_msg_idx;
} duplicate_detector_t;
```

***

#### `cs04_hardware.c/h`
**Purpose**: Hardware abstraction for GPIO, LED, buzzer, and SD card

**Key Functions**:
```c
// Button state management
typedef struct {
    uint pin;
    bool last_state;
} button_t;

void hw_button_init(button_t *btn, uint pin);
bool hw_button_pressed(button_t *btn);

// LED control (WS2812 RGB)
void hw_led_set_color(uint8_t r, uint8_t g, uint8_t b, float brightness);
void hw_led_off(void);
void hw_led_blink(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms);

// Simple feedback patterns
void hw_signal_success(void);
void hw_signal_error(void);
void hw_signal_progress(void);

// Advanced feedback signals
void hw_play_file_complete_signal(PIO pio, uint sm, uint buzzer_pin);
void hw_play_string_signal(PIO pio, uint sm, uint buzzer_pin);
void hw_play_fetch_signal(PIO pio, uint sm, uint buzzer_pin);
void hw_play_append_success_signal(PIO pio, uint sm, uint buzzer_pin);
void hw_play_fetch_success_signal(PIO pio, uint sm, uint buzzer_pin);

// Buzzer control
void hw_buzz(uint pin, uint frequency_hz, uint duration_ms);

// SD card
bool hw_sd_init(FATFS *fs);
bool hw_file_exists(const char *filename);

// LED color helper
uint32_t hw_urgb_u32(uint8_t r, uint8_t g, uint8_t b, float brightness);
```

**LED Feedback Patterns**:
| Signal | Function | LED Color | Buzzer | Use Case |
|--------|----------|-----------|--------|----------|
| File Complete | `hw_play_file_complete_signal` | Green 3-blink | 1500Hz, 60ms × 3 | File transfer complete |
| String Notification | `hw_play_string_signal` | Green 2-blink | 1200Hz, 60ms × 2 | Button notification received |
| Fetch | `hw_play_fetch_signal` | Cyan 3-blink | 1800Hz, 40ms × 3 | FETCH request sent |
| Append Success | `hw_play_append_success_signal` | Green 2-blink | 1800Hz, 60ms × 2 | iPATCH append confirmed |
| Fetch Success | `hw_play_fetch_success_signal` | Cyan 3-blink | 1800Hz, 40ms × 3 | FETCH response received |

***

### `/src/coap_server.c`

**Main Components**:
1. **Endpoint Handlers** - Process incoming CoAP requests
2. **Button State Management** - Monitor GPIO and send notifications
3. **Subscriber Management** - Track Observe clients
4. **File Transfer** - Block-wise file sending

**Key Endpoint Handlers**:

#### `handle_get_actuators()`
```c
int handle_get_actuators(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t id_hi, uint8_t id_lo,
    const ip_addr_t *addr, u16_t port
)
```
- Returns current LED and buzzer state
- Response: `LED=ON,BUZZER=OFF`

***

#### `handle_put_actuators()`
```c
int handle_put_actuators(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t id_hi, uint8_t id_lo,
    const ip_addr_t *addr, u16_t port
)
```
- Parses payload: `LED=ON`, `BUZZER=OFF`, etc.
- Controls GPIO 28 (LED) and GPIO 18 (buzzer)
- Response: `OK`

***

#### `handle_get_buttons()`
```c
int handle_get_buttons(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t id_hi, uint8_t id_lo,
    const ip_addr_t *addr, u16_t port
)
```
- Checks for Observe option (0 = subscribe)
- Adds client to subscriber list
- Returns current button states
- Response: `BTN1=0, BTN2=1, BTN3=0`

***

#### `handle_get_file()`
```c
int handle_get_file(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t id_hi, uint8_t id_lo,
    const ip_addr_t *addr, u16_t port
)
```
- Supports Block2 option for large file transfers
- Query parameter `type=image` switches to JPEG
- Opens `server.txt` or `server.jpg` from SD card
- Sends 1024-byte blocks with Block2 option
- Tracks transfer state per client

**Block2 Format**:
- `NUM=block_number`, `M=more_blocks`, `SZX=size_exponent`
- Example: Block 0 of 1024 bytes, more blocks: `NUM=0, M=1, SZX=6`

***

#### `handle_ipatch_file()`
```c
int handle_ipatch_file(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t idhi, uint8_t idlo,
    const ip_addr_t *addr, u16_t port
)
```
- Opens `server.txt` in append mode
- Writes payload to end of file
- Response: `Appended`

**Error Handling**:
- File not found → `4.04 Not Found`
- SD card error → `5.03 Service Unavailable`

***

#### `handle_fetch_file()` **(RFC 8132 Compliant)**
```c
int handle_fetch_file(
    coap_rw_buffer_t *scratch,
    const coap_packet_t *inpkt,
    coap_packet_t *outpkt,
    uint8_t idhi, uint8_t idlo,
    const ip_addr_t *addr, u16_t port
)
```

**Payload Format**: `"start,end"` (inclusive range, zero-indexed)
- Example: `"0,4"` = lines 0-4 (5 lines)
- Example: `"10,15"` = lines 10-15 (6 lines)
- Backward compatible: `"5"` = first 5 lines (lines 0-4)

**Validation Steps**:
1. ✅ Check Content-Format option is present
2. ✅ Verify Content-Format = 0 (text/plain)
3. ✅ Parse `"start,end"` from payload
4. ✅ Validate `start >= 0` and `end >= 0`
5. ✅ Validate `end >= start`
6. ✅ Open `server.txt` from SD card
7. ✅ Skip to `start` line
8. ✅ Read lines until `end` or buffer full (1024 bytes)

**Error Responses**:
```c
// Missing Content-Format option
return 4.00 Bad Request: "Content-Format required"

// Wrong Content-Format
return 4.15 Unsupported Content-Format

// Negative line numbers
return 4.00 Bad Request: "Invalid start/end line"

// Reversed range (end < start)
return 4.00 Bad Request: "Invalid range"

// File not found
return 4.04 Not Found
```

**Graceful EOF Handling**:
- Start beyond file length → Returns `2.05 Content` with empty payload
- End beyond file length → Returns partial data (lines until EOF)

***

**Button Notification Flow**:
```c
// Main loop monitors button states
bool btn1_pressed = !gpio_get(BUTTON_1_PIN);  // GP20
bool btn2_pressed = !gpio_get(BUTTON_2_PIN);  // GP21
bool btn3_pressed = !gpio_get(BUTTON_3_PIN);  // GP22

if (btn1_pressed && !prev_btn1_state) {
    // Send byte notification to all subscribers
    notify_subscribers_with_data(0x42, 1);
}

if (btn2_pressed && !prev_btn2_state) {
    // Send button state string
    char payload;
    snprintf(payload, sizeof(payload),
             "BTN1=%d,BTN2=1,BTN3=%d", btn1_state, btn3_state);
    notify_subscribers_with_string(payload);
}

if (btn3_pressed && !prev_btn3_state) {
    // Send button state string
    char payload;
    snprintf(payload, sizeof(payload),
             "BTN1=%d,BTN2=%d,BTN3=1", btn1_state, btn2_state);
    notify_subscribers_with_string(payload);
}
```

**Subscriber Management**:
```c
typedef struct {
    ip_addr_t ip;
    uint16_t port;
    coap_buffer_t token;
    uint8_t token_data[MAX_TOKEN_LEN];
    uint16_t observe_seq;
    uint32_t last_ack_time;
    uint8_t timeout_sessions;
    bool active;
} subscriber_t;

subscriber_t subscribers[MAX_SUBSCRIBERS];
```

- Tracks up to 5 subscribers
- Automatic timeout after 3 failed notifications
- Observe sequence number increments per notification

***

### `/src/coap_client.c`

**Main Components**:
1. **Request Functions** - Send CoAP requests to server
2. **Response Handlers** - Process server responses
3. **Button Input** - Trigger requests via GPIO
4. **Auto-subscribe** - Automatically observe server buttons on startup

**Key Request Functions**:

#### `request_put_actuators()`
```c
void request_put_actuators(const char *payload)
```
- Sends PUT `/actuators` with payload
- Example: `"LED=ON,BUZZER=ON"`
- Triggers yellow LED + 1200Hz buzz

***

#### `request_ipatch_file()`
```c
void request_ipatch_file(const char *line)
```
- Sends iPATCH `/file` to append text
- Triggers orange LED + 1400Hz buzz
- On success: Green 2-blink + 1800Hz

***

#### `request_fetch_file()` **(RFC 8132 Compliant)**
```c
void request_fetch_file(int start_line, int end_line)
```
- Builds payload: `"start,end"` format
- Example: `request_fetch_file(0, 4)` → payload `"0,4"`
- Calls `coap_send_fetch_request()` with Content-Format: 0
- Triggers cyan LED + 1600Hz buzz
- On success: Cyan 3-blink + 1800Hz
- Saves response to `from_server_fetch.txt`

**Example Usage**:
```c
// Fetch first 5 lines (lines 0-4)
request_fetch_file(0, 4);

// Fetch lines 10-15
request_fetch_file(10, 15);

// Pagination example
static int fetch_start = 0;
request_fetch_file(fetch_start, fetch_start + 4);
fetch_start += 5;  // Move to next page
```

***

#### `request_get_file()`
```c
void request_get_file(bool is_image)
```
- Sends GET `/file` or GET `/file?type=image`
- Uses Block2 option for 1024-byte blocks
- Triggers purple LED + 1700Hz buzz
- On complete: Green 3-blink + 1800Hz
- Saves to `from_server.txt` or `from_server.jpg`

**Block Transfer State**:
```c
typedef struct {
    FIL file_handle;
    uint32_t next_block_num;
    bool transfer_in_progress;
    bool is_image;
} block_transfer_state_t;
```

***

**Button Handling**:
```c
// Main loop monitors buttons
button_t btn_toggle, btn_append, btn_fetch;
hw_button_init(&btn_toggle, BUTTON_PUT_PIN);   // GP21
hw_button_init(&btn_append, BUTTON_APPEND_PIN); // GP20
hw_button_init(&btn_fetch, BUTTON_FETCH_PIN);   // GP22

if (hw_button_pressed(&btn_toggle)) {
    request_put_actuators("LED=ON,BUZZER=ON");
}

if (hw_button_pressed(&btn_append)) {
    char line;
    snprintf(line, sizeof(line), "Appended at %lu\n", time_us_32());
    request_ipatch_file(line);
}

// GP22: Short press = FETCH, Long press = GET file
if (gpio_get(BUTTON_FETCH_PIN) == 0) {  // Pressed
    if (fetch_press_start == 0) {
        fetch_press_start = to_ms_since_boot(get_absolute_time());
    }
} else if (fetch_press_start > 0) {  // Released
    uint32_t press_duration = to_ms_since_boot(get_absolute_time()) - fetch_press_start;

    if (press_duration > 1000) {  // Long press (>1s)
        request_get_file(file_type_toggle);  // Block transfer
        file_type_toggle = !file_type_toggle;  // Toggle text/image
    } else if (press_duration > 50) {  // Short press (debounced)
        request_fetch_file(0, 4);  // FETCH first 5 lines
    }

    fetch_press_start = 0;
}
```

***

**Response Handling**:
```c
void udp_recv_callback(void *arg, struct udp_pcb *pcb,
                       struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    // Parse CoAP packet
    coap_packet_t pkt;
    coap_parse(&pkt, p->payload, p->tot_len);

    // Extract message ID and check for ACK
    uint16_t msg_id = coap_extract_msg_id(&pkt);
    if (pkt.hdr.t == COAP_TYPE_ACK) {
        coap_clear_pending_message(msg_id);  // Stop retransmissions
    }

    // Check for duplicate
    if (coap_is_duplicate_message(&client_dup_detector, msg_id)) {
        pbuf_free(p);
        return;  // Ignore duplicate
    }
    coap_record_message_id(&client_dup_detector, msg_id);

    // Handle based on response code
    uint8_t code_class = (pkt.hdr.code >> 5);
    uint8_t code_detail = (pkt.hdr.code & 0x1F);

    if (code_class == 2) {
        // Success: 2.xx
        handle_success_response(&pkt);
    } else if (code_class == 4) {
        // Client error: 4.xx
        printf("Error: %d.%02d\n", code_class, code_detail);
    }

    pbuf_free(p);
}
```

***

## Common Patterns

### Adding a New Endpoint

**Server side** (`coap_server.c`):
```c
// 1. Define handler function
int handle_my_endpoint(coap_rw_buffer_t *scratch,
                       const coap_packet_t *inpkt,
                       coap_packet_t *outpkt,
                       uint8_t id_hi, uint8_t id_lo,
                       const ip_addr_t *addr, u16_t port) {
    // Process request
    // Build response
    return coap_make_response(scratch, outpkt, payload, len,
                              id_hi, id_lo, &inpkt->tok,
                              COAP_RSPCODE_CONTENT,
                              COAP_CONTENTTYPE_TEXT_PLAIN);
}

// 2. Register in endpoint table
const coap_endpoint_t endpoints[] = {
    {COAP_METHOD_GET, handle_get_actuators, &path_actuators, "ct=0"},
    {COAP_METHOD_GET, handle_my_endpoint, &path_my, "ct=0"},  // Add here
    // ...
};
```

**Client side** (`coap_client.c`):
```c
// Create request function
void request_my_endpoint(const char *payload) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);

    uint16_t msg_id = coap_send_con_request(
        pcb, &server_ip, COAP_SERVER_PORT,
        COAP_METHOD_GET,
        "my_endpoint",
        &client_token,
        (const uint8_t*)payload, strlen(payload),
        true  // Store for retransmit
    );

    printf("Request sent with msg_id 0x%04X\n", msg_id);
}
```

***

### Adding Hardware Feedback

**In `cs04_hardware.c`**:
```c
void hw_play_my_signal(PIO pio, uint sm, uint buzzer_pin) {
    // Set LED color
    ws2812_put_pixel(pio, sm, hw_urgb_u32(100, 0, 100, 0.5f));  // Purple

    // Play buzzer
    hw_buzz(buzzer_pin, 1500, 100);  // 1500Hz, 100ms

    // Optional: blink pattern
    sleep_ms(100);
    ws2812_put_pixel(pio, sm, 0);  // Off
}
```

***

## Debugging Tips

### Enable Debug Logging
```c
#define DEBUG_COAP 1

#if DEBUG_COAP
    printf("[DEBUG] Message ID: 0x%04X\n", msg_id);
    printf("[DEBUG] Token: ");
    for (int i = 0; i < token.len; i++) {
        printf("%02X", token.p[i]);
    }
    printf("\n");
#endif
```

### Monitor Retransmissions
```c
// In cs04_coap_reliability.c
void coap_check_retransmissions(struct udp_pcb *pcb) {
    for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
        if (pending[i].active) {
            printf("[RETRY] msg_id=0x%04X, attempt=%d/%d\n",
                   pending[i].msg_id, pending[i].retransmit_count, MAX_RETRIES);
        }
    }
}
```

### Capture Packets with Wireshark
```bash
# Filter for CoAP traffic
udp.port == 5683

# Decode as CoAP
Analyze → Decode As → UDP port 5683 → CoAP
```

***

## Performance Considerations

### Memory Usage
- **Client**: ~8KB stack, ~2KB heap
- **Server**: ~10KB stack, ~3KB heap
- **Block transfer buffer**: 1024 bytes
- **FETCH buffer**: 1024 bytes
- **Pending messages**: 10 × 1224 bytes = 12.24KB

### Network Efficiency
- Block2 size: 1024 bytes (optimal for Pi Pico W)
- Token size: 8 bytes (sufficient for uniqueness)
- Observe sequence: 16-bit (65535 notifications before wrap)

### Timing
- ACK timeout: 2 seconds (RFC 7252 default)
- Observe notification interval: Button press triggered
- Subscriber timeout: 3 hours of inactivity

***

## Testing Checklist

- [ ] Server boots and shows green LED
- [ ] Client boots, connects, and subscribes (cyan LED)
- [ ] PUT request controls LED/buzzer
- [ ] iPATCH appends text to file
- [ ] FETCH retrieves specific line range with Content-Format validation
- [ ] GET file transfer completes (text and image)
- [ ] Button notifications received on client
- [ ] Retransmission works (unplug network cable temporarily)
- [ ] Duplicate detection prevents double-processing
- [ ] Timeout shows red LED after 4 retries

***

**Last Updated**: November 23, 2025
**For user documentation, see**: `../README.md`
**For testing commands, see**: `../TESTING.md`
