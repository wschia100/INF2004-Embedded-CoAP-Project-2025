#include "cs04_coap_packet.h"
#include "cs04_coap_reliability.h"
#include "lwip/pbuf.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define BLOCK_SIZE 1024

/**
 * @brief Generate a random CoAP message ID.
 * @return Pseudorandom 16-bit message ID.
 */
uint16_t coap_generate_msg_id(void)
{
    return (uint16_t) rand();
}

/**
 * @brief Generate a random token for CoAP messages.
 * @param token Resulting coap_buffer_t to fill
 * @param tokendata Buffer to receive token bytes
 * @param len Length of token to generate (bytes)
 */
void coap_generate_token(coap_buffer_t *token, uint8_t *token_data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        token_data[i] = (uint8_t) rand();
    }
    token->p = token_data;
    token->len = len;
}

/**
 * @brief Compare two CoAP tokens for equality.
 * @param tok1 First token
 * @param tok2 Second token
 * @return true if tokens identical, false otherwise
 */
bool coap_token_matches(const coap_buffer_t *tok1, const coap_buffer_t *tok2)
{
    if (tok1->len != tok2->len)
        return false;
    return memcmp(tok1->p, tok2->p, tok1->len) == 0;
}

/**
 * @brief Extract the message ID from a CoAP packet.
 * @param pkt Pointer to the CoAP packet struct
 * @return 16-bit message ID extracted from header
 */
uint16_t coap_extract_msg_id(const coap_packet_t *pkt)
{
    return (pkt->hdr.id[0] << 8) | pkt->hdr.id[1];
}

/**
 * @brief Send a confirmable (CON) CoAP request with optional retransmit
 * tracking.
 * @param pcb UDP protocol control block
 * @param dest_ip IP address of destination
 * @param dest_port UDP port number
 * @param method CoAP method (e.g., GET, PUT)
 * @param uri_path Resource path for request
 * @param token CoAP token for message matching
 * @param payload Optional payload data
 * @param payload_len Length of payload data
 * @param store_for_retransmit If true, stores message for retransmission on
 * loss
 * @return Message ID assigned to request; 0 on error.
 */
uint16_t coap_send_con_request(struct udp_pcb *pcb, const ip_addr_t *dest_ip,
                               u16_t dest_port, coap_method_t method,
                               const char *uri_path, const coap_buffer_t *token,
                               const uint8_t *payload, size_t payload_len,
                               bool store_for_retransmit)
{
    uint8_t buf[128];
    coap_packet_t pkt = { 0 };

    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token ? token->len : 0;
    pkt.hdr.code = method;

    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t) (msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t) (msg_id & 0xFF);

    if (token) {
        pkt.tok = *token;
    }

    if (uri_path) {
        coap_add_option(&pkt, COAP_OPTION_URI_PATH, (const uint8_t *) uri_path,
                        strlen(uri_path));
    }

    if (payload && payload_len > 0) {
        pkt.payload.p = payload;
        pkt.payload.len = payload_len;
    }

    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("ERROR: Failed to build CON request\n");
        return 0;
    }

    // Store for retransmission if requested
    if (store_for_retransmit) {
        coap_store_for_retransmit(msg_id, dest_ip, dest_port, buf, buflen);
    }

    // Send packet
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p) {
        printf("ERROR: Failed to allocate pbuf\n");
        return 0;
    }

    memcpy(p->payload, buf, buflen);
    err_t result = udp_sendto(pcb, p, dest_ip, dest_port);
    pbuf_free(p);

    if (result == ERR_OK) {
        printf("✓ CON request sent (msg_id: 0x%04X)\n", msg_id);
    } else {
        printf("ERROR: udp_sendto failed: %d\n", result);
    }

    return msg_id;
}

/**
 * @brief Send a CON Observe notification or Block2 block with retransmit
 * option.
 * @param pcb UDP protocol control block
 * @param dest_ip Client IP
 * @param dest_port Client UDP port
 * @param token Observation token
 * @param observe_seq Current Observe sequence number
 * @param payload Notification payload
 * @param payload_len Size of payload
 * @param is_block True if block-wise transfer (add Block2 option)
 * @param block_num Block number (if block)
 * @param more_blocks True if more blocks are available
 * @param is_image True if sending image (content format JPEG)
 * @return Message ID; 0 on error
 */
