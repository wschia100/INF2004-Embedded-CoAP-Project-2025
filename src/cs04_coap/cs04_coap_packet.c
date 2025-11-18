#include "cs04_coap_packet.h"
#include "cs04_coap_reliability.h"
#include "lwip/pbuf.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define BLOCK_SIZE 1024

uint16_t coap_generate_msg_id(void) {
    return (uint16_t)rand();
}

void coap_generate_token(coap_buffer_t *token, uint8_t *token_data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        token_data[i] = (uint8_t)rand();
    }
    token->p = token_data;
    token->len = len;
}

bool coap_token_matches(const coap_buffer_t *tok1, const coap_buffer_t *tok2) {
    if (tok1->len != tok2->len) return false;
    return memcmp(tok1->p, tok2->p, tok1->len) == 0;
}

uint16_t coap_extract_msg_id(const coap_packet_t *pkt) {
    return (pkt->hdr.id[0] << 8) | pkt->hdr.id[1];
}

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
) {
    uint8_t buf[128];
    coap_packet_t pkt = {0};
    
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token ? token->len : 0;
    pkt.hdr.code = method;
    
    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t)(msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t)(msg_id & 0xFF);
    
    if (token) {
        pkt.tok = *token;
    }
    
    if (uri_path) {
        coap_add_option(&pkt, COAP_OPTION_URI_PATH, 
                       (const uint8_t *)uri_path, strlen(uri_path));
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
) {
    uint8_t buf[BLOCK_SIZE + 200];
    coap_packet_t pkt = {0};
    
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_CON;
    pkt.hdr.tkl = token->len;
    pkt.hdr.code = COAP_RSPCODE_CONTENT;
    
    uint16_t msg_id = coap_generate_msg_id();
    pkt.hdr.id[0] = (uint8_t)(msg_id >> 8);
    pkt.hdr.id[1] = (uint8_t)(msg_id & 0xFF);
    
    pkt.tok = *token;
    
    // Add Observe option
    static uint8_t obs_buf[2];
    size_t obs_len = coap_set_option_uint(obs_buf, observe_seq);
    coap_add_option(&pkt, COAP_OPTION_OBSERVE, obs_buf, obs_len);
    
    // Add Block2 option if this is a block transfer
    if (is_block) {
        if (is_image) {
            static uint8_t cf_buf[2];
            size_t cf_len = coap_set_option_uint(cf_buf, 22); // image/jpeg
            coap_add_option(&pkt, COAP_OPTION_CONTENT_FORMAT, cf_buf, cf_len);
        }
        
        static uint8_t block_buf[3];
        uint32_t block_val = (block_num << 4);
        if (more_blocks) block_val |= 0x08;
        
        #if BLOCK_SIZE == 1024
        block_val |= 0x06; // SZX = 6
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

void coap_send_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const uint8_t *payload,
    size_t payload_len
) {
    uint8_t buf[64];
    coap_packet_t pkt = {0};
    
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
    if (!p) return;
    
    memcpy(p->payload, buf, buflen);
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
    
    uint16_t msg_id = coap_extract_msg_id(req);
    printf("✓ Sent ACK for msg_id 0x%04X\n", msg_id);
}

void coap_send_block_ack(
    struct udp_pcb *pcb,
    const ip_addr_t *addr,
    u16_t port,
    const coap_packet_t *req,
    const coap_option_t *block2_opt
) {
    uint8_t buf[64];
    coap_packet_t pkt = {0};
    
    pkt.hdr.ver = 1;
    pkt.hdr.t = COAP_TYPE_ACK;
    pkt.hdr.tkl = 0;
    pkt.hdr.code = MAKE_RSPCODE(2, 4);
    pkt.hdr.id[0] = req->hdr.id[0];
    pkt.hdr.id[1] = req->hdr.id[1];
    
    coap_add_option(&pkt, COAP_OPTION_BLOCK2, block2_opt->buf.p, block2_opt->buf.len);
    
    size_t buflen = sizeof(buf);
    if (coap_build(buf, &buflen, &pkt) != COAP_ERR_NONE) {
        printf("Failed to build Block ACK\n");
        return;
    }
    
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, buflen, PBUF_RAM);
    if (!p) return;
    
    memcpy(p->payload, buf, buflen);
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
}

bool coap_extract_block2_info(
    const coap_option_t *block2_opt,
    uint32_t *block_num,
    bool *more,
    uint32_t *block_size
) {
    if (!block2_opt) return false;
    
    uint32_t block_val = coap_get_option_uint(&block2_opt->buf);
    *block_num = (block_val >> 4);
    *more = (block_val & 0x08);
    
    uint32_t szx = block_val & 0x07;
    *block_size = (1 << (szx + 4));
    
    return true;
}
