#ifndef CS04_COAP_PACKET_H
#define CS04_COAP_PACKET_H

#include "coap.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include <stdint.h>
#include <stdbool.h>

// Sends a confirmable (CON) CoAP request.
// Optionally stores the message for retransmission.
uint16_t coap_send_con_request(struct udp_pcb *pcb, const ip_addr_t *dest_ip,
                               u16_t dest_port, coap_method_t method,
                               const char *uri_path, const coap_buffer_t *token,
                               const uint8_t *payload, size_t payload_len,
                               bool store_for_retransmit);

// Sends a CON notification with observe and/or blockwise options.
uint16_t coap_send_con_notification(struct udp_pcb *pcb,
                                    const ip_addr_t *dest_ip, u16_t dest_port,
                                    const coap_buffer_t *token,
                                    uint16_t observe_seq,
                                    const uint8_t *payload, size_t payload_len,
                                    bool is_block, uint32_t block_num,
                                    bool more_blocks, bool is_image);

// Sends an ACK packet in reply to a CoAP request.
void coap_send_ack(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port,
                   const coap_packet_t *req, const uint8_t *payload,
                   size_t payload_len);

// Sends an ACK with a Block2 option for blockwise transfer.
void coap_send_block_ack(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port,
                         const coap_packet_t *req,
                         const coap_option_t *block2_opt);

// Sends a FETCH request with a specific Content-Format option.
uint16_t coap_send_fetch_request(struct udp_pcb *pcb, const ip_addr_t *dest_ip,
                                 u16_t dest_port, const char *uri_path,
                                 const coap_buffer_t *token,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t content_format,
                                 bool store_for_retransmit);

// Extracts the message ID from a CoAP packet.
uint16_t coap_extract_msg_id(const coap_packet_t *pkt);

// Generate message ID
uint16_t coap_generate_msg_id(void);

// Generates a random token of specified length and stores it in
// token/tokendata.
void coap_generate_token(coap_buffer_t *token, uint8_t *token_data, size_t len);

// Compares two CoAP tokens for equality.
bool coap_token_matches(const coap_buffer_t *tok1, const coap_buffer_t *tok2);

// Helper to parse block transfer option and extract parameters.
bool coap_extract_block2_info(const coap_option_t *block2_opt,
                              uint32_t *block_num, bool *more,
                              uint32_t *block_size);

// Helper to parse Block2 option and extract parameters for blockwise transfer.
bool coap_parse_block2_option(const coap_option_t *block2_opt,
                              uint32_t *block_num, bool *more, uint8_t *szx);

// Helper to encode Block2 option for a packet.
size_t coap_encode_block2_option(uint8_t *buf, uint32_t block_num, bool more,
                                 uint8_t szx);

// Helper to build a blockwise transfer response with Block2 and Content-Format
// options.
int coap_build_block2_response(coap_rw_buffer_t *scratch, coap_packet_t *outpkt,
                               const coap_packet_t *inpkt, uint8_t id_hi,
                               uint8_t id_lo, uint32_t block_num, bool more,
                               uint8_t szx, const uint8_t *payload,
                               size_t payload_len, uint8_t content_format);

// Helper to build a GET request with Block2 option for a specific block.
// Used by client for blockwise file or image transfer.
int coap_build_get_with_block2(uint8_t *buf, size_t *buflen,
                               const coap_buffer_t *token, const char *uri_path,
                               const char *uri_query, uint32_t block_num,
                               uint8_t szx, uint16_t *msg_id);

// Computes the block size given SZX value.
uint32_t coap_block_size_from_szx(uint8_t szx);


#endif  // CS04_COAP_PACKET_H
