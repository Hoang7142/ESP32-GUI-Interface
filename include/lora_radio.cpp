#include "lora_radio.h"

#include <RadioLib.h>
#include <SPI.h>

#include "lora_config.h"

namespace {

SPIClass loraSpi(VSPI);//Tạo ra một đối tượng đường truyền SPI mới tên là loraSpi dựa trên khối phần cứng VSPI có sẵn trong chip ESP32 để kết nối chuyên biệt với module LoRa.
SX1278 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RST, RADIOLIB_NC, loraSpi);//cáu hình chan lora

/*Thư viện RadioLib khi chạy sẽ trả về các mã lỗi bằng số nguyên (int16_t state).
 Hàm này dùng cấu trúc switch-case để kiểm tra số lỗi đó là gì và trả về 
 một chuỗi ký tự tiếng Anh tương ứng (OK - không lỗi, CHIP_NOT_FOUND - lỏng dây không thấy chip, CRC_MISMATCH - nhiễu sóng sai dữ liệu...) 
 để con người dễ đọc.*/
const char* stateToString(int16_t state) {//hàm dịch mã lỗi
  switch (state) {
    case RADIOLIB_ERR_NONE:
      return "OK";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
      return "CHIP_NOT_FOUND";
    case RADIOLIB_ERR_PACKET_TOO_LONG:
      return "PACKET_TOO_LONG";
    case RADIOLIB_ERR_TX_TIMEOUT:
      return "TX_TIMEOUT";
    case RADIOLIB_ERR_RX_TIMEOUT:
      return "RX_TIMEOUT";
    case RADIOLIB_ERR_CRC_MISMATCH:
      return "CRC_MISMATCH";
    default:
      return "UNKNOWN";
  }
}

/*Nó nhận vào tên hành động bị lỗi (action) và mã số lỗi (state), 
sau đó kết hợp với hàm stateToString ở trên để in ra một dòng thông báo lỗi hoàn chỉnh, trực quan.
 Chữ F() bọc quanh các chuỗi văn bản nhằm mục đích ép các chuỗi này lưu vào bộ nhớ Flash thay vì bộ nhớ RAM,
  giúp tiết kiệm dung lượng RAM cho ESP32.*/
void logState(const char* action, int16_t state) {//Hàm in thông báo lỗi ra màn hình máy tính (Serial Monitor)
  Serial.print(action);
  Serial.print(F(" failed, code "));
  Serial.print(state);
  Serial.print(F(" ("));
  Serial.print(stateToString(state));
  Serial.println(')');
}

}  // namespace

bool loraBegin() {
  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  int16_t state = radio.begin(
      LORA_FREQUENCY_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
      LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN);
  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.begin", state);
    return false;
  }

  state = radio.setCRC(LORA_CRC_ON);
  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.setCRC", state);
    return false;
  }

  Serial.println(F("SX1278 ready"));
  Serial.print(F("  Frequency: "));
  Serial.print(LORA_FREQUENCY_HZ);
  Serial.println(F(" Hz"));
  Serial.print(F("  SF: "));
  Serial.println(LORA_SF);
  Serial.print(F("  BW: "));
  Serial.print(LORA_BW_KHZ, 0);
  Serial.print(F(" kHz (index "));
  Serial.print(LORA_BANDWIDTH_INDEX);
  Serial.println(')');
  Serial.print(F("  CR: 4/"));
  Serial.print(LORA_CR);
  Serial.print(F(" (index "));
  Serial.print(LORA_CODING_RATE_INDEX);
  Serial.println(')');
  Serial.print(F("  TX power: "));
  Serial.print(LORA_TX_POWER_DBM);
  Serial.println(F(" dBm"));
  Serial.print(F("  Preamble: "));
  Serial.println(LORA_PREAMBLE_LEN);
  Serial.print(F("  Sync word: 0x"));
  Serial.println(LORA_SYNC_WORD, HEX);
  Serial.print(F("  CRC: "));
  Serial.println(LORA_CRC_ON ? F("on") : F("off"));
  return true;
}

bool loraSend(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > LORA_MAX_PACKET_LEN) {
    Serial.println(F("Invalid TX payload"));
    return false;
  }

  int16_t state = radio.transmit(const_cast<uint8_t*>(data), len);
  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.transmit", state);
    return false;
  }

  Serial.print(F("TX OK ("));
  Serial.print(len);
  Serial.println(F(" bytes)"));
  return true;
}

int loraReceive(uint8_t* data, size_t maxLen, int16_t* rssiOut, float* snrOut) {
  if (data == nullptr || maxLen == 0) {
    return -1;
  }

  int16_t state = radio.receive(data, maxLen, LORA_RX_TIMEOUT_MS);
  if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    return 0;
  }
  if (state < 0) {
    logState("radio.receive", state);
    return -1;
  }

  if (rssiOut != nullptr) {
    *rssiOut = radio.getRSSI();
  }
  if (snrOut != nullptr) {
    *snrOut = radio.getSNR();
  }

  return state;
}

void loraPrintChipStatus() {
  Serial.println(F("  Chip: SX1278"));
}
