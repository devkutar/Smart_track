#include "PowerManager.h"
#include "hardware.h"

PowerManager::PowerManager() {
    memset(&_status, 0, sizeof(PowerStatus));
}

bool PowerManager::begin() {
    // ADC ayarları
    pinMode(PIN_VBAT_ADC, INPUT);
    pinMode(PIN_CHG_STATUS, INPUT);
    pinMode(PIN_MAINS_SENSE, INPUT);
    
    update();
    return true;
}

void PowerManager::update() {
    _readMainsStatus();
    _readChargingStatus();
    _readBatteryVoltage();
    _status.battPct = _calculateBatteryPercent(_status.vbat);
}

void PowerManager::_readMainsStatus() {
    // AC/USB sense pinini oku
    _status.mainsPresent = digitalRead(PIN_MAINS_SENSE) == HIGH;
    _status.rawPwrAdc = analogRead(PIN_MAINS_SENSE); // Debug için
}

void PowerManager::_readChargingStatus() {
    // BQ24072 CHG pinini oku (LOW = charging)
    _status.charging = digitalRead(PIN_CHG_STATUS) == LOW && _status.mainsPresent;
}

void PowerManager::_readBatteryVoltage() {
    // ADC okuma (örnek: voltage divider ile)
    // Gerçek donanıma göre kalibre edilmeli
    int raw = analogRead(PIN_VBAT_ADC);
    // Örnek hesaplama (3.0V-4.2V aralığı, 12-bit ADC, voltage divider 2:1)
    float v = (raw / 4095.0) * 3.3 * 2.0; // ADC referans 3.3V, divider 2:1
    _status.vbat = v;
}

uint8_t PowerManager::_calculateBatteryPercent(float voltage) {
    // Lityum batarya için basit lineer hesaplama
    // 3.0V = 0%, 4.2V = 100%
    if (voltage <= 3.0) return 0;
    if (voltage >= 4.2) return 100;
    return (uint8_t)((voltage - 3.0) / 1.2 * 100.0);
}

