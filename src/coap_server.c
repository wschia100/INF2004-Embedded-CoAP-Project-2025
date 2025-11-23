#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
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

FATFS server_fs;

// --- Hardware Pins ---
#define LED_PIN 28       // GPIO pin for onboard RGB LED
#define BUZZER_PIN 18    // GPIO pin for buzzer
#define BUTTON_1_PIN 20  // GPIO pin for Byte as Observe Notification
#define BUTTON_2_PIN 21  // GPIO pin for Observe Notification 2
#define BUTTON_3_PIN 22  // GPIO pin for Observe Notification 3

// --- Wi-Fi Credentials ---
#define WIFI_SSID "lomohomo"  // Network SSID for Wi-Fi connection
#define WIFI_PASS "K0piP3ng"  // Network password

// --- CoAP Settings ---
#define COAP_SERVER_PORT 5683  // UDP listening port for CoAP server
#define MAX_SUBSCRIBERS 5  // Max simultaneous button notification subscribers
#define MAX_TOKEN_LEN 8    // Maximum supported CoAP token length

// --- Reliability Settings ---
#define SUBSCRIBER_TIMEOUT_MS \
    (3 * 60 * 60 * 1000)     // Timeout for dead subscribers (ms)
#define TIMEOUT_THRESHOLD 3  // Max consecutive missed ACKs before pruning

// --- File Transfer Settings ---
#define FILE_TO_SEND "server.txt"
#define BLOCK_SIZE 1024
#define IMAGE_TO_SEND "server.jpg"

// --- Network Static IP Configuration ---
#define STATIC_IP_ADDR "192.168.137.50"
#define STATIC_NETMASK "255.255.255.0"
#define STATIC_GATEWAY "192.168.137.1"

// --- WS2812 Settings ---
PIO pio_ws2812 = pio0;
int sm_ws2812 = 0;

// Global buffers for Block2 options (must persist after function returns)
static uint8_t global_block2_buf[3];   // Holds latest Block2 option value
static uint8_t global_content_format;  // Holds last used Content-Format value

static bool led_state = false;     // Tracks current LED state
static bool buzzer_state = false;  // Tracks current buzzer state

// Use shared duplicate detector
static duplicate_detector_t
    server_dup_detector;  // Duplicate detection for CoAP CON requests

// --- Subscriber Management ---
typedef struct {
    bool active;                        // True if slot filled by subscriber
    ip_addr_t ip;                       // Subscriber's IP address
    u16_t port;                         // Subscriber's UDP port
    coap_buffer_t token;                // CoAP observe token
    uint8_t token_data[MAX_TOKEN_LEN];  // Token data for matching
    uint16_t observe_seq;               // Current Observe sequence number
    uint32_t last_ack_time;             // Time (ms) of last ACK from subscriber
    uint32_t timeout_sessions;          // Count of consecutive missed ACKs
} coap_subscriber_t;

static coap_subscriber_t
    subscribers[MAX_SUBSCRIBERS];  // All observe subscribers

// --- File Transfer State ---
typedef struct {
    FIL file;              // FATFS file handle for ongoing blockwise transfer
    uint32_t block_num;    // Current block number being sent
    uint32_t total_size;   // File size in bytes
    uint32_t bytes_sent;   // Total bytes sent so far
    bool transfer_active;  // True if transfer ongoing
    bool is_image;         // True if sending an image
    char filename[32];     // Output filename in use
    bool waiting_for_ack;  // True if awaiting ACK for current block
} file_transfer_state_t;

static file_transfer_state_t file_state;  // Instance for active file transfer

// --- UDP Control Block ---
struct udp_pcb *pcb;  // Main UDP listening socket for server

// --- Function Prototypes ---
void init_hardware(void);
int handle_get_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                    coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                    const ip_addr_t *addr, u16_t port);
int handle_get_buttons(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                       coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                       const ip_addr_t *addr, u16_t port);
int handle_get_actuators(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                         coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                         const ip_addr_t *addr, u16_t port);
int handle_put_actuators(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                         coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                         const ip_addr_t *addr, u16_t port);
int handle_ipatch_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                       coap_packet_t *outpkt, uint8_t idhi, uint8_t idlo,
                       const ip_addr_t *addr, u16_t port);
int handle_fetch_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                      coap_packet_t *outpkt, uint8_t idhi, uint8_t idlo,
                      const ip_addr_t *addr, u16_t port);
void udp_recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port);
void prune_dead_subscribers(void);
void on_retransmit_failure(uint16_t msg_id, const ip_addr_t *ip, u16_t port);

