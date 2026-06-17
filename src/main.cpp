#include <Arduino.h>

// 🔹 BẬT CÁC TÍNH NĂNG MẠNG THẬT CHẠY MÔ HÌNH THỰC TẾ
#define ENABLE_WIFI
#define ENABLE_MQTT
#define ENABLE_LORA

#if defined(ENABLE_MQTT) && !defined(ENABLE_WIFI)
#error "ENABLE_MQTT requires ENABLE_WIFI"
#endif

#ifdef ENABLE_LORA
// Sử dụng extern "C" để liên kết ngôn ngữ vì driver mạng trung gian viết bằng C chuẩn
extern "C" {
  #include "lora_radio.h"
  #include "lora_config.h"
  #include "lora_protocol.h"        // File giao thức chứa cấu trúc 7 cảm biến và nút bấm của bạn
  #include "lora_network_config.h"  // File ID cấu hình chung mạng LoRa
  #include "lora_gateway.h"         // File điều phối quét mạng ngầm tự động
}
#endif

#ifdef ENABLE_WIFI
#include <WiFi.h>
#endif

#ifdef ENABLE_MQTT
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#endif

// Prototypes
#ifdef ENABLE_WIFI
void setup_wifi();
#endif

#ifdef ENABLE_MQTT
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
#endif

#ifdef ENABLE_WIFI
// --- Cấu hình mạng WiFi ---
const char* ssid = "HOANGDIN";
const char* password = "123456789";
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

// Biến lưu trạng thái đồng bộ thực tế với các thanh trượt PWM và nút bấm Web GUI
int fakePumpStatus = 0;
int fakePumpPwm = 100;
int fakeRoofPwm = 100;
String fakeRoofStatus = "STOP";
String fakeSystemMode = "manual";

void setup() {
  Serial.begin(115200);

#ifdef ENABLE_WIFI
  setup_wifi();
#endif

#ifdef ENABLE_MQTT
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
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
void setup_wifi() {
  delay(10);
  Serial.println("Dang ket noi WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}
#endif
#ifdef ENABLE_MQTT
// 🔹 100% TINH TÚY ĐIỀU KHIỂN CỦA BẠN: Hứng lệnh mạng MQTT và đưa vào hàng đợi an toàn
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Nhan lenh tu topic: ");
  Serial.println(topic);

  JsonDocument doc;
  deserializeJson(doc, payload, length);

  JsonDocument feedbackDoc;

  if (doc["device"] == "pump") {
    fakePumpStatus = doc["state"];
    feedbackDoc["pump"] = fakePumpStatus;
    Serial.println(fakePumpStatus == 1 ? "-> BOM: BAT" : "-> BOM: TAT");
  }
  else if (doc["device"] == "pump_pwm") {
    fakePumpPwm = doc["val"];
    Serial.printf("-> PWM BOM: %d%%\n", fakePumpPwm);
  }
  else if (doc["device"] == "roof") {
    String action = doc["action"].as<String>();
    fakeRoofStatus = action;
    feedbackDoc["roof"] = fakeRoofStatus;
    Serial.println("-> MAI CHE: " + fakeRoofStatus);
  }
  else if (doc["device"] == "roof_pwm") {
    fakeRoofPwm = doc["val"];
    Serial.printf("-> PWM MAI CHE: %d%%\n", fakeRoofPwm);
  }
  else if (doc["device"] == "system") {
    fakeSystemMode = doc["mode"].as<String>();
    Serial.println("-> CHE DO: " + fakeSystemMode);
  }

  // 🔹 TẮT ĐOẠN PHÁT MQTT TẠM THỜI Ở ĐÂY ĐỂ CHỐNG NHÁY WEB (THEO ĐÚNG CHỈ DẪN)
  // if (feedbackDoc.size() > 0) {
  //   String feedbackBuffer;
  //   serializeJson(feedbackDoc, feedbackBuffer);
  //   client.publish("smartfarm/feedback", feedbackBuffer.c_str());
  // }

#ifdef ENABLE_LORA
  // BÓC TÁCH NODE_ID ĐỘNG TỪ WEB (Hỗ trợ điều khiển chính xác Node 0x11, 0x12, 0x13)
  uint8_t target_node = doc["node_id"] | 0x11;
  if (target_node != 0x11 && target_node != 0x12 && target_node != 0x13) {
    Serial.printf(" [Cảnh báo] Node ID 0x%02X không thuộc hệ thống!\n", target_node);
    return;
  }

  // ĐÓNG GÓI LỆNH ĐIỀU KHIỂN NÚT NHẤN VÀO HÀNG ĐỢI (CHỜ LUỒNG LOOP PHÁT AN TOÀN)
  g_pending_control_data.pump_status = (fakePumpStatus == 1) ? 1 : 0;
  g_pending_control_data.pump_pwm    = (uint8_t)fakePumpPwm;

  if (fakeRoofStatus == "OPEN")       g_pending_control_data.roof_status = 1;
  else if (fakeRoofStatus == "CLOSE") g_pending_control_data.roof_status = 2;
  else                                g_pending_control_data.roof_status = 0; 
  g_pending_control_data.roof_pwm    = (uint8_t)fakeRoofPwm;

  g_pending_control_data.system_mode = (fakeSystemMode == "auto") ? 1 : 0;
  
  g_pending_target_node = target_node;
  g_mqtt_cmd_pending = true;
  
  Serial.printf(" [Hàng đợi] Đã xếp hàng lệnh cho Node 0x%02X. Chờ luồng Loop rảnh để phát...\n", target_node);
#endif
}

void reconnect() {
  while (!client.connected()) {
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
#ifdef ENABLE_MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
#endif

#ifdef ENABLE_LORA
  // =========================================================================
  // LUỒNG THỰC THI LỆNH: Đóng gói và phát sóng LoRa khi chip RF rảnh rỗi (XEN NGANG NHỊP QUÉT)
  // =========================================================================
  if (g_mqtt_cmd_pending) {
    g_mqtt_cmd_pending = false; // Xóa cờ ngay lập tức để tránh trùng lặp
    
    extern lora_gateway_t g_gateway; // Triệu hồi đối tượng gateway chạy ngầm
    lora_packet_t tx_packet;
    
    // 🌟 SỬA CHUẨN: Lấy nhịp sequence thực tế và tăng trực tiếp giá trị của hệ thống lên 1 đơn vị
    uint32_t ctrl_seq = g_gateway.current_seq;
    g_gateway.current_seq++; 

    if (lora_packet_build(&tx_packet, g_pending_target_node, LORA_GATEWAY_ID,
                          CMD_WRITE_CONTROL, ctrl_seq,
                          (const uint8_t*)&g_pending_control_data,
                          sizeof(g_pending_control_data))) {
      
      uint8_t wire_buffer[LORA_PACKET_MAX_SIZE];
      size_t wire_len = lora_packet_encode(&tx_packet, wire_buffer, sizeof(wire_buffer));
      
      if (wire_len > 0) {
        Serial.printf(" 📬 [Nút Bấm GUI - Luồng An Toàn] Đang phát sóng LoRa lệnh ĐIỀU KHIỂN xuống Node 0x%02X (Seq: %u)...\n", 
                      g_pending_target_node, tx_packet.seq);
        loraSend(wire_buffer, wire_len);
      }
    }
  }

  // ⚡ KẾT HỢP ĐỈNH CAO: Khởi chạy máy trạng thái tự động điều phối quét mạng chu kỳ không chặn
  loraGatewayPoll();
#endif
}


