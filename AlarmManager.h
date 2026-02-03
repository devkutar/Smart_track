#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <Arduino.h>
#include "ConfigManager.h"

class AlarmManager {
public:
    AlarmManager();
    uint8_t checkTemperature(float temp, float tempHigh, float tempLow);
    const char* getAlarmReason(uint8_t alarmState);
    bool isAlarmActive() { return _currentAlarmState != 0; }
    uint8_t getAlarmState() { return _currentAlarmState; }
    
private:
    uint8_t _currentAlarmState;
    // 0: Normal, 1: High, 2: Low
};

#endif

