#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "coap.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "ws2812.h"
#include "ff.h"
#include "sd_card.h"

// ✅ Include shared libraries
#include "cs04_coap_reliability.h"
#include "cs04_coap_packet.h"
#include "cs04_hardware.h"

FATFS client_fs;

// --- Hardware Pins ---
#define LED_PIN 28
#define BUZZER_PIN 18
#define BUTTON_APPEND_PIN 20
#define BUTTON_PUT_PIN 21
#define BUTTON_FETCH_PIN 22

// --- Wi-Fi Credentials ---
#define WIFI_SSID "lomohomo"
#define WIFI_PASS "K0piP3ng"

// --- CoAP Server IP ---
#define COAP_SERVER_IP "192.168.137.50"
#define COAP_SERVER_PORT 5683

// --- File Transfer Settings ---
#define RECEIVED_FILENAME "from_server.txt"
#define MAX_TOKEN_LEN 8
#define RECEIVED_IMAGE_FILENAME "from_server.jpg"
#define BLOCK_SIZE 1024  // Must match server

// --- WS2812 Settings ---
PIO pio_ws2812 = pio0;
int sm_ws2812 = 0;

// --- Global UDP PCB ---
struct udp_pcb *pcb;

// --- Client State ---
static coap_buffer_t client_token;
static uint8_t client_token_data[MAX_TOKEN_LEN];

// Direct SD card file handling
static FIL file_handle;
static bool file_open = false;
static uint32_t last_block_num = 0;
static uint32_t total_bytes_received = 0;  // Track total received

// Use shared duplicate detector
static duplicate_detector_t client_dup_detector;
static bool subscribed = false;

// --- Actuator State ---
static bool led_state = false;
static bool buzzer_state = false;

// Additional state for FETCH
static bool waiting_for_append_response = false;
static bool waiting_for_fetch_response = false;

// --- Function Prototypes ---
void init_hardware(void);
void udp_recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port);
void play_file_complete_signal(void);
void play_string_signal(void);
// Request functions
void request_subscribe_buttons(void);
void request_get_actuators(void);
void request_put_actuators(const char *payload);
void request_ipatch_file(const char *line);
void request_fetch_file(int num_lines);
void on_client_retransmit_failure(uint16_t msg_id, const ip_addr_t *ip, u16_t port);

void play_file_complete_signal(void) {
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1500, 60);
    sleep_ms(70);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1500, 60);
    sleep_ms(70);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1500, 150);
    sleep_ms(80);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
}

void play_string_signal(void) {
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1200, 60);
    sleep_ms(80);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 0, 0.1f));
    sleep_ms(40);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1200, 60);
    sleep_ms(80);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
}

// --- Hardware Initialization ---
void init_hardware(void) {
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    hw_buzz(BUZZER_PIN, 1000, 30);
    
    gpio_init(BUTTON_PUT_PIN);
    gpio_set_dir(BUTTON_PUT_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PUT_PIN);
    
    gpio_init(BUTTON_APPEND_PIN);
    gpio_set_dir(BUTTON_APPEND_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_APPEND_PIN);
    
    gpio_init(BUTTON_FETCH_PIN);
    gpio_set_dir(BUTTON_FETCH_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_FETCH_PIN);
    
    uint offset = pio_add_program(pio_ws2812, &ws2812_program);
    ws2812_program_init(pio_ws2812, sm_ws2812, offset, LED_PIN, 800000, false);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(10, 0, 10, 0.1f));
    
    if (!hw_sd_init(&client_fs)) {
        while (true);
    }
    
    // Initialize state
    file_open = false;
    total_bytes_received = 0;
    last_block_num = 0;
    
    // Initialize shared libraries
    coap_reliability_init();
    coap_duplicate_detector_init(&client_dup_detector);
    coap_generate_token(&client_token, client_token_data, MAX_TOKEN_LEN);
    coap_set_retransmit_failure_callback(on_client_retransmit_failure);
    
    printf("Client initialized with token: ");
    for (int i = 0; i < client_token.len; i++) {
        printf("%02X", client_token.p[i]);
    }
    printf("\n");
}

void on_client_retransmit_failure(uint16_t msg_id, const ip_addr_t *ip, u16_t port) {
    printf("⚠️ Client: Max retransmits reached for msg_id 0x%04X\n", msg_id);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 0, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 600, 200);
    sleep_ms(250);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(10, 0, 10, 0.1f));
}

