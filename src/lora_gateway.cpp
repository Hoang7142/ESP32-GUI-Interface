/**
 * @file lora_gateway.cpp
 * @brief Non-blocking master polling state machine.
 *        Giữ 100% thuật toán gốc của đồng nghiệp và dọn sạch lỗi cấu trúc.
 */
#include "lora_gateway.h"
#include "lora_network_config.h" 
#include <string.h>

// 🔹 ĐẶT MẢNG ĐỊNH NGHĨA CHUNG LÊN ĐẦU FILE NGOÀI TẤT CẢ CÁC CẶP LỆNH
 //const uint8_t LORA_NODE_IDS[LORA_NODE_COUNT] = {0x11, 0x12, 0x13};

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
//quét địa chỉ con trỏ chi tới ô nhớ của ID node đó

/** @brief Return the node ID currently being polled. */
static uint8_t current_node_id(const lora_gateway_t* gw) {
  if (gw->current_node_index >= gw->config.node_count) {
    return 0;
  }
  return gw->config.node_ids[gw->current_node_index];
}
// quét địa chỉ ID hiện tại mà gateway đang làm việc

/** @brief Mark a node online and reset its failure counter. */
static void mark_node_online(lora_gateway_t* gw, uint8_t node_id, uint32_t now_ms) {
  lora_node_status_t* status = find_node_status(gw, node_id);
  if (status != nullptr) {
    status->online = true;
    status->last_seen_ms = now_ms;
    status->fail_count = 0;// hoàn tác biến đếm lỗi về 0 vì node đó đã onine
  }
}
//find_node_status để tìm ô nhớ của Node đó. Nếu tìm thấy, nó ép biến online = true, ghi nhận mốc thời gian sống sót cuối cùng last_seen_ms = now_ms, và reset bộ đếm lỗi liên tiếp về 0 (fail_count = 0).

/** @brief Mark a node offline and increment its failure counter. */
static void mark_node_offline(lora_gateway_t* gw, uint8_t node_id) {
  lora_node_status_t* status = find_node_status(gw, node_id);
  if (status != nullptr) {
    status->online = false;
    status->fail_count++;
  }
}
//Tìm ô nhớ của Node, ép biến online = false, và tăng biến đếm lỗi liên tiếp lên 1 đơn vị (fail_count++).

/** @brief Switch gateway state and record the entry timestamp. */
static void transition(lora_gateway_t* gw, lora_gateway_state_t next_state, uint32_t now_ms) {
  gw->state = next_state;
  gw->state_enter_ms = now_ms;// mốc thời gian chuyển đổi trạng thái, việc này giúp hệ thống biết trạng thái này đứng dc bao lâu để tính timeout
}
//Ghi đè trạng thái mới vào biến gw->state. Đồng thời, nó chụp lại mốc thời gian hiện tại gw->state_enter_ms = now_ms, để bt có lỗi hay k và lỗi lâu chưa

/** @brief Build and transmit CMD_READ_SENSOR to the current node. */
static bool send_read_sensor(lora_gateway_t* gw) {
  lora_packet_t pkt;
  const uint8_t node_id = current_node_id(gw);//Lấy ID của Node hiện tại
  if (!lora_packet_build(&pkt, node_id, gw->config.gateway_id, CMD_READ_SENSOR, gw->current_seq, nullptr, 0)) {
    return false;
  }
  uint8_t wire[LORA_PACKET_MAX_SIZE];
  const size_t wire_len = lora_packet_encode(&pkt, wire, sizeof(wire));//chuyển đổi gói tin cấu trúc sang chuỗi byte nhị phân để có thể truyền qua sóng radio.
  if (wire_len == 0) {
    return false;
  }
  return gw->radio.send != nullptr && gw->radio.send(wire, wire_len);// kiểm tra nếu trống thì gửi sóng lora
}
// hàm gửi gói tin(đã đóng gói và phát sóng)

