#include "NetworkManager.h"
#include "C16QS4GManager.h"
#include <sys/time.h>
#include <time.h>

SmartTrackNetworkManager::SmartTrackNetworkManager() : _timeSynced(false), _modem4G(nullptr) {
}

bool SmartTrackNetworkManager::begin() {
    // 4G modülü lazy initialization ile başlatılacak
    _modem4G = nullptr;
    return true;
}

bool SmartTrackNetworkManager::connect4G() {
    // Lazy initialization: 4G modülü sadece gerektiğinde başlat
    if (!_modem4G) {
        Serial.println("[4G] 4G modülü başlatılıyor...");
        _modem4G = new C16QS4GManager();
        if (!_modem4G->begin()) {
            Serial.println("[4G] Modem initialization failed");
            delete _modem4G;
            _modem4G = nullptr;
            return false;
        }
    }
    
    if (!_modem4G->isReady()) {
        Serial.println("[4G] Modem not ready");
        return false;
    }
    
    if (_modem4G->connectNetwork()) {
        Serial.println("[4G] Network connected");
        return true;
    }
    
    return false;
}

bool SmartTrackNetworkManager::connect(Cfg& cfg) {
    // Smart_track is 4G only
    return connect4G();
}

bool SmartTrackNetworkManager::syncTimeGSM() {
    if (!_modem4G) {
        Serial.println("[GSM-TIME] 4G modem not available");
        return false;
    }
    
    Serial.println("[GSM-TIME] Syncing time from GSM network...");
    
    struct tm gsmTime;
    int modemTimezone = 0;
    if (!_modem4G->getGSMTime(&gsmTime, &modemTimezone)) {
        Serial.println("[GSM-TIME] Failed to get GSM time from modem");
        return false;
    }
    
    // Modem'den gelen zaman UTC zamanı olarak kabul ediliyor
    setenv("TZ", "UTC0", 1);
    tzset();
    
    time_t utcEpoch = mktime(&gsmTime);
    if (utcEpoch == -1) {
        Serial.println("[GSM-TIME] Failed to convert GSM time to time_t");
        setenv("TZ", "TUR-3", 1);
        tzset();
        return false;
    }
    
    // ESP32 RTC'sine UTC epoch olarak set et
    setenv("TZ", "TUR-3", 1);
    tzset();
    
    struct timeval tv;
    tv.tv_sec = utcEpoch;
    tv.tv_usec = 0;
    
    if (settimeofday(&tv, NULL) != 0) {
        Serial.println("[GSM-TIME] Failed to set system time");
        return false;
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[GSM-TIME] Failed to read back set time");
        return false;
    }
    
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("[GSM-TIME] Time synced successfully - TR time: %s (UTC epoch: %lu)\n", 
                 timeStr, (unsigned long)utcEpoch);
    
    _timeSynced = true;
    return true;
}

bool SmartTrackNetworkManager::isConnected() {
    return (_modem4G && _modem4G->isReady());
}

int SmartTrackNetworkManager::getRSSI() {
    if (_modem4G) {
        return _modem4G->getSignalStrength();
    }
    return 0;
}

uint32_t SmartTrackNetworkManager::getEpoch() {
    time_t now;
    time(&now);
    return (uint32_t)now;
}

String SmartTrackNetworkManager::getIPAddress() {
    if (_modem4G) {
        return _modem4G->getIPAddress();
    }
    return "0.0.0.0";
}

void SmartTrackNetworkManager::disconnect() {
    if (_modem4G) {
        _modem4G->disconnectNetwork();
    }
}

void SmartTrackNetworkManager::powerOff() {
    if (_modem4G) {
        _modem4G->powerOff();
        delete _modem4G;
        _modem4G = nullptr;
    }
    Serial.println("[NET] Network power off");
}

// ===== GPS Fonksiyonları (NMEA Stream) =====

bool SmartTrackNetworkManager::startGPS() {
    if (_modem4G) {
        return _modem4G->startGPS();
    }
    return false;
}

void SmartTrackNetworkManager::updateGPS() {
    if (_modem4G) {
        _modem4G->updateGPS();
    }
}

bool SmartTrackNetworkManager::getGPSLocation(float* lat, float* lon) {
    if (_modem4G) {
        return _modem4G->getGPSLocation(lat, lon);
    }
    return false;
}

bool SmartTrackNetworkManager::isGPSFixValid() {
    if (_modem4G) {
        return _modem4G->isGPSFixValid();
    }
    return false;
}

int SmartTrackNetworkManager::getGPSSatellites() {
    if (_modem4G) {
        return _modem4G->getGPSSatellites();
    }
    return 0;
}

float SmartTrackNetworkManager::getGPSHDOP() {
    if (_modem4G) {
        return _modem4G->getGPSHDOP();
    }
    return 99.0;
}

float SmartTrackNetworkManager::getGPSSpeed() {
    if (_modem4G) {
        return _modem4G->getGPSSpeed();
    }
    return 0.0;
}

float SmartTrackNetworkManager::getGPSCourse() {
    if (_modem4G) {
        return _modem4G->getGPSCourse();
    }
    return 0.0;
}

String SmartTrackNetworkManager::getGPSTime() {
    if (_modem4G) {
        return _modem4G->getGPSTime();
    }
    return "";
}

String SmartTrackNetworkManager::getGPSDate() {
    if (_modem4G) {
        return _modem4G->getGPSDate();
    }
    return "";
}
