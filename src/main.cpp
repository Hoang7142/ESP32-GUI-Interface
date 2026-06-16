#include <Arduino.h>
// Uncomment to enable each feature at compile time
// #define ENABLE_WIFI
// #define ENABLE_MQTT
#define ENABLE_LORA

#if defined(ENABLE_MQTT) && !defined(ENABLE_WIFI)
#error "ENABLE_MQTT requires ENABLE_WIFI"
#endif
#ifdef ENABLE_LORA
#include "lora_radio.h"
#include "lora_gateway.h"
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

// 🔹 BIẾN LƯU TRẠNG THÁI GIẢ LẬP ĐỂ ĐỒNG BỘ VỚI CÁC THANH TRƯỢT PWM VÀ NÚT BẤM
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
  if (loraBegin()) {
    initLoRaGateway();
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
// 🔹 HÀM HỨNG LỆNH ĐIỀU KHIỂN TỪ WEB VÀ PHẢN HỒI FEEDBACK TỨC THÌ
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Nhan lenh tu topic: ");
  Serial.println(topic);

  JsonDocument doc;//tao ra bo dem de chuan bi giai ma JSON
  deserializeJson(doc, payload, length);//ra goi du lieu->bien doc

  JsonDocument feedbackDoc;
  char feedbackBuffer[128];
// chuan bi mot goi JAOn moi de phan hoi len web(da doc)//

  // 1. Lệnh Bật/Tắt máy bơm
  if (doc["device"] == "pump") {
    fakePumpStatus = doc["state"];
    feedbackDoc["pump"] = fakePumpStatus;
    Serial.println(fakePumpStatus == 1 ? "-> BOM: BAT" : "-> BOM: TAT");
  }
  // 2. Lệnh kéo thanh trượt PWM Bơm (PB1)
  else if (doc["device"] == "pump_pwm") {
    fakePumpPwm = doc["val"];
    Serial.printf("-> PWM BOM: %d%%\n", fakePumpPwm);
  }
  // 3. Lệnh cụm 3 nút mái che
  else if (doc["device"] == "roof") {
    String action = doc["action"].as<String>();
    fakeRoofStatus = action;
    feedbackDoc["roof"] = fakeRoofStatus;
    Serial.println("-> MAI CHE: " + fakeRoofStatus);
  }
  // 4. Lệnh kéo thanh trượt PWM Mái che (PA8)
  else if (doc["device"] == "roof_pwm") {
    fakeRoofPwm = doc["val"];
    Serial.printf("-> PWM MAI CHE: %d%%\n", fakeRoofPwm);
  }
  // 5. Lệnh chuyển chế độ hệ thống
  else if (doc["device"] == "system") {
    fakeSystemMode = doc["mode"].as<String>();
    Serial.println("-> CHE DO: " + fakeSystemMode);
  }

  // Gửi feedback ngược lại cho server.js xác nhận trạng thái nút bấm
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
      
      // 🔹 Đăng ký nhận kênh điều khiển từ Web gửi xuống thông qua Server Node.js
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

  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 2000) {
    lastMsg = millis();

    // 🎲 Tạo số ngẫu nhiên dao động tự nhiên cho môi trường
    float soil = random(550, 680) / 10.0;
    float temp = random(290, 340) / 10.0;
    float humi = random(650, 850) / 10.0;
    float water = random(700, 850) / 10.0;

    float current_amp = 0.0;
    float flow = 0.0;

    // Logic tính toán dòng điện và lưu lượng phụ thuộc hoàn toàn vào trạng thái nút Bơm trên Web
    if (fakePumpStatus == 1) {
      // Dòng và lưu lượng tính toán tỷ lệ thuận theo thanh trượt PWM máy bơm trên Web
      flow = (random(120, 150) / 10.0) * (fakePumpPwm / 100.0);
      current_amp = (random(14, 20) / 10.0) * (fakePumpPwm / 100.0); // Dòng bình thường ~ 1.5A

      // 🧠 THỬ NGHIỆM KỊCH BẢN CHẨN ĐOÁN LỖI KHẨN CẤP TRÊN WEB GUI
      // Để test Hộp chẩn đoán đổi sang màu ĐỎ (Kẹt Motor quá dòng), mở gạch chéo 2 dòng dưới:
      // current_amp = 3.6; flow = 0.0;

      // Để test Hộp chẩn đoán đổi sang màu CAM (Hụt nước/Nghẹt ống), mở gạch chéo 2 dòng dưới:
      // current_amp = 1.5; flow = 0.0;
    }

#ifdef ENABLE_MQTT
    JsonDocument doc;
    doc["soil"] = soil;
    doc["temp"] = temp;
    doc["humi"] = humi;
    doc["water"] = water;
    doc["flow"] = flow;
    doc["current_amp"] = current_amp;
    doc["mua"] = 0; // 0: không mưa, 1: có mưa
    doc["mode"] = fakeSystemMode;
    doc["pumpPwm"] = fakePumpPwm;
    doc["roofPwm"] = fakeRoofPwm;

    char buffer[256];
    serializeJson(doc, buffer);
    client.publish("smartfarm/sensors", buffer);

    Serial.print("Da gui du lieu fake: ");
    Serial.println(buffer);
#else
    // Serial.printf("soil=%.1f temp=%.1f humi=%.1f water=%.1f flow=%.1f amp=%.1f \r\n",
    //               soil, temp, humi, water, flow, current_amp);
#endif

#ifdef ENABLE_LORA
    loraGatewayPoll();
#endif
  }
}