/** @brief End the current poll round and schedule the next one. */
static void finish_poll_round(lora_gateway_t* gw, uint32_t now_ms) {
  gw->poll_active = false;// hạ cờ khi quét xong
  gw->next_poll_ms = now_ms + gw->config.poll_interval_ms;// thời gian quét tiếp theo bằng thời gian hiện tại cộng thời gian nghỉ
  transition(gw, LORA_GW_STATE_IDLE, now_ms);//gọi hàm transition đưa Gateway về trạng thái nghỉ ngơi LORA_GW_STATE_IDLE.
}
//Tuyên bố kết thúc một vòng đi tuần qua tất cả các Node

/** @brief Move to the next node in the list, or finish the poll round. */
static void advance_to_next_node(lora_gateway_t* gw, uint32_t now_ms) {
  gw->current_node_index++;// qua node kế tiếp
  gw->retry_count = 0;// reset biến thử về 0
  gw->current_seq++;//tăng mã số gói tin để tránh trùng lặp
  if (gw->current_node_index >= gw->config.node_count) {
    finish_poll_round(gw, now_ms);
    return;
  }
  transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
}
// hàm dời con trỏ sang node tiếp theo để quét tiếp tục

/** @brief Handle response timeout: retry or mark node offline and continue. */
static void handle_timeout(lora_gateway_t* gw, uint32_t now_ms) {// hàm xử lý khi node không trả lời
  const uint8_t node_id = current_node_id(gw);
  gw->retry_count++;
  if (gw->retry_count < gw->config.max_retries) {// nếu vẫn trong thời gian cho phép
    transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);// phát lệnh gọi node đó lần nựa
    return;
  }
  mark_node_offline(gw, node_id);// nếu node k trả lời đánh dấu offline
  advance_to_next_node(gw, now_ms);// tăng con trỏ quét qua node mới
}
// nếu  thời gian vẫn nhở hơn thì chuyển về trạng thái (LORA_GW_STATE_SEND_REQUEST) để phát lệnh một lần nữa, nếu gọi lại k dc , đánh dấu offline và dugnf hàm (advance_to_next_node) để quét node khác

/** @brief Validate SENSOR_DATA (src, cmd, seq) and dispatch to callback. */
/** @brief Validate phản hồi (src, cmd, seq) và điều phối sang hàm xử lý tương ứng */
/** @brief Validate phản hồi và điều phối sang hàm xử lý tương ứng */

struct NodeMirrorState {
  int pumpStatus;
  int pumpPwm;
  int roofPwm;
  String roofStatus;
  String systemMode;
};
extern NodeMirrorState nodeStates[3];
extern int nodeIndexFromId(uint8_t id);

