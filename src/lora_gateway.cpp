/**
 * @file lora_gateway.cpp
 * @brief Non-blocking master polling state machine.
 *        Giữ 100% thuật toán gốc của đồng nghiệp và dọn sạch lỗi cấu trúc.
 */
#include "lora_gateway.h"
#include "lora_network_config.h" 
#include <string.h>

// 🔹 ĐẶT MẢNG ĐỊNH NGHĨA CHUNG LÊN ĐẦU FILE NGOÀI TẤT CẢ CÁC CẶP LỆNH
static const uint8_t LORA_NODE_IDS[LORA_NODE_COUNT] = {0x11, 0x12, 0x13};

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "lora_radio.h"
#endif

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
static void transition(lora_gateway_t* gw, lora_gateway_state_t next_state, uint32_t now_ms) {
  gw->state = next_state;
  gw->state_enter_ms = now_ms;
}

/** @brief Build and transmit CMD_READ_SENSOR to the current node. */
static bool send_read_sensor(lora_gateway_t* gw) {
  lora_packet_t pkt;
  const uint8_t node_id = current_node_id(gw);
  if (!lora_packet_build(&pkt, node_id, gw->config.gateway_id, CMD_READ_SENSOR, gw->current_seq, nullptr, 0)) {
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
/** @brief Validate phản hồi (src, cmd, seq) và điều phối sang hàm xử lý tương ứng */
static void process_response(lora_gateway_t* gw, const lora_packet_t* pkt, int16_t rssi, uint32_t now_ms) {
  const uint8_t expected_node = current_node_id(gw);
  
  // Kiểm tra xem có đúng là gói tin phản hồi từ Node mà Gateway vừa gọi tên không
  if (pkt->src != expected_node) return;
  if (pkt->seq != gw->current_seq) return;

  // 🌟 TRƯỜNG HỢP A: Nhận gói dữ liệu 7 cảm biến thực tế theo chu kỳ quét ngầm
  if (pkt->cmd == CMD_SENSOR_DATA) {
    mark_node_online(gw, expected_node, now_ms);
    if (gw->on_sensor_data != nullptr) {
      gw->on_sensor_data(expected_node, pkt->payload, pkt->payload_len, rssi);
    }
    advance_to_next_node(gw, now_ms);
  }
  // 🌟 TRƯỜNG HỢP B: Nhận gói ACK phản hồi trạng thái nút bấm (Xử lý Bước 2 và Bước 3)
  else if (pkt->cmd == CMD_ACK) {
    mark_node_online(gw, expected_node, now_ms);
    
    // Nếu gói ACK này mang theo payload trạng thái nút bấm thực tế từ STM32 báo về
    if (pkt->payload_len == sizeof(lora_control_payload_t)) {
      lora_control_payload_t state;
      memcpy(&state, pkt->payload, sizeof(state));

      Serial.printf("\n[LoRa Gateway] 🎉 Bắt được gói CMD_ACK xác nhận trạng thái thực tế từ Node 0x%02X!\n", pkt->src);

      // Triệu hồi đối tượng MQTT và các biến đồng bộ từ file main.cpp sang
      extern PubSubClient client;
      extern int fakePumpStatus;
      extern int fakePumpPwm;
      extern int fakeRoofPwm;
      extern String fakeRoofStatus;
      extern String fakeSystemMode;

      // Cập nhật lại các biến trạng thái trên ESP32 theo đúng báo cáo thực tế từ phần cứng STM32
      fakePumpStatus = state.pump_status;
      fakePumpPwm    = state.pump_pwm;
      fakeRoofPwm    = state.roof_pwm;
      fakeSystemMode = (state.system_mode == 1) ? "auto" : "manual";
      if (state.roof_status == 1)      fakeRoofStatus = "OPEN";
      else if (state.roof_status == 2) fakeRoofStatus = "CLOSE";
      else                             fakeRoofStatus = "STOP";

      // 🚀 THỰC HIỆN BƯỚC 3: Đóng gói JSON xác nhận trạng thái thực tế và bắn lên MQTT feedback
      if (client.connected()) {
        JsonDocument feedbackDoc;
        feedbackDoc["pump"]   = fakePumpStatus;
        feedbackDoc["roof"]   = fakeRoofStatus;
        feedbackDoc["mode"]   = fakeSystemMode;
        feedbackDoc["status"] = "SUCCESS"; // Báo cho Web biết phần cứng đã thi hành thành công

        char feedbackBuffer[128];
        serializeJson(feedbackDoc, feedbackBuffer);
        
        client.publish("smartfarm/feedback", feedbackBuffer);
        Serial.printf("   🚀 [MQTT Feedback] Đã publish trạng thái thực tế lên Web: %s\n", feedbackBuffer);
      }
    }
    
    // Bản chất lệnh nút bấm là xen ngang, không chặn nhịp nên ta đứng im giữ nguyên máy trạng thái quét cảm biến
  }
}

bool lora_gateway_init(lora_gateway_t* gw, const lora_gateway_config_t* config, const lora_gateway_radio_t* radio,
                       lora_gateway_sensor_cb_t on_sensor_data, lora_node_status_t* status_buf, uint8_t status_buf_len) {
  if (gw == nullptr || config == nullptr || radio == nullptr || config->node_ids == nullptr || config->node_count == 0 ||
      status_buf == nullptr || status_buf_len < config->node_count) {
    return false;
  }
  if (radio->send == nullptr || radio->rx_pending == nullptr || radio->receive == nullptr || radio->millis == nullptr) {
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

void lora_gateway_start_poll(lora_gateway_t* gw) {
  if (gw == nullptr || gw->poll_active) return;
  const uint32_t now_ms = gw->radio.millis();
  gw->poll_active = true;
  gw->current_node_index = 0;
  gw->retry_count = 0;
  transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
}

void lora_gateway_poll(lora_gateway_t* gw) {
  if (gw == nullptr || gw->radio.millis == nullptr) return;
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
      const int rx_len = gw->radio.receive != nullptr ? gw->radio.receive(raw, sizeof(raw), &rssi) : 0;
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

lora_gateway_state_t lora_gateway_get_state(const lora_gateway_t* gw) {
  return gw != nullptr ? gw->state : LORA_GW_STATE_IDLE;
}

const lora_node_status_t* lora_gateway_get_node_status(const lora_gateway_t* gw, uint8_t node_id) {
  if (gw == nullptr) return nullptr;
  return find_node_status(const_cast<lora_gateway_t*>(gw), node_id);
}

#ifdef ARDUINO
namespace {
lora_gateway_t g_gateway;
lora_node_status_t g_node_status[LORA_NODE_COUNT];
lora_gateway_state_t g_last_gateway_state = LORA_GW_STATE_IDLE;

void printGatewayNodeStatus() {
  for (uint8_t i = 0; i < LORA_NODE_COUNT; i++) {
    const lora_node_status_t* status = lora_gateway_get_node_status(&g_gateway, LORA_NODE_IDS[i]);
    Serial.print(F(" Node 0x"));
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

bool gatewayRadioSend(const uint8_t* data, size_t len) { return loraSend(data, len); }
bool gatewayRadioRxPending() { return loraRxPending(); }
int gatewayRadioReceive(uint8_t* data, size_t max_len, int16_t* rssi_out) {
  float snr = 0.0f;
  return loraReceive(data, max_len, rssi_out, &snr);
}
uint32_t gatewayRadioMillis() { return millis(); }

/** @brief Gateway callback: 🌟 TINH TÚY: xử lý giải mã 7 cảm biến thật và truyền MQTT JSON của bạn */
void onSensorData(uint8_t node_id, const uint8_t* payload, uint8_t payload_len, int16_t rssi) {
  Serial.print(F("\n[LoRa] Bắt được gói SENSOR_DATA từ Node 0x"));
  Serial.print(node_id, HEX);
  Serial.print(F(", RSSI: "));
  Serial.print(rssi);
  Serial.print(F(" dBm, len: "));
  Serial.println(payload_len);

  if (payload_len >= sizeof(lora_sensor_payload_t)) {
    lora_sensor_payload_t sensor;
    memcpy(&sensor, payload, sizeof(sensor));
    
    // 🌟 TINH TÚY CỦA BẠN: Rã gói và tính toán số thực đủ cho 7 cảm biến thật
    float real_temp  = static_cast<float>(sensor.temperature_c10) / 10.0f;
    float real_humi  = static_cast<float>(sensor.humidity_pct10) / 10.0f;
    float real_soil  = static_cast<float>(sensor.soil_moisture);
    float real_water = static_cast<float>(sensor.water_level);
    float real_flow  = static_cast<float>(sensor.flow_rate_Lmin_x10) / 10.0f;
    float real_amp   = static_cast<float>(sensor.current_mA) / 1000.0f;
    int real_rain    = (sensor.rain_status == 0) ? 1 : 0;

    Serial.printf("   -> DU LIEU THAT: T:%.1fC | H:%.1f%% | Soil:%.0f%% | Water:%.0f%% | Flow:%.1fL/m | Amp:%.3fA | Rain:%d\n",
                  real_temp, real_humi, real_soil, real_water, real_flow, real_amp, real_rain);

    // Triệu hồi bộ truyền MQTT và các biến đồng bộ từ file main.cpp cũ của bạn sang
    extern PubSubClient client;
    extern int fakePumpStatus;
    extern int fakePumpPwm;
    extern int fakeRoofPwm;
    extern String fakeSystemMode;

    // Đóng gói JSON thực tế đẩy thẳng lên HiveMQ Broker của bạn
    if (client.connected()) {
      JsonDocument doc;
      doc["soil"]        = real_soil;
      doc["temp"]        = real_temp;
      doc["humi"]        = real_humi;
      doc["water"]       = real_water;
      doc["flow"]        = real_flow;
      doc["current_amp"] = real_amp;
      doc["mua"]         = real_rain;
      doc["mode"]        = fakeSystemMode;
      doc["pumpPwm"]     = fakePumpPwm;
      doc["roofPwm"]     = fakeRoofPwm;

      char mqtt_buffer[256];
      serializeJson(doc, mqtt_buffer);
      
      client.publish("smartfarm/sensors", mqtt_buffer);
      Serial.println(F("   🚀 [MQTT HiveMQ] Đã đồng bộ JSON dữ liệu thật lên Web GUI!"));
    }
  } else {
    Serial.println(F("   [⚠️ Lỗi] Kích thước gói tin không khớp cấu trúc 7 cảm biến!"));
  }
}
} // namespace

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
  if (!lora_gateway_init(&g_gateway, &config, &radio, onSensorData, g_node_status, LORA_NODE_COUNT)) {
    Serial.println(F("LoRa gateway init failed"));
    return false;
  }
  Serial.println(F("LoRa gateway polling ready"));
  return true;
}

void loraGatewayPoll() {
  lora_gateway_poll(&g_gateway);
  const lora_gateway_state_t gw_state = lora_gateway_get_state(&g_gateway);
  if (g_last_gateway_state != LORA_GW_STATE_IDLE && gw_state == LORA_GW_STATE_IDLE) {
    Serial.println(F("Poll round complete:"));
    printGatewayNodeStatus();
  }
  g_last_gateway_state = gw_state;
}
#endif