// --- Endpoints ---
static const coap_endpoint_path_t path_buttons = { 1, { "buttons" } };
static const coap_endpoint_path_t path_actuators = { 1, { "actuators" } };
static const coap_endpoint_path_t path_file = { 1, { "file" } };
const coap_endpoint_t endpoints[] = {
    { COAP_METHOD_GET, (coap_endpoint_func) handle_get_buttons, &path_buttons,
      "ct=0;obs" },
    { COAP_METHOD_GET, (coap_endpoint_func) handle_get_actuators,
      &path_actuators, "ct=0" },
    { COAP_METHOD_GET, (coap_endpoint_func) handle_get_file, &path_file,
      "ct=0" },  // NEW
    { COAP_METHOD_PUT, (coap_endpoint_func) handle_put_actuators,
      &path_actuators, "ct=0" },
    { COAP_METHOD_iPATCH, (coap_endpoint_func) handle_ipatch_file, &path_file,
      "ct=0" },
    { COAP_METHOD_FETCH, (coap_endpoint_func) handle_fetch_file, &path_file,
      "ct=0" },
    { (coap_method_t) 0, NULL, NULL, NULL }
};

// Called when a retransmission for a subscriber or block fails beyond
// threshold.
void on_retransmit_failure(uint16_t msg_id, const ip_addr_t *ip, u16_t port)
{
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 0, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 800, 300);
    sleep_ms(350);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 0, 0.1f));

    if (file_state.transfer_active) {
        printf("Stopping file transfer due to retransmission failure\n");
        f_close(&file_state.file);
        file_state.transfer_active = false;
        file_state.waiting_for_ack = false;
        file_state.block_num = 0;
    }

    for (int j = 0; j < MAX_SUBSCRIBERS; j++) {
        if (subscribers[j].active && ip_addr_cmp(&subscribers[j].ip, ip) &&
            subscribers[j].port == port) {
            subscribers[j].timeout_sessions++;
            printf("⚠ Subscriber %d timeout session count: %lu\n", j,
                   subscribers[j].timeout_sessions);
            break;
        }
    }
}

// Sets up all required hardware (LED, buttons, buzzer, SD card, WS2812).
void init_hardware(void)
{
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    hw_buzz(BUZZER_PIN, 1000, 30);

    gpio_init(BUTTON_1_PIN);
    gpio_set_dir(BUTTON_1_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_1_PIN);

    gpio_init(BUTTON_2_PIN);
    gpio_set_dir(BUTTON_2_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_2_PIN);

    gpio_init(BUTTON_3_PIN);
    gpio_set_dir(BUTTON_3_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_3_PIN);

    uint offset = pio_add_program(pio_ws2812, &ws2812_program);
    ws2812_program_init(pio_ws2812, sm_ws2812, offset, LED_PIN, 800000, false);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(10, 0, 10, 0.1f));

    if (!hw_sd_init(&server_fs)) {
        while (true)
            ;
    }

    file_state.transfer_active = false;
    file_state.block_num = 0;
    file_state.waiting_for_ack = false;

    // Initialize shared libraries
    coap_reliability_init();
    coap_duplicate_detector_init(&server_dup_detector);
    coap_set_retransmit_failure_callback(on_retransmit_failure);
}

// Removes dead subscribers who miss too many ACKs/heartbeats.
void prune_dead_subscribers(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subscribers[i].active)
            continue;

        if (subscribers[i].timeout_sessions >= TIMEOUT_THRESHOLD) {
            printf("⚠ Removing subscriber %d after %lu timeout sessions\n", i,
                   subscribers[i].timeout_sessions);
            subscribers[i].active = false;
        }

        if (now - subscribers[i].last_ack_time > SUBSCRIBER_TIMEOUT_MS) {
            printf("⚠ Subscriber %d timed out (no ACK for %lu ms)\n", i,
                   now - subscribers[i].last_ack_time);
            subscribers[i].timeout_sessions++;
            subscribers[i].last_ack_time = now;
        }
    }
}

// Add new subscribers to CoAP Observe
int add_subscriber(const ip_addr_t *ip, u16_t port, const coap_buffer_t *token)
{
    printf("add_subscriber called from %s:%d\n", ip4addr_ntoa(ip), port);

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!subscribers[i].active) {
            subscribers[i].active = true;
            subscribers[i].ip = *ip;
            subscribers[i].port = port;
            subscribers[i].observe_seq = 0;
            subscribers[i].last_ack_time = to_ms_since_boot(
                get_absolute_time());
            subscribers[i].timeout_sessions = 0;

            size_t len = token->len > MAX_TOKEN_LEN ? MAX_TOKEN_LEN
                                                    : token->len;
            memcpy(subscribers[i].token_data, token->p, len);
            subscribers[i].token.p = subscribers[i].token_data;
            subscribers[i].token.len = len;

            printf("✓ Added subscriber at index %d\n", i);
            return i;
        }
    }

    printf("✗ No free subscriber slots!\n");
    return -1;
}

