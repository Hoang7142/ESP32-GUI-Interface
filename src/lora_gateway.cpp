/**
 * @file lora_gateway.cpp
 * @brief Non-blocking master polling state machine.
 *        Sends READ_SENSOR, waits for SENSOR_DATA, retries on timeout,
 *        and tracks per-node online/offline status.
 */
#include "lora_gateway.h"

#include <string.h>

/** @brief Find the status entry for a node ID in the gateway status table. */
static lora_node_status_t* find_node_status(lora_gateway_t* gw, uint8_t node_id) {
  for (uint8_t i = 0; i < gw->node_status_count; i++) {
    if (gw->node_status[i].node_id == node_id) {
      return &gw->node_status[i];
    }
  }
  return nullptr;
}

/** @brief Return the node ID currently being polled. */
static uint8_t current_node_id(const lora_gateway_t* gw) {
  if (gw->current_node_index >= gw->config.node_count) {
    return 0;
  }
  return gw->config.node_ids[gw->current_node_index];
}

/** @brief Mark a node online and reset its failure counter. */
static void mark_node_online(lora_gateway_t* gw, uint8_t node_id, uint32_t now_ms) {
  lora_node_status_t* status = find_node_status(gw, node_id);
  if (status != nullptr) {
    status->online = true;
    status->last_seen_ms = now_ms;
    status->fail_count = 0;
  }
}

/** @brief Mark a node offline and increment its failure counter. */
static void mark_node_offline(lora_gateway_t* gw, uint8_t node_id) {
  lora_node_status_t* status = find_node_status(gw, node_id);
  if (status != nullptr) {
    status->online = false;
    status->fail_count++;
  }
}

/** @brief Switch gateway state and record the entry timestamp. */
static void transition(lora_gateway_t* gw, lora_gateway_state_t next_state,
                       uint32_t now_ms) {
  gw->state = next_state;
  gw->state_enter_ms = now_ms;
}

/** @brief Build and transmit CMD_READ_SENSOR to the current node. */
static bool send_read_sensor(lora_gateway_t* gw) {
  lora_packet_t pkt;
  const uint8_t node_id = current_node_id(gw);

  if (!lora_packet_build(&pkt, node_id, gw->config.gateway_id, CMD_READ_SENSOR,
                         gw->current_seq, nullptr, 0)) {
    return false;
  }

  uint8_t wire[LORA_PACKET_MAX_SIZE];
  const size_t wire_len = lora_packet_encode(&pkt, wire, sizeof(wire));
  if (wire_len == 0) {
    return false;
  }

  return gw->radio.send != nullptr && gw->radio.send(wire, wire_len);
}

/** @brief End the current poll round and schedule the next one. */
static void finish_poll_round(lora_gateway_t* gw, uint32_t now_ms) {
  gw->poll_active = false;
  gw->next_poll_ms = now_ms + gw->config.poll_interval_ms;
  transition(gw, LORA_GW_STATE_IDLE, now_ms);
}

/** @brief Move to the next node in the list, or finish the poll round. */
static void advance_to_next_node(lora_gateway_t* gw, uint32_t now_ms) {
  gw->current_node_index++;
  gw->retry_count = 0;
  gw->current_seq++;

  if (gw->current_node_index >= gw->config.node_count) {
    finish_poll_round(gw, now_ms);
    return;
  }

  transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
}

/** @brief Handle response timeout: retry or mark node offline and continue. */
static void handle_timeout(lora_gateway_t* gw, uint32_t now_ms) {
  const uint8_t node_id = current_node_id(gw);

  gw->retry_count++;
  if (gw->retry_count < gw->config.max_retries) {
    transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
    return;
  }

  mark_node_offline(gw, node_id);
  advance_to_next_node(gw, now_ms);
}

/** @brief Validate SENSOR_DATA (src, cmd, seq) and dispatch to callback. */
static void process_response(lora_gateway_t* gw, const lora_packet_t* pkt,
                             int16_t rssi, uint32_t now_ms) {
  const uint8_t expected_node = current_node_id(gw);

  if (pkt->src != expected_node) {
    return;
  }
  if (pkt->cmd != CMD_SENSOR_DATA) {
    return;
  }
  if (pkt->seq != gw->current_seq) {
    return;
  }

  mark_node_online(gw, expected_node, now_ms);

  if (gw->on_sensor_data != nullptr) {
    gw->on_sensor_data(expected_node, pkt->payload, pkt->payload_len, rssi);
  }

  advance_to_next_node(gw, now_ms);
}

