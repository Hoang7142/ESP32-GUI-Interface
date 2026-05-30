#pragma once

/**
 * @file lora_node.h
 * @brief Slave node request handler and response state machine API.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "lora_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Slave node state machine:
 *
 *   +---------+
 *   | RX_WAIT |<------------------+
 *   +---------+                   |
 *        | packet for this node   |
 *        v                        |
 * +------------------+            |
 * | PROCESS_REQUEST  |            |
 * +------------------+            |
 *        | CMD_READ_SENSOR         |
 *        v                        |
 * +-------------+                 |
 * | READ_SENSOR |                 |
 * +-------------+                 |
 *        |                        |
 *        v                        |
 * +----------------+              |
 * | SEND_RESPONSE  |--------------+
 * +----------------+
 *
 * Nodes MUST stay in RX until the gateway requests data.
 * Never transmit without a gateway request.
 */

/** Static identity of one slave node. */
typedef struct {
  uint8_t node_id;    /**< This node's address (e.g. 0x11). */
  uint8_t gateway_id; /**< Gateway address to reply to (e.g. 0x01). */
} lora_node_config_t;

/** @brief Send raw bytes through the radio driver. @return true if TX succeeded. */
typedef bool (*lora_node_send_fn)(const uint8_t* data, size_t len);
/** @brief Return true when the radio has a received packet ready to read. */
typedef bool (*lora_node_rx_pending_fn)(void);
/**
 * @brief Read one received packet from the radio driver.
 * @return Number of bytes read, 0 if nothing, negative on error.
 */
typedef int (*lora_node_receive_fn)(uint8_t* data, size_t max_len,
                                    int16_t* rssi_out);

/**
 * @brief Application callback: read sensors and fill payload before responding.
 * @param payload   Buffer to write sensor bytes into.
 * @param max_len   Maximum writable bytes (LORA_MAX_PAYLOAD).
 * @return Number of bytes written (0..max_len).
 */
typedef uint8_t (*lora_node_read_sensor_fn)(uint8_t* payload, uint8_t max_len);

/** Radio HAL + sensor callbacks injected by the application (STM32 / ESP32). */
typedef struct {
  lora_node_send_fn send;               /**< Transmit raw packet bytes. */
  lora_node_rx_pending_fn rx_pending;   /**< Check RX interrupt / flag. */
  lora_node_receive_fn receive;         /**< Read one RX packet. */
  lora_node_read_sensor_fn read_sensor; /**< Fill SENSOR_DATA payload. */
} lora_node_radio_t;

/** Internal states of the non-blocking node state machine. */
typedef enum {
  LORA_NODE_STATE_RX_WAIT = 0,        /**< Listening; default idle state. */
  LORA_NODE_STATE_PROCESS_REQUEST,    /**< Parsing and dispatching a request. */
  LORA_NODE_STATE_READ_SENSOR,        /**< Reading sensors (via callback). */
  LORA_NODE_STATE_SEND_RESPONSE,      /**< Transmitting SENSOR_DATA or ACK. */
} lora_node_state_t;

/** Node context; call lora_node_poll() repeatedly from loop/main. */
typedef struct {
  lora_node_config_t config;          /**< Local node_id and gateway_id. */
  lora_node_radio_t radio;            /**< Injected send/receive/sensor callbacks. */
  lora_node_state_t state;            /**< Current state machine state. */
  lora_packet_t pending_request;    /**< Last accepted request from gateway. */
} lora_node_t;

/**
 * @brief Initialize node context with local ID, gateway ID, and radio callbacks.
 * @param node    Node instance to initialize.
 * @param config  Node identity (node_id, gateway_id).
 * @param radio   Radio driver and sensor callbacks.
 */
void lora_node_init(lora_node_t* node, const lora_node_config_t* config,
                    const lora_node_radio_t* radio);

/**
 * @brief Run one step of the node state machine (non-blocking; call from loop).
 *        Checks for RX, filters by dst address, ignores bad CRC, and responds
 *        only to CMD_READ_SENSOR / CMD_PING directed at this node.
 * @param node  Initialized node instance.
 */
void lora_node_poll(lora_node_t* node);

/**
 * @brief Get the current node state machine state.
 * @param node  Node instance (may be NULL).
 * @return Current state, or LORA_NODE_STATE_RX_WAIT if node is NULL.
 */
lora_node_state_t lora_node_get_state(const lora_node_t* node);

#ifdef __cplusplus
}
#endif
