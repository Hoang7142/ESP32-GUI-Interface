#include <Arduino.h>

// 🔹 BẬT CÁC TÍNH NĂNG MẠNG THẬT CHẠY MÔ HÌNH THỰC TẾ
#define ENABLE_WIFI
#define ENABLE_MQTT
#define ENABLE_LORA
#define CONTROL_TEST_DEBUG  1

#if defined(ENABLE_MQTT) && !defined(ENABLE_WIFI)
#error "ENABLE_MQTT requires ENABLE_WIFI"
#endif

#ifdef ENABLE_LORA
// Sử dụng extern "C" để liên kết ngôn ngữ vì driver mạng trung gian viết bằng C chuẩn
//extern "C" {
  #include "lora_radio.h"
  #include "lora_config.h"
  #include "lora_protocol.h"        // File giao thức chứa cấu trúc 7 cảm biến và nút bấm của bạn
  #include "lora_network_config.h"  // File ID cấu hình chung mạng LoRa
  #include "lora_gateway.h"         // File điều phối quét mạng ngầm tự động
//}
#endif

#ifdef ENABLE_WIFI
#include <WiFi.h>
#include <WiFiManager.h>
#endif

#ifdef ENABLE_MQTT
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#endif

// Prototypes
#ifdef ENABLE_WIFI
void connectWiFi();   // setup: WiFiManager (portal SmartFarm_SetupWifi)
void ensureWiFi();    // loop: tự nối lại nếu mất WiFi
#endif

#ifdef ENABLE_MQTT
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
#endif

#ifdef ENABLE_MQTT
// --- MQTT Broker HiveMQ ---
const char* mqtt_server = "364b003ce9c44c90a68e7930b601f369.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "dth7142_db_user";
const char* mqtt_pass = "Hoang2410@";

WiFiClientSecure espClient;
PubSubClient client(espClient);
#endif

// Cờ báo hiệu có lệnh điều khiển mới từ MQTT đang chờ được gửi đi
volatile bool g_mqtt_cmd_pending = false;
// Biến lưu tạm cấu trúc lệnh nút nhấn từ Web
lora_control_payload_t g_pending_control_data;
// Biến lưu ID của Node cần điều khiển (nếu hệ thống của bạn có nhiều Node)
uint8_t g_pending_target_node = 0x11;
// 🌟 THÊM DÒNG NÀY: Cờ báo hiệu hệ thống đang đứng đợi gói ACK của nút bấm GUI
volatile bool g_waiting_for_control_ack = false;
volatile bool g_threshold_cmd_pending = false;
lora_threshold_payload_t g_pending_threshold_data;
uint8_t g_pending_threshold_node = 0x11;

// Trạng thái riêng cho từng node: 0=0x11, 1=0x12, 2=0x13
struct NodeMirrorState {
  int pumpStatus = 0;
  int pumpPwm = 100;
  int roofPwm = 20;
  String roofStatus = "STOP";
  String systemMode = "manual";
};

NodeMirrorState nodeStates[3];

int nodeIndexFromId(uint8_t id) {
  if (id == 0x11) return 0;
  if (id == 0x12) return 1;
  if (id == 0x13) return 2;
  return -1;
}

NodeMirrorState* getNodeState(uint8_t id) {
  int idx = nodeIndexFromId(id);
  if (idx < 0) return nullptr;
  return &nodeStates[idx];
}

void setup() {
  Serial.begin(115200);

#ifdef ENABLE_WIFI
  connectWiFi();  // WiFiManager — khớp lưu đồ Trọng (nông nghiệp)
#endif

#ifdef ENABLE_MQTT
  espClient.setInsecure();// Bỏ qua bước xác thực chứng chỉ SSL phức tạp để ESP32 kết nối HiveMQ nhanh hơn, đỡ tốn RAM
  client.setServer(mqtt_server, mqtt_port);// Khai báo địa chỉ Server và Cổng (8883) của HiveMQ Cloud
  client.setCallback(callback);// Cắm hàm callback (Hàm số 3) làm tai nghe ngóng lệnh MQTT gửi xuống
#endif

#ifdef ENABLE_LORA
  // 1. Khởi động driver thu phát LoRa chế độ ngắt chân DIO0 nhạy sóng của đồng nghiệp
  if (loraBegin()) {
    Serial.println("Driver LoRa phần cứng ready!");
    // 2. Kích hoạt cỗ máy Scheduler quét mạng tự động ONLINE/OFFLINE của đồng nghiệp
    if (initLoRaGateway()) {
      Serial.println("Cỗ máy Scheduler quét mạng tự động đã khởi tạo!");
    } else {
      Serial.println("Khởi động Scheduler quét mạng THẤT BẠI!");
    }
  } else {
    Serial.println("Khởi động driver LoRa phần cứng THẤT BẠI!");
  }
#endif
}

