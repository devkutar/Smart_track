#ifndef SMART_TRACK_NETWORK_MANAGER_H
#define SMART_TRACK_NETWORK_MANAGER_H

#include <time.h>
#include "ConfigManager.h"

// Forward declaration
class C16QS4GManager;

class SmartTrackNetworkManager {
public:
    SmartTrackNetworkManager();
    bool begin();
    bool connect(Cfg& cfg); // 4G only
    bool syncTimeGSM(); // GSM time synchronization
    uint32_t getEpoch();
    bool isConnected();
    int getRSSI(); // 4G signal strength
    void disconnect();
    void powerOff(); // Power off 4G module
    
    String getIPAddress();
    C16QS4GManager* get4GModem() { return _modem4G; }
    
    // GPS fonksiyonları (4G modem üzerinden NMEA stream)
    bool startGPS();
    void updateGPS();  // Loop'ta çağrılmalı
    bool getGPSLocation(float* lat, float* lon);
    bool isGPSFixValid();
    int getGPSSatellites();
    float getGPSHDOP();
    float getGPSSpeed();       // Hız (km/h)
    float getGPSCourse();      // Yön (derece)
    String getGPSTime();       // UTC Saat
    String getGPSDate();       // Tarih

private:
    bool _timeSynced;
    C16QS4GManager* _modem4G;
    bool connect4G(); // Internal 4G connection helper
};

#endif

