#ifndef C16QS_4G_MANAGER_H
#define C16QS_4G_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <time.h>  // struct tm için
#include "ConfigManager.h"

class C16QS4GManager {
public:
    C16QS4GManager();
    bool begin();
    bool isReady();
    bool connectNetwork();
    bool connectMQTT(const char* broker, int port, const char* clientId, const char* username, const char* password);
    bool publishMQTT(const char* topic, const char* payload);
    bool subscribeMQTT(const char* topic);
    bool isMQTTConnected();
    void loop(); // MQTT mesajlarını işle
    void disconnectMQTT();
    void disconnectNetwork();
    void powerOff(); // Modülü tamamen kapat (güç tasarrufu)
    void hardReset(); // Modem'e hard reset at (RESETKEY pin ile)
    int getSignalStrength(); // RSSI in dBm
    int getCSQ(); // CSQ ham değeri (0-31)
    String getCSQString(); // CSQ string formatı: "CSQ,BER" (örn: "24,0")
    String getIPAddress();
    String getIMEI(); // IMEI numarasını döndür
    bool getGSMTime(struct tm* timeinfo, int* timezoneOffset = nullptr); // GSM time'ı al ve timeinfo'ya yaz, timezone offset'i de döndür
    
    // GPS fonksiyonları (NMEA stream üzerinden)
    bool startGPS();
    bool stopGPS();
    void updateGPS();  // Loop'ta çağrılmalı - NMEA stream okur
    bool getGPSLocation(float* lat, float* lon, float* alt = nullptr, int* sats = nullptr);
    bool isGPSFixValid();
    int getGPSSatellites();
    float getGPSHDOP();
    float getGPSSpeed();       // Hız (km/h)
    float getGPSCourse();      // Yön (derece, 0-360)
    String getGPSTime();       // UTC Saat (HH:MM:SS)
    String getGPSDate();       // Tarih (DD/MM/YY)
    
    // Callback için (wrapper ile Arduino PubSubClient formatına uyumlu)
    void setMqttCallback(void (*callback)(char* topic, byte* payload, unsigned int len));
    
private:
    HardwareSerial* _serial;
    bool _initialized;
    bool _networkConnected;
    bool _mqttConnected;
    bool _gpsStarted;
    bool _gpsFixValid;
    float _gpsLat;
    float _gpsLon;
    float _gpsAlt;
    int _gpsSats;
    float _gpsHdop;
    float _gpsSpeed;       // Hız (km/h)
    float _gpsCourse;      // Yön (derece)
    String _gpsTime;       // UTC Saat (HHMMSS)
    String _gpsDate;       // Tarih (DDMMYY)
    unsigned long _gpsLastUpdate;
    String _nmeaBuffer;
    int _mqttSessionId;
    String _ipAddress;
    void (*_mqttCallback)(const char* topic, const char* payload, int len); // Internal callback
    
    // Static callback pointer for wrapper
    static void (*_arduinoCallback)(char* topic, byte* payload, unsigned int len);
    static void _mqtt4GCallbackWrapper(const char* topic, const char* payload, int len);
    
    bool _sendATCommand(const char* cmd, const char* expected, uint32_t timeoutMs = 2000);
    String _sendATCommandResponse(const char* cmd, uint32_t timeoutMs = 2000);
    bool _waitForResponse(const char* expected, uint32_t timeoutMs = 2000);
    void _processUrc(); // Unsolicited Result Code işleme
    
    // GPS NMEA parsing
    void _readNMEAStream();
    bool _parseGNGGA(const String& line);
    bool _parseGNRMC(const String& line);
};

#endif
