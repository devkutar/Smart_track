#include "C16QS4GManager.h"
#include "hardware.h"

C16QS4GManager::C16QS4GManager() 
    : _serial(nullptr), _initialized(false), _networkConnected(false), 
      _mqttConnected(false), _gpsStarted(false), _gpsFixValid(false),
      _gpsLat(0.0), _gpsLon(0.0), _gpsAlt(0.0), _gpsSats(0), _gpsHdop(99.0),
      _gpsSpeed(0.0), _gpsCourse(0.0), _gpsTime(""), _gpsDate(""),
      _gpsLastUpdate(0), _nmeaBuffer(""),
      _mqttSessionId(-1), _mqttCallback(nullptr) {
}

bool C16QS4GManager::begin() {
    Serial.println("[4G] C16QS başlatılıyor...");
    
    // 1) Önce kontrol pinlerini LOW yap (pasif durum - active low pinler için)
    pinMode(PIN_MODEM_POWERKEY, OUTPUT);
    pinMode(PIN_MODEM_RESETKEY, OUTPUT);
    digitalWrite(PIN_MODEM_POWERKEY, LOW);
    digitalWrite(PIN_MODEM_RESETKEY, LOW);
    
    // Status pinlerini ayarla
    pinMode(PIN_MODEM_NET_STATUS, INPUT);
    pinMode(PIN_MODEM_STATUS, INPUT);
    
    Serial.println("[4G] Pinler hazırlandı, modül için bekleniyor...");
    delay(2000);
    
    // 2) UART başlat (RX/TX çapraz bağlantı - RX=17, TX=18)
    _serial = new HardwareSerial(1);
    // RX buffer'ı artır - büyük MQTT payload'ları (1KB+) için gerekli
    // Varsayılan 256 byte, biz 4096 byte yapıyoruz
    _serial->setRxBufferSize(4096);
    _serial->begin(MODEM_UART_BAUD, SERIAL_8N1, PIN_MODEM_UART_RX, PIN_MODEM_UART_TX);
    _serial->setTimeout(2000);
    
    // 3) CTS/RTS pinlerini manuel ayarla (isteğe bağlı)
    pinMode(PIN_MODEM_UART_CTS, INPUT);
    pinMode(PIN_MODEM_UART_RTS, OUTPUT);
    digitalWrite(PIN_MODEM_UART_RTS, LOW);  // RTS başlangıçta LOW
    
    // 4) Modülü aç (Power-on pulse)
    Serial.println("[4G] Power key pulse gönderiliyor...");
    digitalWrite(PIN_MODEM_POWERKEY, HIGH);
    delay(1000);  // 1 saniye HIGH
    digitalWrite(PIN_MODEM_POWERKEY, LOW);
    
    Serial.println("[4G] Power on sinyali gönderildi, modül başlatılıyor...");
    delay(5000);  // Modülün başlaması için bekle
    
    // 5) Modülün hazır olup olmadığını kontrol et
    Serial.println("[4G] Modül hazır mı kontrol ediliyor...");
    int retries = 3; // Maksimum 3 deneme
    bool moduleReady = false;
    
    while (retries > 0 && !moduleReady) {
        // Serial buffer'ı temizle
        _serial->flush();
        while (_serial->available()) {
            _serial->read(); // Buffer'ı temizle
        }
        
        _serial->print("AT\r\n");
        delay(500);
        
        String response = "";
        unsigned long startTime = millis();
        while (millis() - startTime < 2000) { // 1 saniyeden 2 saniyeye artırıldı
            while (_serial->available()) {
                char c = _serial->read();
                response += c;
            }
            delay(10);
        }
        
        // Response'u temizle ve logla
        response.trim();
        if (response.length() > 0) {
            Serial.printf("[4G] AT response: %s\n", response.c_str());
        } else {
            Serial.println("[4G] AT response: (empty - no response from modem)");
        }
        
        if (response.indexOf("OK") >= 0) {
            moduleReady = true;
            Serial.println("[4G] Modül hazır!");
        } else {
            Serial.printf("[4G] Bekleniyor... (%d retries left)\n", retries - 1);
            retries--;
            delay(1000);
        }
    }
    
    if (!moduleReady) {
        Serial.println("[4G] UYARI: Modül hazır değil! Hard reset deneniyor...");
        
        // Hard reset uygula
        Serial.println("[4G] === HARD RESET BAŞLATILIYOR ===");
        
        // UART'ı kapat
        if (_serial) {
            _serial->end();
            delete _serial;
            _serial = nullptr;
        }
        
        // RESETKEY pin'i ile hard reset
        pinMode(PIN_MODEM_RESETKEY, OUTPUT);
        digitalWrite(PIN_MODEM_RESETKEY, LOW);
        delay(100);
        digitalWrite(PIN_MODEM_RESETKEY, HIGH);
        delay(500);
        digitalWrite(PIN_MODEM_RESETKEY, LOW);
        
        Serial.println("[4G] Hard reset sinyali gönderildi, modül yeniden başlatılıyor...");
        delay(8000);  // Modülün tamamen yeniden başlaması için bekle
        
        // UART'ı yeniden başlat
        _serial = new HardwareSerial(1);
        _serial->setRxBufferSize(4096);
        _serial->begin(MODEM_UART_BAUD, SERIAL_8N1, PIN_MODEM_UART_RX, PIN_MODEM_UART_TX);
        _serial->setTimeout(2000);
        
        // CTS/RTS pinlerini yeniden ayarla
        pinMode(PIN_MODEM_UART_CTS, INPUT);
        pinMode(PIN_MODEM_UART_RTS, OUTPUT);
        digitalWrite(PIN_MODEM_UART_RTS, LOW);
        
        // Power key pulse (yeniden)
        Serial.println("[4G] Power key pulse gönderiliyor (reset sonrası)...");
        digitalWrite(PIN_MODEM_POWERKEY, HIGH);
        delay(1000);
        digitalWrite(PIN_MODEM_POWERKEY, LOW);
        delay(5000);
        
        // İkinci deneme - AT komutu ile modül kontrolü
        Serial.println("[4G] Hard reset sonrası modül kontrol ediliyor...");
        int retries2 = 3; // Maksimum 3 deneme
        moduleReady = false;
        
        while (retries2 > 0 && !moduleReady) {
            _serial->flush();
            while (_serial->available()) {
                _serial->read();
            }
            
            _serial->print("AT\r\n");
            delay(500);
            
            String response = "";
            unsigned long startTime = millis();
            while (millis() - startTime < 2000) {
                while (_serial->available()) {
                    char c = _serial->read();
                    response += c;
                }
                delay(10);
            }
            
            response.trim();
            if (response.length() > 0) {
                Serial.printf("[4G] AT response (reset sonrası): %s\n", response.c_str());
            }
            
            if (response.indexOf("OK") >= 0) {
                moduleReady = true;
                Serial.println("[4G] Modül hard reset sonrası hazır!");
            } else {
                Serial.printf("[4G] Bekleniyor (reset sonrası)... (%d retries left)\n", retries2 - 1);
                retries2--;
                delay(1000);
            }
        }
        
        if (!moduleReady) {
            Serial.println("[4G] HATA: Hard reset sonrası da modül hazır değil!");
            return false;
        }
    }
    
    delay(1000);
    
    // 5.5) GPS'i kapat (önceki oturumdan açık kalmış olabilir - NMEA verileri karışmasın)
    Serial.println("[4G] === GPS Kapatılıyor (başlangıçta) ===");
    _sendATCommand("AT+GPSPORT=0", "OK", 2000);  // NMEA çıkışını kapat
    delay(500);
    _sendATCommand("AT+CGPS=0", "OK", 2000);     // GPS'i kapat
    delay(500);
    _gpsStarted = false;
    Serial.println("[4G] GPS kapatıldı");
        
    // 6) SIM Yapılandırması
    Serial.println("[4G] === SIM Yapılandırması Başlıyor ===");
    
    // SIM Swap kontrolü
    Serial.println("[4G] SIM Swap kontrol ediliyor...");
    _sendATCommand("AT^SIMSWAP=1", "OK", 2000);
    delay(1000);
    
    // SIM Sleep kontrolü kaldırıldı - sürekli başarısız oluyordu ve gereksiz
    
    // SIM Power Save KAPAT
    Serial.println("[4G] SIM Power Save kapatılıyor...");
    _sendATCommand("AT$QCSIMCFG=\"SimPowerSave\",0", "OK", 2000);
    delay(1000);
    
    // SIM Presence Detection AÇ
    Serial.println("[4G] SIM Presence Detection açılıyor...");
    _sendATCommand("AT$QCSIMCFG=\"SimPresenceDetect\",0", "OK", 2000);
    delay(1000);
    
    // Timezone otomatik güncelleme (NITZ) - AT+CTZU=1
    Serial.println("[4G] === Timezone Otomatik Güncelleme (NITZ) ===");
    Serial.println("[4G] AT+CTZU? komutu gönderiliyor (mevcut durumu kontrol)...");
    String ctzuResponse = _sendATCommandResponse("AT+CTZU?", 3000);
    Serial.printf("[4G] AT+CTZU? RAW RESPONSE: [%s]\n", ctzuResponse.c_str());
    
    Serial.println("[4G] AT+CTZU=1 komutu gönderiliyor (NITZ etkinleştiriliyor)...");
    String ctzuSetResponse = _sendATCommandResponse("AT+CTZU=1", 3000);
    Serial.printf("[4G] AT+CTZU=1 RAW RESPONSE: [%s]\n", ctzuSetResponse.c_str());
    
    if (ctzuSetResponse.indexOf("OK") >= 0) {
        Serial.println("[4G] Timezone otomatik güncelleme (NITZ) başarıyla etkinleştirildi");
    } else {
        Serial.println("[4G] WARNING: Timezone otomatik güncelleme ayarı başarısız (devam ediliyor)");
        if (ctzuSetResponse.indexOf("ERROR") >= 0) {
            Serial.println("[4G] AT+CTZU=1 ERROR döndü");
        }
    }
    
    // Tekrar kontrol et
    delay(1000);
    Serial.println("[4G] AT+CTZU? komutu tekrar gönderiliyor (güncellenmiş durumu kontrol)...");
    String ctzuCheckResponse = _sendATCommandResponse("AT+CTZU?", 3000);
    Serial.printf("[4G] AT+CTZU? (after set) RAW RESPONSE: [%s]\n", ctzuCheckResponse.c_str());
    Serial.println("[4G] ============================================");
    delay(1000);
    
    // SIM'i yeniden başlat (CFUN=0 sonra CFUN=1)
    Serial.println("[4G] SIM yeniden başlatılıyor...");
    Serial.println("[4G] CFUN=0 (radio kapatılıyor)...");
    if (!_sendATCommand("AT+CFUN=0", "OK", 5000)) {
        Serial.println("[4G] WARNING: CFUN=0 timeout");
    }
    delay(3000); // CFUN=0 sonrası daha uzun bekleme
    
    Serial.println("[4G] CFUN=1 (radio açılıyor, sinyal aranıyor)...");
    if (!_sendATCommand("AT+CFUN=1", "OK", 15000)) { // Daha uzun timeout (15 saniye)
        Serial.println("[4G] WARNING: CFUN=1 timeout, devam ediliyor...");
    }
    delay(2000); // CFUN=1 sonrası ilk bekleme (radio başlasın)
    
    // Radio açıldıktan sonra sinyal bulunana kadar bekle (kritik!)
    Serial.println("[4G] Radio açıldı, sinyal bekleniyor...");
    int signalWaitRetry = 0;
    bool signalFound = false;
    
    while (signalWaitRetry < 45 && !signalFound) { // Max 45 saniye bekle (daha uzun)
        delay(1000);
        
        String csqResp = _sendATCommandResponse("AT+CSQ");
        int csqPos = csqResp.indexOf("+CSQ:");
        if (csqPos >= 0) {
            int csq = 0, ber = 0;
            sscanf(csqResp.substring(csqPos).c_str(), "+CSQ: %d,%d", &csq, &ber);
            
            if (csq != 99) {
                // Sinyal bulundu!
                int rssi = -113 + (csq * 2);
                Serial.printf("[4G] Sinyal bulundu! CSQ: %d, RSSI: %d dBm (%d saniye sonra)\n", 
                             csq, rssi, signalWaitRetry + 1);
                signalFound = true;
                // Sinyal bulunduktan sonra biraz daha bekle (stabilize olsun)
                delay(2000);
                break;
            } else {
                // Sinyal henüz yok
                if (signalWaitRetry % 5 == 4) { // Her 5 saniyede bir log
                    Serial.printf("[4G] Sinyal aranıyor... (%d/%d saniye)\n", signalWaitRetry + 1, 45);
                }
            }
        }
        signalWaitRetry++;
    }
    
    if (!signalFound) {
        Serial.printf("[4G] WARNING: Sinyal bulunamadı (45 saniye sonra timeout)!\n");
        Serial.println("[4G] Devam ediliyor, connectNetwork() sırasında tekrar denenecek...");
    }
    
    Serial.println("[4G] === SIM Yapılandırması Tamamlandı ===");
    
    // 7) Modül fonksiyonlarını kontrol et - ÖNEMLİ: CFUN=1 zaten yaptık ve sinyal bulundu
    // CFUN? kontrolü yapıp tekrar CFUN=1 yapmaya GEREK YOK - bu radio'yu resetler ve sinyal kaybolur!
    // Bu yüzden sadece log için kontrol ediyoruz, tekrar CFUN=1 yapmıyoruz
    String response = _sendATCommandResponse("AT+CFUN?");
    if (response.indexOf("+CFUN:1") >= 0) {
        Serial.println("[4G] CFUN=1 onaylandı (sinyal mevcut)");
    } else {
        Serial.println("[4G] NOTE: CFUN durumu kontrol edildi (sinyal bulunduğu için devam ediliyor)");
        // CFUN=1 zaten yaptık ve sinyal bulundu, tekrar yapmıyoruz - bu radio'yu resetler!
    }
    
    _initialized = true;
    Serial.println("[4G] C16QS başarıyla başlatıldı!");
    return true;
}