// Handles incoming GET file requests; processes Block2 and sends correct block
// or error.
int handle_get_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                    coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                    const ip_addr_t *addr, u16_t port)
{
    printf("Received GET /file from %s:%d\n", ip4addr_ntoa(addr), port);

    // Parse Block2 option to see which block client is requesting
    uint8_t count = 0;
    const coap_option_t *block2_opt = coap_findOptions(
        inpkt, COAP_OPTION_BLOCK2, &count);

    uint32_t block_num = 0;
    uint8_t szx = 6;  // Default: SZX=6 = 1024 bytes
    bool more_requested = false;

    if (block2_opt && count > 0) {
        // Use cs04 helper to parse Block2 option
        if (!coap_parse_block2_option(block2_opt, &block_num, &more_requested,
                                      &szx)) {
            printf("✗ Failed to parse Block2 option\n");
            return coap_make_response(
                scratch, outpkt, (uint8_t *) "Invalid Block2", 14, id_hi, id_lo,
                &inpkt->tok, COAP_RSPCODE_BAD_REQUEST,
                COAP_CONTENTTYPE_TEXT_PLAIN);
        }
        printf("  Client requesting block %lu (SZX=%d)\n", block_num, szx);
    } else {
        printf("  Initial GET request, starting from block 0\n");
    }

    // Determine file to send based on query parameter
    bool send_image = false;
    const coap_option_t *query_opt = coap_findOptions(
        inpkt, COAP_OPTION_URI_QUERY, &count);
    if (query_opt && count > 0) {
        if (strncmp((char *) query_opt->buf.p, "type=image", 10) == 0) {
            send_image = true;
        }
    }

    const char *filename = send_image ? IMAGE_TO_SEND : FILE_TO_SEND;

    // Visual feedback
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 50, 0, 0.5f));

    hw_buzz(BUZZER_PIN, 1500, 50);

    // Open file
    FIL fil;
    FRESULT res = f_open(&fil, filename, FA_READ);
    if (res != FR_OK) {
        printf("✗ Failed to open file %s: %d\n", filename, res);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "File not found", 14, id_hi, id_lo,
            &inpkt->tok, COAP_RSPCODE_NOT_FOUND, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    // Calculate block size from SZX
    uint32_t block_size = coap_block_size_from_szx(szx);
    if (block_size > BLOCK_SIZE) {
        block_size = BLOCK_SIZE;  // Clamp to our maximum
        szx = 6;                  // Adjust SZX
    }

    FSIZE_t file_size = f_size(&fil);
    uint32_t total_blocks = (file_size + block_size - 1) / block_size;

    // Seek to requested block position
    f_lseek(&fil, block_num * block_size);

    // Read one block
    static uint8_t block_data[BLOCK_SIZE];
    UINT bytes_read = 0;
    res = f_read(&fil, block_data, block_size, &bytes_read);
    f_close(&fil);

    if (res != FR_OK) {
        printf("✗ File read error: %d\n", res);
        return coap_make_response(scratch, outpkt, (uint8_t *) "Read error", 10,
                                  id_hi, id_lo, &inpkt->tok,
                                  COAP_RSPCODE_SERVICE_UNAVAILABLE,
                                  COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    printf("  Sending block %lu/%lu (%u bytes, block_size=%lu)\n",
           block_num + 1, total_blocks, bytes_read, block_size);

    // Determine if there are more blocks
    bool more_blocks = (block_num + 1) < total_blocks;

    // Use cs04 helper to build Block2 response
    uint8_t content_format = send_image ? 42 : 0;
    coap_build_block2_response(scratch, outpkt, inpkt, id_hi, id_lo, block_num,
                               more_blocks, szx, block_data, bytes_read,
                               content_format);

    // Visual feedback
    if (!more_blocks) {
        printf("✓ File transfer complete (last block)\n");
        sleep_ms(80);
        hw_play_file_complete_signal(pio_ws2812, sm_ws2812, BUZZER_PIN);
    } else {
        sleep_ms(50);
        ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 10, 0.1f));
    }

    return 0;  // Success
}

