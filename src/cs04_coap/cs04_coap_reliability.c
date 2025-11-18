#include "cs04_coap_reliability.h"
#include "lwip/pbuf.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

static pending_message_t pending_messages[MAX_PENDING_MESSAGES];
static retransmit_failure_cb_t failure_callback = NULL;

void coap_reliability_init(void) {
    memset(pending_messages, 0, sizeof(pending_messages));
}

bool coap_store_for_retransmit(uint16_t msg_id, const ip_addr_t *dest_ip,
                                 u16_t dest_port, const uint8_t *packet, size_t len) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
        if (!pending_messages[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        printf("⚠ No free pending slots\n");
        return false;
    }
    
    pending_messages[slot].active = true;
    pending_messages[slot].msg_id = msg_id;
    pending_messages[slot].dest_ip = *dest_ip;
    pending_messages[slot].dest_port = dest_port;
    pending_messages[slot].retransmit_count = 0;
    pending_messages[slot].next_retry_ms = to_ms_since_boot(get_absolute_time()) + ACK_TIMEOUT_MS;
    memcpy(pending_messages[slot].packet_buf, packet, len);
    pending_messages[slot].packet_len = len;
    
    printf("📝 Stored msg_id 0x%04X for retransmission (slot %d)\n", msg_id, slot);
    return true;
}

void coap_clear_pending_message(uint16_t msg_id) {
    for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
        if (pending_messages[i].active && pending_messages[i].msg_id == msg_id) {
            pending_messages[i].active = false;
            printf("✓ Cleared pending message 0x%04X\n", msg_id);
            return;
        }
    }
}

void coap_check_retransmissions(struct udp_pcb *pcb) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
        if (!pending_messages[i].active) continue;
        
        if (now >= pending_messages[i].next_retry_ms) {
            if (pending_messages[i].retransmit_count >= MAX_RETRANSMITS) {
                printf("⚠ Max retransmits (%d) reached for msg_id 0x%04X\n",
                       MAX_RETRANSMITS, pending_messages[i].msg_id);
                
                // Call failure callback if set
                if (failure_callback) {
                    failure_callback(pending_messages[i].msg_id,
                                   &pending_messages[i].dest_ip,
                                   pending_messages[i].dest_port);
                }
                
                pending_messages[i].active = false;
                continue;
            }
            
            // Retransmit with exponential backoff
            struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,
                                       pending_messages[i].packet_len, PBUF_RAM);
            if (p) {
                memcpy(p->payload, pending_messages[i].packet_buf,
                      pending_messages[i].packet_len);
                udp_sendto(pcb, p, &pending_messages[i].dest_ip,
                          pending_messages[i].dest_port);
                pbuf_free(p);
                
                pending_messages[i].retransmit_count++;
                uint32_t backoff = ACK_TIMEOUT_MS *
                                  (1 << pending_messages[i].retransmit_count);
                pending_messages[i].next_retry_ms = now + backoff;
                
                printf("🔄 Retransmit #%d for msg_id 0x%04X (timeout: %lu ms)\n",
                       pending_messages[i].retransmit_count,
                       pending_messages[i].msg_id, backoff);
            }
        }
    }
}

void coap_set_retransmit_failure_callback(retransmit_failure_cb_t callback) {
    failure_callback = callback;
}

// Duplicate detection
void coap_duplicate_detector_init(duplicate_detector_t *detector) {
    memset(detector->recent_msg_ids, 0, sizeof(detector->recent_msg_ids));
    detector->recent_msg_idx = 0;
}

bool coap_is_duplicate_message(duplicate_detector_t *detector, uint16_t msg_id) {
    for (int i = 0; i < RECENT_MSG_HISTORY; i++) {
        if (detector->recent_msg_ids[i] == msg_id) {
            return true;
        }
    }
    return false;
}

void coap_record_message_id(duplicate_detector_t *detector, uint16_t msg_id) {
    detector->recent_msg_ids[detector->recent_msg_idx] = msg_id;
    detector->recent_msg_idx = (detector->recent_msg_idx + 1) % RECENT_MSG_HISTORY;
}