bool C16QS4GManager::isReady() {
    if (!_initialized) return false;
    
    // STATUS pinini kontrol et (HIGH = ready)
    bool status = digitalRead(PIN_MODEM_STATUS);
    return status;
}

bool C16QS4GManager::connectNetwork() {
    if (!_initialized) return false;
    
    Serial.println("[4G] Connecting to network...");
    
    // Flowchart adımı: Modem Ready kontrolü (+ATREADY)
    // Modem STATUS pinini kontrol et (HIGH = ready)
    Serial.println("[4G] Flowchart: Checking modem ready state (STATUS pin)...");
    if (!isReady()) {
        Serial.println("[4G] Flowchart: Modem not ready (STATUS pin LOW) - waiting...");
        // Modem hazır olana kadar bekle (max 10 saniye)
        int readyRetry = 0;
        while (readyRetry < 10 && !isReady()) {
            delay(1000);
            readyRetry++;
            Serial.printf("[4G] Waiting for modem ready... (%d/10 seconds)\n", readyRetry);
        }
        if (!isReady()) {
            Serial.println("[4G] Flowchart: Modem not ready after timeout - returning");
            return false;
        }
    }
    Serial.println("[4G] Flowchart: Modem ready (+ATREADY state) - OK");
    
    // Önce sinyal seviyesini kontrol et
    String csqResponse = _sendATCommandResponse("AT+CSQ");
    Serial.printf("[4G] Signal strength (CSQ): %s\n", csqResponse.c_str());
    
    // CSQ değerini parse et (0-31 arası, 99 = sinyal yok)
    int rssi = getSignalStrength();
    Serial.printf("[4G] RSSI: %d dBm\n", rssi);
    if (rssi < -110) {
        Serial.println("[4G] WARNING: Very weak signal! Check antenna connection.");
    }
    
    // Ağ durumunu kontrol et
    String response = _sendATCommandResponse("AT+CREG?");
    if (response.indexOf("+CREG:") < 0) {
        Serial.println("[4G] CREG query failed - Hard reset deneniyor...");
        
        // Static counter ile sadece 1 kez hard reset dene
        static int cregResetCount = 0;
        if (cregResetCount < 1) {
            cregResetCount++;
            Serial.println("[4G] === CREG HATASI - HARD RESET BAŞLATILIYOR ===");
            hardReset();
            delay(3000);
            
            // begin() fonksiyonunu tekrar çağır
            if (begin()) {
                cregResetCount = 0;  // Başarılı olursa sayacı sıfırla
                return connectNetwork();  // Tekrar dene
            }
        }
        
        cregResetCount = 0;  // Sayacı sıfırla (bir sonraki çağrı için)
        Serial.println("[4G] CREG query failed - Hard reset sonrası da başarısız!");
        return false;
    }
    
    // CREG: 0,1 veya 0,5 ise bağlı (1=home, 5=roaming)
    int regStatus = -1;
    int pos = response.indexOf("+CREG:");
    if (pos >= 0) {
        sscanf(response.substring(pos).c_str(), "+CREG: %*d,%d", &regStatus);
    }
    
    if (regStatus != 1 && regStatus != 5) {
        // CREG Status kodları:
        // 0 = Not registered, not searching
        // 1 = Registered (home network)
        // 2 = Not registered, searching
        // 3 = Registration denied
        // 4 = Unknown
        // 5 = Registered (roaming)
        
        const char* statusStr = "Unknown";
        if (regStatus == 0) statusStr = "Not registered, not searching";
        else if (regStatus == 2) statusStr = "Searching for network";
        else if (regStatus == 3) statusStr = "Registration denied";
        else if (regStatus == 4) statusStr = "Unknown";
        
        Serial.printf("[4G] Network not registered (status: %d - %s)\n", regStatus, statusStr);
        Serial.println("[4G] Waiting for network registration (max 60 seconds)...");
        
        // Bağlantıyı bekle (daha uzun süre - 60 saniye)
        int retry = 0;
        while (retry < 60 && regStatus != 1 && regStatus != 5) {
            delay(1000);
            
            // Her 10 saniyede bir durum raporu ver ve sinyal seviyesini kontrol et
            if (retry % 10 == 0 && retry > 0) {
                int currentRssi = getSignalStrength();
                Serial.printf("[4G] Still waiting... (%d/%d seconds) | RSSI: %d dBm\n", retry, 60, currentRssi);
            }
            
            response = _sendATCommandResponse("AT+CREG?");
            pos = response.indexOf("+CREG:");
            if (pos >= 0) {
                sscanf(response.substring(pos).c_str(), "+CREG: %*d,%d", &regStatus);
                if (regStatus == 1 || regStatus == 5) {
                    int finalRssi = getSignalStrength();
                    Serial.printf("[4G] Network registered! (status: %d) | RSSI: %d dBm\n", regStatus, finalRssi);
                    break;
                }
            }
            retry++;
        }
        
        if (regStatus != 1 && regStatus != 5) {
            Serial.printf("[4G] Network registration timeout after %d seconds (status: %d)\n", retry, regStatus);
            Serial.println("[4G] Check SIM card, APN settings, and network coverage");
            return false;
        }
    } else {
        Serial.printf("[4G] Already registered (status: %d)\n", regStatus);
    }
    
    // APN ayarını yap (Türk Telekom için - genel olarak internet.turkcell veya internet)
    Serial.println("[4G] Setting APN for internet connection...");
    _sendATCommand("AT+CGDCONT=1,\"IP\",\"internet\"", "OK", 3000);
    delay(1000);
    
    // DNS sunucularını manuel olarak set et (DNS çözümleme sorunu olabilir)
    // Google DNS: 8.8.8.8, 8.8.4.4
    // Cloudflare DNS: 1.1.1.1, 1.0.0.1
    Serial.println("[4G] Setting DNS servers (Google DNS)...");
    // AT+CDNSCFG komutu ile DNS ayarı (bazı modüllerde farklı olabilir)
    if (_sendATCommand("AT+CDNSCFG=0,\"8.8.8.8\",\"8.8.4.4\"", "OK", 3000)) {
        Serial.println("[4G] DNS servers configured successfully");
    } else {
        // Alternatif DNS ayarlama komutu deneyelim
        Serial.println("[4G] Trying alternative DNS configuration...");
        if (_sendATCommand("AT+CDNSCFG=\"8.8.8.8\",\"8.8.4.4\"", "OK", 3000)) {
            Serial.println("[4G] Alternative DNS config succeeded");
        } else {
            Serial.println("[4G] DNS configuration failed - using operator DNS (may cause DNS resolution issues)");
        }
    }
    delay(500);
    
    // APN ayarını kontrol et
    String apnResponse = _sendATCommandResponse("AT+CGDCONT?");
    Serial.printf("[4G] Current APN settings: %s\n", apnResponse.c_str());
    
    // DNS ayarını kontrol et (eğer destekleniyorsa)
    String dnsResponse = _sendATCommandResponse("AT+CDNSCFG?");
    Serial.printf("[4G] Current DNS settings: %s\n", dnsResponse.c_str());
    
    // Flowchart adımı: PDP context kontrolü (AT+CGACT?)
    Serial.println("[4G] Flowchart: Checking PDP context status (AT+CGACT?)...");
    String cgactResp = _sendATCommandResponse("AT+CGACT?", 3000);
    Serial.printf("[4G] CGACT? response: %s\n", cgactResp.c_str());
    
    // CGACT: <ContID>,<state> formatında (state: 0=deactivated, 1=activated)
    int cgactPos = cgactResp.indexOf("+CGACT:");
    int contextState = -1;
    if (cgactPos >= 0) {
        // Format: +CGACT: 1,0 veya +CGACT: 1,1
        int contextId = -1;
        sscanf(cgactResp.substring(cgactPos).c_str(), "+CGACT: %d,%d", &contextId, &contextState);
        Serial.printf("[4G] Context ID: %d, State: %d (0=deactivated, 1=activated)\n", contextId, contextState);
    }
    
    // PDP context aktif değilse aktif et
    if (contextState != 1) {
        Serial.println("[4G] Flowchart: PDP context is deactivated - activating...");
        // PDP context'i aktif et (internet bağlantısı)
        Serial.println("[4G] Activating PDP context...");
        if (!_sendATCommand("AT+CGACT=1,1", "OK", 10000)) {
            Serial.println("[4G] Failed to activate PDP context");
            Serial.println("[4G] Flowchart: CGACT activation failed - returning to +ATREADY state");
            Serial.println("[4G] Trying again after delay...");
            delay(2000);
            if (!_sendATCommand("AT+CGACT=1,1", "OK", 10000)) {
                Serial.println("[4G] PDP context activation failed after retry");
                return false;
            }
        }
        Serial.println("[4G] Flowchart: PDP context activated successfully (CGACT: <ContID>,1 - OK)");
        
        // Context aktif olduktan sonra durumu tekrar kontrol et
        delay(1000);
        cgactResp = _sendATCommandResponse("AT+CGACT?", 3000);
        Serial.printf("[4G] CGACT? after activation: %s\n", cgactResp.c_str());
    } else {
        Serial.println("[4G] Flowchart: PDP context already activated (CGACT: <ContID>,1 - OK)");
    }
    
    // IP adresini al (PDP context aktif olduktan sonra birkaç saniye bekle)
    delay(2000); // IP adresinin atanması için bekleme
    Serial.println("[4G] Getting IP address...");
    response = _sendATCommandResponse("AT+CGPADDR=1", 5000);
    Serial.printf("[4G] CGPADDR response: %s\n", response.c_str());
    
    // DNS çözümleme testi - broker adresini test et
    Serial.println("[4G] Testing DNS resolution for broker...");
    String dnsTestCmd = "AT+CDNSGIP=\"iotb.kutar.com.tr\"";
    String dnsTestResp = _sendATCommandResponse(dnsTestCmd.c_str(), 10000);
    Serial.printf("[4G] DNS resolution test: %s\n", dnsTestResp.c_str());
    
    // Parse IP adresi - formatlar:
    // +CGPADDR: 1,"10.187.31.120" (tırnaklı)
    // +CGPADDR: 1,10.187.31.120   (tırnaksız) <- Bu format geliyor!
    int ipPos = response.indexOf("+CGPADDR:");
    if (ipPos >= 0) {
        String ipStr = "";
        
        // Önce tırnaklı formatı dene: +CGPADDR: 1,"IP"
        int quoteStart = response.indexOf('"', ipPos);
        if (quoteStart >= 0) {
            // Tırnaklı format
            quoteStart++; // İlk " karakterinden sonrası
            int quoteEnd = response.indexOf('"', quoteStart);
            if (quoteEnd > quoteStart) {
                ipStr = response.substring(quoteStart, quoteEnd);
            }
        } else {
            // Tırnaksız format: +CGPADDR: 1,IP
            // "1," dan sonrasını al
            int commaPos = response.indexOf(',', ipPos);
            if (commaPos >= 0) {
                commaPos++; // Virgülden sonrası
                // Satır sonuna kadar veya OK'a kadar al
                int lineEnd = response.indexOf('\n', commaPos);
                if (lineEnd < 0) lineEnd = response.indexOf('\r', commaPos);
                if (lineEnd < 0) lineEnd = response.length();
                
                ipStr = response.substring(commaPos, lineEnd);
                ipStr.trim(); // Whitespace temizle
            }
        }
        
        // IP adresini kontrol et
        if (ipStr.length() > 0 && ipStr != "0.0.0.0" && ipStr != "") {
            _ipAddress = ipStr;
            Serial.printf("[4G] Network connected, IP: %s\n", _ipAddress.c_str());
            _networkConnected = true;
            return true;
        } else {
            Serial.printf("[4G] IP address is empty or invalid: '%s'\n", ipStr.c_str());
        }
    }
    
    // IP adresi alınamadıysa retry yap (PDP context aktif olduktan sonra IP atanması biraz zaman alabilir)
    Serial.println("[4G] IP address not found in first attempt, retrying after delay...");
    delay(3000);
    response = _sendATCommandResponse("AT+CGPADDR=1", 5000);
    Serial.printf("[4G] CGPADDR retry response: %s\n", response.c_str());
    
    ipPos = response.indexOf("+CGPADDR:");
    if (ipPos >= 0) {
        String ipStr = "";
        
        // Tırnaklı format
        int quoteStart = response.indexOf('"', ipPos);
        if (quoteStart >= 0) {
            quoteStart++;
            int quoteEnd = response.indexOf('"', quoteStart);
            if (quoteEnd > quoteStart) {
                ipStr = response.substring(quoteStart, quoteEnd);
            }
        } else {
            // Tırnaksız format
            int commaPos = response.indexOf(',', ipPos);
            if (commaPos >= 0) {
                commaPos++;
                int lineEnd = response.indexOf('\n', commaPos);
                if (lineEnd < 0) lineEnd = response.indexOf('\r', commaPos);
                if (lineEnd < 0) lineEnd = response.length();
                
                ipStr = response.substring(commaPos, lineEnd);
                ipStr.trim();
            }
        }
        
        if (ipStr.length() > 0 && ipStr != "0.0.0.0" && ipStr != "") {
            _ipAddress = ipStr;
            Serial.printf("[4G] Network connected, IP: %s (retry success)\n", _ipAddress.c_str());
            _networkConnected = true;
            return true;
        }
    }
    
    Serial.println("[4G] Failed to get IP address after retry");
    Serial.println("[4G] NOTE: PDP context is active but IP assignment may be delayed");
    Serial.println("[4G] NOTE: Some networks assign IP after first data transmission");
    return false;
}

