#include "OTAUpdate.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>

void (*OTAUpdate::_progressCallback)(int, int) = nullptr;

bool OTAUpdate::performUpdate(const char* url, const char* user, const char* pass, bool insecureTLS) {
    Serial.println("[OTA] Starting update...");
    
    HTTPClient http;
    
    // URL'nin HTTP mi HTTPS mi olduğunu kontrol et
    bool isHTTPS = (strncmp(url, "https://", 8) == 0);
    
    if (isHTTPS) {
        WiFiClientSecure client;
        if (insecureTLS) {
            client.setInsecure();
        }
        http.begin(client, url);
    } else {
        // HTTP için normal client kullan
        WiFiClient client;
        http.begin(client, url);
    }
    
    // Basic Auth
    if (strlen(user) > 0 && strlen(pass) > 0) {
        http.setAuthorization(user, pass);
    }
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "HTTP error: %d", httpCode);
        Serial.printf("[OTA] %s\n", errorMsg);
        if (_progressCallback) {
            _progressCallback(-1, -1); // Error indicator
        }
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("[OTA] Content length: %d bytes\n", contentLength);
    
    if (Update.begin(contentLength)) {
        Stream* stream = http.getStreamPtr();
        uint8_t buffer[1024];
        int totalBytes = 0;
        
        while (http.connected() && totalBytes < contentLength) {
            int bytesRead = stream->readBytes(buffer, min(1024, contentLength - totalBytes));
            if (bytesRead > 0) {
                Update.write(buffer, bytesRead);
                totalBytes += bytesRead;
                
                if (_progressCallback) {
                    _progressCallback(totalBytes, contentLength);
                }
            }
            yield();
        }
        
        if (Update.end()) {
            Serial.println("[OTA] Update successful, restarting...");
            if (_progressCallback) {
                _progressCallback(totalBytes, totalBytes); // 100% - Success
            }
            http.end();
            delay(2000); // Success ekranını görmek için
            ESP.restart();
            return true;
        } else {
            const char* errorStr = Update.errorString();
            Serial.printf("[OTA] Update error: %s\n", errorStr);
            if (_progressCallback) {
                _progressCallback(-2, -2); // Update error indicator
            }
        }
    } else {
        Serial.printf("[OTA] Not enough space: %d bytes needed\n", contentLength);
        if (_progressCallback) {
            _progressCallback(-3, -3); // Space error indicator
        }
    }
    
    http.end();
    return false;
}

void OTAUpdate::setProgressCallback(void (*callback)(int, int)) {
    _progressCallback = callback;
}