// --- Request Functions with ORIGINAL Feedback Patterns ---
void request_subscribe_buttons(void) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);
    
    printf("\n=== Subscribing to /buttons (Observe) ===\n");
    
    uint8_t buf[128];
    coap_packet_t pkt = {0};
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = client_token.len;
    pkt.hdr.code = COAP_METHOD_GET;
    
    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t)(msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t)(msg_id & 0xFF);
    pkt.tok = client_token;
    
    static uint8_t obs_buf[1] = {0};
    coap_add_option(&pkt, COAP_OPTION_OBSERVE, obs_buf, 1);
    coap_add_option(&pkt, COAP_OPTION_URI_PATH, (const uint8_t *)"buttons", 7);
    
    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("✗ Failed to build subscribe packet\n");
        return;
    }
    
    coap_store_for_retransmit(msg_id, &server_ip, COAP_SERVER_PORT, buf, buflen);
    
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p) {
        printf("✗ Failed to allocate pbuf\n");
        return;
    }
    
    memcpy(p->payload, buf, buflen);
    err_t result = udp_sendto(pcb, p, &server_ip, COAP_SERVER_PORT);
    pbuf_free(p);
    
    if (result == ERR_OK) {
        printf("✓ Subscribe request sent with msg_id 0x%04X\n", msg_id);
        subscribed = true;
        ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 10, 0.1f));
    } else {
        printf("✗ udp_sendto failed: %d\n", result);
    }
}

void request_get_actuators(void) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);
    printf("\n=== Sending GET /actuators ===\n");
    
    uint16_t msg_id = coap_send_con_request(
        pcb, &server_ip, COAP_SERVER_PORT,
        COAP_METHOD_GET, "actuators", &client_token,
        NULL, 0, true
    );
    
    if (msg_id) {
        printf("✓ GET request sent with msg_id 0x%04X\n", msg_id);
    }
}

void request_put_actuators(const char *payload) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);
    printf("\n=== Sending PUT /actuators ===\n");
    printf("Payload: %s\n", payload);
    
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 50, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1200, 50);
    
    uint16_t msg_id = coap_send_con_request(
        pcb, &server_ip, COAP_SERVER_PORT,
        COAP_METHOD_PUT, "actuators", &client_token,
        (const uint8_t *)payload, strlen(payload), true
    );
    
    if (msg_id) {
        printf("✓ PUT request sent with msg_id 0x%04X\n", msg_id);
        sleep_ms(80);
        ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
    }
}

void request_ipatch_file(const char *line) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);
    printf("\n=== Sending iPATCH /file (APPEND) ===\n");
    printf("Line to append: %s\n", line);
    
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 20, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1400, 50);
    
    waiting_for_append_response = true;
    
    uint16_t msg_id = coap_send_con_request(
        pcb, &server_ip, COAP_SERVER_PORT,
        COAP_METHOD_iPATCH, "file", &client_token,
        (const uint8_t *)line, strlen(line), true
    );
    
    if (msg_id) {
        printf("✓ iPATCH request sent with msg_id 0x%04X\n", msg_id);
    }
}

void request_fetch_file(int num_lines) {
    ip_addr_t server_ip;
    ip4addr_aton(COAP_SERVER_IP, &server_ip);
    printf("\n=== Sending FETCH /file ===\n");
    printf("Requesting %d lines\n", num_lines);
    
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 20, 50, 0.5f));
    hw_buzz(BUZZER_PIN, 1600, 50);
    
    waiting_for_fetch_response = true;
    
    char payload[16];
    snprintf(payload, sizeof(payload), "%d", num_lines);
    
    for (int i = 0; i < client_token.len; i++) {
        printf("%02X", client_token.p[i]);
    }
    printf("\n");
    
    uint16_t msg_id = coap_send_con_request(
        pcb, &server_ip, COAP_SERVER_PORT,
        COAP_METHOD_FETCH, "file", &client_token,
        (const uint8_t *)payload, strlen(payload), true
    );
    
    if (msg_id) {
        printf("✓ FETCH request sent with msg_id 0x%04X\n", msg_id);
        printf("  Payload: '%s'\n", payload);
        printf("  Waiting for response...\n");
    } else {
        printf("✗ FETCH request failed to send\n");
        waiting_for_fetch_response = false;
    }
}