#ifdef ENABLE_WIFI
/**
 * connectWiFi() — theo lưu đồ:
 * - Đã lưu WiFi trước → tự nối
 * - Thất bại → mở portal "SmartFarm_SetupWifi", timeout 120s → restart ESP
 */
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;

  // Timeout portal 120 giây (như lưu đồ Trọng)
  wm.setConfigPortalTimeout(120);
  wm.setConnectTimeout(30);

  Serial.println("WiFiManager: dang ket noi / mo portal SmartFarm_SetupWifi ...");
  bool ok = wm.autoConnect("SmartFarm_SetupWifi");

  if (!ok) {
    Serial.println("WiFiManager that bai / timeout 120s -> restart ESP32");
    delay(1500);
    ESP.restart();
  }

  Serial.println("WiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

/**
 * ensureWiFi() — trong loop: mất WiFi thì reconnect bằng credential đã lưu.
 * Vẫn fail sau ~15s → restart để chạy lại WiFiManager (portal nếu cần).
 */
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("WiFi mat ket noi -> ensureWiFi() reconnect...");
  WiFi.reconnect();

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000UL) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi reconnect OK");
    return;
  }

  Serial.println("\nensureWiFi that bai -> restart ESP (mo lai WiFiManager)");
  delay(1000);
  ESP.restart();
}
#endif
#ifdef ENABLE_MQTT
// 🔹 100% TINH TÚY ĐIỀU KHIỂN CỦA BẠN: Hứng lệnh mạng MQTT và đưa vào hàng đợi an toàn
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Nhan lenh tu topic: ");
  Serial.println(topic);

  // 1. Dùng thư viện ArduinoJson để giải mã mảng Byte thô thành một cây thư mục dữ liệu JSON
  JsonDocument doc;
  deserializeJson(doc, payload, length);
  uint8_t target_node = (uint8_t)doc["node_id"].as<int>();
if (target_node == 0) {
  target_node = 0x11;  // fallback nếu MQTT thiếu node_id
}
if (target_node != 0x11 && target_node != 0x12 && target_node != 0x13) {
    Serial.printf(" [Cảnh báo] Node ID 0x%02X không thuộc hệ thống!\n", target_node);
    return;
}

NodeMirrorState* ns = getNodeState(target_node);
if (!ns) return;

  JsonDocument feedbackDoc;
  String device = doc["device"] | "";
  bool queue_lora = false;
  bool touch_roof = false;

  // 2. PHÂN PHỐI LỆNH: Kiểm tra xem Web đang muốn điều khiển thiết bị nào (device)
