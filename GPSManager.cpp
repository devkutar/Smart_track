#include "GPSManager.h"
#include "hardware.h"
#include <HardwareSerial.h>

GPSManager::GPSManager() 
    : _serial(nullptr), _initialized(false), _gnssStarted(false), _fixValid(false),
      _latitude(0.0), _longitude(0.0), _altitude(0.0),
      _satelliteCount(0), _hdop(0.0), _lastUpdateTime(0) {
}

bool GPSManager::begin() {
    Serial.println("[GPS] Initializing GNSS via Cavli C16QS...");
    
    // UART1'i kullan (C16QS4GManager ile aynı - zaten başlatılmış olmalı)
    // Kullanıcının çalışan kodunda HardwareSerial(1) kullanılıyor
    _serial = &Serial1; // UART1 (pin 17 RX, pin 18 TX)
    
    // UART1 zaten C16QS4GManager tarafından başlatılmış olmalı
    // Aynı UART'tan hem AT komutları hem de NMEA satırları geliyor
    
    Serial.println("[GPS] Initializing GNSS...");
    if (!_initGNSS()) {
        Serial.println("[GPS] Failed to initialize GNSS");
        return false;
    }
    
    _initialized = true;
    Serial.println("[GPS] GNSS initialized successfully");
    return true;
}

String GPSManager::_sendATCommandWithResponse(const char* cmd, int timeout) {
    if (!_serial) {
        Serial.println("[GPS] ERROR: Serial not initialized!");
        return "";
    }
    
    Serial.print(">> ");
    Serial.println(cmd);
    
    // Buffer'ı temizle - daha agresif temizleme
    unsigned long clearStart = millis();
    while (_serial->available() && (millis() - clearStart < 500)) {
        _serial->read();
    }
    
    // Komutu gönder
    _serial->print(cmd);
    _serial->print("\r\n");
    _serial->flush(); // Buffer'ı gönder
    delay(200); // Biraz daha uzun bekle
    
    // Response'u bekle
    long startTime = millis();
    String response = "";
    bool gotResponse = false;
    
    while (millis() - startTime < timeout) {
        while (_serial->available()) {
            char c = _serial->read();
            response += c;
            
            // Eğer satır sonu geldiyse kontrol et
            if (c == '\n') {
                if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                    gotResponse = true;
                    break;
                }
            }
        }
        
        if (gotResponse) break;
        delay(10);
    }
    
    if (response.length() > 0) {
        Serial.print("<< ");
        Serial.println(response);
    } else {
        Serial.println("<< [TIMEOUT]");
        Serial.printf("[GPS] No response after %d ms\n", timeout);
    }
    
    return response;
}

bool GPSManager::_initGNSS() {
    Serial.println("[GPS] Resetting GPSPORT...");
    _sendATCommandWithResponse("AT+GPSPORT=0", 2000);
    delay(500);
    
    Serial.println("[GPS] Checking GNSS status...");
    String response = _sendATCommandWithResponse("AT+CGPS?", 2000);
    delay(500);
    
    if (response.indexOf("+CGPS: 1") >= 0 || response.indexOf("+CGPS:1") >= 0) {
        Serial.println("[GPS] GNSS already on, turning off...");
        _sendATCommandWithResponse("AT+CGPS=0", 3000);
        delay(2000);
    }
    
    Serial.println("[GPS] Starting GNSS (AT+CGPS=1)...");
    response = _sendATCommandWithResponse("AT+CGPS=1", 3000);
    
    if (response.indexOf("OK") >= 0) {
        Serial.println("[GPS] GNSS started successfully!");
    } else {
        Serial.println("[GPS] ERROR: Failed to start GNSS!");
        return false;
    }
    
    delay(2000);
    
    Serial.println("[GPS] Routing NMEA to AT port (AT+GPSPORT=1)...");
    response = _sendATCommandWithResponse("AT+GPSPORT=1", 2000);
    
    if (response.indexOf("OK") >= 0) {
        Serial.println("[GPS] NMEA stream routed to AT port!");
        _gnssStarted = true;
        return true;
    } else {
        Serial.println("[GPS] WARNING: GPSPORT routing error (continuing)");
        _gnssStarted = true;
        return true; // Continue anyway
    }
}