// Handles Observe/GET for button status/notifications.
int handle_get_buttons(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                       coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                       const ip_addr_t *addr, u16_t port)
{
    printf("\n=== handle_get_buttons ===\n");
    printf("Request from: %s:%d\n", ip4addr_ntoa(addr), port);

    uint8_t count = 0;
    const coap_option_t *observe_opt = coap_findOptions(
        inpkt, COAP_OPTION_OBSERVE, &count);

    if (observe_opt && count > 0) {
        uint32_t observe_val = coap_get_option_uint(&observe_opt->buf);
        printf("Observe value: %lu\n", observe_val);

        if (observe_val == 0) {
            printf("\n>>> Observe registration from: %s:%d\n\n",
                   ip4addr_ntoa(addr), port);

            int sub_index = add_subscriber(addr, port, &inpkt->tok);
            if (sub_index != -1) {
                coap_make_response(scratch, outpkt, NULL, 0, id_hi, id_lo,
                                   &inpkt->tok, COAP_RSPCODE_CONTENT,
                                   COAP_CONTENTTYPE_TEXT_PLAIN);

                uint8_t obs_buf[2];
                size_t obs_len = coap_set_option_uint(
                    obs_buf, subscribers[sub_index].observe_seq);
                coap_add_option(outpkt, COAP_OPTION_OBSERVE, obs_buf, obs_len);

                printf("Subscription acknowledged.\n\n");
                return 0;
            } else {
                return coap_make_response(
                    scratch, outpkt, NULL, 0, id_hi, id_lo, &inpkt->tok,
                    COAP_RSPCODE_BAD_REQUEST, COAP_CONTENTTYPE_NONE);
            }
        }
    }

    char payload[64];
    snprintf(payload, sizeof(payload), "BTN1=%d, BTN2=%d, BTN3=%d",
             !gpio_get(BUTTON_1_PIN), !gpio_get(BUTTON_2_PIN),
             !gpio_get(BUTTON_3_PIN));

    return coap_make_response(
        scratch, outpkt, (const uint8_t *) payload, strlen(payload), id_hi,
        id_lo, &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
}

// Handles GET for actuator status (LED/buzzer).
int handle_get_actuators(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                         coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                         const ip_addr_t *addr, u16_t port)
{
    printf("Received GET /actuators from %s:%d\n", ip4addr_ntoa(addr), port);

    char payload[64];
    snprintf(payload, sizeof(payload), "LED=%s,BUZZER=%s",
             led_state ? "ON" : "OFF", buzzer_state ? "ON" : "OFF");

    printf("📤 Sending actuator status: %s\n", payload);

    return coap_make_response(
        scratch, outpkt, (const uint8_t *) payload, strlen(payload), id_hi,
        id_lo, &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
}

// Handles PUT requests to modify actuator state.
int handle_put_actuators(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                         coap_packet_t *outpkt, uint8_t id_hi, uint8_t id_lo,
                         const ip_addr_t *addr, u16_t port)
{
    printf("Received PUT /actuators from %s:%d\n", ip4addr_ntoa(addr), port);

    if (inpkt->payload.len > 0) {
        printf("📥 Received payload (%d bytes): %.*s\n", inpkt->payload.len,
               inpkt->payload.len, inpkt->payload.p);
    }

    if (inpkt->payload.len == 0)
        return coap_make_response(scratch, outpkt, NULL, 0, id_hi, id_lo,
                                  &inpkt->tok, COAP_RSPCODE_BAD_REQUEST,
                                  COAP_CONTENTTYPE_NONE);

    if (strstr((char *) inpkt->payload.p, "LED=ON")) {
        ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 50, 50, 0.5f));
        led_state = true;
    } else if (strstr((char *) inpkt->payload.p, "LED=OFF")) {
        ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 0, 0, 0.0f));
        led_state = false;
    }

    if (strstr((char *) inpkt->payload.p, "BUZZER=ON")) {
        hw_buzz(BUZZER_PIN, 1200, 100);
        buzzer_state = true;
        buzzer_state = false;
    }

    const char *resp = "OK";
    return coap_make_response(scratch, outpkt, (uint8_t *) resp, strlen(resp),
                              id_hi, id_lo, &inpkt->tok, COAP_RSPCODE_CHANGED,
                              COAP_CONTENTTYPE_TEXT_PLAIN);
}