if (device == "pump") {
    // FIX BUG #8: chỉ nhận lệnh bơm khi đang MANUAL
    if (ns->systemMode == "manual") {
        ns->pumpStatus = doc["state"];
        feedbackDoc["pump"] = ns->pumpStatus;
        Serial.println(ns->pumpStatus == 1 ? "-> BOM: BAT" : "-> BOM: TAT");
        queue_lora = true;
    } else {
        Serial.println("-> [Từ chối] Lệnh BOM bị bỏ qua vì đang ở chế độ AUTO");
    }
}
  else if (device == "pump_pwm") {
    	
    ns->pumpPwm = doc["val"];// Cập nhật tốc độ bơm từ thanh trượt (0 - 100%)
        Serial.printf("-> PWM BOM: %d%%\n", 	
          ns->pumpPwm);
        queue_lora = true;
  }
  else if (device == "roof") {
    // FIX (giống Bug #8): chỉ nhận lệnh mái che khi đang MANUAL
    if (ns->systemMode == "manual") {
        String action = doc["action"].as<String>();// Đọc chuỗi hành động mái che: "OPEN", "CLOSE", "STOP"
        ns->roofStatus = action;
        feedbackDoc["roof"] = ns->roofStatus;
        Serial.println("-> MAI CHE: " + ns->roofStatus);
        queue_lora = true;
        touch_roof = true;
    } else {
        Serial.println("-> [Từ chối] Lệnh MAI CHE bị bỏ qua vì đang ở chế độ AUTO");
    }
}
  else if (device == "roof_pwm") {
    ns->roofPwm = doc["val"];// Cập nhật tốc độ mở mái che (0 - 100%)
    Serial.printf("-> PWM MAI CHE: %d%%\n", ns->roofPwm);
    queue_lora = true;
    touch_roof = true;
  }
  else if (device == "system") {
    ns->systemMode = doc["mode"].as<String>();
    Serial.println("-> CHE DO: " + ns->systemMode);
    queue_lora = true;
  }
  else if (device == "auto_threshold") {
    uint8_t soilOn = (uint8_t)doc["soil_on"].as<int>();
    uint8_t soilOff = (uint8_t)doc["soil_off"].as<int>();
    int coolRaw = doc["cooldown_sec"] | 15;
    if (coolRaw < 0) coolRaw = 0;
    if (coolRaw > 60) coolRaw = 60;
    uint8_t coolSec = (uint8_t)coolRaw;
    if (soilOn < soilOff) {
      g_pending_threshold_data.soil_on = soilOn;
      g_pending_threshold_data.soil_off = soilOff;
      g_pending_threshold_data.cooldown_sec = coolSec;
      g_pending_threshold_node = target_node;
      g_threshold_cmd_pending = true;
      Serial.printf("-> NGUONG AUTO: bat<%u%% tat>%u%% cool=%us (Node 0x%02X)\n",
                    soilOn, soilOff, coolSec, target_node);
    }
    return;
  }

  // 🔹 TẮT ĐOẠN PHÁT MQTT TẠM THỜI Ở ĐÂY ĐỂ CHỐNG NHÁY WEB (THEO ĐÚNG CHỈ DẪN)
  // if (feedbackDoc.size() > 0) {
  //   String feedbackBuffer;
  //   serializeJson(feedbackDoc, feedbackBuffer);
  //   client.publish("smartfarm/feedback", feedbackBuffer.c_str());
  // }

#ifdef ENABLE_LORA
  if (!queue_lora) {
    return;
  }
  // ĐÓNG GÓI LỆNH ĐIỀU KHIỂN NÚT NHẤN VÀO HÀNG ĐỢI (CHỜ LUỒNG LOOP PHÁT AN TOÀN)
  g_pending_control_data.pump_status = (ns->pumpStatus == 1) ? 1 : 0;
  g_pending_control_data.pump_pwm    = (uint8_t)ns->pumpPwm;

  /*
   * Chi gui OPEN/CLOSE/STOP mai khi lenh dung la roof / roof_pwm.
   * Lenh bom/mode: roof_status=0xFF = "khong doi mai" (STM bo qua),
   * tranh keo lai OPEN cu va tranh xoa latch CTHT.
   */
  if (touch_roof) {
    if (ns->roofStatus == "OPEN")       g_pending_control_data.roof_status = 1;
    else if (ns->roofStatus == "CLOSE") g_pending_control_data.roof_status = 2;
    else                                g_pending_control_data.roof_status = 0;
  } else {
    g_pending_control_data.roof_status = 0xFFu; /* no-change */
  }
  g_pending_control_data.roof_pwm    = (uint8_t)ns->roofPwm;

  g_pending_control_data.system_mode = (ns->systemMode == "auto") ? 1 : 0;// Tự động = 1, Thủ công = 0
  
  g_pending_target_node = target_node;// Ghi nhận ID đích cần điều khiển
  g_mqtt_cmd_pending = true;// PHẤT CỜ HIỆU! Báo cho hàm loop biết có bưu phẩm khẩn cấp cần phát
  
  Serial.printf(" [Hang doi] Node 0x%02X roof_touch=%d roof=%u\n",
                target_node, touch_roof ? 1 : 0,
                (unsigned)g_pending_control_data.roof_status);
#endif
}

