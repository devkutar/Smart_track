#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "ConfigManager.h"

class PowerManager {
public:
    PowerManager();
    bool begin();
    void update();
    PowerStatus getStatus() { return _status; }
    
private:
    PowerStatus _status;
    void _readBatteryVoltage();
    void _readChargingStatus();
    void _readMainsStatus();
    uint8_t _calculateBatteryPercent(float voltage);
};

#endif