bool C16QS4GManager::connectMQTT(const char* broker, int port, const char* clientId, const char* username, const char* password) {
    if (!_networkConnected) {
        Serial.println("[4G] Network not connected");
        return false;
    }
    
    // GPS'i kapat - NMEA verileri MQTT response'a karışmasın!
    Serial.println("[4G] Disabling GPS before MQTT connection...");
    _sendATCommand("AT+GPSPORT=0", "OK", 2000);  // NMEA çıkışını kapat
    delay(300);
    _sendATCommand("AT+CGPS=0", "OK", 2000);     // GPS'i kapat
    delay(300);
    _gpsStarted = false;
    
    // Serial buffer'ı temizle (kalan GPS verileri olabilir)
    while (_serial->available()) {
        _serial->read();
    }
    Serial.println("[4G] GPS disabled, serial buffer cleared");
    
    // Flowchart: MQTT bağlantısı öncesi ön koşulları kontrol et
    Serial.println("[4G] Flowchart: Verifying prerequisites before MQTT connection...");
    
    // 1. Modem Ready kontrolü
    if (!isReady()) {
        Serial.println("[4G] Flowchart: Modem not ready - MQTT connection cannot proceed");
        return false;
    }
    
    // 2. Network Registration kontrolü
    String cregResp = _sendATCommandResponse("AT+CREG?", 2000);
    int regStatus = -1;
    int cregPos = cregResp.indexOf("+CREG:");
    if (cregPos >= 0) {
        sscanf(cregResp.substring(cregPos).c_str(), "+CREG: %*d,%d", &regStatus);
    }
    if (regStatus != 1 && regStatus != 5) {
        Serial.printf("[4G] Flowchart: Network not registered (status: %d) - MQTT connection cannot proceed\n", regStatus);
        return false;
    }
    
    // 3. PDP Context kontrolü
    String cgactResp = _sendATCommandResponse("AT+CGACT?", 2000);
    int contextState = -1;
    int cgactPos = cgactResp.indexOf("+CGACT:");
    if (cgactPos >= 0) {
        sscanf(cgactResp.substring(cgactPos).c_str(), "+CGACT: %*d,%d", &contextState);
    }
    if (contextState != 1) {
        Serial.printf("[4G] Flowchart: PDP context not activated (state: %d) - MQTT connection cannot proceed\n", contextState);
        return false;
    }
    
    Serial.println("[4G] Flowchart: All prerequisites met - proceeding with MQTT connection");
    Serial.printf("[4G] Connecting to MQTT broker: %s:%d\n", broker, port);
    
    // Önce MQTT modülünün durumunu kontrol et
    Serial.println("[4G] Checking MQTT module status...");
    String statusResp = _sendATCommandResponse("AT+MQTTSTATUS?", 2000);
    Serial.printf("[4G] MQTT status response: %s\n", statusResp.c_str());
    
    // Dokümana göre ERROR kodları:
    // ERROR:4 = "Operation not supported" - MQTT modülü başlatılmamış olabilir
    if (statusResp.indexOf("CME ERROR: 4") >= 0 || statusResp.indexOf("CME ERROR:4") >= 0) {
        Serial.println("[4G] ERROR:4 = Operation not supported (MQTT module may not be initialized)");
        Serial.println("[4G] Doc: This may mean MQTT function is not available yet");
        Serial.println("[4G] Proceeding with MQTT create - first create may initialize the module");
        delay(1000);
    } else if (statusResp.indexOf("ERROR") >= 0) {
        Serial.println("[4G] MQTT status check returned error - module may not be ready");
        delay(1000);
    } else {
        Serial.println("[4G] MQTT module appears to be ready");
    }
    delay(500);
    
    // MQTT oturumu oluştur - Dokümana göre tam format:
    // AT+MQTTCREATE=<hostname>,<port>,<clientid>,<keepalive>,<cleansession>,[username],[password],
    // [lastwillTopic],[lastwillMessage],[lastwillQos],[lastwillRetain],[version(3~4)]
    // Örnek: AT+MQTTCREATE="broker.test.net",1883,"TEST",250,0,"Mehmet","12345AB","LWT","LWM",2,0
    Serial.println("[4G] Creating MQTT session with full parameters (documentation format)...");
    
    // Format: AT+MQTTCREATE="broker",port,"clientid",keepalive,cleansession,"username","password",...
    String cmd = "AT+MQTTCREATE=\"" + String(broker) + "\"," + String(port);
    
    // Client ID (WiFi'de kullanılan: "STC_" + macAddr)
    String clientIdStr = (clientId && strlen(clientId) > 0) ? String(clientId) : String("STC_CLIENT");
    cmd += ",\"" + clientIdStr + "\"";
    
    // Keepalive: 250 saniye (doküman örneği)
    cmd += ",250";
    
    // Cleansession: 0 (false - session persistent)
    cmd += ",0";
    
    // Username ve password (WiFi MQTT'deki değerler)
    // Özel karakterler (!, ., vb.) sorun çıkarabilir - dokümana göre kontrol et
    String userStr = (username && strlen(username) > 0) ? String(username) : String("");
    String passStr = (password && strlen(password) > 0) ? String(password) : String("");
    
    // Password'taki özel karakterleri kontrol et
    Serial.printf("[4G] Username length: %d, Password length: %d\n", userStr.length(), passStr.length());
    if (passStr.indexOf('!') >= 0 || passStr.indexOf('.') >= 0) {
        Serial.println("[4G] WARNING: Password contains special characters (! or .) which might cause issues");
        Serial.println("[4G] Attempting to escape special characters in password...");
        
        // Password'taki özel karakterleri escape et (backslash ile)
        // AT command'de özel karakterler escape edilmeli
        String escapedPass = passStr;
        escapedPass.replace("\\", "\\\\"); // Önce backslash'i escape et
        escapedPass.replace("\"", "\\\""); // Tırnak işaretini escape et
        // ! ve . karakterleri genellikle escape edilmez ama deneyebiliriz
        // escapedPass.replace("!", "\\!");
        // escapedPass.replace(".", "\\.");
        
        Serial.printf("[4G] Original password: %s\n", passStr.c_str());
        Serial.printf("[4G] Escaped password: %s\n", escapedPass.c_str());
        passStr = escapedPass;
    }
    
    cmd += ",\"" + userStr + "\",\"" + passStr + "\"";
    
    // Last Will parameters
    cmd += ",\"LWT\",\"LWM\",2,0";  // QoS=2, Retain=0
    
    // Version parametresi yok (default kullanılacak - MQTT 3.1.1)
    // SSL/TLS kullanmıyoruz - normal MQTT bağlantısı (port 6001 SSL portu olabilir ama komutta TLS parametresi yok)
    
    // Komutun uzunluğunu kontrol et (parse sorunları için)
    Serial.printf("[4G] MQTT create command length: %d bytes\n", cmd.length());
    Serial.printf("[4G] MQTT create command: %s\n", cmd.c_str());
    Serial.printf("[4G] Parameters: broker=%s, port=%d, clientId=%s, username=%s, keepalive=250, cleansession=0\n", 
                 broker, port, clientIdStr.c_str(), userStr.c_str());
    
    Serial.printf("[4G] Sending MQTT create command: %s\n", cmd.c_str());
    
    // Komutu gönder ve tüm response'u al
    // Cavli örneğine göre: HAL_UART_Transmit sonrası HAL_Delay(1000) kullanılıyor (C16QS.c:239)
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    delay(1000); // Cavli örneğine göre 1000ms bekleme
    
    // Response'u bekle - Cavli örneğine göre response gelmesi için yeterli süre bekle
    delay(2000); // İlk bekleme - modülün komutu işlemesi için
    Serial.println("[4G] Waiting for MQTT create response...");
    
    // Response'u karakter karakter oku ve logla
    unsigned long startTime = millis();
    String response = "";
    
    while (millis() - startTime < 20000) { // 20 saniye timeout
        if (_serial->available()) {
            char c = _serial->read();
            response += c;
            Serial.print(c); // Her karakteri anında göster
            if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 || response.indexOf("CME ERROR") >= 0) {
                // Tam response gelsin - biraz daha bekle
                delay(500);
                while (_serial->available()) {
                    response += _serial->read();
                }
                break;
            }
        }
        delay(50); // Biraz daha uzun delay
    }
    Serial.println(""); // Yeni satır
    
    Serial.printf("[4G] Full MQTT create response (length: %d):\n", response.length());
    Serial.println(response);
    
    // Dokümana göre response formatı: +MQTTCREATE: <session_id> veya +MQTTCREATE: <session_id>: CREATED
    // Örnek: +MQTTCREATE: 3 veya +MQTTCREATE: 3: CREATED
    Serial.print("[4G] Response hex (first 100 bytes): ");
    for (int i = 0; i < response.length() && i < 100; i++) {
        Serial.printf("%02X ", (unsigned char)response.charAt(i));
    }
    Serial.println("");
    
    // Response'u hex olarak da göster (debug için)
    Serial.print("[4G] Response hex: ");
    for (int i = 0; i < response.length() && i < 100; i++) {
        Serial.printf("%02X ", (unsigned char)response.charAt(i));
    }
    Serial.println("");
    
    if (response.indexOf("OK") >= 0) {
        Serial.println("[4G] MQTT session created successfully!");
        
        // Dokümana göre response formatı: +MQTTCREATE: <session_id> veya +MQTTCREATE: <session_id>: CREATED
        // Örnek: +MQTTCREATE: 3 veya +MQTTCREATE: 3: CREATED
        int createPos = response.indexOf("+MQTTCREATE:");
        if (createPos >= 0) {
            // +MQTTCREATE: den sonraki sayıyı al
            int idStart = createPos + 12; // "+MQTTCREATE:" = 12 karakter
            // Boşlukları ve satır sonlarını atla
            while (idStart < response.length() && (response.charAt(idStart) == ' ' || response.charAt(idStart) == '\r' || response.charAt(idStart) == '\n')) {
                idStart++;
            }
            
            // Session ID'yi parse et (sayı olarak)
            String sessionStr = "";
            while (idStart < response.length()) {
                char c = response.charAt(idStart);
                if (c >= '0' && c <= '9') {
                    sessionStr += c;
                    idStart++;
                } else if (c == ':' || c == ' ' || c == '\r' || c == '\n') {
                    // Session ID bitti
                    break;
                } else {
                    break;
                }
            }
            
            if (sessionStr.length() > 0) {
                _mqttSessionId = sessionStr.toInt();
                Serial.printf("[4G] Parsed session ID from response: %d (doc format: +MQTTCREATE: %d)\n", 
                             _mqttSessionId, _mqttSessionId);
            } else {
                _mqttSessionId = 3;
                Serial.println("[4G] Session ID not found after +MQTTCREATE:, using default: 3");
            }
        } else {
            // Session ID response'ta yoksa default olarak 3 kullan
            _mqttSessionId = 3;
            Serial.println("[4G] Session ID not in response, using default: 3");
        }
    } else if (response.indexOf("ERROR") >= 0 || response.indexOf("CME ERROR") >= 0) {
        Serial.println("[4G] ERROR received in response");
        
        // ERROR'dan sonra gelen detaylı mesajı kontrol et
        int errorPos = response.indexOf("CME ERROR");
        if (errorPos < 0) errorPos = response.indexOf("ERROR");
        
        // CME ERROR kodunu parse et (genellikle +CME ERROR: <numara> formatında)
        String afterError = response.substring(errorPos);
        Serial.printf("[4G] Full error response: %s\n", afterError.c_str());
        
        // CME ERROR kodunu extract et ve dokümana göre açıkla
        // Format kontrolü: +CME ERROR: <numara> veya +CME ERROR <numara> veya +CME ERROR<numara>
        int cmeErrorPos = afterError.indexOf("CME ERROR");
        if (cmeErrorPos >= 0) {
            int codeStart = cmeErrorPos + 9; // "CME ERROR" = 9 karakter
            
            // : karakteri var mı kontrol et
            bool hasColon = (codeStart < afterError.length() && afterError.charAt(codeStart) == ':');
            if (hasColon) {
                codeStart++; // : karakterini atla
            }
            
            // Boşlukları atla
            while (codeStart < afterError.length() && afterError.charAt(codeStart) == ' ') {
                codeStart++;
            }
            
            // Sayıyı al (sadece rakamlar, ilk non-digit karaktere kadar)
            // CME ERROR kodları genellikle 1-3 haneli (max 3 hane al)
            String errorCode = "";
            int digitCount = 0;
            while (codeStart < afterError.length() && digitCount < 3) {
                char c = afterError.charAt(codeStart);
                if (c >= '0' && c <= '9') {
                    errorCode += c;
                    codeStart++;
                    digitCount++;
                } else if (c == '\r' || c == '\n' || c == ' ' || c == '\0') {
                    // Satır sonu veya boşluk - sayı bitti
                    break;
                } else {
                    // Beklenmeyen karakter - sayı bitti
                    break;
                }
            }
            
            if (errorCode.length() > 0 && errorCode.length() <= 3) {
                Serial.printf("[4G] Extracted ERROR code: %s\n", errorCode.c_str());
                
                // Dokümana göre önemli error kodlarını açıkla
                int errCode = errorCode.toInt();
                if (errCode == 3) {
                    Serial.println("[4G] ERROR:3 = Operation not allowed - Wait for previous command acknowledgment!");
                    Serial.println("[4G] SOLUTION: Add more delay between AT commands!");
                } else if (errCode == 50) {
                    Serial.println("[4G] ERROR:50 = Incorrect input command - Check AT command format!");
                    Serial.println("[4G] SOLUTION: Verify command syntax matches documentation exactly!");
                } else if (errCode == 100) {
                    Serial.println("[4G] ERROR:100 = Unknown error - Check SIM card has valid active data connection!");
                    Serial.println("[4G] SOLUTION: Verify PDP context is active (AT+CGACT?)");
                } else {
                    Serial.printf("[4G] Unknown ERROR code: %d (check C16QS AT Command Manual)\n", errCode);
                    Serial.println("[4G] Common causes: Command format error, password special characters, or module not ready");
                }
            } else if (errorCode.length() > 3) {
                // Çok uzun error kodu - parse sorunu olabilir
                Serial.printf("[4G] WARNING: Error code too long: '%s' (length: %d) - parse issue detected!\n", 
                             errorCode.c_str(), errorCode.length());
                Serial.println("[4G] This suggests the error response format is different than expected");
                Serial.println("[4G] Trying to extract first valid error code...");
                
                // İlk 3 haneyi al ve kontrol et
                String shortCode = errorCode.substring(0, 3);
                int errCode = shortCode.toInt();
                Serial.printf("[4G] First 3 digits as error code: %d\n", errCode);
                
                if (errCode == 3 || errCode == 50 || errCode == 100) {
                    Serial.println("[4G] This matches a known error code - treating as parse issue!");
                    Serial.printf("[4G] Treating as ERROR:%d\n", errCode);
                } else {
                    Serial.println("[4G] First 3 digits don't match known error codes");
                    Serial.println("[4G] Possible causes:");
                    Serial.println("[4G]  1. Modem firmware version has different error format");
                    Serial.println("[4G]  2. Response parsing issue (characters lost/corrupted)");
                    Serial.println("[4G]  3. Command format completely wrong for this firmware version");
                }
            } else {
                Serial.printf("[4G] WARNING: Invalid error code format: '%s' (length: %d)\n", 
                             errorCode.c_str(), errorCode.length());
                Serial.println("[4G] This might indicate a parsing issue or unexpected response format");
            }
        }
        
        // ERROR:50 = Incorrect input command - komut formatı yanlış olabilir
        // Password'taki özel karakterler (!, .) sorun çıkarabilir
        Serial.println("[4G] ERROR detected - possible causes:");
        Serial.println("[4G] 1. Command format mismatch with documentation");
        Serial.println("[4G] 2. Password special characters (!, .) may need escaping");
        Serial.println("[4G] 3. Previous command acknowledgment not received (ERROR:3)");
        
        // Komutlar arasında bekleme - modülün önceki komutu işlemesi için
        Serial.println("[4G] Waiting before retry (5 seconds - allow previous command to finish)...");
        delay(5000); // Daha uzun bekleme - ERROR:3 için
        
        // İlk retry - aynı format ile tekrar dene (belki timeout oldu)
        Serial.println("[4G] Retry #1: Same format, waiting for full response...");
        cmd = "AT+MQTTCREATE=\"" + String(broker) + "\"," + String(port);
        cmd += ",\"" + clientIdStr + "\",250,0";
        cmd += ",\"" + userStr + "\",\"" + passStr + "\"";
        cmd += ",\"LWT\",\"LWM\",2,0";  // QoS=2, Retain=0
        Serial.printf("[4G] Retry #1 command: %s\n", cmd.c_str());
        
        _serial->flush();
        delay(500); // Serial buffer'ın temizlenmesi için ekstra bekleme
        _serial->print(cmd);
        _serial->print("\r\n");
        delay(2000); // Cavli örneğine göre 1000ms, ama biraz daha uzun bekleyelim
        
        startTime = millis();
        response = "";
        Serial.println("[4G] Waiting for retry #1 response...");
        while (millis() - startTime < 20000) { // 20 saniye timeout
            if (_serial->available()) {
                char c = _serial->read();
                response += c;
                Serial.print(c);
                if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 || response.indexOf("CME ERROR") >= 0) {
                    // Tam response gelsin
                    delay(500);
                    while (_serial->available()) {
                        response += _serial->read();
                    }
                    break;
                }
            }
            delay(50); // Biraz daha uzun delay
        }
        Serial.println("");
        Serial.printf("[4G] Retry #1 response: %s\n", response.c_str());
        
        if (response.indexOf("OK") >= 0) {
            Serial.println("[4G] Retry #1 succeeded!");
            // Response'tan session ID'yi parse et (Cavli formatına göre)
            int createPos = response.indexOf("+MQTTCREATE:");
            if (createPos >= 0) {
                int idStart = createPos + 12;
                while (idStart < response.length() && response.charAt(idStart) == ' ') {
                    idStart++;
                }
                if (idStart < response.length()) {
                    char sessionChar = response.charAt(idStart);
                    if (sessionChar >= '0' && sessionChar <= '9') {
                        _mqttSessionId = sessionChar - '0';
                        Serial.printf("[4G] Parsed session ID from response: %d (char: '%c')\n", 
                                     _mqttSessionId, sessionChar);
                    } else {
                        String sessionStr = response.substring(idStart);
                        sessionStr.trim();
                        int colonPos = sessionStr.indexOf(':');
                        int spacePos = sessionStr.indexOf(' ');
                        if (colonPos > 0 && (spacePos < 0 || colonPos < spacePos)) {
                            sessionStr = sessionStr.substring(0, colonPos);
                        } else if (spacePos > 0) {
                            sessionStr = sessionStr.substring(0, spacePos);
                        }
                        _mqttSessionId = sessionStr.toInt();
                        Serial.printf("[4G] Parsed session ID from response: %d\n", _mqttSessionId);
                    }
                } else {
                    _mqttSessionId = 3;
                    Serial.println("[4G] Session ID not found after +MQTTCREATE:, using default: 3");
                }
            } else {
                _mqttSessionId = 3;
                Serial.println("[4G] Session ID not in response, using default: 3");
            }
        } else {
            Serial.println("[4G] Retry #1 failed - no OK in response");
            Serial.println("[4G] NOTE: Same broker works with WiFi, so broker is reachable");
            Serial.println("[4G] Possible issues:");
            Serial.println("[4G] 1. Password special characters (! and .) may need escaping");
            Serial.println("[4G] 2. AT command syntax difference (check C16QS AT Command Manual)");
            Serial.println("[4G] 3. MQTT function not properly initialized on 4G module");
            Serial.println("[4G] 4. Module firmware version may have different MQTT command format");
            return false;
        }
    } else {
        Serial.println("[4G] No OK or ERROR in response - timeout or unexpected response");
        return false;
    }
    
    // Dokümana göre: AT+MQTTCONN=<session_id>,0
    // Response format: +MQTTCONN: <session_id>: CONNECTING sonra +MQTTCONN: <session_id>: CONNECTED,<return_code>
    // Örnek: +MQTTCONN: 3: CONNECTING, sonra +MQTTCONN: 3: CONNECTED,0
    cmd = "AT+MQTTCONN=" + String(_mqttSessionId) + ",0";
    Serial.printf("[4G] MQTT connect command (doc format): %s\n", cmd.c_str());
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    delay(1000); // Dokümana göre komut sonrası bekleme
    
    // Dokümana göre response formatı: +MQTTCONN: <session_id>: CONNECTED,<return_code>
    if (!_waitForResponse("CONNECTED", 10000)) {
        Serial.println("[4G] MQTT connection failed (CONNECTED not found in response)");
        // Response'u logla
        delay(500);
        String connResponse = "";
        unsigned long startTime = millis();
        while (millis() - startTime < 2000 && _serial->available()) {
            connResponse += (char)_serial->read();
        }
        Serial.printf("[4G] Connection response: %s\n", connResponse.c_str());
        return false;
    }
    
    _mqttConnected = true;
    Serial.println("[4G] MQTT connected!");
    return true;
}

