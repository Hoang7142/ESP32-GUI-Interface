#pragma once

/**
 * @file lora_gateway.h
 * @brief Master/gateway polling scheduler and state machine API.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "lora_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gateway polling state machine (master):
 *
 *                    +--------+
 *                    |  IDLE  |<---------------------------+
 *                    +--------+                            |
 *                         | start poll round                |
 *                         v                                 |
 *                 +---------------+                         |
 *                 | SEND_REQUEST  |                         |
 *                 +---------------+                         |
 *                         |                                 |
 *                         v                                 |
 *                 +-----------------+   timeout / retry     |
 *        +------->| WAIT_RESPONSE   |------------------+    |
 *        |        +-----------------+                  |    |
 *        |                | valid SENSOR_DATA           |    |
 *        |                v                             |    |
 *        |        +-------------------+                 |    |
 *        |        | PROCESS_RESPONSE  |                 |    |
 *        |        +-------------------+                 |    |
 *        |                |                             |    |
 *        |                v                             |    |
 *        |        +-------------+                       |    |
 *        +--------|  NEXT_NODE   | (retry or next node)  |    |
 *                 +-------------+                       |    |
 *                         | all nodes polled            |    |
 *                         +-----------------------------+
 */

/** Static configuration for the gateway polling scheduler. */
typedef struct {
  uint8_t gateway_id;           /**< This gateway's address (e.g. 0x01). */
  const uint8_t* node_ids;      /**< Array of slave node IDs to poll. */
  uint8_t node_count;           /**< Number of entries in node_ids. */
  uint32_t response_timeout_ms; /**< Max wait for SENSOR_DATA after READ_SENSOR. */
  uint8_t max_retries;          /**< Retries per node before marking offline. */
  uint32_t poll_interval_ms;    /**< Delay between full poll rounds. */
} lora_gateway_config_t;

/** Runtime online/offline tracking for one slave node. */
typedef struct {
  uint8_t node_id;       /**< Node address. */
  bool online;           /**< true if last poll succeeded. */
  uint32_t last_seen_ms; /**< millis() timestamp of last good response. */
  uint8_t fail_count;    /**< Consecutive failed poll rounds. */
} lora_node_status_t;

/** @brief Send raw bytes through the radio driver. @return true if TX succeeded. */
typedef bool (*lora_gateway_send_fn)(const uint8_t* data, size_t len);
/** @brief Return true when the radio has a received packet ready to read. */
typedef bool (*lora_gateway_rx_pending_fn)(void);
/**
 * @brief Read one received packet from the radio driver.
 * @return Number of bytes read, 0 if nothing, negative on error.
 */
typedef int (*lora_gateway_receive_fn)(uint8_t* data, size_t max_len,
                                       int16_t* rssi_out);
/** @brief Platform time in milliseconds (Arduino millis() or HAL_GetTick()). */
typedef uint32_t (*lora_gateway_millis_fn)(void);

/** Radio HAL callbacks injected by the application (ESP32 / STM32). */
typedef struct {
  lora_gateway_send_fn send;           /**< Transmit raw packet bytes. */
  lora_gateway_rx_pending_fn rx_pending; /**< Check RX interrupt / flag. */
  lora_gateway_receive_fn receive;     /**< Read one RX packet. */
  lora_gateway_millis_fn millis;       /**< Current time [ms]. */
} lora_gateway_radio_t;

/**
 * @brief Called when a valid SENSOR_DATA response is received from a node.
 * @param node_id     Source node that replied.
 * @param payload     Sensor bytes from the packet.
 * @param payload_len Length of payload.
 * @param rssi        RSSI of the received frame [dBm].
 */
typedef void (*lora_gateway_sensor_cb_t)(uint8_t node_id, const uint8_t* payload,
                                         uint8_t payload_len, int16_t rssi);