void GPSManager::update() {
    if (!_initialized || !_serial) return;
    
    // Read NMEA lines from UART1
    String line = _readNMEALine();
    if (line.length() > 0) {
        // NMEA satırını göster
        Serial.printf("[GPS-NMEA] %s\n", line.c_str());
        _parseNMEA(line);
    }
    
    // Debug: Mevcut durumu yazdır (30 saniyede bir)
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 30000) {
        lastDebugTime = millis();
        Serial.println("\n========== GPS STATUS ==========");
        Serial.printf("[GPS] Fix Valid: %s\n", _fixValid ? "YES" : "NO");
        Serial.printf("[GPS] Latitude:  %.6f\n", _latitude);
        Serial.printf("[GPS] Longitude: %.6f\n", _longitude);
        Serial.printf("[GPS] Altitude:  %.1f m\n", _altitude);
        Serial.printf("[GPS] Satellites: %d\n", _satelliteCount);
        Serial.printf("[GPS] HDOP: %.2f\n", _hdop);
        Serial.printf("[GPS] Last Update: %lu ms ago\n", millis() - _lastUpdateTime);
        Serial.println("================================\n");
    }
}

String GPSManager::_readNMEALine() {
    if (!_serial) return "";
    
    static String buffer = "";
    
    while (_serial->available()) {
        char c = _serial->read();
        buffer += c;
        
        if (c == '\n') {
            String line = buffer;
            line.trim();
            buffer = "";
            
            if (line.length() > 0 && (line.startsWith("$GNGGA") || line.startsWith("$GNRMC") || 
                                      line.startsWith("+GNGGA") || line.startsWith("+GNRMC"))) {
                return line;
            }
        }
    }
    
    return "";
}

void GPSManager::_parseNMEA(const String& line) {
    if (line.startsWith("$GNGGA") || line.startsWith("+GNGGA")) {
        _parseGNGGA(line);
    } else if (line.startsWith("$GNRMC") || line.startsWith("+GNRMC")) {
        _parseGNRMC(line);
    }
}

bool GPSManager::_parseGNGGA(const String& line) {
    // Format: $GNGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,sep,M,diffAge,diffStation*checksum
    // Or: +GNGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,sep,M,diffAge,diffStation*checksum
    
    int idx = line.indexOf("GNGGA");
    if (idx < 0) return false;
    
    // Skip "GNGGA,"
    idx += 6;
    int starIdx = line.indexOf('*', idx);
    if (starIdx < 0) return false;
    
    String values = line.substring(idx, starIdx);
    
    String fields[15];
    int fieldCount = 0;
    int lastIdx = -1;
    
    for (int i = 0; i <= values.length() && fieldCount < 15; i++) {
        if (i == values.length() || values[i] == ',') {
            fields[fieldCount++] = values.substring(lastIdx + 1, i);
            lastIdx = i;
        }
    }
    
    if (fieldCount < 10) {
        _fixValid = false;
        return false;
    }
    
    // Time (fields[0])
    // Latitude (fields[1]) - format: DDMM.MMMM
    // Lat direction (fields[2]) - N/S
    // Longitude (fields[3]) - format: DDDMM.MMMM
    // Lon direction (fields[4]) - E/W
    // Quality (fields[5]) - 0=no fix, 1=GPS fix, 2=DGPS fix
    // Satellite count (fields[6])
    // HDOP (fields[7])
    // Altitude (fields[8])
    
    int quality = fields[5].toInt();
    if (quality == 0) {
        _fixValid = false;
        return false;
    }
    
    // Parse latitude: DDMM.MMMM format
    if (fields[1].length() > 0 && fields[2].length() > 0) {
        float latRaw = fields[1].toFloat();
        int latDeg = (int)(latRaw / 100);
        float latMin = latRaw - (latDeg * 100);
        _latitude = latDeg + (latMin / 60.0);
        
        if (fields[2].charAt(0) == 'S') {
            _latitude = -_latitude;
        }
    }
    
    // Parse longitude: DDDMM.MMMM format
    if (fields[3].length() > 0 && fields[4].length() > 0) {
        float lonRaw = fields[3].toFloat();
        int lonDeg = (int)(lonRaw / 100);
        float lonMin = lonRaw - (lonDeg * 100);
        _longitude = lonDeg + (lonMin / 60.0);
        
        if (fields[4].charAt(0) == 'W') {
            _longitude = -_longitude;
        }
    }
    
    // Satellite count
    if (fields[6].length() > 0) {
        _satelliteCount = fields[6].toInt();
    }
    
    // HDOP
    if (fields[7].length() > 0) {
        _hdop = fields[7].toFloat();
    }
    
    // Altitude
    if (fields[8].length() > 0) {
        _altitude = fields[8].toFloat();
    }
    
    _fixValid = true;
    _lastUpdateTime = millis();
    return true;
}

