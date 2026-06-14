#include <Arduino.h>

// 🔹 BẬT CÁC TÍNH NĂNG CHẠY THẬT
#define ENABLE_WIFI
#define ENABLE_MQTT
#define ENABLE_LORA

#if defined(ENABLE_MQTT) && !defined(ENABLE_WIFI)
#error "ENABLE_MQTT requires ENABLE_WIFI"
#endif

#ifdef ENABLE_LORA
#include "lora_radio.h"
#include "lora_config.h"
#include "lora_protocol.h"        // Thêm file giao thức
#include "lora_network_config.h"  // Thêm file ID mạng chung
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
// --- Cấu hình WiFi ---
const char* ssid = "HOANGDIN";
const char* password = "123456789";
#endif

#ifdef ENABLE_MQTT
// --- MQTT ---
const char* mqtt_server = "364b003ce9c44c90a68e7930b601f369.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "dth7142_db_user";
const char* mqtt_pass = "Hoang2410@";

WiFiClientSecure espClient;
PubSubClient client(espClient);
#endif

// 🔹 Biến lưu trạng thái đồng bộ với Web GUI
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
  loraBegin();
  Serial.println("LoRa initialized");
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
// 🔹 Hàm hứng lệnh điều khiển từ Web xuống
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Nhan lenh tu topic: ");
  Serial.println(topic);

  JsonDocument doc;
  deserializeJson(doc, payload, length);

  JsonDocument feedbackDoc;
  char feedbackBuffer[128];

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

  if (feedbackDoc.size() > 0) {
    serializeJson(feedbackDoc, feedbackBuffer);
    client.publish("smartfarm/feedback", feedbackBuffer);
  }
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

#ifdef ENABLE_LORA
void printHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i + 1 < len) {
      Serial.print(' ');
    }
  }
}

// 🔹 Hàm nhận gói tin LoRa thật, giải mã kiểm tra CRC và đẩy MQTT lên Web GUI
void tryReceive() {
  uint8_t buffer[LORA_MAX_PACKET_LEN];
  int16_t rssi = 0;
  float snr = 0.0f;

  // 1. Nhận mảng byte thô từ module LoRa
  int len = loraReceive(buffer, sizeof(buffer), &rssi, &snr);
  if (len <= 0) {
    return;
  }

  Serial.printf("[LoRa] Nhận %d bytes. RSSI: %d dBm, SNR: %.1f dB\n", len, rssi, snr);

  // 2. Giải mã gói tin và tự động kiểm tra lỗi CRC16 bằng protocol chung
  lora_packet_t rx_packet;
  if (!lora_packet_decode(buffer, static_cast<size_t>(len), &rx_packet)) {
    Serial.println("[⚠️ LoRa] Gói tin không hợp lệ hoặc SAI số CRC!");
    return;
  }

  // 3. Kiểm tra xem gói tin có đúng gửi cho Gateway (0x01) hay không
  if (rx_packet.dst != LORA_GATEWAY_ID) {
    return;
  }

  // 4. Nếu đúng là mã lệnh chứa dữ liệu cảm biến từ STM32 gửi về
  if (rx_packet.cmd == CMD_SENSOR_DATA) {
    lora_sensor_payload_t actual_data;// <--- TẠO RA STRUCT ĐỂ LƯU TRỮ ĐÂY BẠN!
    
    // 5. Khúc "Thần thánh" nhất: Đổ dữ liệu thô vào Struct
    if (rx_packet.payload_len == sizeof(lora_sensor_payload_t)) {


      // Sao chép vùng nhớ payload thô sang cấu trúc struct cảm biến
      // Lệnh memcpy này sẽ hốt toàn bộ các byte thô trong rx_packet.payload
      // xếp khít vào đúng vị trí của struct actual_data theo thứ tự từ trên xuống dưới.
      memcpy(&actual_data, rx_packet.payload, sizeof(lora_sensor_payload_t));

      // Giải nén các giá trị số nguyên về lại số thực giống như bên STM32 cấu hình
      // LÚC NÀY, DỮ LIỆU ĐÃ NẰM TRONG STRUCT actual_data!
      // Bạn bắt đầu lôi từng biến ra để chia tỉ lệ (giải nén) đưa về số thực:
      float real_temp  = static_cast<float>(actual_data.temperature_c10) / 10.0f;
      float real_humi  = static_cast<float>(actual_data.humidity_pct10) / 10.0f;
      float real_soil  = static_cast<float>(actual_data.soil_moisture);
      float real_water = static_cast<float>(actual_data.water_level);
      float real_flow  = static_cast<float>(actual_data.flow_rate_Lmin_x10) / 10.0f;
      float real_amp   = static_cast<float>(actual_data.current_mA) / 1000.0f;
      int real_rain    = (actual_data.rain_status == 0) ? 1 : 0; // 0 ở STM32 nghĩa là có mưa -> đổi thành 1 để Web hiểu

      // In dữ liệu thật ra màn hình Serial Monitor để kiểm soát tại chỗ
      Serial.printf("[DỮ LIỆU THẬT STM32] Temp: %.1f, Hum: %.1f, Soil: %.0f%%, Water: %.0f%%, Flow: %.1f, Amp: %.3f, Rain: %d\n", 
                    real_temp, real_humi, real_soil, real_water, real_flow, real_amp, real_rain);

#ifdef ENABLE_MQTT
      // 5. Đóng gói dữ liệu THẬT lấy từ sóng LoRa vào JSON để đẩy thẳng lên Web GUI hiển thị
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
      
      if (client.connected()) {
        client.publish("smartfarm/sensors", mqtt_buffer);
        Serial.print("🚀 Đã đẩy DỮ LIỆU THẬT từ LoRa lên MQTT: ");
        Serial.println(mqtt_buffer);
      }
#endif
    }
  }
}

// 🔹 Hàm phát lệnh gọi dữ liệu (Polling) yêu cầu Node STM32 (0x11) gửi cảm biến về
void tryTransmit() {
  lora_packet_t tx_packet;// Bước 2: Tạo một cái thùng hàng rỗng tên là tx_packet
  static uint8_t gateway_seq = 0;
  // Bước 3: Đóng gói thông tin + Tự động tính toán dán tem lỗi CRC16
  if (lora_packet_build(&tx_packet, 0x11, LORA_GATEWAY_ID, 
                        CMD_READ_SENSOR, gateway_seq++, nullptr, 0)) {
   // Bước 4: Duỗi thẳng Struct thành một mảng byte thô (wire_buffer) để chuẩn bị phát 
    uint8_t wire_buffer[LORA_PACKET_MAX_SIZE];
    size_t wire_len = lora_packet_encode(&tx_packet, wire_buffer, sizeof(wire_buffer));
    
    if (wire_len > 0) {
      Serial.printf("📡 Gateway phát lệnh READ_SENSOR tới Node 0x11 (Seq: %u)...\n", tx_packet.seq);
      loraSend(wire_buffer, wire_len);
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
  // ⚡ Luôn luôn lắng nghe xem có sóng LoRa mang dữ liệu thật bay tới hay không
  tryReceive();
#endif

  // 📡 Định kỳ 5 giây bắn lệnh xuống kích STM32 gửi dữ liệu một lần
  static unsigned long lastPollTime = 0;
  if (millis() - lastPollTime > 5000) {
    lastPollTime = millis();
    
#ifdef ENABLE_LORA
    tryTransmit(); 
#endif
  }
}
