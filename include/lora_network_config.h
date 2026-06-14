#pragma once

/**
 * @file lora_network_config.h
 * @brief Gateway/node IDs (must match ESP32_LORA gateway).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_GATEWAY_ID           0x01
#define LORA_NODE_COUNT           3
#define LORA_RESPONSE_TIMEOUT_MS  2000
#define LORA_MAX_RETRIES          3
#define LORA_POLL_INTERVAL_MS     5000

/** Set one ID per board at compile time: 0x11, 0x12, or 0x13. */
#ifndef MY_NODE_ID
#define MY_NODE_ID                0x11 // TODO: Update to correct node ID by reading from input GPIO
#endif

#define GATEWAY_ID                LORA_GATEWAY_ID

#ifdef __cplusplus
}
#endif
