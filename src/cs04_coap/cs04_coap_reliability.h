#ifndef CS04_COAP_RELIABILITY_H
#define CS04_COAP_RELIABILITY_H

#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include <stdint.h>
#include <stdbool.h>

// Configuration
#define MAX_RETRANSMITS 4
#define ACK_TIMEOUT_MS 2000
#define MAX_PENDING_MESSAGES 10
#define RECENT_MSG_HISTORY 16

// Pending message for retransmission
typedef struct {
    bool active;
    uint16_t msg_id;
    ip_addr_t dest_ip;
    u16_t dest_port;
    uint8_t retransmit_count;
    uint32_t next_retry_ms;
    uint8_t packet_buf[1224]; // BLOCK_SIZE + 200
    size_t packet_len;
} pending_message_t;

// Duplicate detector
typedef struct {
    uint16_t recent_msg_ids[RECENT_MSG_HISTORY];
    uint8_t recent_msg_idx;
} duplicate_detector_t;

// Retransmission failure callback
typedef void (*retransmit_failure_cb_t)(uint16_t msg_id, const ip_addr_t *ip, u16_t port);

// Initialize reliability system
void coap_reliability_init(void);

// Store packet for retransmission
bool coap_store_for_retransmit(uint16_t msg_id, const ip_addr_t *dest_ip,
                                 u16_t dest_port, const uint8_t *packet, size_t len);

// Clear pending message (on ACK received)
void coap_clear_pending_message(uint16_t msg_id);

// Check and handle retransmissions (call in main loop)
void coap_check_retransmissions(struct udp_pcb *pcb);

// Set callback for retransmission failure
void coap_set_retransmit_failure_callback(retransmit_failure_cb_t callback);

// Duplicate detection
void coap_duplicate_detector_init(duplicate_detector_t *detector);
bool coap_is_duplicate_message(duplicate_detector_t *detector, uint16_t msg_id);
void coap_record_message_id(duplicate_detector_t *detector, uint16_t msg_id);

#endif // CS04_COAP_RELIABILITY_H
