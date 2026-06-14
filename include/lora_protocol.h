#pragma once

/**
 * @file lora_protocol.h
 * @brief LoRa packet format, commands, CRC, and encode/decode API.
 *        Shared by ESP32 gateway and STM32 slave nodes.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMD_READ_SENSOR   0x01
#define CMD_SENSOR_DATA   0x02
#define CMD_ACK           0x03
#define CMD_PING          0x04

#define LORA_MAX_PAYLOAD        32
#define LORA_PACKET_HEADER_SIZE 5
#define LORA_PACKET_CRC_SIZE    2
#define LORA_PACKET_MIN_SIZE \
  (LORA_PACKET_HEADER_SIZE + LORA_PACKET_CRC_SIZE)
#define LORA_PACKET_MAX_SIZE \
  (LORA_PACKET_HEADER_SIZE + LORA_MAX_PAYLOAD + LORA_PACKET_CRC_SIZE)

#if defined(__GNUC__)
#define LORA_PACKED __attribute__((packed))
#else
#define LORA_PACKED
#endif

typedef struct LORA_PACKED {
  uint8_t dst;
  uint8_t src;
  uint8_t cmd;
  uint8_t seq;
  uint8_t payload_len;
  uint8_t payload[LORA_MAX_PAYLOAD];
  uint16_t crc;
} lora_packet_t;

typedef struct LORA_PACKED {
  int16_t temperature_c10;
  uint16_t humidity_pct10;
  uint16_t soil_moisture;
  uint16_t water_level;
	uint16_t current_mA;  
	uint8_t rain_status;
	uint8_t flow_rate_Lmin_x10;
} lora_sensor_payload_t;

uint16_t lora_crc16(const uint8_t* data, size_t len);

bool lora_packet_build(lora_packet_t* pkt, uint8_t dst, uint8_t src, uint8_t cmd,
                       uint8_t seq, const uint8_t* payload, uint8_t payload_len);

size_t lora_packet_encode(const lora_packet_t* pkt, uint8_t* buf, size_t buf_len);

bool lora_packet_decode(const uint8_t* buf, size_t buf_len, lora_packet_t* pkt_out);

bool lora_packet_verify_crc(const lora_packet_t* pkt);

#ifdef __cplusplus
}
#endif