uint16_t coap_send_con_notification(struct udp_pcb *pcb,
                                    const ip_addr_t *dest_ip, u16_t dest_port,
                                    const coap_buffer_t *token,
                                    uint16_t observe_seq,
                                    const uint8_t *payload, size_t payload_len,
                                    bool is_block, uint32_t block_num,
                                    bool more_blocks, bool is_image)
{
    uint8_t buf[BLOCK_SIZE + 200];
    coap_packet_t pkt = { 0 };

    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token->len;
    pkt.hdr.code = COAP_RSPCODE_CONTENT;

    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t) (msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t) (msg_id & 0xFF);

    pkt.tok = *token;

    // Add Observe option
    static uint8_t obs_buf[2];
    size_t obs_len = coap_set_option_uint(obs_buf, observe_seq);
    coap_add_option(&pkt, COAP_OPTION_OBSERVE, obs_buf, obs_len);

    // Add Block2 option if this is a block transfer
    if (is_block) {
        if (is_image) {
            static uint8_t cf_buf[2];
            size_t cf_len = coap_set_option_uint(cf_buf, 22);  // image/jpeg
            coap_add_option(&pkt, COAP_OPTION_CONTENT_FORMAT, cf_buf, cf_len);
        }

        static uint8_t block_buf[3];
        uint32_t block_val = (block_num << 4);
        if (more_blocks)
            block_val |= 0x08;

#if BLOCK_SIZE == 1024
        block_val |= 0x06;  // SZX = 6
#endif

        size_t block_len = coap_set_option_uint(block_buf, block_val);
        coap_add_option(&pkt, COAP_OPTION_BLOCK2, block_buf, block_len);
    }

    pkt.payload.p = payload;
    pkt.payload.len = payload_len;

    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("ERROR: Failed to build notification\n");
        return 0;
    }

    // Store for retransmission
    coap_store_for_retransmit(msg_id, dest_ip, dest_port, buf, buflen);

    // Send packet
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p) {
        printf("ERROR: Failed to allocate pbuf\n");
        return 0;
    }

    memcpy(p->payload, buf, buflen);
    err_t result = udp_sendto(pcb, p, dest_ip, dest_port);
    pbuf_free(p);

    if (result == ERR_OK) {
        printf("✓ Notification sent (msg_id: 0x%04X)\n", msg_id);
    }

    return msg_id;
}

/**
 * @brief Send an ACK for a given request with optional payload.
 * @param pcb UDP protocol control block
 * @param addr Client IP address
 * @param port UDP port
 * @param req Original request packet (for message ID)
 * @param payload Optional ACK payload
 * @param payload_len Payload length
 */
void coap_send_ack(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port,
                   const coap_packet_t *req, const uint8_t *payload,
                   size_t payload_len)
{
    uint8_t buf[64];
    coap_packet_t pkt = { 0 };

    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_ACK;
    pkt.hdr.tkl = 0;
    pkt.hdr.code = 0;
    pkt.hdr.id[0] = req->hdr.id[0];
    pkt.hdr.id[1] = req->hdr.id[1];

    pkt.payload.p = payload;
    pkt.payload.len = payload_len;

    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("Failed to build ACK\n");
        return;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p)
        return;

    memcpy(p->payload, buf, buflen);
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);

    uint16_t msg_id = coap_extract_msg_id(req);
    printf("✓ Sent ACK for msg_id 0x%04X\n", msg_id);
}

/**
 * @brief Send a Block2 ACK with Block2 option value.
 * @param pcb UDP protocol control block
 * @param addr Destination IP
 * @param port UDP port
 * @param req CoAP request being acknowledged
 * @param block2_opt Block2 option to include
 */
void coap_send_block_ack(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port,
                         const coap_packet_t *req,
                         const coap_option_t *block2_opt)
{
    uint8_t buf[64];
    coap_packet_t pkt = { 0 };

    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_ACK;
    pkt.hdr.tkl = 0;
    pkt.hdr.code = MAKE_RSPCODE(2, 4);
    pkt.hdr.id[0] = req->hdr.id[0];
    pkt.hdr.id[1] = req->hdr.id[1];

    coap_add_option(&pkt, COAP_OPTION_BLOCK2, block2_opt->buf.p,
                    block2_opt->buf.len);

    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("Failed to build Block ACK\n");
        return;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p)
        return;

    memcpy(p->payload, buf, buflen);
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
}

/**
 * @brief Send a CoAP FETCH request with content format, optionally store for
 * retransmission.
 * @param pcb UDP control block
 * @param dest_ip Destination address
 * @param dest_port UDP port
 * @param uri_path Resource path
 * @param token CoAP token
 * @param payload Optional payload
 * @param payload_len Payload size
 * @param content_format Content-Format number
 * @param store_for_retransmit If true, enables retransmit tracking
 * @return Message ID assigned, or 0 on error
 */