bool C16QS4GManager::publishMQTT(const char* topic, const char* payload) {
    if (!_mqttConnected) {
        Serial.println("[4G] MQTT not connected - cannot publish");
        return false;
    }
    
    int payloadLen = strlen(payload);
    Serial.printf("[4G] MQTT publish: topic=%s, payload length=%d bytes\n", topic, payloadLen);
    
    // Dokümana göre büyük mesajlar için AT+MQTTPUBLM kullanılmalı (max 20KB)
    // Format: AT+MQTTPUBLM=<client_id>,<topic>,<qos>,<duplicate>,<retain>,[message_size],[message_id]
    // Sonra >message<ctrl+z|esc> formatında mesaj gönderilir
    
    // AT+MQTTPUBLM kullan (tüm mesajlar için)
    int messageSize = payloadLen;
    int messageId = 1; // Message ID (isteğe bağlı)
    
    // AT+MQTTPUBLM komutu
    String cmd = "AT+MQTTPUBLM=" + String(_mqttSessionId) + ",\"" + String(topic) + "\",0,0,0," + String(messageSize) + "," + String(messageId);
    
    Serial.printf("[4G] Using AT+MQTTPUBLM - command: %s\n", cmd.c_str());
    
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    delay(500); // Komut sonrası bekleme
    
    // Dokümana göre modül ">" karakteri gönderir, sonra mesajı göndeririz, sonra Ctrl+Z (0x1A) göndeririz
    unsigned long startTime = millis();
    String response = "";
    bool gotPrompt = false;
    
    // İlk olarak ">" prompt'unu bekle (modül mesaj bekliyor demektir)
    while (millis() - startTime < 5000) { // 5 saniye timeout
        if (_serial->available()) {
            char c = _serial->read();
            response += c;
            Serial.print(c); // Debug için
            if (c == '>') {
                gotPrompt = true;
                Serial.println("\n[4G] Got '>' prompt - sending payload...");
                break;
            }
            if (response.indexOf("ERROR") >= 0 || response.indexOf("CME ERROR") >= 0) {
                Serial.printf("\n[4G] ERROR before prompt - response: %s\n", response.c_str());
                return false;
            }
        }
        delay(50);
    }
    
    if (!gotPrompt) {
        Serial.printf("\n[4G] Timeout waiting for '>' prompt - response: %s\n", response.c_str());
        return false;
    }
    
    // Mesajı gönder (payload'u direkt gönder - escape etmeye gerek yok, çünkü Ctrl+Z ile bitiriyoruz)
    _serial->print(payload);
    Serial.printf("[4G] Sent payload (%d bytes)\n", payloadLen);
    
    // Ctrl+Z (0x1A) gönder (mesajın bittiğini belirtir)
    _serial->write(0x1A);
    Serial.println("[4G] Sent Ctrl+Z (0x1A) to terminate message");
    
    delay(1000); // Mesaj gönderildikten sonra bekle (response gelmesi için zaman ver)
    
    // Response'u bekle: +MQTTPUBLM: <session_id>: PUBLISHING sonra +MQTTPUBLM: <session_id>: PUBLISH SUCCESS,<message_id>
    // veya sadece OK gelebilir
    startTime = millis();
    response = "";
    bool publishSuccess = false;
    
    // Önce response'u oku, sonra URC'leri işle
    while (millis() - startTime < 10000) { // 10 saniye timeout
        if (_serial->available()) {
            char c = _serial->read();
            response += c;
            Serial.print(c); // Debug için
            
            // Response kontrolü
            if (response.indexOf("PUBLISH SUCCESS") >= 0 || 
                response.indexOf("+MQTTPUBLM:") >= 0 || 
                response.indexOf("\r\nOK\r\n") >= 0 || 
                response.indexOf("\nOK\n") >= 0) {
                publishSuccess = true;
                delay(200); // Tam response gelsin
                while (_serial->available()) {
                    response += (char)_serial->read();
                }
                Serial.println("");
                Serial.println("[4G] MQTT publish successful (response received)");
                // Şimdi URC'leri kontrol et (config mesajları için)
                _processUrc();
                return true;
            }
            
            if (response.indexOf("ERROR") >= 0 || response.indexOf("CME ERROR") >= 0) {
                delay(200);
                while (_serial->available()) {
                    response += (char)_serial->read();
                }
                Serial.println("");
                Serial.printf("[4G] MQTT publish failed - ERROR in response\n");
                // Hata olsa bile URC'leri kontrol et
                _processUrc();
                return false;
            }
        }
        delay(50);
    }
    Serial.println(""); // Yeni satır
    
    // Timeout durumu - belki response gelmiştir ama parse edilememiştir
    if (response.length() > 0) {
        Serial.printf("[4G] Response received but not recognized: %s\n", response.c_str());
        // Response varsa başarılı sayalım (belki OK farklı formatta geldi)
        if (response.indexOf("OK") >= 0 || response.indexOf("SUCCESS") >= 0) {
            Serial.println("[4G] MQTT publish assumed successful (OK/SUCCESS found in response)");
            _processUrc();
            return true;
        }
    }
    
    // Response gelmedi veya tanınmadı - ama publish başarılı olabilir
    Serial.printf("[4G] No response received, but publish may have succeeded\n");
    Serial.printf("[4G] Response buffer: %s\n", response.c_str());
    // Timeout olsa bile URC'leri kontrol et
    _processUrc();
    
    // Response gelmedi ama publish başarılı olabilir - true döndür
    Serial.println("[4G] MQTT publish assumed successful (no error received)");
    return true;
}