// --- UDP Receive Callback with ORIGINAL Direct SD Write ---
void udp_recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port) {
    printf("\n--- UDP packet received from %s:%d (%d bytes) ---\n",
           ip4addr_ntoa(addr), port, p->len);
    
    coap_packet_t pkt = {0};
    int parse_rc = coap_parse(&pkt, (const uint8_t *)p->payload, p->len);
    if (parse_rc != 0) {
        printf("Parse failed! Error=%d\n", parse_rc);
        pbuf_free(p);
        return;
    }
    
    // Handle ACK responses
    if (pkt.hdr.t == COAP_TYPE_ACK) {
        uint16_t msg_id = coap_extract_msg_id(&pkt);
        printf("✓ Received ACK for msg_id 0x%04X\n", msg_id);
        printf("  Response code: %d.%02d\n", (pkt.hdr.code >> 5) & 0x7, pkt.hdr.code & 0x1F);
        printf("  Payload length: %d bytes\n", pkt.payload.len);
        printf("  Token length: %d bytes\n", pkt.tok.len);
        if (pkt.tok.len > 0) {
            printf("  Token: ");
            for (int i = 0; i < pkt.tok.len; i++) {
                printf("%02X", pkt.tok.p[i]);
            }
            printf("\n");
        }
        coap_clear_pending_message(msg_id);
        
        // Handle subscription ACK
        if (pkt.tok.len == client_token.len &&
            memcmp(pkt.tok.p, client_token.p, client_token.len) == 0) {
            uint8_t obs_count = 0;
            const coap_option_t *obs_opt = coap_findOptions(&pkt, COAP_OPTION_OBSERVE, &obs_count);
            if (obs_opt) {
                printf("✓ Subscription ACK received!\n");
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
            }
        }
        
        // Handle iPATCH response
        if (waiting_for_append_response) {
            printf("✓ Received append confirmation\n");
            waiting_for_append_response = false;
            
            // Success feedback - Green double blink (matching original pattern)
            ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
            hw_buzz(BUZZER_PIN, 1800, 60);
            sleep_ms(70);
            ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 0, 0.0f));
            sleep_ms(30);
            ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
            hw_buzz(BUZZER_PIN, 1800, 60);
            sleep_ms(80);
            ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
            pbuf_free(p);
            return;
        }
        
        // Handle FETCH response - check token FIRST
        bool token_match = (pkt.tok.len == client_token.len &&
                           memcmp(pkt.tok.p, client_token.p, client_token.len) == 0);
        
        // Process FETCH response regardless of waiting flag if token matches and payload exists
        if (token_match && pkt.payload.len > 0 && waiting_for_fetch_response) {
            printf("✓ Received FETCH response (%d bytes)\n", pkt.payload.len);
            waiting_for_fetch_response = false;
            
            // Open file for writing
            FIL fetch_file_handle;
            FRESULT fr = f_open(&fetch_file_handle, "from_server_fetch.txt",
                              FA_WRITE | FA_CREATE_ALWAYS);
            if (fr == FR_OK) {
                UINT bw = 0;
                f_write(&fetch_file_handle, pkt.payload.p, pkt.payload.len, &bw);
                f_close(&fetch_file_handle);
                printf("✓ Saved %d bytes to from_server_fetch.txt\n", bw);
                
                // Success feedback - Cyan triple blink (matching original)
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 50, 0.5f));
                hw_buzz(BUZZER_PIN, 1800, 40);
                sleep_ms(50);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 0, 0.0f));
                sleep_ms(30);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 50, 0.5f));
                hw_buzz(BUZZER_PIN, 1800, 40);
                sleep_ms(50);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 0, 0.0f));
                sleep_ms(30);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 50, 0.5f));
                hw_buzz(BUZZER_PIN, 1800, 40);
                sleep_ms(80);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
            } else {
                printf("✗ Failed to save file: %d\n", fr);
                // Error feedback - Red
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 0, 0, 0.5f));
                hw_buzz(BUZZER_PIN, 400, 100);
                sleep_ms(100);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
            }
            pbuf_free(p);
            return;
        }
        
        // Handle GET /actuators response
        if (pkt.hdr.code >= COAP_RSPCODE_CONTENT && pkt.hdr.code < COAP_RSPCODE_BAD_REQUEST) {
            printf("✓ Success response code: %d.%02d\n",
                   (pkt.hdr.code >> 5) & 0x7, pkt.hdr.code & 0x1F);
            
            if (pkt.payload.len > 0) {
                printf("📥 Response payload (%d bytes): %.*s\n",
                       pkt.payload.len, pkt.payload.len, pkt.payload.p);
                
                // Parse LED state
                if (strstr((char *)pkt.payload.p, "LED=ON")) {
                    led_state = true;
                } else if (strstr((char *)pkt.payload.p, "LED=OFF")) {
                    led_state = false;
                }
                
                if (strstr((char *)pkt.payload.p, "BUZZER=ON")) {
                    buzzer_state = true;
                } else if (strstr((char *)pkt.payload.p, "BUZZER=OFF")) {
                    buzzer_state = false;
                }
            }
        } else if (pkt.hdr.code >= COAP_RSPCODE_BAD_REQUEST) {
            printf("⚠️ Error response code: %d.%02d\n",
                   (pkt.hdr.code >> 5) & 0x7, pkt.hdr.code & 0x1F);
        }
        
        pbuf_free(p);
        return;
    }
    
    // Handle CON notifications
    if (pkt.hdr.t == COAP_TYPE_CON) {
        uint16_t msg_id = coap_extract_msg_id(&pkt);
        printf("Received CON notification (msg_id: 0x%04X)\n", msg_id);
        
        // Duplicate detection with re-ACK
        if (coap_is_duplicate_message(&client_dup_detector, msg_id)) {
            printf("⚠️ Duplicate notification (0x%04X), resending ACK\n", msg_id);
            
            // Check if this is a block transfer
            uint8_t block2_count = 0;
            const coap_option_t *block2_opt = coap_findOptions(&pkt, COAP_OPTION_BLOCK2, &block2_count);
            
            if (block2_opt) {
                // It's a file block - use send_block_ack()
                coap_send_block_ack(pcb, addr, port, &pkt, block2_opt);
            } else {
                // Normal notification - use send_ack()
                coap_send_ack(pcb, addr, port, &pkt, NULL, 0);
            }
            pbuf_free(p);
            return;
        }
        
        coap_record_message_id(&client_dup_detector, msg_id);
        
        uint8_t obs_count = 0;
        const coap_option_t *obs_opt = coap_findOptions(&pkt, COAP_OPTION_OBSERVE, &obs_count);
        
        if (obs_opt && obs_count > 0) {
            uint32_t observe_seq = coap_get_option_uint(&obs_opt->buf);
            printf("📬 Observe notification (seq=%lu)\n", observe_seq);
        }
        
        uint8_t block_count = 0;
        const coap_option_t *block2_opt = coap_findOptions(&pkt, COAP_OPTION_BLOCK2, &block_count);
        
        if (block2_opt && block_count > 0) {
            // Block transfer with DIRECT SD CARD WRITE
            uint32_t block_val = coap_get_option_uint(&block2_opt->buf);
            uint32_t block_num = (block_val >> 4);
            bool more = (block_val & 0x08);
            
            printf("📥 Received file block #%lu (%d bytes)\n", block_num, pkt.payload.len);
            
            // Short buzz feedback per block
            hw_buzz(BUZZER_PIN, 1500, 30);
            ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
            
            // Check if this is block 0 (first block or restart)
            if (block_num == 0) {
                if (!file_open) {
                    // Check for Content-Format option
                    uint8_t cf_count = 0;
                    const coap_option_t *cf_opt = coap_findOptions(&pkt, 
                                                    COAP_OPTION_CONTENT_FORMAT, &cf_count);
                    
                    const char *filename = RECEIVED_FILENAME;  // Default: text
                    if (cf_opt && cf_count > 0) {
                        uint32_t cf_val = coap_get_option_uint(&cf_opt->buf);
                        if (cf_val == 22) {  // 22 = image/jpeg
                            filename = RECEIVED_IMAGE_FILENAME;
                            printf("📷 Receiving JPEG image\n");
                        }
                    }
                    
                    // Open file for DIRECT WRITE
                    FRESULT fr = f_open(&file_handle, filename, FA_WRITE | FA_CREATE_ALWAYS);
                    if (fr == FR_OK) {
                        file_open = true;
                        last_block_num = 0;
                        total_bytes_received = 0;
                        printf("Created new file: %s\n", filename);
                    } else {
                        printf("Failed to create file: %d\n", fr);
                        pbuf_free(p);
                        return;
                    }
                }
            }
            
            // Validate block sequence
            if (file_open && block_num > 0) {
                if (block_num < last_block_num) {
                    // Duplicate - acknowledge it
                    printf("⚠️ Duplicate block %lu (expected %lu), sending ACK\n",
                           block_num, last_block_num);
                    coap_send_block_ack(pcb, addr, port, &pkt, block2_opt);
                    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
                    pbuf_free(p);
                    return;
                } else if (block_num > last_block_num) {
                    // Gap - reject without ACK
                    printf("⚠️ Block gap: expected %lu, got %lu\n",
                           last_block_num, block_num);
                    pbuf_free(p);
                    return;
                }
            }
            
            // Write block DIRECTLY to SD card
            if (file_open) {
                UINT bw = 0;
                
                // Calculate actual block size from Block2 option
                uint32_t szx = block_val & 0x07;  // Extract SZX (3 bits)
                uint32_t block_size = (1 << (szx + 4));  // Calculate: 2^(SZX+4)
                
                f_lseek(&file_handle, block_num * block_size);
                f_write(&file_handle, pkt.payload.p, pkt.payload.len, &bw);
                total_bytes_received += bw;
                
                printf("✓ Wrote block %lu (%d bytes) directly to SD\n", block_num, bw);
                
                // Increment only if this is the expected block
                if (block_num == last_block_num) {
                    last_block_num++;
                }
                
                coap_send_block_ack(pcb, addr, port, &pkt, block2_opt);
                
                // Add small delay to prevent overwhelming the system
                sleep_ms(10);
                
                if (!more) {
                    printf("✓ File transfer complete! Total bytes: %lu\n", total_bytes_received);
                    f_close(&file_handle);
                    file_open = false;
                    last_block_num = 0;
                    total_bytes_received = 0;
                    
                    // Triple blink completion signal
                    play_file_complete_signal();
                } else {
                    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
                }
            }
        } else {
            // Non-block notification (byte or string)
            if (pkt.payload.len == 1) {
                // Byte notification
                printf("📥 Received byte notification: 0x%02X\n", pkt.payload.p[0]);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));
                hw_buzz(BUZZER_PIN, 1500, 60);
                coap_send_ack(pcb, addr, port, &pkt, pkt.payload.p, 1);
                sleep_ms(80);
                ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
            } else if (pkt.payload.len > 1) {
                // String notification
                printf("📥 Received string notification (%d bytes): %.*s\n",
                       pkt.payload.len, pkt.payload.len, pkt.payload.p);
                coap_send_ack(pcb, addr, port, &pkt, pkt.payload.p, pkt.payload.len);
                
                // String signal feedback
                play_string_signal();
            }
        }
        
        pbuf_free(p);
        return;
    }
    
    pbuf_free(p);
}