void reconnect() {
  while (!client.connected()) {
#ifdef ENABLE_WIFI
    // MQTT chỉ nối được khi WiFi còn sống
    ensureWiFi();
#endif
    Serial.println("Dang ket noi MQTT...");
    if (client.connect("ESP32_Gateway", mqtt_user, mqtt_pass)) {
      Serial.println("MQTT Connected!");
      client.subscribe("smartfarm/control");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}
#endif

void loop() {
#ifdef ENABLE_WIFI
  ensureWiFi();
#endif

#ifdef ENABLE_MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
#endif

#ifdef ENABLE_LORA
  // =========================================================================
  // LUỒNG THỰC THI LỆNH: Đóng gói và phát sóng LoRa xen ngang khi có yêu cầu từ Web
  // =========================================================================
  if (g_mqtt_cmd_pending) {
    g_mqtt_cmd_pending = false; // Xóa cờ ngay lập tức để tránh trùng lặp
    
    lora_packet_t tx_packet;
    
    // Sử dụng bộ đếm mã số gói (seq) ĐỘC LẬP cho nút bấm để không phá nhịp quét cảm biến
    static uint8_t control_seq = 0;
    control_seq++; 

    // Gọi hàm lora_packet_build để nén dữ liệu nút bấm gửi đích danh tới Node đích
    if (lora_packet_build(&tx_packet, g_pending_target_node, LORA_GATEWAY_ID,
                          CMD_WRITE_CONTROL, control_seq,
                          (const uint8_t*)&g_pending_control_data,
                          sizeof(g_pending_control_data))) {
      
      uint8_t wire_buffer[LORA_PACKET_MAX_SIZE]; 
      size_t wire_len = lora_packet_encode(&tx_packet, wire_buffer, sizeof(wire_buffer)); 
      
      if (wire_len > 0) {
        Serial.printf(" 📬 [Nút Bấm GUI] Đang phát sóng LoRa lệnh ĐIỀU KHIỂN xuống Node 0x%02X (Ctrl Seq: %u)...\n", 
                      g_pending_target_node, tx_packet.seq);
        loraSend(wire_buffer, wire_len); // PHÓNG SÓNG LORA!
        #if CONTROL_TEST_DEBUG
        Serial.printf(" [CTRL-TX] -> Node 0x%02X | pump=%u pwm=%u roof=%u roof_pwm=%u mode=%s\n",
          g_pending_target_node,
          g_pending_control_data.pump_status,
          g_pending_control_data.pump_pwm,
          g_pending_control_data.roof_status,
          g_pending_control_data.roof_pwm,
          g_pending_control_data.system_mode ? "auto" : "manual");
          #endif
        
        // 🔥 VÁ LỖI TRẠNG THÁI VÀ BẬT CỜ ĐỢI ACK ĐIỀU KHIỂN
        extern lora_gateway_t g_gateway;
        extern volatile bool g_waiting_for_control_ack;
        
        g_gateway.state = LORA_GW_STATE_WAIT_RESPONSE;
        g_gateway.state_enter_ms = millis(); // Chụp mốc thời gian bắt đầu đợi ACK
        g_waiting_for_control_ack = true;    // Kích hoạt cờ: "Tôi đang đợi ACK nút bấm"
        
        Serial.println("   🔄 [Hệ Thống] Đã sửa lỗi Anten Điếc & Chuyển Gateway sang trạng thái đợi phản hồi ACK!");
      }
    }
  }

  if (g_threshold_cmd_pending) {
    g_threshold_cmd_pending = false;
    lora_packet_t tx_packet;
    static uint8_t threshold_seq = 0;
    threshold_seq++;

    if (lora_packet_build(&tx_packet, g_pending_threshold_node, LORA_GATEWAY_ID,
                          CMD_SET_THRESHOLDS, threshold_seq,
                          (const uint8_t*)&g_pending_threshold_data,
                          sizeof(g_pending_threshold_data))) {
      uint8_t wire_buffer[LORA_PACKET_MAX_SIZE];
      size_t wire_len = lora_packet_encode(&tx_packet, wire_buffer, sizeof(wire_buffer));
      if (wire_len > 0) {
        Serial.printf(" [THRESHOLD-TX] -> Node 0x%02X ON<%u OFF>%u cool=%us\n",
                      g_pending_threshold_node,
                      g_pending_threshold_data.soil_on,
                      g_pending_threshold_data.soil_off,
                      g_pending_threshold_data.cooldown_sec);
        loraSend(wire_buffer, wire_len);
      }
    }
  }

  // ⚡ KẾT HỢP ĐỈNH CAO: Kích hoạt máy trạng thái đi tuần ngầm (Không chặn luồng điều khiển)
  loraGatewayPoll();
#endif
}