/** @brief See lora_gateway_init() in lora_gateway.h. */
bool lora_gateway_init(lora_gateway_t* gw, const lora_gateway_config_t* config,
                       const lora_gateway_radio_t* radio,
                       lora_gateway_sensor_cb_t on_sensor_data,
                       lora_node_status_t* status_buf, uint8_t status_buf_len) {
  if (gw == nullptr || config == nullptr || radio == nullptr ||
      config->node_ids == nullptr || config->node_count == 0 ||
      status_buf == nullptr || status_buf_len < config->node_count) {
    return false;
  }
  if (radio->send == nullptr || radio->rx_pending == nullptr ||
      radio->receive == nullptr || radio->millis == nullptr) {
    return false;
  }

  memset(gw, 0, sizeof(*gw));
  gw->config = *config;
  gw->radio = *radio;
  gw->on_sensor_data = on_sensor_data;
  gw->node_status = status_buf;
  gw->node_status_count = config->node_count;
  gw->state = LORA_GW_STATE_IDLE;
  gw->current_seq = 1;

  for (uint8_t i = 0; i < config->node_count; i++) {
    gw->node_status[i].node_id = config->node_ids[i];
    gw->node_status[i].online = false;
  }

  return true;
}

/** @brief See lora_gateway_start_poll() in lora_gateway.h. */
void lora_gateway_start_poll(lora_gateway_t* gw) {
  if (gw == nullptr || gw->poll_active) {
    return;
  }

  const uint32_t now_ms = gw->radio.millis();
  gw->poll_active = true;
  gw->current_node_index = 0;
  gw->retry_count = 0;
  transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
}

/**
 * @brief Run one step of the gateway state machine (non-blocking; call from loop).
 *        Sends READ_SENSOR, waits for SENSOR_DATA, retries on timeout, marks
 *        nodes offline, and invokes on_sensor_data when data arrives.
 * @param gw  Initialized gateway instance.
 */
void lora_gateway_poll(lora_gateway_t* gw) {
  if (gw == nullptr || gw->radio.millis == nullptr) {
    return;
  }

  const uint32_t now_ms = gw->radio.millis();

  if (gw->state == LORA_GW_STATE_IDLE) {
    if (!gw->poll_active && now_ms >= gw->next_poll_ms) {
      lora_gateway_start_poll(gw);
    }
    return;
  }

  if (gw->state == LORA_GW_STATE_SEND_REQUEST) {
    if (send_read_sensor(gw)) {
      transition(gw, LORA_GW_STATE_WAIT_RESPONSE, now_ms);
    } else {
      handle_timeout(gw, now_ms);
    }
    return;
  }

  if (gw->state == LORA_GW_STATE_WAIT_RESPONSE) {
    if (gw->radio.rx_pending != nullptr && gw->radio.rx_pending()) {
      uint8_t raw[LORA_PACKET_MAX_SIZE];
      int16_t rssi = 0;
      const int rx_len =
          gw->radio.receive != nullptr
              ? gw->radio.receive(raw, sizeof(raw), &rssi)
              : 0;

      if (rx_len > 0) {
        lora_packet_t pkt;
        if (lora_packet_decode(raw, static_cast<size_t>(rx_len), &pkt)) {
          if (pkt.dst == gw->config.gateway_id) {
            process_response(gw, &pkt, rssi, now_ms);
            return;
          }
        }
      }
    }

    if (now_ms - gw->state_enter_ms >= gw->config.response_timeout_ms) {
      handle_timeout(gw, now_ms);
    }
    return;
  }

  if (gw->state == LORA_GW_STATE_NEXT_NODE) {
    advance_to_next_node(gw, now_ms);
  }
}

/** @brief See lora_gateway_get_state() in lora_gateway.h. */
lora_gateway_state_t lora_gateway_get_state(const lora_gateway_t* gw) {
  return gw != nullptr ? gw->state : LORA_GW_STATE_IDLE;
}

/** @brief See lora_gateway_get_node_status() in lora_gateway.h. */
const lora_node_status_t* lora_gateway_get_node_status(const lora_gateway_t* gw,
                                                       uint8_t node_id) {
  if (gw == nullptr) {
    return nullptr;
  }
  return find_node_status(const_cast<lora_gateway_t*>(gw), node_id);
}

#ifdef ARDUINO

#include <Arduino.h>

#include "lora_network_config.h"
#include "lora_radio.h"

