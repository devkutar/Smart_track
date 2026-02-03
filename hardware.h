#pragma once
#include <Arduino.h>
#include <Wire.h>

// ===== Donanım Pinleri =====
static constexpr int PIN_I2C_SDA   = 8;
static constexpr int PIN_I2C_SCL   = 9;

static constexpr int PIN_LIS_INT1  = 3;   // LIS3DHTR INT1 -> GPIO3
static constexpr int PIN_SENSOREN  = 2;   // SensorEN: T117 okumak için HIGH olmalı

static constexpr int PIN_HW_TIMER_PULSE = 7;

// ===== Buzzer =====
static constexpr int PIN_BUZZER = 21;  // GPIO21

// ===== Power Management =====
static constexpr int PIN_VBAT_ADC = 1;   // GPIO1 - Battery voltage ADC
static constexpr int PIN_CHG_STATUS = 5; // GPIO5 - BQ24072 CHG pin
static constexpr int PIN_MAINS_SENSE = 39; // GPIO39 - AC/USB sense

// ===== C16QS 4G Modem =====
static constexpr int PIN_MODEM_NET_STATUS = 19;  // GPIO19 - NET-STATUS (network status)
static constexpr int PIN_MODEM_STATUS = 20;      // GPIO20 - STATUS (modem status)
static constexpr int PIN_MODEM_UART_RX = 17;     // GPIO17 - UART1_RX (from modem TX) - ÇAPRAZ BAĞLANTI
static constexpr int PIN_MODEM_UART_TX = 18;     // GPIO18 - UART1_TX (to modem RX) - ÇAPRAZ BAĞLANTI
static constexpr int PIN_MODEM_UART_CTS = 15;    // GPIO15 - UART1_CTS (Clear To Send)
static constexpr int PIN_MODEM_UART_RTS = 16;    // GPIO16 - UART1_RTS (Request To Send)
static constexpr int PIN_MODEM_POWERKEY = 13;    // GPIO13 - POWERKEY (modem power on/off)
static constexpr int PIN_MODEM_RESETKEY = 38;    // GPIO38 - RESETKEY (modem reset)
static constexpr uint32_t MODEM_UART_BAUD = 115200;

// ===== GPS Module ===== 
// GPS modülü UART2 üzerinden bağlanacak (pinler belirtilecek)
static constexpr int PIN_GPS_UART_RX = 40;  // GPIO40 - GPS UART RX
static constexpr int PIN_GPS_UART_TX = 41;  // GPIO41 - GPS UART TX
static constexpr uint32_t GPS_UART_BAUD = 9600;

// ===== I2C =====
static constexpr uint32_t I2C_FREQ = 400000;

// ===== LCD (ST7789) Pinleri =====
static constexpr int TFT_CS   = 10;
static constexpr int TFT_DC   = 6;
static constexpr int TFT_RST  = 14;
static constexpr int TFT_MOSI = 11;
static constexpr int TFT_SCLK = 12;
static constexpr int TFT_BLK  = 4;

static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 296;

// ===== BLE Eddystone Target Prefix =====
static constexpr const char* BLE_TARGET_PREFIX = "8C696B";

// Yardımcı başlatma
inline void HW_beginI2C() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_FREQ);
}

inline void HW_sensorPower(bool en) {
  pinMode(PIN_SENSOREN, OUTPUT);
  digitalWrite(PIN_SENSOREN, en ? HIGH : LOW);
}

inline void HW_sendTimerPulse() {
  pinMode(PIN_HW_TIMER_PULSE, OUTPUT);
  digitalWrite(PIN_HW_TIMER_PULSE, HIGH);
  delay(100); // 100ms pulse süresi
  digitalWrite(PIN_HW_TIMER_PULSE, LOW);
  // Uyku sırasında sızıntı akımı olmaması için pini girişe çekin
  pinMode(PIN_HW_TIMER_PULSE, INPUT);
}
