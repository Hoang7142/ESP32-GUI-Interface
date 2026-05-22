#pragma once

// ESP32 DevKit (DOIT / ESP32-WROOM-32) VSPI + typical SX1278 wiring.
// Do not use GPIO 6–11 for external SPI (connected to internal flash).
// Change NSS/RST/DIO0 if your module uses different control pins.
//
// | SX1278 | ESP32 DevKit (VSPI) |
// |--------|---------------------|
// | VCC    | 3V3                 |
// | GND    | GND                 |
// | NSS/CS | GPIO 5              |
// | RST    | GPIO 14             |
// | DIO0   | GPIO 26             |
// | SCK    | GPIO 18             |
// | MISO   | GPIO 19             |
// | MOSI   | GPIO 23             |

constexpr uint8_t LORA_NSS = 5;
constexpr uint8_t LORA_RST = 14;
constexpr uint8_t LORA_DIO0 = 26;
constexpr uint8_t LORA_SCK = 18;
constexpr uint8_t LORA_MISO = 19;
constexpr uint8_t LORA_MOSI = 23;

// LoRa modem settings — must match LoRa_Config_t on all nodes (STM32 + ESP32).
//
// | LoRa_Config_t field   | Value | RadioLib / register equivalent      |
// |-----------------------|-------|-------------------------------------|
// | frequency_hz          | 433M  | LORA_FREQUENCY_MHZ = 433.0        |
// | spreading_factor      | 7     | LORA_SF = 7                         |
// | bandwidth (index 7)   | 7     | LORA_BW_KHZ = 125.0 (125 kHz)       |
// | coding_rate (index 1) | 1     | LORA_CR = 5 (4/5, use denominator)  |
// | tx_power              | 17    | LORA_TX_POWER_DBM = 17              |
// | preamble_len          | 8     | LORA_PREAMBLE_LEN = 8               |
// | sync_word             | 0xF1  | LORA_SYNC_WORD = 0xF1               |
// | crc_on                | 1     | LORA_CRC_ON = true                  |
constexpr uint32_t LORA_FREQUENCY_HZ = 433000000UL;
constexpr float LORA_FREQUENCY_MHZ =
    static_cast<float>(LORA_FREQUENCY_HZ) / 1000000.0f;

constexpr uint8_t LORA_SF = 7;
constexpr uint8_t LORA_BANDWIDTH_INDEX = 7;   // SX1278 reg: 7 = 125 kHz
constexpr float LORA_BW_KHZ = 125.0f;
constexpr uint8_t LORA_CODING_RATE_INDEX = 1; // SX1278 reg: 1 = 4/5
constexpr uint8_t LORA_CR = 5;                // RadioLib: 5 = 4/5
constexpr int8_t LORA_TX_POWER_DBM = 17;
constexpr uint16_t LORA_PREAMBLE_LEN = 8;
constexpr uint8_t LORA_SYNC_WORD = 0xF1;
constexpr bool LORA_CRC_ON = true;

constexpr uint16_t LORA_MAX_PACKET_LEN = 255;
constexpr uint32_t LORA_TX_INTERVAL_MS = 1000;
constexpr uint32_t LORA_RX_TIMEOUT_MS = 2000;
