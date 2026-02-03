#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include "ConfigManager.h"

class OTAUpdate {
public:
    static bool performUpdate(const char* url, const char* user, const char* pass, bool insecureTLS);
    static void setProgressCallback(void (*callback)(int, int));
    
private:
    static void (*_progressCallback)(int, int);
};

#endif