static void process_response(lora_gateway_t* gw, const lora_packet_t* pkt, int16_t rssi, uint32_t now_ms) {
  
  // 🌟 ƯU TIÊN TOÀN CỤC: Nếu nhận gói ACK phản hồi từ nút bấm, xử lý ngay lập tức
  if (pkt->cmd == CMD_ACK) {
    mark_node_online(gw, pkt->src, now_ms);
    
    if (pkt->payload_len == sizeof(lora_control_payload_t)) {
      lora_control_payload_t state;
      memcpy(&state, pkt->payload, sizeof(state));

      Serial.printf("\n[LoRa Gateway] 🎉 Bắt được gói CMD_ACK xác nhận trạng thái thực tế từ Node 0x%02X!\n", pkt->src);

      extern PubSubClient client;
      extern NodeMirrorState nodeStates[3];
      extern int nodeIndexFromId(uint8_t id);

      int ackIdx = nodeIndexFromId(pkt->src);
      if (ackIdx < 0) return;
      NodeMirrorState& ns = nodeStates[ackIdx];

      // ✅ Cập nhật fake variables
      ns.pumpStatus = state.pump_status;
      ns.pumpPwm    = state.pump_pwm;
      ns.roofPwm    = state.roof_pwm;
      ns.systemMode = (state.system_mode == 1) ? "auto" : "manual";
      if (state.roof_status == 1)       ns.roofStatus = "OPEN";
      else if (state.roof_status == 2)  ns.roofStatus = "CLOSE";
      else                              ns.roofStatus = "STOP";

      // ✅ Publish MQTT feedback
      if (client.connected()) {
        JsonDocument feedbackDoc;
        feedbackDoc["node_id"] = pkt->src; 
        feedbackDoc["pump"]   = ns.pumpStatus;
        feedbackDoc["roof"]   = ns.roofStatus;
        feedbackDoc["mode"]   = ns.systemMode;
        feedbackDoc["status"] = "SUCCESS"; 

        char feedbackBuffer[128];
        serializeJson(feedbackDoc, feedbackBuffer);
        
        client.publish("smartfarm/feedback", feedbackBuffer);
        Serial.printf("   🚀 [MQTT Feedback] Đã publish trạng thái thực tế lên Web: %s\n", feedbackBuffer);
      }
    }

    // 🌟 GIẢI CỨU MÁY TRẠNG THÁI: Thoát khỏi bẫy kẹt trạng thái WAIT_RESPONSE sau khi nhận ACK thành công
    // 🔥 FIX: Hạ cờ ACK ngay lập tức
    extern volatile bool g_waiting_for_control_ack;
    g_waiting_for_control_ack = false; // Tắt cờ vì đã nhận được ACK thành công

    // 🌟 FIX: Thay vì quay lại SEND_REQUEST ngay, chuyển sang state tạm thời "chờ xử lý"
    // Điều này đảm bảo ACK data có đủ thời gian được xử lý trước khi quay lại quét cảm biến
    transition(gw, LORA_GW_STATE_WAIT_ACK_PROCESSED, now_ms);
    return; 
  }

  // 🌟 CHU KỲ TUẦN TRA CẢM BIẾN NGUYÊN BẢN CỦA ĐỒNG NGHIỆP
  const uint8_t expected_node = current_node_id(gw);
  if (pkt->src != expected_node) return;

  if (pkt->cmd == CMD_SENSOR_DATA) {
    if (pkt->seq != gw->current_seq) return; 

    mark_node_online(gw, expected_node, now_ms);
    if (gw->on_sensor_data != nullptr) {
      gw->on_sensor_data(expected_node, pkt->payload, pkt->payload_len, rssi);
    }
    advance_to_next_node(gw, now_ms);
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
  memset(gw, 0, sizeof(*gw));//Dùng memset xóa sạch sẽ cấu trúc gw về trạng thái trống.
  gw->config = *config;
  gw->radio = *radio;//nạp các con trỏ hàm điều khiển phần cứng (radio).
  gw->on_sensor_data = on_sensor_data;
  gw->node_status = status_buf;
  gw->node_status_count = config->node_count;
  gw->state = LORA_GW_STATE_IDLE;// trạng thái ban đầu
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
// hàm ép gatewa bắt đầu quét các node

void lora_gateway_poll(lora_gateway_t* gw) {
  if (gw == nullptr || gw->radio.millis == nullptr) return;
  const uint32_t now_ms = gw->radio.millis();// Liên tục cập nhật thời gian thực hiện tại hành trình
  if (gw->state == LORA_GW_STATE_IDLE) {
    // Nếu chưa quét và thời gian hiện tại đã vượt qua mốc thời gian nghỉ quy định
    if (!gw->poll_active && now_ms >= gw->next_poll_ms) {
      lora_gateway_start_poll(gw);// Tự động kích hoạt chu kỳ quét mới
    }
    return;
    //Nếu đang IDLE: Kiểm tra xem đã hết thời gian nghỉ chưa (now_ms >= gw->next_poll_ms). Nếu hết giờ nghỉ, tự động gọi lora_gateway_start_poll() để đi quét lượt mới.
  }
  if (gw->state == LORA_GW_STATE_SEND_REQUEST) {
    if (send_read_sensor(gw)) {
      // Nếu phát sóng lệnh thành công, chuyển máy sang trạng thái "Nằm vùng đợi phản hồi"
      transition(gw, LORA_GW_STATE_WAIT_RESPONSE, now_ms);
    } else {
      // Nếu lỗi phần cứng không phát được, xử lý tính toán lỗi/thử lại luôn
      handle_timeout(gw, now_ms);
    }
    return;
  }
  //Nếu đang SEND_REQUEST: Gọi hàm phát lệnh send_read_sensor(). Nếu phát thành công, chuyển máy sang trạng thái nằm vùng chờ đợi (WAIT_RESPONSE). Nếu phát thất bại do lỗi chip, gọi ngay handle_timeout()
  if (gw->state == LORA_GW_STATE_WAIT_RESPONSE) {
    // Bước 3.1: Kiểm tra xem Anten phần cứng có báo có sóng dữ liệu về không
    if (gw->radio.rx_pending != nullptr && gw->radio.rx_pending()) {
      uint8_t raw[LORA_PACKET_MAX_SIZE];
      int16_t rssi = 0;
      // Thực hiện đọc mảng byte thô từ chip LoRa về biến mang tên 'raw'
      const int rx_len = gw->radio.receive != nullptr ? gw->radio.receive(raw, sizeof(raw), &rssi) : 0;
      if (rx_len > 0) {
        lora_packet_t pkt;
        // Tiến hành giải mã chuỗi byte nhị phân thô thành gói tin cấu trúc logic 'pkt'
        if (lora_packet_decode(raw, static_cast<size_t>(rx_len), &pkt)) {
          // Kiểm tra xem địa chỉ đích đến của gói tin này có phải gửi đích danh cho Gateway không
          if (pkt.dst == gw->config.gateway_id) {
            process_response(gw, &pkt, rssi, now_ms);
            return;
            //Nếu đang WAIT_RESPONSE: 1. Kiểm tra xem phần cứng có báo nhận được sóng không (rx_pending()). Nếu có sóng, đọc mảng byte thô về, giải mã gói tin. Nếu gói tin chuẩn và gửi đích danh cho Gateway (pkt.dst == gateway_id), ném gói tin vào hàm process_response() để xử lý.
          }
        }
      }
    }
    // Bước 3.2: Nếu Anten không nhận được gì, liên tục kiểm tra thời gian đứng đợi tại trạng thái này
// 🌟 KHU VỰC VÁ LỖI TIMEOUT CHO NÚT BẤM GUI: Kiểm tra thời gian đứng đợi phản hồi
if (now_ms - gw->state_enter_ms >= gw->config.response_timeout_ms) {
  extern volatile bool g_waiting_for_control_ack; // Gọi cờ hiệu từ file main.cpp sang
  
  // TRƯỜNG HỢP A: Nếu hết giờ khi đang đợi lệnh NÚT BẤM WEB
  if (g_waiting_for_control_ack) {
    g_waiting_for_control_ack = false; // 1. Hạ cờ xuống
    Serial.println(F("\n[LoRa Gateway] ❌ Timeout! Node dưới vườn không phản hồi gói ACK điều khiển nút bấm."));
    
    // 2. Trả máy trạng thái về đúng vị trí cũ trước khi bị nút bấm xen ngang
    if (gw->poll_active) {
      transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms); // Nếu đang quét cảm biến dở thì quay lại quét tiếp
    } else {
      transition(gw, LORA_GW_STATE_IDLE, now_ms); // Nếu đang rảnh thì trả về nghỉ ngơi
    }
  } 
  // TRƯỜNG HỢP B: Nếu hết giờ khi đang quét CẢM BIẾN định kỳ (Không chạm vào nút bấm)
  else {
    handle_timeout(gw, now_ms); // Gọi hàm xử lý gốc của đồng nghiệp (Hợp lý 100%)
  }
}
return;
    //Nếu không có sóng, nó liên tục kiểm tra thời gian. Nếu thời gian chờ vượt ngưỡng timeout (now_ms - state_enter_ms >= response_timeout_ms), nó kích hoạt hàm handle_timeout().
  }

    // 🌟 NEW STATE: Chờ ACK được xử lý đầy đủ (tối thiểu 1 vòng loop)
  if (gw->state == LORA_GW_STATE_WAIT_ACK_PROCESSED) {
    // Chỉ cần 1 lần lặp để xử lý xong, sau đó quay lại
    if (gw->poll_active) {
      transition(gw, LORA_GW_STATE_SEND_REQUEST, now_ms);
    } else {
      transition(gw, LORA_GW_STATE_IDLE, now_ms);
    }
    return;
  }
  
  if (gw->state == LORA_GW_STATE_NEXT_NODE) {
    advance_to_next_node(gw, now_ms);
  }
}
//Nếu đang LORA_GW_STATE_NEXT_NODE: Gọi hàm advance_to_next_node() để nhảy Node.
lora_gateway_state_t lora_gateway_get_state(const lora_gateway_t* gw) {//rả về trạng thái hiện tại của máy trạng thái nhằm mục đích Debug.
  return gw != nullptr ? gw->state : LORA_GW_STATE_IDLE;
}

const lora_node_status_t* lora_gateway_get_node_status(const lora_gateway_t* gw, uint8_t node_id) {//Cho phép các file code khác gọi tới để xem Node bất kỳ đang Online hay Offline để hiển thị lên đèn LED hoặc màn hình.
  if (gw == nullptr) return nullptr;
  return find_node_status(const_cast<lora_gateway_t*>(gw), node_id);
}

#ifdef ARDUINO
lora_gateway_t g_gateway;
    extern PubSubClient client;
namespace {
//lora_gateway_t g_gateway;
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
//Đây là nơi tác giả "móc" các hàm logic của máy trạng thái vào hàm thực tế của thư viện phần cứng ESP32 (loraSend, loraRxPending, loraReceive, millis()).



/** @brief Gateway callback: 🌟 TINH TÚY: xử lý giải mã 7 cảm biến thật và truyền MQTT JSON của bạn */
void onSensorData(uint8_t node_id, const uint8_t* payload, uint8_t payload_len, int16_t rssi) {// hàm xử lý dữ liệu từ các node
  Serial.print(F("\n[LoRa] Bắt được gói SENSOR_DATA từ Node 0x"));
  Serial.print(node_id, HEX);
  Serial.print(F(", RSSI: "));
  Serial.print(rssi);
  Serial.print(F(" dBm, len: "));
  Serial.println(payload_len);

  if (payload_len >= sizeof(lora_sensor_payload_t)) {
    lora_sensor_payload_t sensor;
    memcpy(&sensor, payload, sizeof(sensor));// đổ dữ liệu vào cấu trúc struct
    
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
    // extern PubSubClient client;
    // extern int fakePumpStatus;
    // extern int fakePumpPwm;
    // extern int fakeRoofPwm;
    // extern String fakeSystemMode;

    int idx = nodeIndexFromId(node_id);
    if (idx < 0) return;
    NodeMirrorState& ns = nodeStates[idx];
    ns.systemMode = (sensor.system_mode == 1) ? "auto" : "manual";
    ns.pumpStatus = sensor.pump_status;
    if (sensor.roof_status == 1)      ns.roofStatus = "OPEN";
    else if (sensor.roof_status == 2) ns.roofStatus = "CLOSE";
    else                              ns.roofStatus = "STOP";
    /* PWM tu node (LCD / lenh Web da ap) — dong bo slider GUI */
    ns.pumpPwm = sensor.pump_pwm;
    ns.roofPwm = sensor.roof_pwm;

  
    // Đóng gói JSON thực tế đẩy thẳng lên HiveMQ Broker của bạn
    if (client.connected()) {
      JsonDocument doc;
      doc["node_id"]      = node_id;        // 🆕 THÊM DÒNG NÀY — để backend biết gói này là của node nào
      doc["soil"]        = real_soil;
      doc["temp"]        = real_temp;
      doc["humi"]        = real_humi;
      doc["water"]       = real_water;
      doc["flow"]        = real_flow;
      doc["current_amp"] = real_amp;
      doc["mua"]         = real_rain;
      doc["mode"]        = ns.systemMode;
      doc["pumpPwm"]     = ns.pumpPwm;
      doc["roofPwm"]     = ns.roofPwm;
      doc["pump"]        = ns.pumpStatus;
      doc["roof"]        = ns.roofStatus;
      doc["pumpAlert"] = sensor.pump_diagnostic;

      char mqtt_buffer[256];//320
      serializeJson(doc, mqtt_buffer);//// Chuyển cấu trúc JSON thành một chuỗi văn bản văn bản
      
      client.publish("smartfarm/sensors", mqtt_buffer);//đẩy gói JSON này lên HiveMQ để giao diện Web bắt lấy hiển thị lên biểu đồ.
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