// Handles iPATCH requests to append lines to file.
int handle_ipatch_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                       coap_packet_t *outpkt, uint8_t idhi, uint8_t idlo,
                       const ip_addr_t *addr, u16_t port)
{
    printf("Received iPATCH /file from %s:%d\n", ip4addr_ntoa(addr), port);

    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(50, 20, 0, 0.5f));
    hw_buzz(BUZZER_PIN, 1400, 50);

    if (inpkt->payload.len == 0) {
        printf("⚠️ No payload in iPATCH request\n");
        return coap_make_response(scratch, outpkt, NULL, 0, idhi, idlo,
                                  &inpkt->tok, COAP_RSPCODE_BAD_REQUEST,
                                  COAP_CONTENTTYPE_NONE);
    }

    printf("📥 Received append payload (%d bytes): '%.*s'\n",
           inpkt->payload.len, inpkt->payload.len, inpkt->payload.p);

    FIL file;
    FRESULT fr = f_open(&file, FILE_TO_SEND, FA_OPEN_APPEND | FA_WRITE);

    if (fr != FR_OK) {
        printf("✗ Failed to open file for append: %d\n", fr);
        return coap_make_response(scratch, outpkt, NULL, 0, idhi, idlo,
                                  &inpkt->tok, COAP_RSPCODE_SERVICE_UNAVAILABLE,
                                  COAP_CONTENTTYPE_NONE);
    }

    UINT bytes_written = 0;
    fr = f_write(&file, inpkt->payload.p, inpkt->payload.len, &bytes_written);

    if (fr == FR_OK) {
        f_write(&file, "\n", 1, &bytes_written);
        f_close(&file);
        printf("✓ Appended %d bytes to file\n", inpkt->payload.len);

        hw_play_string_signal(pio_ws2812, sm_ws2812, BUZZER_PIN);

        const char *resp = "Appended";
        return coap_make_response(
            scratch, outpkt, (uint8_t *) resp, strlen(resp), idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_CHANGED, COAP_CONTENTTYPE_TEXT_PLAIN);
    } else {
        f_close(&file);
        printf("✗ Failed to write to file: %d\n", fr);
        return coap_make_response(scratch, outpkt, NULL, 0, idhi, idlo,
                                  &inpkt->tok, COAP_RSPCODE_SERVICE_UNAVAILABLE,
                                  COAP_CONTENTTYPE_NONE);
    }
}

