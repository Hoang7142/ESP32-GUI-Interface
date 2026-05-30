/**
 * @file lora_node.cpp
 * @brief Non-blocking slave response state machine.
 *        Listens in RX, replies only when addressed by the gateway.
 */
#include "lora_node.h"

#include <string.h>

/** @brief Encode a packet struct and send it via the node radio callback. */
static bool send_packet(const lora_node_t* node, const lora_packet_t* pkt) {
  uint8_t wire[LORA_PACKET_MAX_SIZE];
  const size_t wire_len = lora_packet_encode(pkt, wire, sizeof(wire));

  if (wire_len == 0 || node->radio.send == nullptr) {
    return false;
  }

  return node->radio.send(wire, wire_len);
}

/** @brief Read sensors and reply with CMD_SENSOR_DATA (same seq as request). */
static bool handle_read_sensor(lora_node_t* node, const lora_packet_t* request) {
  uint8_t sensor_payload[LORA_MAX_PAYLOAD];
  uint8_t payload_len = 0;

  if (node->radio.read_sensor != nullptr) {
    payload_len = node->radio.read_sensor(sensor_payload, LORA_MAX_PAYLOAD);
    if (payload_len > LORA_MAX_PAYLOAD) {
      payload_len = LORA_MAX_PAYLOAD;
    }
  }

  lora_packet_t response;
  if (!lora_packet_build(&response, node->config.gateway_id, node->config.node_id,
                         CMD_SENSOR_DATA, request->seq, sensor_payload,
                         payload_len)) {
    return false;
  }

  return send_packet(node, &response);
}

/** @brief Reply to CMD_PING with CMD_ACK (same seq as request). */
static bool handle_ping(lora_node_t* node, const lora_packet_t* request) {
  lora_packet_t response;
  if (!lora_packet_build(&response, node->config.gateway_id, node->config.node_id,
                         CMD_ACK, request->seq, nullptr, 0)) {
    return false;
  }

  return send_packet(node, &response);
}

/** @brief Dispatch a validated request to the appropriate command handler. */
static void process_request(lora_node_t* node, const lora_packet_t* request) {
  switch (request->cmd) {
    case CMD_READ_SENSOR:
      handle_read_sensor(node, request);
      break;
    case CMD_PING:
      handle_ping(node, request);
      break;
    default:
      break;
  }
}

/** @brief See lora_node_init() in lora_node.h. */
void lora_node_init(lora_node_t* node, const lora_node_config_t* config,
                    const lora_node_radio_t* radio) {
  if (node == nullptr || config == nullptr || radio == nullptr) {
    return;
  }

  memset(node, 0, sizeof(*node));
  node->config = *config;
  node->radio = *radio;
  node->state = LORA_NODE_STATE_RX_WAIT;
}

/** @brief See lora_node_poll() in lora_node.h. */
void lora_node_poll(lora_node_t* node) {
  if (node == nullptr || node->radio.rx_pending == nullptr ||
      node->radio.receive == nullptr) {
    return;
  }

  if (!node->radio.rx_pending()) {
    return;
  }

  uint8_t raw[LORA_PACKET_MAX_SIZE];
  int16_t rssi = 0;
  const int rx_len = node->radio.receive(raw, sizeof(raw), &rssi);
  if (rx_len <= 0) {
    return;
  }

  lora_packet_t pkt;
  if (!lora_packet_decode(raw, static_cast<size_t>(rx_len), &pkt)) {
    return;
  }

  if (pkt.dst != node->config.node_id) {
    return;
  }

  node->pending_request = pkt;
  node->state = LORA_NODE_STATE_PROCESS_REQUEST;
  process_request(node, &pkt);
  node->state = LORA_NODE_STATE_RX_WAIT;
}

/** @brief See lora_node_get_state() in lora_node.h. */
lora_node_state_t lora_node_get_state(const lora_node_t* node) {
  return node != nullptr ? node->state : LORA_NODE_STATE_RX_WAIT;
}