bool GPSManager::_parseGNRMC(const String& line) {
    // Format: $GNRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,E/W*checksum
    // Or: +GNRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,E/W*checksum
    
    int idx = line.indexOf("GNRMC");
    if (idx < 0) return false;
    
    idx += 6;
    int starIdx = line.indexOf('*', idx);
    if (starIdx < 0) return false;
    
    String values = line.substring(idx, starIdx);
    
    String fields[13];
    int fieldCount = 0;
    int lastIdx = -1;
    
    for (int i = 0; i <= values.length() && fieldCount < 13; i++) {
        if (i == values.length() || values[i] == ',') {
            fields[fieldCount++] = values.substring(lastIdx + 1, i);
            lastIdx = i;
        }
    }
    
    if (fieldCount < 6) return false;
    
    // Status: A=active (valid), V=void (invalid)
    if (fields[2].length() == 0 || fields[2].charAt(0) != 'A') {
        _fixValid = false;
        return false;
    }
    
    // Latitude (fields[3]) - format: DDMM.MMMM
    // Lat direction (fields[4]) - N/S
    // Longitude (fields[5]) - format: DDDMM.MMMM
    // Lon direction (fields[6]) - E/W
    
    // Parse latitude
    if (fields[3].length() > 0 && fields[4].length() > 0) {
        float latRaw = fields[3].toFloat();
        int latDeg = (int)(latRaw / 100);
        float latMin = latRaw - (latDeg * 100);
        _latitude = latDeg + (latMin / 60.0);
        
        if (fields[4].charAt(0) == 'S') {
            _latitude = -_latitude;
        }
    }
    
    // Parse longitude
    if (fields[5].length() > 0 && fields[6].length() > 0) {
        float lonRaw = fields[5].toFloat();
        int lonDeg = (int)(lonRaw / 100);
        float lonMin = lonRaw - (lonDeg * 100);
        _longitude = lonDeg + (lonMin / 60.0);
        
        if (fields[6].charAt(0) == 'W') {
            _longitude = -_longitude;
        }
    }
    
    _fixValid = true;
    _lastUpdateTime = millis();
    return true;
}

bool GPSManager::isFixValid() {
    return _fixValid && (millis() - _lastUpdateTime < 10000); // Valid if updated within 10 seconds
}

float GPSManager::getLatitude() {
    return _latitude;
}

float GPSManager::getLongitude() {
    return _longitude;
}

float GPSManager::getAltitude() {
    return _altitude;
}

uint8_t GPSManager::getSatelliteCount() {
    return _satelliteCount;
}

float GPSManager::getHDOP() {
    return _hdop;
}

uint32_t GPSManager::getLastUpdateTime() {
    return _lastUpdateTime;
}
