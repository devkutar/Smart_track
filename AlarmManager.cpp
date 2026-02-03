#include "AlarmManager.h"

AlarmManager::AlarmManager() : _currentAlarmState(0) {
}

uint8_t AlarmManager::checkTemperature(float temp, float tempHigh, float tempLow) {
    uint8_t newState = 0;
    
    if (temp > tempHigh) {
        newState = 1; // High alarm
    } else if (temp < tempLow) {
        newState = 2; // Low alarm
    }
    
    _currentAlarmState = newState;
    return newState;
}

const char* AlarmManager::getAlarmReason(uint8_t alarmState) {
    switch(alarmState) {
        case 1: return "TEMP_HIGH";
        case 2: return "TEMP_LOW";
        default: return "NORMAL";
    }
}

