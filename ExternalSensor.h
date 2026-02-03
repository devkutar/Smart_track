#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "hardware.h"

// Sensör tipleri
enum SensorType {
    SENSOR_NONE = 0,
    SENSOR_T117 = 1,
    SENSOR_AHT20 = 2
};

class ExternalSensor {
public:
    ExternalSensor();
    
    bool begin();  // Sensör algıla ve başlat
    void end();    // Sensörü kapat
    
    bool readTemperature(float &tempC);
    bool readHumidity(float &humidity);  // Sadece AHT20 için
    
    SensorType getType() { return _sensorType; }
    const char* getTypeName();
    bool isAvailable() { return _sensorType != SENSOR_NONE; }
    
private:
    SensorType _sensorType;
    
    // I2C adresleri
    static constexpr uint8_t T117_ADDR = 0x41;
    static constexpr uint8_t AHT20_ADDR = 0x38;
    
    // T117 registers
    static constexpr uint8_t T117_REG_TEMP_LSB = 0x00;
    static constexpr uint8_t T117_REG_TEMP_MSB = 0x01;
    static constexpr uint8_t T117_REG_CRC_TEMP = 0x02;
    static constexpr uint8_t T117_REG_TEMP_CMD = 0x04;
    static constexpr uint8_t T117_SINGLE_CONVERT = 0xC0;
    
    // AHT20 commands
    static constexpr uint8_t AHT20_CMD_INIT = 0xBE;
    static constexpr uint8_t AHT20_CMD_TRIGGER = 0xAC;
    static constexpr uint8_t AHT20_CMD_SOFTRESET = 0xBA;
    
    // Cached values (AHT20 için)
    float _lastTemp;
    float _lastHumidity;
    
    // Helper functions
    bool probeI2C(uint8_t addr);
    uint8_t crc8_t117(const uint8_t *data, uint8_t len);
    uint8_t crc8_aht20(const uint8_t *data, uint8_t len);
    
    // T117 functions
    void t117_startConvert();
    bool t117_readTemp(float &tempC);
    
    // AHT20 functions
    bool aht20_init();
    bool aht20_readTempHumidity(float &tempC, float &humidity);
};