uint16_t coap_send_fetch_request(struct udp_pcb *pcb, const ip_addr_t *dest_ip,
                                 u16_t dest_port, const char *uri_path,
                                 const coap_buffer_t *token,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t content_format,
                                 bool store_for_retransmit)
{
    uint8_t buf[256];  // Larger buffer for FETCH
    coap_packet_t pkt = { 0 };

    // Build header
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token ? token->len : 0;
    pkt.hdr.code = COAP_METHOD_FETCH;

    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t) (msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t) (msg_id & 0xFF);

    if (token) {
        pkt.tok = *token;
    }

    // Add URI-Path option
    if (uri_path) {
        coap_add_option(&pkt, COAP_OPTION_URI_PATH, (const uint8_t *) uri_path,
                        strlen(uri_path));
    }

    // ✅ Add Content-Format option
    uint8_t cf_buf[2];
    size_t cf_len = coap_set_option_uint(cf_buf, content_format);
    coap_add_option(&pkt, COAP_OPTION_CONTENT_FORMAT, cf_buf, cf_len);

    // ✅ Add Accept option (same as Content-Format for simplicity)
    uint8_t accept_buf[2];
    size_t accept_len = coap_set_option_uint(accept_buf, content_format);
    coap_add_option(&pkt, COAP_OPTION_ACCEPT, accept_buf, accept_len);

    // Add payload
    if (payload && payload_len > 0) {
        pkt.payload.p = (uint8_t *) payload;
        pkt.payload.len = payload_len;
    }

    // Build packet
    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("ERROR: Failed to build FETCH request\n");
        return 0;
    }

    // Store for retransmission if requested
    if (store_for_retransmit) {
        coap_store_for_retransmit(msg_id, dest_ip, dest_port, buf, buflen);
    }

    // Send packet
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p) {
        printf("ERROR: Failed to allocate pbuf\n");
        return 0;
    }

    memcpy(p->payload, buf, buflen);
    err_t result = udp_sendto(pcb, p, dest_ip, dest_port);
    pbuf_free(p);

    if (result == ERR_OK) {
        printf("✓ FETCH request sent (msg_id: 0x%04X, Content-Format: %d)\n",
               msg_id, content_format);
    } else {
        printf("ERROR: udp_sendto failed: %d\n", result);
    }

    return msg_id;
}

/**
 * @brief Extract block number, more bit, and block size from Block2 option.
 * @param block2_opt Block2 option pointer
 * @param block_num Output block number
 * @param more Output: true if more blocks to follow
 * @param block_size Output: block size in bytes
 * @return True if successfully parsed; false otherwise
 */
bool coap_extract_block2_info(const coap_option_t *block2_opt,
                              uint32_t *block_num, bool *more,
                              uint32_t *block_size)
{
    if (!block2_opt)
        return false;

    uint32_t block_val = coap_get_option_uint(&block2_opt->buf);
    *block_num = (block_val >> 4);
    *more = (block_val & 0x08);

    uint32_t szx = block_val & 0x07;
    *block_size = (1 << (szx + 4));

    return true;
}

// Global buffers for Block2 encoding (must persist after function returns)
static uint8_t cs04_block2_buf[3];
static uint8_t cs04_content_format;

bool coap_parse_block2_option(const coap_option_t *block2_opt,
                              uint32_t *block_num, bool *more, uint8_t *szx)
{
    if (!block2_opt || !block_num || !more || !szx) {
        return false;
    }

    uint32_t block_val = 0;
    for (size_t i = 0; i < block2_opt->buf.len; i++) {
        block_val = (block_val << 8) | block2_opt->buf.p[i];
    }

    *block_num = block_val >> 4;
    *more = (block_val & 0x08) != 0;
    *szx = block_val & 0x07;

    return true;
}

/**
 * @brief Parse Block2 option to extract block number, more bit, and SZX value.
 * @param block2_opt Block2 option pointer
 * @param block_num Output: block number
 * @param more Output: true if more blocks are available
 * @param szx Output: block size exponent
 * @return true on success, false if input invalid
 */
size_t coap_encode_block2_option(uint8_t *buf, uint32_t block_num, bool more,
                                 uint8_t szx)
{
    if (!buf)
        return 0;

    uint32_t block2_value = (block_num << 4) | (more ? 0x08 : 0x00) | szx;

    if (block2_value < 256) {
        buf[0] = (uint8_t) block2_value;
        return 1;
    } else if (block2_value < 65536) {
        buf[0] = (uint8_t) (block2_value >> 8);
        buf[1] = (uint8_t) (block2_value & 0xFF);
        return 2;
    } else {
        buf[0] = (uint8_t) (block2_value >> 16);
        buf[1] = (uint8_t) ((block2_value >> 8) & 0xFF);
        buf[2] = (uint8_t) (block2_value & 0xFF);
        return 3;
    }
}