bool C16QS4GManager::subscribeMQTT(const char* topic) {
    if (!_mqttConnected) return false;
    
    // Dokümana göre: AT+MQTTSUBUNSUB=<client_id>,<topic>,<sub_flag>,<qos>
    // sub_flag: 1=subscribe, 0=unsubscribe
    // qos: 0, 1, veya 2
    String cmd = "AT+MQTTSUBUNSUB=" + String(_mqttSessionId) + ",\"" + String(topic) + "\",1,0";
    
    Serial.printf("[4G] MQTT subscribe command: %s\n", cmd.c_str());
    
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    
    if (!_waitForResponse("SUBSCRIBE SUCCESS", 5000)) {
        Serial.println("[4G] MQTT subscribe failed - SUBSCRIBE SUCCESS not found");
        return false;
    }
    
    Serial.printf("[MQTT-4G] Subscribed to %s\n", topic);
    return true;
}

bool C16QS4GManager::isMQTTConnected() {
    if (!_mqttConnected) return false;
    
    String cmd = "AT+MQTTSTATUS=" + String(_mqttSessionId);
    String response = _sendATCommandResponse(cmd.c_str());
    return response.indexOf("+MQTTSTATUS: 1") >= 0;
}

void C16QS4GManager::loop() {
    if (!_serial || !_mqttConnected) return;
    
    // URC (Unsolicited Result Code) kontrolü
    // 4G modem'de gelen MQTT mesajları URC olarak gelir (+MQTTPUBLISH: ...)
    // Bu fonksiyon serial portu sürekli kontrol eder ve gelen URC'leri işler
    // Subscribe işlemi connectMQTT() içinde yapılır (bir kez yeterli)
    // Callback'ler _processUrc() içinde çağrılır
    _processUrc();
}

void C16QS4GManager::disconnectMQTT() {
    if (!_mqttConnected) return;
    
    String cmd = "AT+MQTTDISCONN=" + String(_mqttSessionId);
    _sendATCommand(cmd.c_str(), "DISCONNECTED", 5000);
    
    // Oturum bilgilerini sil
    cmd = "AT+MQTTDELETE=" + String(_mqttSessionId);
    _sendATCommand(cmd.c_str(), "DELETED", 2000);
    
    _mqttConnected = false;
    _mqttSessionId = -1;
}

void C16QS4GManager::disconnectNetwork() {
    if (!_networkConnected) return;
    
    disconnectMQTT();
    _sendATCommand("AT+CGACT=0,1", "OK", 3000);
    _networkConnected = false;
    _ipAddress = "";
}

