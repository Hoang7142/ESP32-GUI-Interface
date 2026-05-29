#include "lora_radio.h"

#include <RadioLib.h>
#include <SPI.h>

#include "lora_config.h"

namespace {

SPIClass loraSpi(VSPI);
SX1278 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RST, RADIOLIB_NC, loraSpi);

volatile bool rxFlag = false; // stand for received data is ready to be read

constexpr uint16_t kIrqRxDone = RADIOLIB_SX127X_CLEAR_IRQ_FLAG_RX_DONE;

#if defined(ESP8266) || defined(ESP32)
void IRAM_ATTR onPacketReceived() {
  // DIO0 is mapped to RxDone only while in RX; ignore any other IRQ source.
  if (radio.getIRQFlags() & kIrqRxDone) {
    rxFlag = true;
  }
}
#else
void onPacketReceived() {
  if (radio.getIRQFlags() & kIrqRxDone) {
    rxFlag = true;
  }
}
#endif

void attachRxInterrupt() { radio.setPacketReceivedAction(onPacketReceived); }

void detachRxInterrupt() { radio.clearPacketReceivedAction(); }

const char* stateToString(int16_t state) {
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

void logState(const char* action, int16_t state) {
  Serial.print(action);
  Serial.print(F(" failed, code "));
  Serial.print(state);
  Serial.print(F(" ("));
  Serial.print(stateToString(state));
  Serial.println(')');
}

bool startRx() {
  int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.startReceive", state);
    return false;
  }
  return true;
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

  if (!startRx()) {
    return false;
  }
  attachRxInterrupt();

  Serial.println(F("SX1278 ready (DIO0 = RxDone in RX mode)"));
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
  Serial.print(F("  DIO0 (RxDone in RX): GPIO "));
  Serial.println(LORA_DIO0);
  return true;
}

bool loraSend(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > LORA_MAX_PACKET_LEN) {
    Serial.println(F("Invalid TX payload"));
    return false;
  }

  // During TX the chip maps DIO0 to TxDone; detach ISR so TxDone cannot set rxFlag.
  detachRxInterrupt();
  rxFlag = false;

  int16_t state = radio.transmit(const_cast<uint8_t*>(data), len);

  rxFlag = false;

  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.transmit", state);
    if (!startRx()) {
      return false;
    }
    attachRxInterrupt();
    return false;
  }

  Serial.print(F("TX OK ("));
  Serial.print(len);
  Serial.println(F(" bytes)"));

  // startReceive() remaps DIO0 to RxDone before re-enabling the ISR.
  if (!startRx()) {
    attachRxInterrupt();
    return false;
  }
  attachRxInterrupt();
  return true;
}

// check if there is any received data waiting to be read
bool loraRxPending() { return rxFlag; }

int loraReceive(uint8_t* data, size_t maxLen, int16_t* rssiOut, float* snrOut) {
  // check if there is any received data waiting to be read
  if (!rxFlag) {
    return 0;
  }

  if (data == nullptr || maxLen == 0) {
    return -1;
  }

  rxFlag = false;

  size_t length = radio.getPacketLength(true);
  if (length > maxLen) {
    length = maxLen;
  }

  int16_t state = radio.readData(data, length);
  if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    logState("radio.readData", state);
    startRx();
    return -1;
  }
  if (state != RADIOLIB_ERR_NONE) {
    logState("radio.readData", state);
    startRx();
    return -1;
  }

  if (rssiOut != nullptr) {
    *rssiOut = radio.getRSSI();
  }
  if (snrOut != nullptr) {
    *snrOut = radio.getSNR();
  }

  if (!startRx()) {
    return -1;
  }

  return static_cast<int>(length);
}

void loraPrintChipStatus() {
  Serial.println(F("  Chip: SX1278"));
  Serial.print(F("  RX: DIO0 = RxDone, GPIO "));
  Serial.println(LORA_DIO0);
}
