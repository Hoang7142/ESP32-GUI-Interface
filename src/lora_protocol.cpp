/**
 * @file lora_protocol.c
 * @brief Packet build/encode/decode and CRC-16-CCITT validation.
 */
#include "lora_protocol.h"

#include <string.h>

uint16_t lora_crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }

  return crc;
}

bool lora_packet_build(lora_packet_t* pkt, uint8_t dst, uint8_t src, uint8_t cmd,
                       uint8_t seq, const uint8_t* payload, uint8_t payload_len) {
  if (pkt == NULL || payload_len > LORA_MAX_PAYLOAD) {
    return false;
  }
  if (payload_len > 0 && payload == NULL) {
    return false;
  }

  pkt->dst = dst;
  pkt->src = src;
  pkt->cmd = cmd;
  pkt->seq = seq;
  pkt->payload_len = payload_len;
  memset(pkt->payload, 0, sizeof(pkt->payload));
  if (payload_len > 0) {
    memcpy(pkt->payload, payload, payload_len);
  }

  {
    uint8_t header[LORA_PACKET_HEADER_SIZE + LORA_MAX_PAYLOAD];
    header[0] = pkt->dst;
    header[1] = pkt->src;
    header[2] = pkt->cmd;
    header[3] = pkt->seq;
    header[4] = pkt->payload_len;
    memcpy(header + LORA_PACKET_HEADER_SIZE, pkt->payload, payload_len);
    pkt->crc = lora_crc16(header, LORA_PACKET_HEADER_SIZE + payload_len);
  }
  return true;
}

size_t lora_packet_encode(const lora_packet_t* pkt, uint8_t* buf, size_t buf_len) {
  if (pkt == NULL || buf == NULL || pkt->payload_len > LORA_MAX_PAYLOAD) {
    return 0;
  }

  const size_t wire_len =
      LORA_PACKET_HEADER_SIZE + pkt->payload_len + LORA_PACKET_CRC_SIZE;
  if (buf_len < wire_len) {
    return 0;
  }

  buf[0] = pkt->dst;
  buf[1] = pkt->src;
  buf[2] = pkt->cmd;
  buf[3] = pkt->seq;
  buf[4] = pkt->payload_len;
  memcpy(buf + LORA_PACKET_HEADER_SIZE, pkt->payload, pkt->payload_len);

  const uint16_t crc =
      lora_crc16(buf, LORA_PACKET_HEADER_SIZE + pkt->payload_len);
  buf[LORA_PACKET_HEADER_SIZE + pkt->payload_len] = (uint8_t)(crc & 0xFF);
  buf[LORA_PACKET_HEADER_SIZE + pkt->payload_len + 1] =
      (uint8_t)((crc >> 8) & 0xFF);

  return wire_len;
}

bool lora_packet_decode(const uint8_t* buf, size_t buf_len, lora_packet_t* pkt_out) {
  if (buf == NULL || pkt_out == NULL || buf_len < LORA_PACKET_MIN_SIZE) {
    return false;
  }

  const uint8_t payload_len = buf[4];
  if (payload_len > LORA_MAX_PAYLOAD) {
    return false;
  }

  const size_t expected_len =
      LORA_PACKET_HEADER_SIZE + payload_len + LORA_PACKET_CRC_SIZE;
  if (buf_len < expected_len) {
    return false;
  }

  pkt_out->dst = buf[0];
  pkt_out->src = buf[1];
  pkt_out->cmd = buf[2];
  pkt_out->seq = buf[3];
  pkt_out->payload_len = payload_len;
  memset(pkt_out->payload, 0, sizeof(pkt_out->payload));
  if (payload_len > 0) {
    memcpy(pkt_out->payload, buf + LORA_PACKET_HEADER_SIZE, payload_len);
  }

  {
    const size_t crc_offset = LORA_PACKET_HEADER_SIZE + payload_len;
    pkt_out->crc = (uint16_t)buf[crc_offset] |
                   ((uint16_t)buf[crc_offset + 1] << 8);
  }

  return lora_packet_verify_crc(pkt_out);
}

bool lora_packet_verify_crc(const lora_packet_t* pkt) {
  if (pkt == NULL || pkt->payload_len > LORA_MAX_PAYLOAD) {
    return false;
  }

  uint8_t temp[LORA_PACKET_HEADER_SIZE + LORA_MAX_PAYLOAD];

  temp[0] = pkt->dst;
  temp[1] = pkt->src;
  temp[2] = pkt->cmd;
  temp[3] = pkt->seq;
  temp[4] = pkt->payload_len;
  memcpy(temp + LORA_PACKET_HEADER_SIZE, pkt->payload, pkt->payload_len);

  const uint16_t computed =
      lora_crc16(temp, LORA_PACKET_HEADER_SIZE + pkt->payload_len);
  return computed == pkt->crc;
}