void C16QS4GManager::powerOff() {
    Serial.println("[4G] Modül kapatılıyor (power off)...");
    
    // Önce network bağlantılarını kapat
    disconnectNetwork();
    
    // Modülü kapat (power off)
    // CFUN=0 ile minimal power mode
    _sendATCommand("AT+CFUN=0", "OK", 3000);
    delay(1000);
    
    // POWERKEY ile power off (optional - eğer destekleniyorsa)
    // Bazı modüllerde AT+CPOWD=1 komutu ile de kapatılabilir
    // Şimdilik CFUN=0 yeterli (minimum power mode)
    
    _initialized = false;
    _networkConnected = false;
    _mqttConnected = false;
    _mqttSessionId = -1;
    
    // UART'ı kapat (optional - güç tasarrufu için)
    if (_serial) {
        _serial->end();
    }
    
    Serial.println("[4G] Modül kapatıldı");
}

void C16QS4GManager::hardReset() {
    Serial.println("[4G] Hard reset başlatılıyor (RESETKEY pin)...");
    
    // Önce network bağlantılarını kapat
    disconnectNetwork();
    
    // UART'ı kapat (reset sırasında)
    if (_serial) {
        _serial->end();
    }
    
    // RESETKEY pin'ini LOW'a çek (normal durum)
    pinMode(PIN_MODEM_RESETKEY, OUTPUT);
    digitalWrite(PIN_MODEM_RESETKEY, LOW);
    delay(100);
    
    // RESETKEY pin'ini HIGH yap (reset pulse)
    digitalWrite(PIN_MODEM_RESETKEY, HIGH);
    delay(500); // Reset pulse süresi
    digitalWrite(PIN_MODEM_RESETKEY, LOW);
    
    Serial.println("[4G] Hard reset pulse gönderildi, modül yeniden başlatılıyor...");
    delay(5000); // Modülün yeniden başlaması için bekle
    
    // UART'ı yeniden başlat
    if (_serial) {
        delete _serial;
    }
    _serial = new HardwareSerial(1);
    // RX buffer'ı artır - büyük MQTT payload'ları (1KB+) için gerekli
    _serial->setRxBufferSize(4096);
    _serial->begin(MODEM_UART_BAUD, SERIAL_8N1, PIN_MODEM_UART_RX, PIN_MODEM_UART_TX);
    _serial->setTimeout(2000);
    
    // Durumu sıfırla
    _initialized = false;
    _networkConnected = false;
    _mqttConnected = false;
    _mqttSessionId = -1;
    
    // Modülün hazır olmasını bekle
    Serial.println("[4G] Hard reset sonrası modül hazır mı kontrol ediliyor...");
    int retries = 15;
    bool moduleReady = false;
    
    while (retries > 0 && !moduleReady) {
        _serial->flush();
        _serial->print("AT\r\n");
        delay(500);
        
        unsigned long startTime = millis();
        String response = "";
        while (millis() - startTime < 2000) {
            if (_serial->available()) {
                response += (char)_serial->read();
                if (response.indexOf("OK") >= 0) {
                    moduleReady = true;
                    break;
                }
            }
            delay(50);
        }
        
        if (!moduleReady) {
            retries--;
            Serial.printf("[4G] Hard reset sonrası modül hazır değil, kalan deneme: %d\n", retries);
            delay(1000);
        }
    }
    
    if (moduleReady) {
        Serial.println("[4G] Hard reset başarılı - modül hazır!");
        _initialized = true;
    } else {
        Serial.println("[4G] Hard reset başarısız - modül hala hazır değil!");
    }
}

int C16QS4GManager::getSignalStrength() {
    if (!_initialized) return -100;
    
    String response = _sendATCommandResponse("AT+CSQ");
    int pos = response.indexOf("+CSQ:");
    if (pos >= 0) {
        int rssi = 0, ber = 0;
        sscanf(response.substring(pos).c_str(), "+CSQ: %d,%d", &rssi, &ber);
        if (rssi == 99) return -100; // No signal
        // CSQ değerini dBm'ye çevir (0-31 -> -113 to -51 dBm)
        return -113 + (rssi * 2);
    }
    return -100;
}

int C16QS4GManager::getCSQ() {
    if (!_initialized) return 99; // No signal
    
    String response = _sendATCommandResponse("AT+CSQ");
    int pos = response.indexOf("+CSQ:");
    if (pos >= 0) {
        int csq = 0, ber = 0;
        sscanf(response.substring(pos).c_str(), "+CSQ: %d,%d", &csq, &ber);
        return csq; // Ham CSQ değeri (0-31, 99 = no signal)
    }
    return 99;
}

String C16QS4GManager::getCSQString() {
    if (!_initialized) return "99,0"; // No signal
    
    String response = _sendATCommandResponse("AT+CSQ");
    int pos = response.indexOf("+CSQ:");
    if (pos >= 0) {
        int csq = 0, ber = 0;
        sscanf(response.substring(pos).c_str(), "+CSQ: %d,%d", &csq, &ber);
        return String(csq) + "," + String(ber); // Format: "CSQ,BER" (örn: "24,0")
    }
    return "99,0"; // No signal
}

String C16QS4GManager::getIPAddress() {
    return _ipAddress;
}

String C16QS4GManager::getIMEI() {
    if (!_initialized) return "";
    
    String response = _sendATCommandResponse("AT+CGSN");
    Serial.printf("[4G] IMEI raw response: %s\n", response.c_str());
    
    // Response format: IMEI\r\nOK veya +CGSN: IMEI\r\nOK veya AT+CGSN\r\nIMEI\r\nOK
    // Önce +CGSN: formatını kontrol et
    int pos = response.indexOf("+CGSN:");
    if (pos >= 0) {
        // Format: +CGSN: IMEI
        String imei = response.substring(pos + 7); // "+CGSN: " = 7 karakter
        // Satır sonlarını ve OK'yi temizle
        imei.replace("\r", "");
        imei.replace("\n", "");
        imei.replace("OK", "");
        imei.trim();
        // Sadece rakamları al (15 haneli IMEI için)
        String cleanImei = "";
        for (int i = 0; i < imei.length(); i++) {
            if (imei.charAt(i) >= '0' && imei.charAt(i) <= '9') {
                cleanImei += imei.charAt(i);
            }
        }
        Serial.printf("[4G] Parsed IMEI (from +CGSN:): %s\n", cleanImei.c_str());
        return cleanImei;
    }
    
    // AT+CGSN satırını atla, direkt IMEI satırını bul
    // Response: AT+CGSN\r\nIMEI\r\nOK veya sadece IMEI\r\nOK
    response.replace("AT+CGSN", "");
    response.replace("\r\n", "\n");
    
    // Satır satır oku ve IMEI'yi bul
    int startPos = 0;
    while (startPos < response.length()) {
        int newlinePos = response.indexOf('\n', startPos);
        if (newlinePos < 0) newlinePos = response.length();
        
        String line = response.substring(startPos, newlinePos);
        line.trim();
        line.replace("\r", "");
        line.replace("OK", "");
        line.trim();
        
        // Eğer satır sadece rakamlardan oluşuyorsa ve 14-15 haneli ise IMEI'dir
        if (line.length() >= 14 && line.length() <= 15) {
            bool allDigits = true;
            for (int i = 0; i < line.length(); i++) {
                if (line.charAt(i) < '0' || line.charAt(i) > '9') {
                    allDigits = false;
                    break;
                }
            }
            if (allDigits) {
                Serial.printf("[4G] Parsed IMEI (from line): %s\n", line.c_str());
                return line;
            }
        }
        
        startPos = newlinePos + 1;
    }
    
    // Son çare: Tüm response'tan sadece rakamları çıkar
    String cleanImei = "";
    for (int i = 0; i < response.length(); i++) {
        if (response.charAt(i) >= '0' && response.charAt(i) <= '9') {
            cleanImei += response.charAt(i);
        }
    }
    
    // 14-15 haneli IMEI olmalı
    if (cleanImei.length() >= 14) {
        String imei = cleanImei.substring(0, 15); // Max 15 hane
        Serial.printf("[4G] Parsed IMEI (from digits): %s\n", imei.c_str());
        return imei;
    }
    
    Serial.printf("[4G] WARNING: Could not parse IMEI from response\n");
    return "";
}

