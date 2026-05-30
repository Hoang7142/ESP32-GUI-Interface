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

/* -------------------------------------------------------------------------- */
/*  Commands                                                                  */
/* -------------------------------------------------------------------------- */

/** Gateway asks a node to read its sensors. */
#define CMD_READ_SENSOR   0x01
/** Node replies with sensor readings. */
#define CMD_SENSOR_DATA   0x02
/** Generic acknowledge (e.g. reply to PING). */
#define CMD_ACK           0x03
/** Gateway/node reachability check. */
#define CMD_PING          0x04

/** Maximum application payload bytes inside one packet. */
#define LORA_MAX_PAYLOAD        32
/** Header size: dst + src + cmd + seq + payload_len. */
#define LORA_PACKET_HEADER_SIZE 5
/** CRC field size on the wire (little-endian). */
#define LORA_PACKET_CRC_SIZE    2
/** Smallest valid packet (header + CRC, zero payload). */
#define LORA_PACKET_MIN_SIZE \
  (LORA_PACKET_HEADER_SIZE + LORA_PACKET_CRC_SIZE)
/** Largest valid packet (header + max payload + CRC). */
#define LORA_PACKET_MAX_SIZE \
  (LORA_PACKET_HEADER_SIZE + LORA_MAX_PAYLOAD + LORA_PACKET_CRC_SIZE)

#if defined(__GNUC__)
#define LORA_PACKED __attribute__((packed))
#else
#define LORA_PACKED
#endif

/** On-air packet layout used by gateway and nodes. */
typedef struct LORA_PACKED {
  uint8_t dst;                          /**< Destination node or gateway ID. */
  uint8_t src;                          /**< Sender ID. */
  uint8_t cmd;                          /**< Command code (CMD_*). */
  uint8_t seq;                          /**< Sequence number for matching req/resp. */
  uint8_t payload_len;                  /**< Number of valid bytes in payload[]. */
  uint8_t payload[LORA_MAX_PAYLOAD];    /**< Application data. */
  uint16_t crc;                         /**< CRC-16 over header + payload. */
} lora_packet_t;

/**
 * Example sensor payload (8 bytes).
 * Nodes may use a different layout; the gateway treats payload as opaque bytes.
 */
typedef struct LORA_PACKED {
  int16_t temperature_c10;   /**< Temperature x10, e.g. 285 = 28.5 C. */
  uint16_t humidity_pct10;   /**< Relative humidity x10, e.g. 620 = 62.0 %. */
  uint16_t soil_moisture;    /**< Raw ADC or scaled soil value. */
  uint16_t water_level;      /**< Raw ADC or scaled water level. */
} lora_sensor_payload_t;

/* -------------------------------------------------------------------------- */
/*  CRC                                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Compute CRC-16-CCITT (poly 0x1021, init 0xFFFF).
 * @param data  Bytes to checksum.
 * @param len   Number of bytes.
 * @return 16-bit CRC value.
 */
uint16_t lora_crc16(const uint8_t* data, size_t len);

/* -------------------------------------------------------------------------- */
/*  Encode / decode                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fill a packet struct and compute its CRC.
 * @param pkt          Output packet (must not be NULL).
 * @param dst          Destination address.
 * @param src          Source address.
 * @param cmd          Command byte.
 * @param seq          Sequence number.
 * @param payload      Payload bytes (NULL if payload_len is 0).
 * @param payload_len  Payload length (0..LORA_MAX_PAYLOAD).
 * @return true on success, false on invalid arguments.
 */
bool lora_packet_build(lora_packet_t* pkt, uint8_t dst, uint8_t src, uint8_t cmd,
                       uint8_t seq, const uint8_t* payload, uint8_t payload_len);

/**
 * @brief Serialize a packet to a byte buffer for LoRa transmission.
 * @param pkt      Valid packet with CRC already set.
 * @param buf      Output buffer.
 * @param buf_len  Size of buf.
 * @return Wire length in bytes, or 0 if buf is too small or pkt is invalid.
 */
size_t lora_packet_encode(const lora_packet_t* pkt, uint8_t* buf, size_t buf_len);

/**
 * @brief Parse raw RX bytes into a packet and verify CRC.
 * @param buf       Received bytes from the radio driver.
 * @param buf_len   Number of received bytes.
 * @param pkt_out   Decoded packet (must not be NULL).
 * @return true if length, fields, and CRC are valid; false otherwise.
 */
bool lora_packet_decode(const uint8_t* buf, size_t buf_len, lora_packet_t* pkt_out);

/**
 * @brief Re-check CRC of an already decoded packet.
 * @param pkt  Packet to verify.
 * @return true if stored CRC matches recomputed CRC.
 */
bool lora_packet_verify_crc(const lora_packet_t* pkt);

#ifdef __cplusplus
}
#endif