// Handles FETCH requests for specific file lines.
int handle_fetch_file(coap_rw_buffer_t *scratch, const coap_packet_t *inpkt,
                      coap_packet_t *outpkt, uint8_t idhi, uint8_t idlo,
                      const ip_addr_t *addr, u16_t port)
{
    printf("Received FETCH /file from %s:%d\n", ip4addr_ntoa(addr), port);

    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 20, 50, 0.5f));
    hw_buzz(BUZZER_PIN, 1600, 50);

    // ✅ Step 1: Validate Content-Format option is present
    uint8_t count = 0;
    const coap_option_t *cf_opt = coap_findOptions(
        inpkt, COAP_OPTION_CONTENT_FORMAT, &count);

    if (!cf_opt || count == 0) {
        printf("✗ Missing Content-Format option\n");
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Content-Format required", 23, idhi,
            idlo, &inpkt->tok, COAP_RSPCODE_BAD_REQUEST,
            COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    // ✅ Step 2: Check if we support this Content-Format
    uint32_t content_format = coap_get_option_uint(&cf_opt->buf);

    if (content_format != COAP_CONTENTTYPE_TEXT_PLAIN) {
        printf(
            "✗ Unsupported Content-Format: %lu (expected 0 for text/plain)\n",
            content_format);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Unsupported format", 18, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_UNSUPPORTED_CONTENT_FORMAT,
            COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    // ✅ Step 3: Validate payload is not empty
    if (inpkt->payload.len == 0) {
        printf("✗ Empty FETCH payload\n");
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Empty payload", 13, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_BAD_REQUEST, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    // ✅ Step 4: Parse payload (Range format: "start,end")
    char line_count_str[32] = { 0 };
    size_t copy_len = inpkt->payload.len < 31 ? inpkt->payload.len : 31;
    memcpy(line_count_str, inpkt->payload.p, copy_len);

    // ✅ Parse "start,end" format (inclusive range)
    int start_line = 0;
    int end_line = 4;  // Default: lines 0-4 (5 lines)

    char *comma = strchr(line_count_str, ',');
    if (comma) {
        *comma = '\0';
        start_line = atoi(line_count_str);
        end_line = atoi(comma + 1);
        printf("📖 Parsed: lines %d to %d (inclusive)\n", start_line, end_line);
    } else {
        int num = atoi(line_count_str);
        if (num > 0) {
            end_line = num - 1;
        }
        printf("📖 Parsed: first %d lines (0-%d)\n", num, end_line);
    }

    // ✅ Calculate number of lines requested
    int num_lines = end_line - start_line + 1;

    // ✅ Step 5: Validate ONLY invalid inputs (negative or reversed)
    if (start_line < 0) {
        printf("✗ Invalid start line: %d (must be >= 0)\n", start_line);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Invalid start line", 18, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_BAD_REQUEST, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    if (end_line < 0) {
        printf("✗ Invalid end line: %d (must be >= 0)\n", end_line);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Invalid end line", 16, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_BAD_REQUEST, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    if (end_line < start_line) {
        printf("✗ End line (%d) must be >= start line (%d)\n", end_line,
               start_line);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "Invalid range", 13, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_BAD_REQUEST, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    printf("📖 Fetching lines %d to %d (%d lines requested)\n", start_line,
           end_line, num_lines);

    // ✅ Step 6: Open file and start reading
    FIL file;
    FRESULT fr = f_open(&file, FILE_TO_SEND, FA_READ);

    if (fr != FR_OK) {
        printf("✗ Failed to open file: %d\n", fr);
        return coap_make_response(
            scratch, outpkt, (uint8_t *) "File not found", 14, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_NOT_FOUND, COAP_CONTENTTYPE_TEXT_PLAIN);
    }

#define FETCH_BUFFER_SIZE 1024
    static char fetch_buffer[FETCH_BUFFER_SIZE];
    int buffer_pos = 0;
    int lines_read = 0;
    char line[256];
    bool buffer_full = false;

    // ✅ Skip to start_line
    int current_line = 0;
    while (current_line < start_line &&
           f_gets(line, sizeof(line), &file) != NULL) {
        current_line++;
    }

    // Check if we reached start_line or hit EOF early
    if (current_line < start_line) {
        // Started beyond file length
        f_close(&file);
        printf("⚠️ Start line %d is beyond file length (file has ~%d lines)\n",
               start_line, current_line);
        // Return empty payload (graceful)
        return coap_make_response(scratch, outpkt, NULL, 0, idhi, idlo,
                                  &inpkt->tok, COAP_RSPCODE_CONTENT,
                                  COAP_CONTENTTYPE_TEXT_PLAIN);
    }

    // ✅ Read from start_line to end_line (or until buffer full)
    while (lines_read < num_lines &&
           f_gets(line, sizeof(line), &file) != NULL) {
        int line_len = strlen(line);

        // ✅ Check if adding this line would overflow buffer
        if (buffer_pos + line_len >= FETCH_BUFFER_SIZE) {
            printf("⚠️ Buffer would overflow! Stopping at line %d (read %d of "
                   "%d requested)\n",
                   start_line + lines_read, lines_read, num_lines);
            buffer_full = true;
            break;  // Stop reading, send what we have
        }

        // Add line to buffer
        memcpy(&fetch_buffer[buffer_pos], line, line_len);
        buffer_pos += line_len;
        lines_read++;
    }

    f_close(&file);

    // ✅ Determine response based on what happened
    if (buffer_full) {
        // Buffer filled before completing request
        printf("⚠️ Buffer full! Returning %d lines (requested %d)\n", lines_read,
               num_lines);

        return coap_make_response(
            scratch, outpkt, (uint8_t *) fetch_buffer, buffer_pos, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);

    } else if (lines_read == 0) {
        // No lines read (start was beyond EOF or file empty)
        printf("⚠️ No lines read (file might be empty or start beyond EOF)\n");
        return coap_make_response(scratch, outpkt, NULL, 0, idhi, idlo,
                                  &inpkt->tok, COAP_RSPCODE_CONTENT,
                                  COAP_CONTENTTYPE_TEXT_PLAIN);

    } else {
        // Success: got all requested lines (or file ended naturally)
        printf("✓ Successfully read %d lines (%d bytes) from lines %d to %d\n",
               lines_read, buffer_pos, start_line, start_line + lines_read - 1);

        hw_play_fetch_signal(pio_ws2812, sm_ws2812, BUZZER_PIN);

        return coap_make_response(
            scratch, outpkt, (uint8_t *) fetch_buffer, buffer_pos, idhi, idlo,
            &inpkt->tok, COAP_RSPCODE_CONTENT, COAP_CONTENTTYPE_TEXT_PLAIN);
    }
}

// UDP packet receive callback. Handles all incoming UDP/CoAP packets.
void udp_recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
    printf("\n--- UDP packet from %s:%d ---\n", ip4addr_ntoa(addr), port);

    coap_packet_t pkt = { 0 };
    int parse_rc = coap_parse(&pkt, (const uint8_t *) p->payload, p->len);

    if (parse_rc != 0) {
        printf("Parse failed! Error=%d\n", parse_rc);
        pbuf_free(p);
        return;
    }

    // Handle ACK
    if (pkt.hdr.t == COAP_TYPE_ACK) {
        uint16_t msg_id = coap_extract_msg_id(&pkt);
        printf("✓ Received ACK for msg_id 0x%04X\n", msg_id);
        coap_clear_pending_message(msg_id);

        for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
            if (subscribers[i].active &&
                ip_addr_cmp(&subscribers[i].ip, addr) &&
                subscribers[i].port == port) {
                subscribers[i].last_ack_time = to_ms_since_boot(
                    get_absolute_time());
                subscribers[i].timeout_sessions = 0;
                printf("✓ Subscriber %d timeout session count reset to 0\n", i);
                break;
            }
        }

        pbuf_free(p);
        return;
    }

    // Handle CON/NON requests
    if (pkt.hdr.t == COAP_TYPE_CON || pkt.hdr.t == COAP_TYPE_NONCON) {
        uint16_t msg_id = coap_extract_msg_id(&pkt);

        // Check if it's a GET request to /file (skip duplicate detection for
        // block transfers)
        bool is_get_file_request = false;
        if (pkt.hdr.code == COAP_METHOD_GET) {
            uint8_t path_count = 0;
            const coap_option_t *path_opt = coap_findOptions(
                &pkt, COAP_OPTION_URI_PATH, &path_count);
            if (path_opt && path_count > 0) {
                if (path_opt->buf.len == 4 &&
                    strncmp((char *) path_opt->buf.p, "file", 4) == 0) {
                    is_get_file_request = true;
                    printf("  GET /file request (bypassing duplicate "
                           "detection)\n");
                }
            }
        }

        // ✅ Duplicate detection (but SKIP for GET /file)
        if (!is_get_file_request &&
            coap_is_duplicate_message(&server_dup_detector, msg_id)) {
            printf("⚠️ Duplicate CON request (0x%04X), sending ACK\n", msg_id);
            coap_send_ack(pcb, addr, port, &pkt, NULL, 0);
            pbuf_free(p);
            return;
        }


        // Record message ID (but skip GET /file to avoid blocking subsequent
        // blocks)
        if (!is_get_file_request) {
            coap_record_message_id(&server_dup_detector, msg_id);
        }


        uint8_t scratch_buf[1536];  // ⚡ INCREASED from 256 to 1536 for FETCH
                                    // responses
        coap_rw_buffer_t scratch = { scratch_buf, sizeof(scratch_buf) };
        coap_packet_t resp;

        int handler_result = -1;
        const coap_endpoint_t *ep = endpoints;

        while (ep->handler != NULL) {
            if (ep->method != pkt.hdr.code) {
                ep++;
                continue;
            }

            const coap_option_t *opt;
            uint8_t count;

            if (NULL !=
                (opt = coap_findOptions(&pkt, COAP_OPTION_URI_PATH, &count))) {
                if (count != ep->path->count) {
                    ep++;
                    continue;
                }

                bool path_match = true;
                for (int i = 0; i < count; i++) {
                    if (opt[i].buf.len != strlen(ep->path->elems[i]) ||
                        memcmp(ep->path->elems[i], opt[i].buf.p,
                               opt[i].buf.len) != 0) {
                        path_match = false;
                        break;
                    }
                }

                if (!path_match) {
                    ep++;
                    continue;
                }

            } else if (ep->path->count > 0) {
                ep++;
                continue;
            }

            printf("MATCH FOUND! Dispatching to handler...\n");
            handler_result = ((int (*)(
                coap_rw_buffer_t *, const coap_packet_t *, coap_packet_t *,
                uint8_t, uint8_t, const ip_addr_t *, u16_t)) ep->handler)(
                &scratch, &pkt, &resp, pkt.hdr.id[0], pkt.hdr.id[1], addr,
                port);
            break;
        }

        if (handler_result == -1) {
            coap_make_response(&scratch, &resp, NULL, 0, pkt.hdr.id[0],
                               pkt.hdr.id[1], &pkt.tok, COAP_RSPCODE_NOT_FOUND,
                               COAP_CONTENTTYPE_NONE);
            handler_result = 0;
        }

        // ⚡ FIX: Send response if handler succeeded AND request was CON
        if (handler_result == 0 && pkt.hdr.t == COAP_TYPE_CON) {
            size_t resplen = sizeof(scratch_buf);
            int build_rc = coap_build(scratch_buf, &resplen, &resp);

            // printf("🔍 DEBUG: handler_result=%d, build_rc=%d, resplen=%zu\n",
            //    handler_result, build_rc, resplen);

            if (build_rc == 0) {
                struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, resplen, PBUF_RAM);
                if (q) {
                    memcpy(q->payload, scratch_buf, resplen);
                    err_t send_result = udp_sendto(pcb, q, addr, port);
                    pbuf_free(q);

                    if (send_result == ERR_OK) {
                        printf("✓ Sent response (%zu bytes)\n", resplen);
                    } else {
                        printf("✗ udp_sendto failed: %d\n", send_result);
                    }
                } else {
                    printf("✗ pbuf_alloc failed!\n");
                }
            } else {
                printf("✗ coap_build failed: %d\n", build_rc);
            }
        }
    }

    pbuf_free(p);
}

