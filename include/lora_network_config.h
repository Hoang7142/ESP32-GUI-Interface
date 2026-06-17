#pragma once

/**
 * @file lora_network_config.h
 * @brief Default gateway/node IDs and polling timing constants.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Gateway (master) address on the LoRa network. */
#define LORA_GATEWAY_ID           0x01

/** Number of slave nodes in the default polling list. */
#define LORA_NODE_COUNT           3

/** Default slave node IDs polled by the gateway (0x11, 0x12, 0x13). */
extern const uint8_t LORA_NODE_IDS[LORA_NODE_COUNT];

/** Time to wait for SENSOR_DATA after sending READ_SENSOR [ms]. */
#define LORA_RESPONSE_TIMEOUT_MS  500

/** Maximum READ_SENSOR retries per node before marking it offline. */
#define LORA_MAX_RETRIES          3

/** Pause between complete poll rounds (all nodes) [ms]. */
#define LORA_POLL_INTERVAL_MS     3000

#ifdef __cplusplus
}
#endif