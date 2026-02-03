#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "hardware.h"

class T117 {
public:
  explicit T117(uint8_t addr = 0x41);

  bool begin();                  // SensorEN HIGH + I2C probe
  void end();                    // SensorEN LOW

  void startSingleConvert();
  bool readTemperatureC(float &tempC);

private:
  uint8_t _addr;

  // register
  static constexpr uint8_t REG_TEMP_LSB  = 0x00;
  static constexpr uint8_t REG_TEMP_MSB  = 0x01;
  static constexpr uint8_t REG_CRC_TEMP  = 0x02;
  static constexpr uint8_t REG_TEMP_CMD  = 0x04;

  static constexpr uint8_t SINGLE_CONVERT = 0xC0;

  uint8_t crc8(const uint8_t *data, uint8_t len);
  bool probe();
};

