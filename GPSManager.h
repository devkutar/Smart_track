#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>

class GPSManager {
public:
    GPSManager();
    bool begin(); // UART1'i kullanacak (C16QS4GManager ile paylaşılacak)
    void update(); // Call in loop to parse GPS data
    bool isFixValid();
    float getLatitude();
    float getLongitude();
    float getAltitude();
    uint8_t getSatelliteCount();
    float getHDOP(); // Horizontal Dilution of Precision
    uint32_t getLastUpdateTime(); // Milliseconds since last valid fix
    
private:
    HardwareSerial* _serial;
    bool _initialized;
    bool _gnssStarted;
    bool _fixValid;
    float _latitude;
    float _longitude;
    float _altitude;
    uint8_t _satelliteCount;
    float _hdop;
    uint32_t _lastUpdateTime;
    
    bool _initGNSS(); // Initialize GNSS via AT commands
    String _sendATCommandWithResponse(const char* cmd, int timeout = 2000);
    void _parseNMEA(const String& line);
    bool _parseGNGGA(const String& line);
    bool _parseGNRMC(const String& line);
    String _readNMEALine(); // Read NMEA line from UART
};

#endif
