#ifndef BUZZER_MANAGER_H
#define BUZZER_MANAGER_H

#include <Arduino.h>
#include "hardware.h"

class BuzzerManager {
public:
    BuzzerManager();
    bool begin();
    void alarmStart(uint32_t durationMs = 0); // 0 = unlimited
    void alarmStop();
    void update(); // Non-blocking update (call in loop)
    bool isAlarming() { return _alarming; }
    
private:
    bool _initialized;
    bool _alarming;
    uint32_t _alarmStartTime;
    uint32_t _alarmDuration;
    
    void _setBuzzerState(bool on);
};

#endif