bool C16QS4GManager::getGSMTime(struct tm* timeinfo, int* timezoneOffset) {
    if (!_initialized || !timeinfo) return false;
    
    // AT+CCLK? komutu ile GSM time al (C16QS AT Command Manual ERV 2.9 Section 3.7)
    // Response format: +CCLK: "YY/MM/DD,HH:MM:SS±ZZ"
    // Örnek: +CCLK: "26/01/03,18:45:00+03" veya +CCLK: "26/01/03,18:45:00-05"
    // YY: year (00-99, 2000-2099)
    // MM: month (01-12)
    // DD: day (01-31)
    // HH: hour (00-23)
    // MM: minute (00-59)
    // SS: second (00-59)
    // ±ZZ: timezone offset in hours (e.g., +03 for UTC+3, -05 for UTC-5)
    
    Serial.println("========== [GSM-TIME DEBUG] ==========");
    
    // Önce NITZ durumunu kontrol et
    Serial.println("[4G] NITZ durumu kontrol ediliyor (AT+CTZU?)...");
    String ctzuCheck = _sendATCommandResponse("AT+CTZU?", 2000);
    Serial.printf("[4G] AT+CTZU? response: [%s]\n", ctzuCheck.c_str());
    
    String response = _sendATCommandResponse("AT+CCLK?", 3000); // 3 saniye timeout
    Serial.printf("[4G] AT+CCLK? RAW RESPONSE (hex): ");
    for (int i = 0; i < response.length(); i++) {
        Serial.printf("%02X ", (uint8_t)response.charAt(i));
    }
    Serial.println();
    Serial.printf("[4G] AT+CCLK? RAW RESPONSE (text): [%s]\n", response.c_str());
    Serial.printf("[4G] Response length: %d bytes\n", response.length());
    
    // Response'da +CCLK: satırını bul
    int pos = response.indexOf("+CCLK:");
    if (pos < 0) {
        Serial.println("[4G] GSM time error: +CCLK: not found in response");
        // Error response kontrolü
        if (response.indexOf("ERROR") >= 0) {
            Serial.println("[4G] GSM time error: Modem returned ERROR");
        }
        Serial.println("========================================");
        return false;
    }
    
    Serial.printf("[4G] +CCLK: found at position: %d\n", pos);
    
    // Tırnak işaretleri arasındaki zaman string'ini al
    int quoteStart = response.indexOf('"', pos);
    if (quoteStart < 0) {
        Serial.println("[4G] GSM time error: Opening quote not found");
        Serial.println("========================================");
        return false;
    }
    
    int quoteEnd = response.indexOf('"', quoteStart + 1);
    if (quoteEnd < 0) {
        Serial.println("[4G] GSM time error: Closing quote not found");
        Serial.println("========================================");
        return false;
    }
    
    String timeStr = response.substring(quoteStart + 1, quoteEnd);
    Serial.printf("[4G] Quote positions: start=%d, end=%d\n", quoteStart, quoteEnd);
    Serial.printf("[4G] EXTRACTED TIME STRING from modem: [%s]\n", timeStr.c_str());
    Serial.printf("[4G] Time string length: %d chars\n", timeStr.length());
    
    // Parse: "YY/MM/DD,HH:MM:SS±ZZ"
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int tz = 0;
    
    // Tarih ve zaman kısmını ayır (virgül ile)
    int commaPos = timeStr.indexOf(',');
    if (commaPos < 0) {
        Serial.println("[4G] GSM time parse error: Date/time separator (,) not found");
        return false;
    }
    
    // Tarih kısmını parse et: YY/MM/DD
    String dateStr = timeStr.substring(0, commaPos);
    int slash1 = dateStr.indexOf('/');
    int slash2 = dateStr.indexOf('/', slash1 + 1);
    if (slash1 < 0 || slash2 < 0) {
        Serial.printf("[4G] GSM time parse error: Invalid date format: %s\n", dateStr.c_str());
        return false;
    }
    
    year = dateStr.substring(0, slash1).toInt();
    month = dateStr.substring(slash1 + 1, slash2).toInt();
    day = dateStr.substring(slash2 + 1).toInt();
    
    // Zaman kısmını parse et: HH:MM:SS±ZZ
    String timePart = timeStr.substring(commaPos + 1);
    int colon1 = timePart.indexOf(':');
    int colon2 = timePart.indexOf(':', colon1 + 1);
    if (colon1 < 0 || colon2 < 0) {
        Serial.printf("[4G] GSM time parse error: Invalid time format: %s\n", timePart.c_str());
        return false;
    }
    
    hour = timePart.substring(0, colon1).toInt();
    minute = timePart.substring(colon1 + 1, colon2).toInt();
    
    // Saniye ve timezone kısmı: SS±ZZ
    String secTzPart = timePart.substring(colon2 + 1);
    
    // Timezone işaretini bul (+ veya -)
    int tzStart = -1;
    if (secTzPart.indexOf('+') >= 0) {
        tzStart = secTzPart.indexOf('+');
    } else if (secTzPart.indexOf('-') >= 0) {
        tzStart = secTzPart.indexOf('-');
    }
    
    if (tzStart < 0) {
        Serial.printf("[4G] GSM time parse error: Timezone sign (+/-) not found in: %s\n", secTzPart.c_str());
        return false;
    }
    
    // Saniye kısmını al (timezone işaretinden önceki kısım)
    String secStr = secTzPart.substring(0, tzStart);
    second = secStr.toInt();
    
    // Timezone offset'ini al
    String tzStr = secTzPart.substring(tzStart + 1);
    tz = tzStr.toInt();
    if (secTzPart.charAt(tzStart) == '-') {
        tz = -tz;
    }
    
    // Yıl formatını düzelt: 26 -> 2026, 99 -> 2099, 00 -> 2000
    if (year < 100) {
        year += 2000;
    }
    
    // Değer doğrulama
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || 
        second < 0 || second > 59) {
        Serial.printf("[4G] GSM time parse error: Invalid time values (year=%d, month=%d, day=%d, hour=%d, min=%d, sec=%d)\n",
                     year, month, day, hour, minute, second);
        return false;
    }
    
    // timeinfo'yu doldur
    memset(timeinfo, 0, sizeof(struct tm));
    timeinfo->tm_year = year - 1900;  // struct tm'de yıl 1900'den başlar
    timeinfo->tm_mon = month - 1;     // struct tm'de ay 0'dan başlar
    timeinfo->tm_mday = day;
    timeinfo->tm_hour = hour;
    timeinfo->tm_min = minute;
    timeinfo->tm_sec = second;
    
    // Timezone offset'i döndür (eğer parametre verilmişse)
    if (timezoneOffset) {
        *timezoneOffset = tz;
    }
    
    Serial.printf("[4G] PARSED VALUES:\n");
    Serial.printf("[4G]   Year: %d\n", year);
    Serial.printf("[4G]   Month: %d\n", month);
    Serial.printf("[4G]   Day: %d\n", day);
    Serial.printf("[4G]   Hour: %d\n", hour);
    Serial.printf("[4G]   Minute: %d\n", minute);
    Serial.printf("[4G]   Second: %d\n", second);
    Serial.printf("[4G]   Timezone offset: %+d hours\n", tz);
    Serial.printf("[4G] FINAL PARSED TIME: %04d-%02d-%02d %02d:%02d:%02d (timezone: %+d)\n", 
                 year, month, day, hour, minute, second, tz);
    Serial.println("========================================");
    
    return true;
}

// Static callback pointer
void (*C16QS4GManager::_arduinoCallback)(char* topic, byte* payload, unsigned int len) = nullptr;

// 4G callback wrapper - C16QS format'tan Arduino format'a çevirir
void C16QS4GManager::_mqtt4GCallbackWrapper(const char* topic, const char* payload, int len) {
    if (!_arduinoCallback) return;
    
    // Arduino callback formatına çevir
    char* topicCopy = const_cast<char*>(topic);
    byte* payloadBytes = const_cast<byte*>(reinterpret_cast<const byte*>(payload));
    unsigned int payloadLen = (unsigned int)len;
    
    _arduinoCallback(topicCopy, payloadBytes, payloadLen);
}

void C16QS4GManager::setMqttCallback(void (*callback)(char* topic, byte* payload, unsigned int len)) {
    _arduinoCallback = callback;
    // Internal callback'i wrapper'a bağla
    _mqttCallback = _mqtt4GCallbackWrapper;
}

// Private helper functions

bool C16QS4GManager::_sendATCommand(const char* cmd, const char* expected, uint32_t timeoutMs) {
    if (!_serial) return false;
    
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    
    return _waitForResponse(expected, timeoutMs);
}

String C16QS4GManager::_sendATCommandResponse(const char* cmd, uint32_t timeoutMs) {
    if (!_serial) return "";
    
    _serial->flush();
    _serial->print(cmd);
    _serial->print("\r\n");
    
    unsigned long start = millis();
    String response = "";
    
    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            response += _serial->readStringUntil('\n');
            response += "\n";
            if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                break;
            }
        }
        delay(10);
    }
    
    return response;
}

bool C16QS4GManager::_waitForResponse(const char* expected, uint32_t timeoutMs) {
    unsigned long start = millis();
    String response = "";
    
    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            String line = _serial->readStringUntil('\n');
            response += line;
            
            if (line.indexOf(expected) >= 0) {
                return true;
            }
            if (line.indexOf("ERROR") >= 0) {
                return false;
            }
        }
        delay(10);
    }
    
    return false;
}

void C16QS4GManager::_processUrc() {
    if (!_serial) return;
    
    // Dokümana göre: +MQTTPUBLISH: <session_id>,<qos>,<topic>,<len>,<payload>
    // Örnek: +MQTTPUBLISH: 3,28,KUTARIoT/config/1CDBD4BB2D54,213,{...json...}
    // Payload çok satırlı olabilir, tamamını okumamız gerekiyor
    
    static String pendingUrcLine = ""; // Kısmi okunan satırları tutmak için
    
    while (_serial->available()) {
        String line = _serial->readStringUntil('\n');
        if (line.length() == 0) continue;
        
        line.trim();
        
        // +MQTTPUBLISH ile başlayan satırı bul (çift T - MQTT)
        if (line.indexOf("+MQTTPUBLISH:") >= 0 || pendingUrcLine.indexOf("+MQTTPUBLISH:") >= 0) {
            // Eğer zaten bir pending line varsa, yeni satırı ekle
            if (pendingUrcLine.length() > 0) {
                pendingUrcLine += line;
            } else {
                pendingUrcLine = line;
            }
            
            // +MQTTPUBLISH: satırını parse et
            int pos = pendingUrcLine.indexOf("+MQTTPUBLISH:");
            if (pos >= 0) {
                String rest = pendingUrcLine.substring(pos + 13); // "+MQTTPUBLISH:" = 13 karakter
                rest.trim();
                
                // Format: <session_id>,<qos>,<topic>,<len>,<payload>
                int comma1 = rest.indexOf(',');
                int comma2 = rest.indexOf(',', comma1 + 1);
                int comma3 = rest.indexOf(',', comma2 + 1);
                int comma4 = rest.indexOf(',', comma3 + 1);
                
                if (comma4 > 0) {
                    int sessionId = rest.substring(0, comma1).toInt();
                    int qos = rest.substring(comma1 + 1, comma2).toInt();
                    String topicStr = rest.substring(comma2 + 1, comma3);
                    topicStr.replace("\"", ""); // Tırnak işaretlerini temizle
                    topicStr.trim();
                    int payloadLen = rest.substring(comma3 + 1, comma4).toInt();
                    
                    // Payload'u al (comma4'ten sonrası)
                    String payloadStr = rest.substring(comma4 + 1);
                    int initialPayloadLen = payloadStr.length();
                    
                    Serial.printf("[4G-URC] Parsed URC: sessionId=%d, qos=%d, topic=%s, expectedLen=%d, initialPayloadBytes=%d\n",
                                 sessionId, qos, topicStr.c_str(), payloadLen, initialPayloadLen);
                    
                    // Eğer payload tam değilse, devam eden satırları bekle
                    // (JSON çok satırlı olabilir, satır satır okumalıyız)
                    if (payloadStr.length() < payloadLen) {
                        // Payload henüz tam değil, devam eden satırları oku
                        unsigned long startTime = millis();
                        // Timeout: büyük payload'lar için daha uzun timeout
                        unsigned long timeoutMs = max(15000UL, (unsigned long)payloadLen * 20);
                        
                        while (millis() - startTime < timeoutMs && payloadStr.length() < payloadLen) {
                            if (_serial->available()) {
                                String nextLine = _serial->readStringUntil('\n');
                                // Satır sonu karakterlerini (\r\n) kaldır ama JSON içindeki boşlukları koru
                                if (nextLine.endsWith("\r")) {
                                    nextLine.remove(nextLine.length() - 1);
                                }
                                payloadStr += nextLine;
                                
                                if (payloadStr.length() % 200 == 0 || payloadStr.length() >= payloadLen) {
                                    Serial.printf("[4G-URC] Reading payload: current=%d/%d bytes (%.1f%%)\n",
                                                 payloadStr.length(), payloadLen, 
                                                 (payloadStr.length() * 100.0f) / payloadLen);
                                }
                            }
                            delay(10);
                        }
                        Serial.printf("[4G-URC] Payload reading complete: totalBytes=%d/%d (in %lu ms)\n",
                                     payloadStr.length(), payloadLen, millis() - startTime);
                    }
                    
                    // Payload'u payloadLen kadar kes (fazlasını at)
                    if (payloadStr.length() > payloadLen) {
                        Serial.printf("[4G-URC] Payload too long (%d > %d), truncating...\n",
                                     payloadStr.length(), payloadLen);
                        payloadStr = payloadStr.substring(0, payloadLen);
                    }
                    
                    Serial.printf("[4G-URC] Final payload: topic=%s, receivedBytes=%d, expectedBytes=%d, match=%s\n", 
                                 topicStr.c_str(), payloadStr.length(), payloadLen,
                                 (payloadStr.length() == payloadLen) ? "YES" : "NO");
                    Serial.printf("[4G-URC] Payload preview (first 200 bytes): %s\n", 
                                 payloadStr.substring(0, min(200, (int)payloadStr.length())).c_str());
                    
                    // Callback'i çağır
                    if (_mqttCallback && payloadStr.length() > 0) {
                        char topic[128] = {0};
                        char payload[2048] = {0}; // Büyük payload'lar için buffer artırıldı
                        strncpy(topic, topicStr.c_str(), sizeof(topic) - 1);
                        strncpy(payload, payloadStr.c_str(), sizeof(payload) - 1);
                        _mqttCallback(topic, payload, payloadStr.length());
                    }
                    
                    // Pending line'ı temizle
                    pendingUrcLine = "";
                } else {
                    // Henüz tam parse edilemedi, bekleyelim
                    continue;
                }
            }
        } else {
            // +MQTTPUBLISH değilse pending line'ı temizle
            pendingUrcLine = "";
        }
    }
}