bool init_udp_server()
{
    pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb)
        return false;

    if (udp_bind(pcb, IP_ADDR_ANY, COAP_SERVER_PORT) != ERR_OK)
        return false;

    udp_recv(pcb, udp_recv_callback, NULL);
    return true;
}

int main()
{
    stdio_init_all();
    printf("Starting CoAP Server (FIXED Response Sending)...\n");

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi (%s)...\n", WIFI_SSID);

    while (1) {
        if (cyw43_arch_wifi_connect_timeout_ms(
                WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 30000) == 0) {
            printf("Wi-Fi connected successfully!\n");
            break;
        }

        printf("Wi-Fi connect failed, retrying in 2 seconds...\n");
        sleep_ms(2000);
    }

    struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
    sleep_ms(2000);
    dhcp_stop(netif);

    ip_addr_t ip, mask, gw;
    ip4addr_aton(STATIC_IP_ADDR, &ip);
    ip4addr_aton(STATIC_NETMASK, &mask);
    ip4addr_aton(STATIC_GATEWAY, &gw);

    netif_set_addr(netif_default, &ip, &mask, &gw);
    netif_set_up(netif);

    printf("Static IP set to: %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));

    init_hardware();

    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "/");
    if (fr == FR_OK) {
        printf("\nFiles on SD card:\n");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            printf("  - %s (%lu bytes)\n", fno.fname, fno.fsize);
        }
        f_closedir(&dir);
    }

    memset(subscribers, 0, sizeof(subscribers));
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        subscribers[i].active = false;
    }

    if (!init_udp_server()) {
        printf("UDP server init failed\n");
        return 1;
    }

    printf("CoAP server listening on port %d\n", COAP_SERVER_PORT);
    ws2812_put_pixel(pio_ws2812, sm_ws2812, hw_urgb_u32(0, 10, 0, 0.1f));

    bool btn1_state = true, btn2_state = true, btn3_state = true;
    uint32_t last_prune_time = 0;

    while (true) {
        cyw43_arch_poll();
        coap_check_retransmissions(pcb);

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_prune_time > 5000) {
            prune_dead_subscribers();
            last_prune_time = now;
        }

        bool btn1_pressed = !gpio_get(BUTTON_1_PIN);
        bool btn2_pressed = !gpio_get(BUTTON_2_PIN);
        bool btn3_pressed = !gpio_get(BUTTON_3_PIN);

        if (btn1_pressed && btn1_state) {
            printf("\n=== Button 1: Sending byte ===\n");
            uint8_t payload = 0x42;
            for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                if (subscribers[i].active) {
                    coap_send_con_notification(
                        pcb, &subscribers[i].ip, subscribers[i].port,
                        &subscribers[i].token, subscribers[i].observe_seq++,
                        &payload, 1, false, 0, false, false);
                }
            }
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 50, 0, 0.5f));
            sleep_ms(100);
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 10, 0, 0.1f));
        }
        btn1_state = !btn1_pressed;

        if (btn2_pressed && btn2_state) {
            printf("\n=== Button 2: Sending button state update ===\n");

            char button_payload[64];
            snprintf(button_payload, sizeof(button_payload),
                     "BTN1=%d,BTN2=1,BTN3=%d", !gpio_get(BUTTON_1_PIN),
                     !gpio_get(BUTTON_3_PIN));

            for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                if (subscribers[i].active) {
                    coap_send_con_notification(
                        pcb, &subscribers[i].ip, subscribers[i].port,
                        &subscribers[i].token, subscribers[i].observe_seq++,
                        (const uint8_t *) button_payload,
                        strlen(button_payload), false, 0, false, false);
                }
            }
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 50, 0, 0.5f));
            sleep_ms(100);
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 10, 0, 0.1f));
        }
        btn2_state = !btn2_pressed;


        if (btn3_pressed && btn3_state) {
            printf("\n=== Button 3: Sending button state update ===\n");

            char button_payload[64];
            snprintf(button_payload, sizeof(button_payload),
                     "BTN1=%d,BTN2=%d,BTN3=1", !gpio_get(BUTTON_1_PIN),
                     !gpio_get(BUTTON_2_PIN));

            for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                if (subscribers[i].active) {
                    coap_send_con_notification(
                        pcb, &subscribers[i].ip, subscribers[i].port,
                        &subscribers[i].token, subscribers[i].observe_seq++,
                        (const uint8_t *) button_payload,
                        strlen(button_payload), false, 0, false, false);
                }
            }

            // Visual feedback
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 50, 0, 0.5f));
            sleep_ms(150);
            ws2812_put_pixel(pio_ws2812, sm_ws2812,
                             hw_urgb_u32(0, 10, 0, 0.1f));
        }
        btn3_state = !btn3_pressed;


        sleep_ms(20);
    }

    cyw43_arch_deinit();
    return 0;
}