bool init_udp_client() {
    pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return false;
    udp_recv(pcb, udp_recv_callback, NULL);
    return true;
}

// --- Main ---
int main() {
    stdio_init_all();
    printf("\n=== CoAP Client (FIXED - Direct SD Write) ===\n");
    
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return 1;
    }
    
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi (%s)...\n", WIFI_SSID);
    
    while (1) {
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS,
                                               CYW43_AUTH_WPA2_AES_PSK, 30000) == 0) {
            printf("✓ Wi-Fi connected!\n");
            break;
        }
        printf("Wi-Fi connect failed, retrying...\n");
        sleep_ms(2000);
    }
    
    init_hardware();
    
    if (!init_udp_client()) {
        printf("UDP client init failed\n");
        return 1;
    }
    
    printf("✓ CoAP client initialized\n");
    printf("Server: %s:%d\n", COAP_SERVER_IP, COAP_SERVER_PORT);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(10, 0, 10, 0.1f));
    
    // Auto-subscribe on startup
    sleep_ms(1000);
    printf("\n📡 Auto-subscribing to /buttons...\n");
    request_subscribe_buttons();
    
    button_t btn_toggle, btn_append, btn_fetch;
    hw_button_init(&btn_toggle, BUTTON_PUT_PIN);
    hw_button_init(&btn_append, BUTTON_APPEND_PIN);
    hw_button_init(&btn_fetch, BUTTON_FETCH_PIN);
    
    printf("\n=== Controls ===\n");
    printf("GP21: Toggle LED/BUZZER\n");
    printf("GP20: APPEND to file\n");
    printf("GP22: FETCH from file\n\n");
    
    bool toggle_action = false;
    
    while (true) {
        cyw43_arch_poll();
        coap_check_retransmissions(pcb);
        
        if (hw_button_pressed(&btn_toggle)) {
            if (toggle_action) {
                printf("💡 LED ON, BUZZER ON\n");
                request_put_actuators("LED=ON,BUZZER=ON");
            } else {
                printf("💡 LED OFF\n");
                request_put_actuators("LED=OFF");
            }
            toggle_action = !toggle_action;
        }
        
        if (hw_button_pressed(&btn_append)) {
            printf("📝 Appending to file...\n");
            static int append_count = 0;
            char line[64];
            snprintf(line, sizeof(line), "Client append #%d", ++append_count);
            request_ipatch_file(line);
        }
        
        if (hw_button_pressed(&btn_fetch)) {
            printf("📖 Fetching from file...\n");
            request_fetch_file(5);
        }
        
        sleep_ms(20);
    }
    
    cyw43_arch_deinit();
    return 0;
}