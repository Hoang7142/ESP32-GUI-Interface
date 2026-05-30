#pragma once

/**
 * @file lora_radio.h
 * @brief ESP32 SX1278 low-level radio driver (send/receive raw bytes).
 */

#include <Arduino.h>

/**
 * @brief Initialize SX1278 over SPI, apply modem settings from lora_config.h,
 *        and enter continuous RX mode with DIO0 interrupt.
 * @return true if the radio started successfully.
 */
bool loraBegin();

/**
 * @brief Transmit a raw byte buffer and return to RX mode.
 * @param data  Bytes to send (protocol layer output).
 * @param len   Length (1..LORA_MAX_PACKET_LEN).
 * @return true if transmission completed without error.
 */
bool loraSend(const uint8_t* data, size_t len);

/**
 * @brief Check whether a packet has been received (DIO0 / rxFlag set).
 * @return true if loraReceive() should be called.
 */
bool loraRxPending();

/**
 * @brief Read one received packet from the SX1278 FIFO.
 * @param data      Output buffer for raw bytes.
 * @param maxLen    Size of data buffer.
 * @param rssiOut   Optional RSSI output [dBm] (may be NULL).
 * @param snrOut    Optional SNR output [dB] (may be NULL).
 * @return Number of bytes read, 0 if nothing pending, negative on CRC/error.
 */
int loraReceive(uint8_t* data, size_t maxLen, int16_t* rssiOut, float* snrOut);

/** @brief Print pin and chip info to Serial (debug helper). */
void loraPrintChipStatus();