/** Internal states of the non-blocking gateway state machine. */
typedef enum {
  LORA_GW_STATE_IDLE = 0,           /**< Waiting for next poll interval. */
  LORA_GW_STATE_SEND_REQUEST,       /**< Sending CMD_READ_SENSOR. */
  LORA_GW_STATE_WAIT_RESPONSE,      /**< Waiting for CMD_SENSOR_DATA. */
  LORA_GW_STATE_PROCESS_RESPONSE,   /**< Validating and handling response. */
  LORA_GW_STATE_NEXT_NODE,          /**< Advance to next node in the list. */
} lora_gateway_state_t;

/** Gateway context; call lora_gateway_poll() repeatedly from loop/main. */
typedef struct {
  lora_gateway_config_t config;       /**< Polling IDs, timeouts, and intervals. */
  lora_gateway_radio_t radio;         /**< Injected send/receive/millis callbacks. */
  lora_gateway_sensor_cb_t on_sensor_data; /**< App callback for valid SENSOR_DATA. */
  lora_node_status_t* node_status;    /**< Per-node online/offline table. */
  uint8_t node_status_count;          /**< Length of node_status array. */
  lora_gateway_state_t state;         /**< Current state machine state. */
  uint8_t current_node_index;         /**< Index into config.node_ids being polled. */
  uint8_t current_seq;                /**< Sequence number for the active request. */
  uint8_t retry_count;                /**< Retries so far for the current node. */
  uint32_t state_enter_ms;            /**< millis() when the current state was entered. */
  uint32_t next_poll_ms;              /**< millis() when the next poll round may start. */
  bool poll_active;                   /**< true while a poll round is in progress. */
} lora_gateway_t;

/**
 * @brief Initialize gateway context, config, radio callbacks, and node status table.
 * @param gw              Gateway instance to initialize.
 * @param config          Polling configuration (IDs, timeouts, retries).
 * @param radio           Radio driver callbacks (send/receive/millis).
 * @param on_sensor_data  Optional callback for valid sensor responses (may be NULL).
 * @param status_buf      Caller-owned array for per-node online status.
 * @param status_buf_len  Length of status_buf (must be >= config->node_count).
 * @return true on success, false if any argument is invalid.
 */
bool lora_gateway_init(lora_gateway_t* gw, const lora_gateway_config_t* config,
                       const lora_gateway_radio_t* radio,
                       lora_gateway_sensor_cb_t on_sensor_data,
                       lora_node_status_t* status_buf, uint8_t status_buf_len);

/**
 * @brief Start a poll round immediately (normally triggered automatically by poll()).
 * @param gw  Initialized gateway instance.
 */
void lora_gateway_start_poll(lora_gateway_t* gw);

/**
 * @brief Run one step of the gateway state machine (non-blocking; call from loop).
 *        Sends READ_SENSOR, waits for SENSOR_DATA, retries on timeout, marks
 *        nodes offline, and invokes on_sensor_data when data arrives.
 * @param gw  Initialized gateway instance.
 */
void lora_gateway_poll(lora_gateway_t* gw);

/**
 * @brief Get the current gateway state machine state.
 * @param gw  Gateway instance (may be NULL).
 * @return Current state, or LORA_GW_STATE_IDLE if gw is NULL.
 */
lora_gateway_state_t lora_gateway_get_state(const lora_gateway_t* gw);

/**
 * @brief Look up runtime status for a specific node ID.
 * @param gw       Gateway instance.
 * @param node_id  Node address to query.
 * @return Pointer to status entry, or NULL if not found.
 */
const lora_node_status_t* lora_gateway_get_node_status(const lora_gateway_t* gw,
                                                       uint8_t node_id);

#ifdef __cplusplus
/**
 * @brief ESP32 application setup: wire lora_radio callbacks and start polling.
 *        Call after loraBegin() succeeds.
 * @return true if lora_gateway_init succeeded.
 */
bool initLoRaGateway();

/**
 * @brief ESP32 application loop helper: run gateway poll and log round status.
 *        Call every loop() iteration when LoRa is enabled.
 */
void loraGatewayPoll();
#endif

#ifdef __cplusplus
}
#endif