// ===== GPS Fonksiyonları (NMEA Stream) =====

bool C16QS4GManager::startGPS() {
    if (!_serial || !_initialized) return false;
    
    Serial.println("\n========================================");
    Serial.println("      GPS/GNSS Başlatılıyor");
    Serial.println("========================================\n");
    
    // 1) GPSPORT sıfırla
    Serial.println("[GPS] GPSPORT sıfırlanıyor...");
    _sendATCommandResponse("AT+GPSPORT=0", 2000);
    delay(300);
    
    // 2) GPS durumunu kontrol et
    Serial.println("[GPS] GNSS durum kontrolü...");
    String response = _sendATCommandResponse("AT+CGPS?", 2000);
    delay(300);
    
    if (response.indexOf("+CGPS: 1") >= 0 || response.indexOf("+CGPS:1") >= 0) {
        Serial.println("[GPS] GNSS zaten açık, kapatılıyor...");
        _sendATCommandResponse("AT+CGPS=0", 3000);
        delay(1500);
    }
    
    // 3) GPS'i başlat
    Serial.println("[GPS] GNSS açılıyor (AT+CGPS=1)...");
    response = _sendATCommandResponse("AT+CGPS=1", 3000);
    
    if (response.indexOf("OK") < 0) {
        Serial.println("[GPS] GNSS açma HATASI!");
        return false;
    }
    
    delay(1500);
    
    // 4) NMEA verileri AT portuna yönlendir
    Serial.println("[GPS] NMEA veriler AT portuna yönlendiriliyor (AT+GPSPORT=1)...");
    response = _sendATCommandResponse("AT+GPSPORT=1", 2000);
    
    if (response.indexOf("OK") >= 0) {
        Serial.println("[GPS] NMEA stream AT portuna yönlendirildi!");
    } else {
        Serial.println("[GPS] GPSPORT yönlendirme hatası (devam ediliyor)");
    }
    
    _gpsStarted = true;
    _nmeaBuffer = "";
    
    Serial.println("\n[GPS] GNSS Aktif - Fix bekleniyor...");
    Serial.println("[GPS] Açık gökyüzü altında olun (ilk fix: 30-120 sn)");
    Serial.println("========================================\n");
    
    return true;
}

bool C16QS4GManager::stopGPS() {
    if (!_serial || !_initialized) return false;
    
    String response = _sendATCommandResponse("AT+CGPS=0", 2000);
    _gpsStarted = false;
    _gpsFixValid = false;
    
    return response.indexOf("OK") >= 0;
}

void C16QS4GManager::updateGPS() {
    if (!_serial || !_initialized || !_gpsStarted) return;
    
    // NMEA stream'i oku
    _readNMEAStream();
}

void C16QS4GManager::_readNMEAStream() {
    if (!_serial) return;
    
    while (_serial->available()) {
        char c = _serial->read();
        
        if (c == '\n') {
            String line = _nmeaBuffer;
            line.trim();
            _nmeaBuffer = "";
            
            // NMEA satırlarını parse et
            if (line.indexOf("GNGGA") >= 0 || line.indexOf("$GPGGA") >= 0) {
                if (_parseGNGGA(line)) {
                    _gpsLastUpdate = millis();
                }
            } else if (line.indexOf("GNRMC") >= 0 || line.indexOf("$GPRMC") >= 0) {
                if (_parseGNRMC(line)) {
                    _gpsLastUpdate = millis();
                }
            }
        } else if (c != '\r') {
            _nmeaBuffer += c;
            
            // Buffer taşmasını önle
            if (_nmeaBuffer.length() > 200) {
                _nmeaBuffer.remove(0, 100);
            }
        }
    }
}

bool C16QS4GManager::_parseGNGGA(const String& line) {
    // Format: $GNGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,...
    // veya: +GNGGA,...
    
    int idx = line.indexOf("GNGGA");
    if (idx < 0) idx = line.indexOf("GPGGA");
    if (idx < 0) return false;
    
    idx += 6; // "GNGGA," veya "GPGGA," atla
    
    int starIdx = line.indexOf('*', idx);
    if (starIdx < 0) return false;
    
    String values = line.substring(idx, starIdx);
    
    // Virgülle ayrılmış alanları parse et
    String fields[15];
    int fieldCount = 0;
    int lastIdx = -1;
    
    for (int i = 0; i <= (int)values.length() && fieldCount < 15; i++) {
        if (i == (int)values.length() || values[i] == ',') {
            fields[fieldCount++] = values.substring(lastIdx + 1, i);
            lastIdx = i;
        }
    }
    
    if (fieldCount < 10) {
        return false;
    }
    
    // Time (fields[0])
    if (fields[0].length() == 0) {
        _gpsFixValid = false;
        return false;
    }
    
    // Latitude (fields[1]) - DDMM.MMMM format
    if (fields[1].length() == 0) {
        _gpsFixValid = false;
        return false;
    }
    
    float latRaw = fields[1].toFloat();
    int latDeg = (int)(latRaw / 100);
    float latMin = latRaw - (latDeg * 100);
    _gpsLat = latDeg + (latMin / 60.0);
    
    if (fields[2].length() > 0 && fields[2].charAt(0) == 'S') {
        _gpsLat = -_gpsLat;
    }
    
    // Longitude (fields[3]) - DDDMM.MMMM format
    if (fields[3].length() == 0) {
        _gpsFixValid = false;
        return false;
    }
    
    float lonRaw = fields[3].toFloat();
    int lonDeg = (int)(lonRaw / 100);
    float lonMin = lonRaw - (lonDeg * 100);
    _gpsLon = lonDeg + (lonMin / 60.0);
    
    if (fields[4].length() > 0 && fields[4].charAt(0) == 'W') {
        _gpsLon = -_gpsLon;
    }
    
    // Fix quality (fields[5])
    int fixQuality = fields[5].toInt();
    
    // Satellite count (fields[6])
    _gpsSats = fields[6].toInt();
    
    // HDOP (fields[7])
    _gpsHdop = fields[7].toFloat();
    
    // Altitude (fields[8])
    if (fields[8].length() > 0) {
        _gpsAlt = fields[8].toFloat();
    }
    
    // Fix geçerli mi?
    _gpsFixValid = (fixQuality > 0 && _gpsSats > 0 && _gpsHdop < 20.0);
    
    if (_gpsFixValid) {
        // Fix alındığında log bas (her seferinde değil, sadece ilk veya periyodik)
        static unsigned long lastFixLog = 0;
        if (millis() - lastFixLog > 30000) {
            lastFixLog = millis();
            Serial.println("\n============ GPS FIX ============");
            Serial.printf("[GPS] Konum: %.6f, %.6f\n", _gpsLat, _gpsLon);
            Serial.printf("[GPS] Alt: %.1f m | Sats: %d | HDOP: %.1f\n", _gpsAlt, _gpsSats, _gpsHdop);
            Serial.printf("[GPS] Hız: %.1f km/h | Yön: %.1f°\n", _gpsSpeed, _gpsCourse);
            Serial.printf("[GPS] Zaman: %s | Tarih: %s (UTC)\n", _gpsTime.c_str(), _gpsDate.c_str());
            Serial.println("=================================\n");
        }
    }
    
    return true;
}

bool C16QS4GManager::_parseGNRMC(const String& line) {
    // Format: $GNRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,E/W*checksum
    // Örnek: $GNRMC,123519.00,A,3955.1234,N,03245.5678,E,0.5,45.3,130126,,,A*XX
    
    int idx = line.indexOf("GNRMC");
    if (idx < 0) idx = line.indexOf("GPRMC");
    if (idx < 0) return false;
    
    idx += 6;
    
    int starIdx = line.indexOf('*', idx);
    if (starIdx < 0) return false;
    
    String values = line.substring(idx, starIdx);
    
    String fields[13];
    int fieldCount = 0;
    int lastIdx = -1;
    
    for (int i = 0; i <= (int)values.length() && fieldCount < 13; i++) {
        if (i == (int)values.length() || values[i] == ',') {
            fields[fieldCount++] = values.substring(lastIdx + 1, i);
            lastIdx = i;
        }
    }
    
    if (fieldCount < 10) return false;
    
    // Time (fields[0]) - HHMMSS.SS format
    if (fields[0].length() >= 6) {
        _gpsTime = fields[0].substring(0, 2) + ":" + 
                   fields[0].substring(2, 4) + ":" + 
                   fields[0].substring(4, 6);
    }
    
    // Status: A=active (valid), V=void (invalid)
    if (fields[1].length() == 0 || fields[1].charAt(0) != 'A') {
        return false;
    }
    
    // Latitude (fields[2], fields[3])
    if (fields[2].length() > 0) {
        float latRaw = fields[2].toFloat();
        int latDeg = (int)(latRaw / 100);
        float latMin = latRaw - (latDeg * 100);
        _gpsLat = latDeg + (latMin / 60.0);
        
        if (fields[3].length() > 0 && fields[3].charAt(0) == 'S') {
            _gpsLat = -_gpsLat;
        }
    }
    
    // Longitude (fields[4], fields[5])
    if (fields[4].length() > 0) {
        float lonRaw = fields[4].toFloat();
        int lonDeg = (int)(lonRaw / 100);
        float lonMin = lonRaw - (lonDeg * 100);
        _gpsLon = lonDeg + (lonMin / 60.0);
        
        if (fields[5].length() > 0 && fields[5].charAt(0) == 'W') {
            _gpsLon = -_gpsLon;
        }
    }
    
    // Speed (fields[6]) - knots -> km/h (1 knot = 1.852 km/h)
    if (fields[6].length() > 0) {
        float speedKnots = fields[6].toFloat();
        _gpsSpeed = speedKnots * 1.852;
    }
    
    // Course/Heading (fields[7]) - derece (0-360)
    if (fields[7].length() > 0) {
        _gpsCourse = fields[7].toFloat();
    }
    
    // Date (fields[8]) - DDMMYY format
    if (fields[8].length() >= 6) {
        _gpsDate = fields[8].substring(0, 2) + "/" + 
                   fields[8].substring(2, 4) + "/" + 
                   fields[8].substring(4, 6);
    }
    
    _gpsFixValid = true;
    return true;
}

bool C16QS4GManager::getGPSLocation(float* lat, float* lon, float* alt, int* sats) {
    if (lat) *lat = _gpsLat;
    if (lon) *lon = _gpsLon;
    if (alt) *alt = _gpsAlt;
    if (sats) *sats = _gpsSats;
    
    return _gpsFixValid;
}

bool C16QS4GManager::isGPSFixValid() {
    // Fix 10 saniyeden eski değilse geçerli
    return _gpsFixValid && (millis() - _gpsLastUpdate < 10000);
}

int C16QS4GManager::getGPSSatellites() {
    return _gpsSats;
}

float C16QS4GManager::getGPSHDOP() {
    return _gpsHdop;
}

float C16QS4GManager::getGPSSpeed() {
    return _gpsSpeed;
}

float C16QS4GManager::getGPSCourse() {
    return _gpsCourse;
}

String C16QS4GManager::getGPSTime() {
    return _gpsTime;
}

String C16QS4GManager::getGPSDate() {
    return _gpsDate;
}
