#ifndef CS04_COAP_PACKET_H
#define CS04_COAP_PACKET_H

#include "coap.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include <stdint.h>
#include <stdbool.h>

// Send CON request with retransmission tracking
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

// Send CON notification (for server)
uint16_t coap_send_con_notification(
    struct udp_pcb *pcb,
    const ip_addr_t *dest_ip,
    u16_t dest_port,
    const coap_buffer_t *token,
    uint16_t observe_seq,
    const uint8_t *payload,
    size_t payload_len,
    bool is_block,
    uint32_t block_num,
    bool more_blocks,
    bool is_image
);

// Send ACK
void coap_send_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const uint8_t *payload,
    size_t payload_len
);

// Send Block2 ACK
void coap_send_block_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const coap_option_t *block2_opt
);

// Extract message ID
uint16_t coap_extract_msg_id(const coap_packet_t *pkt);

// Generate message ID
uint16_t coap_generate_msg_id(void);

// Token utilities
void coap_generate_token(coap_buffer_t *token, uint8_t *token_data, size_t len);
bool coap_token_matches(const coap_buffer_t *tok1, const coap_buffer_t *tok2);

// Extract Block2 info
bool coap_extract_block2_info(
    const coap_option_t *block2_opt,
    uint32_t *block_num,
    bool *more,
    uint32_t *block_size
);

#endif // CS04_COAP_PACKET_H
