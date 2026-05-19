// // #include <SPI.h>
// // #include <LoRa.h>

// // // Chot cac chan theo bang tren
// // #define SCK     18
// // #define MISO    19
// // #define MOSI    23
// // #define SS      5
// // #define RST     14
// // #define DIO0    2

// // void setup() {
// //   // Khoi tao Serial de debug
// //   Serial.begin(115200);
// //   while (!Serial);

// //   Serial.println("--- DANG KHOI TAO LORA ---");

// //   // 1. Khoi tao giao tiep SPI
// //   SPI.begin(SCK, MISO, MOSI, SS);

// //   // 2. Gan cac chan cho thu vien LoRa
// //   LoRa.setPins(SS, RST, DIO0);

// //   // 3. Bat dau khoi tao module o tan so 433MHz
// //   // Neu dung module 433MHz thi de 433E6, neu 915MHz thi de 915E6
// //   if (!LoRa.begin(433E6)) {
// //     Serial.println("KHOI TAO THAT BAI!");
// //     while (1); 
// //   }

// //   // 4. Tuy chinh (Optional)
// //   LoRa.setSyncWord(0xF1); // Dat ma dong bo trung voi STM32
  
// //   Serial.println("KHOI TAO THANH CONG!");
// //   Serial.println("Dang cho tin hieu...");
// // }

// // void loop() {
// //   // Kiem tra goi tin den
// //   int packetSize = LoRa.parsePacket();
// //   if (packetSize) {
// //     Serial.print("Nhan duoc: ");
// //     while (LoRa.available()) {
// //       Serial.print((char)LoRa.read());
// //     }
// //     Serial.print(" | RSSI: ");
// //     Serial.println(LoRa.packetRssi());
// //   }
// // }

// #include <SPI.h>
// #include <LoRa.h>

// // ===== PIN CONFIG =====
// #define SCK     18
// #define MISO    19
// #define MOSI    23
// #define SS      5
// #define RST     14
// #define DIO0    2

// // 👉 Pin debug để bắt timing trên logic analyzer
// #define DEBUG_PIN 4

// void setup() {
//   Serial.begin(115200);
//   while (!Serial);

//   Serial.println("\n--- DANG KHOI TAO LORA ---");

//   pinMode(DEBUG_PIN, OUTPUT);
//   digitalWrite(DEBUG_PIN, LOW);

//   // ===== SPI INIT =====
//   SPI.begin(SCK, MISO, MOSI, SS);

//   // ===== LORA CONFIG =====
//   LoRa.setPins(SS, RST, DIO0);

//   // 🔥 QUAN TRỌNG: giảm tốc độ SPI để đo được clock
//   LoRa.setSPIFrequency(100000);   // 100kHz
//   // Nếu vẫn khó đo → đổi thành: 10000

//   // ===== INIT LORA =====
//   if (!LoRa.begin(433E6)) {
//     Serial.println("KHOI TAO THAT BAI!");
//     while (1);
//   }

//   LoRa.setSyncWord(0xF1);

//   Serial.println("KHOI TAO THANH CONG!");
//   Serial.println("Dang cho tin hieu...");
// }

// void loop() {

//   // 🔥 Bật debug pin → báo bắt đầu SPI
//   digitalWrite(DEBUG_PIN, HIGH);

//   int packetSize = LoRa.parsePacket();

//   // 🔥 Tắt debug pin → kết thúc SPI
//   digitalWrite(DEBUG_PIN, LOW);

//   if (packetSize) {
//     Serial.print("Nhan duoc: ");

//     while (LoRa.available()) {
//       Serial.print((char)LoRa.read());
//     }

//     Serial.print(" | RSSI: ");
//     Serial.println(LoRa.packetRssi());
//   }

//   delay(10); // giúp analyzer dễ bắt hơn
// }
////////////////////////////////////////////////////////

// #include <WiFi.h>
// #include <PubSubClient.h>
// #include <ArduinoJson.h>
// #include <WiFiClientSecure.h>

// // Prototype
// void setup_wifi();
// void reconnect();

// // --- Cấu hình WiFi ---
// const char* ssid = "HOANGDIN";
// const char* password = "123456789";

// // --- MQTT ---
// const char* mqtt_server = "364b003ce9c44c90a68e7930b601f369.s1.eu.hivemq.cloud";
// const int mqtt_port = 8883;
// const char* mqtt_user = "dth7142_db_user";
// const char* mqtt_pass = "Hoang2410@";

// WiFiClientSecure espClient;
// PubSubClient client(espClient);

// void setup() {
//   Serial.begin(115200);

//   setup_wifi();

//   espClient.setInsecure();

//   client.setServer(mqtt_server, mqtt_port);
// }

// void setup_wifi() {
//   delay(10);

//   Serial.println("Dang ket noi WiFi...");

//   WiFi.begin(ssid, password);

//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }

//   Serial.println("\nWiFi Connected!");
// }

// void reconnect() {
//   while (!client.connected()) {

//     Serial.println("Dang ket noi MQTT...");

//     if (client.connect("ESP32_Gateway", mqtt_user, mqtt_pass)) {

//       Serial.println("MQTT Connected!");

//     } else {

//       Serial.print("Failed, rc=");
//       Serial.println(client.state());

//       delay(5000);
//     }
//   }
// }

// void loop() {

//   if (!client.connected()) {
//     reconnect();
//   }

//   client.loop();

//   static unsigned long lastMsg = 0;

//   if (millis() - lastMsg > 5000) {

//     lastMsg = millis();

//     float soil = random(400, 700) / 10.0;
//     float temp = random(250, 350) / 10.0;
//     float humi = random(600, 900) / 10.0;
//     float water = random(50, 100);
//     float flow = random(0, 20) / 10.0;

//     JsonDocument doc;

//     doc["soil"] = soil;
//     doc["temp"] = temp;
//     doc["humi"] = humi;
//     doc["water"] = water;
//     doc["flow"] = flow;

//     char buffer[256];

//     serializeJson(doc, buffer);

//     client.publish("smartfarm/sensors", buffer);

//     Serial.print("Da gui du lieu fake: ");
//     Serial.println(buffer);
//   }
// }
///////////////////////////////////////
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// Prototype
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);

// --- Cấu hình WiFi ---
const char* ssid = "HOANGDIN";
const char* password = "123456789";

// --- MQTT ---
const char* mqtt_server = "364b003ce9c44c90a68e7930b601f369.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "dth7142_db_user";
const char* mqtt_pass = "Hoang2410@";

// 🔹 BIẾN LƯU TRẠNG THÁI GIẢ LẬP ĐỂ ĐỒNG BỘ VỚI CÁC THANH TRƯỢT PWM VÀ NÚT BẤM
int fakePumpStatus = 0;
int fakePumpPwm = 100;
int fakeRoofPwm = 100;
String fakeRoofStatus = "STOP";
String fakeSystemMode = "manual";

WiFiClientSecure espClient;//bao mat
PubSubClient client(espClient);// giao thuc mqtt

void setup() {
  Serial.begin(115200);
  setup_wifi();
  espClient.setInsecure();//boqua buoc kiem tra bao mat
  client.setServer(mqtt_server, mqtt_port);
  
  // 🔹 Đăng ký hàm callback nhận lệnh điều khiển
  client.setCallback(callback);
}

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

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 4000) {
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
  }
}