namespace {

lora_gateway_t g_gateway;                              /**< Global gateway instance (ESP32). */
lora_node_status_t g_node_status[LORA_NODE_COUNT];   /**< Per-node online tracking table. */
lora_gateway_state_t g_last_gateway_state = LORA_GW_STATE_IDLE; /**< Previous state for round-end log. */

/** @brief Print ONLINE/OFFLINE status for every configured node to Serial. */
void printGatewayNodeStatus() {
  for (uint8_t i = 0; i < LORA_NODE_COUNT; i++) {
    const lora_node_status_t* status =
        lora_gateway_get_node_status(&g_gateway, LORA_NODE_IDS[i]);
    Serial.print(F("  Node 0x"));
    Serial.print(LORA_NODE_IDS[i], HEX);
    Serial.print(status != nullptr && status->online ? F(" ONLINE") : F(" OFFLINE"));
    if (status != nullptr && !status->online && status->fail_count > 0) {
      Serial.print(F(" (failures: "));
      Serial.print(status->fail_count);
      Serial.print(')');
    }
    Serial.println();
  }
}

/** @brief Radio callback: forward encoded packets to loraSend(). */
bool gatewayRadioSend(const uint8_t* data, size_t len) {
  return loraSend(data, len);
}

/** @brief Radio callback: check SX1278 RX flag. */
bool gatewayRadioRxPending() { return loraRxPending(); }

/** @brief Radio callback: read one RX frame and return RSSI. */
int gatewayRadioReceive(uint8_t* data, size_t max_len, int16_t* rssi_out) {
  float snr = 0.0f;
  return loraReceive(data, max_len, rssi_out, &snr);
}

/** @brief Radio callback: platform uptime for timeout handling. */
uint32_t gatewayRadioMillis() { return millis(); }

/** @brief Gateway callback: log and decode SENSOR_DATA from a polled node. */
void onSensorData(uint8_t node_id, const uint8_t* payload, uint8_t payload_len,
                  int16_t rssi) {
  Serial.print(F("SENSOR_DATA from 0x"));
  Serial.print(node_id, HEX);
  Serial.print(F(", RSSI "));
  Serial.print(rssi);
  Serial.print(F(" dBm, len "));
  Serial.println(payload_len);

  if (payload_len >= sizeof(lora_sensor_payload_t)) {
    lora_sensor_payload_t sensor;
    memcpy(&sensor, payload, sizeof(sensor));

    Serial.print(F("  temp="));
    Serial.print(sensor.temperature_c10 / 10.0f, 1);
    Serial.print(F(" C  humi="));
    Serial.print(sensor.humidity_pct10 / 10.0f, 1);
    Serial.print(F(" %  soil="));
    Serial.print(sensor.soil_moisture);
    Serial.print(F("  water="));
    Serial.println(sensor.water_level);
  }
}

}  // namespace

/** @brief See initLoRaGateway() in lora_gateway.h. */
bool initLoRaGateway() {
  const lora_gateway_config_t config = {
      .gateway_id = LORA_GATEWAY_ID,
      .node_ids = LORA_NODE_IDS,
      .node_count = LORA_NODE_COUNT,
      .response_timeout_ms = LORA_RESPONSE_TIMEOUT_MS,
      .max_retries = LORA_MAX_RETRIES,
      .poll_interval_ms = LORA_POLL_INTERVAL_MS,
  };

  const lora_gateway_radio_t radio = {
      .send = gatewayRadioSend,
      .rx_pending = gatewayRadioRxPending,
      .receive = gatewayRadioReceive,
      .millis = gatewayRadioMillis,
  };

  if (!lora_gateway_init(&g_gateway, &config, &radio, onSensorData, g_node_status,
                         LORA_NODE_COUNT)) {
    Serial.println(F("LoRa gateway init failed"));
    return false;
  }

  Serial.println(F("LoRa gateway polling ready"));
  Serial.print(F("  Gateway ID: 0x"));
  Serial.println(LORA_GATEWAY_ID, HEX);
  Serial.print(F("  Nodes: "));
  for (uint8_t i = 0; i < LORA_NODE_COUNT; i++) {
    Serial.print(F("0x"));
    Serial.print(LORA_NODE_IDS[i], HEX);
    if (i + 1 < LORA_NODE_COUNT) {
      Serial.print(F(", "));
    }
  }
  Serial.println();
  return true;
}

/**
 * @brief ESP32 application loop helper: run gateway poll and log round status.
 *        Call every loop() iteration when LoRa is enabled.
 */
void loraGatewayPoll() {
  lora_gateway_poll(&g_gateway);

  const lora_gateway_state_t gw_state = lora_gateway_get_state(&g_gateway);
  if (g_last_gateway_state != LORA_GW_STATE_IDLE &&
      gw_state == LORA_GW_STATE_IDLE) {
    Serial.println(F("Poll round complete:"));
    printGatewayNodeStatus();
  }
  g_last_gateway_state = gw_state;
}

#endif
