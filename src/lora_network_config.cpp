/** @file lora_network_config.cpp
 *  @brief Default network addresses and timing constants for the gateway.
 */

#include "lora_network_config.h"

/** Default slave node ID table: Node1=0x11, Node2=0x12, Node3=0x13. */
const uint8_t LORA_NODE_IDS[LORA_NODE_COUNT] = {0x11, 0x12, 0x13};