/**
 * @brief Build a CoAP blockwise response with Block2 and Content-Format
 * options.
 * @param scratch Scratch buffer for packet building
 * @param outpkt Output: CoAP response to fill in
 * @param inpkt Incoming request (for tokens, etc.)
 * @param idhi Message ID MSB
 * @param idlo Message ID LSB
 * @param block_num Block number to use
 * @param more True if more blocks are to follow
 * @param szx Block size exponent
 * @param payload Pointer to block payload
 * @param payload_len Payload length in bytes
 * @param content_format Content-Format value to use
 * @return 0 on success, nonzero on error
 */
int coap_build_block2_response(coap_rw_buffer_t *scratch, coap_packet_t *outpkt,
                               const coap_packet_t *inpkt, uint8_t id_hi,
                               uint8_t id_lo, uint32_t block_num, bool more,
                               uint8_t szx, const uint8_t *payload,
                               size_t payload_len, uint8_t content_format)
{
    // Initialize response packet
    outpkt->hdr.ver = 1;
    outpkt->hdr.t = COAP_TYPE_ACK;
    outpkt->hdr.tkl = inpkt->tok.len;
    outpkt->hdr.code = COAP_RSPCODE_CONTENT;
    outpkt->hdr.id[0] = id_hi;
    outpkt->hdr.id[1] = id_lo;
    outpkt->tok = inpkt->tok;
    outpkt->numopts = 0;

    // Add Content-Format option if needed
    if (content_format != 0 || block_num > 0) {
        cs04_content_format = content_format;
        outpkt->opts[outpkt->numopts].num = COAP_OPTION_CONTENT_FORMAT;
        outpkt->opts[outpkt->numopts].buf.p = &cs04_content_format;
        outpkt->opts[outpkt->numopts].buf.len = 1;
        outpkt->numopts++;
    }

    // Add Block2 option
    size_t block2_len = coap_encode_block2_option(cs04_block2_buf, block_num,
                                                  more, szx);
    outpkt->opts[outpkt->numopts].num = COAP_OPTION_BLOCK2;
    outpkt->opts[outpkt->numopts].buf.p = cs04_block2_buf;
    outpkt->opts[outpkt->numopts].buf.len = block2_len;
    outpkt->numopts++;

    // Set payload
    outpkt->payload.p = (uint8_t *) payload;
    outpkt->payload.len = payload_len;

    return 0;
}

/**
 * @brief Build a GET request with Block2 option for blockwise transfer.
 * @param buf Output buffer for built packet
 * @param buflen Input: buffer size, Output: packet length
 * @param token Client token
 * @param uri_path URI path (e.g., "file")
 * @param uri_query Optional URI query (NULL if none, e.g., "type=image")
 * @param block_num Block number to request
 * @param szx Size exponent
 * @param msg_id Output: generated message ID
 * @return 0 on success, error code otherwise
 */
int coap_build_get_with_block2(uint8_t *buf, size_t *buflen,
                               const coap_buffer_t *token, const char *uri_path,
                               const char *uri_query, uint32_t block_num,
                               uint8_t szx, uint16_t *msg_id)
{
    if (!buf || !buflen || !token || !uri_path || !msg_id) {
        return -1;
    }

    coap_packet_t pkt = { 0 };
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token->len;
    pkt.hdr.code = COAP_METHOD_GET;

    *msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t) (*msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t) (*msg_id & 0xFF);
    pkt.tok = *token;

    // Add URI-Path option
    coap_add_option(&pkt, COAP_OPTION_URI_PATH, (const uint8_t *) uri_path,
                    strlen(uri_path));

    // Add optional URI-Query
    if (uri_query) {
        coap_add_option(&pkt, COAP_OPTION_URI_QUERY,
                        (const uint8_t *) uri_query, strlen(uri_query));
    }

    // Add Block2 option if block_num > 0 (not initial request)
    if (block_num > 0 || szx != 6) {
        static uint8_t block2_buf[3];
        size_t block2_len = coap_encode_block2_option(block2_buf, block_num,
                                                      false, szx);
        coap_add_option(&pkt, COAP_OPTION_BLOCK2, block2_buf, block2_len);
    }

    return coap_build(buf, buflen, &pkt);
}


/**
 * @brief Calculate block size in bytes from SZX value.
 * @param szx Block size exponent (0–6)
 * @return Block size in bytes
 */
uint32_t coap_block_size_from_szx(uint8_t szx)
{
    if (szx > 6)
        szx = 6;              // Cap at maximum
    return (1 << (szx + 4));  // 2^(SZX+4)
}
