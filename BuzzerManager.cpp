#include "BuzzerManager.h"
#include "driver/ledc.h"

// PWM Configuration
#define LEDC_BUZZER_CHANNEL    LEDC_CHANNEL_0
#define LEDC_BUZZER_TIMER      LEDC_TIMER_0
#define LEDC_BUZZER_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_BUZZER_RESOLUTION LEDC_TIMER_8_BIT
#define BUZZER_FREQUENCY       2000  // 2kHz

BuzzerManager::BuzzerManager() 
    : _initialized(false), _alarming(false), _alarmStartTime(0), _alarmDuration(0) {
}

bool BuzzerManager::begin() {
    if (_initialized) return true;
    
    // LEDC timer configuration
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_BUZZER_MODE,
        .duty_resolution = LEDC_BUZZER_RESOLUTION,
        .timer_num       = LEDC_BUZZER_TIMER,
        .freq_hz         = BUZZER_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);
    
    // LEDC channel configuration
    ledc_channel_config_t channel_conf = {
        .gpio_num   = PIN_BUZZER,
        .speed_mode = LEDC_BUZZER_MODE,
        .channel    = LEDC_BUZZER_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_BUZZER_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ledc_channel_config(&channel_conf);
    
    _initialized = true;
    Serial.println("[BUZZER] Initialized PWM on GPIO21 at 4000 Hz (channel: 0, timer: 0)");
    return true;
}

void BuzzerManager::alarmStart(uint32_t durationMs) {
    if (!_initialized) return;
    
    _alarming = true;
    _alarmStartTime = millis();
    _alarmDuration = durationMs;
    _setBuzzerState(true);
    Serial.printf("[BUZZER] Alarm started (duration: %lu ms)\n", (unsigned long)durationMs);
}

void BuzzerManager::alarmStop() {
    if (!_initialized) return;
    
    if (_alarming) { // Only log if it was actually alarming
        _alarming = false;
        _setBuzzerState(false);
        // Serial.println("[BUZZER] Alarm stopped"); // Commented to reduce serial spam
    }
}

void BuzzerManager::update() {
    if (!_initialized || !_alarming) return;
    
    // Check timeout
    if (_alarmDuration > 0 && (millis() - _alarmStartTime >= _alarmDuration)) {
        alarmStop();
        return;
    }
    
    // Pulse pattern: 500ms ON, 500ms OFF
    uint32_t elapsed = millis() - _alarmStartTime;
    uint32_t cycle = elapsed % 1000;
    _setBuzzerState(cycle < 500);
}

void BuzzerManager::_setBuzzerState(bool on) {
    if (!_initialized) return;
    
    uint32_t duty = on ? 127 : 0; // 50% duty cycle when on
    ledc_set_duty(LEDC_BUZZER_MODE, LEDC_BUZZER_CHANNEL, duty);
    ledc_update_duty(LEDC_BUZZER_MODE, LEDC_BUZZER_CHANNEL);
}

