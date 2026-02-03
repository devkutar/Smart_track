#include "T117.h"

T117::T117(uint8_t addr) : _addr(addr) {}

uint8_t T117::crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x01) crc = (crc >> 1) ^ 0x8C; // reflected poly 0x31
      else crc >>= 1;
    }
  }
  return crc;
}

bool T117::probe() {
  Wire.beginTransmission(_addr);
  return (Wire.endTransmission() == 0);
}

bool T117::begin() {
  // T117 okumak için SensorEN HIGH olmalı
  HW_sensorPower(true);
  delay(5);
  return probe();
}

void T117::end() {
  HW_sensorPower(false);
}

void T117::startSingleConvert() {
  Wire.beginTransmission(_addr);
  Wire.write(REG_TEMP_CMD);
  Wire.write(SINGLE_CONVERT);
  Wire.endTransmission();
}

bool T117::readTemperatureC(float &tempC) {
  uint8_t data[3];

  Wire.beginTransmission(_addr);
  Wire.write(REG_TEMP_LSB);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(_addr, (uint8_t)3) != 3) return false;

  data[0] = Wire.read();
  data[1] = Wire.read();
  data[2] = Wire.read();

  uint8_t calc = crc8(data, 2);
  if (data[2] != calc) return false;

  int16_t raw = (int16_t)((data[1] << 8) | data[0]);
  tempC = (float)raw / 256.0f + 25.0f;
  return true;
}

