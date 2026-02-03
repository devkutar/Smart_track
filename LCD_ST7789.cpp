#include "LCD_ST7789.h"
#include "hardware.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Backlight LEDC (senin kullandığın)
#include "driver/ledc.h"

#define LEDC_BLK_MODE     LEDC_LOW_SPEED_MODE
#define LEDC_BLK_TIMER    LEDC_TIMER_1
#define LEDC_BLK_CHANNEL  LEDC_CHANNEL_1
#define LEDC_BLK_RES      LEDC_TIMER_8_BIT
#define LEDC_BLK_FREQ_HZ  5000

static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool lcd_ok = false;

// Renk tanımlamaları
#define ST77XX_NAVY        0x000F  // Koyu mavi
#define ST77XX_DARKGREEN   0x0320  // Koyu yeşil
#define ST77XX_DARKGREY    0x4208  // Koyu gri

// Radiuslu ekran için view alanı (köşeler görünmez)
#define VIEW_X        0
#define VIEW_Y        0
#define VIEW_W        SCREEN_W
#define VIEW_H        SCREEN_H
#define STATUS_BAR_H  48  // Üst bar yüksekliği

// Helper fonksiyonlar
static void drawTemperatureIcon(int x, int y) {
  tft.drawRect(x+8, y, 4, 20, ST77XX_WHITE);
  tft.fillRect(x+9, y+1, 2, 18, ST77XX_WHITE);
  tft.fillCircle(x+10, y+22, 6, ST77XX_RED);
  tft.drawCircle(x+10, y+22, 6, ST77XX_WHITE);
  for (int i = 0; i < 3; i++) 
    tft.drawLine(x+12, y+4+i*4, x+15, y+4+i*4, ST77XX_WHITE);
}

static void drawAlarmIcon(int x, int y, bool isHigh) {
  if (isHigh) {
    tft.fillTriangle(x, y+20, x+15, y+20, x+7, y, ST77XX_RED);
    tft.drawTriangle(x, y+20, x+15, y+20, x+7, y, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(x+5, y+8);
    tft.print("!");
  } else {
    tft.fillTriangle(x, y, x+15, y, x+7, y+20, ST77XX_BLUE);
    tft.drawTriangle(x, y, x+15, y, x+7, y+20, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(x+5, y+8);
    tft.print("!");
  }
}

static String formatMacPretty(const String& mac) {
  // Son 6 haneyi al (MAC adresi 12 karakter, son 6 karakter = son 3 byte)
  String macClean = mac;
  macClean.replace(":", "");
  macClean.toUpperCase();
  
  // Son 6 karakteri al
  int len = macClean.length();
  if (len >= 6) {
    String last6 = macClean.substring(len - 6);
    // Format: AA:BB:CC
    String result = last6.substring(0, 2) + ":" + 
                    last6.substring(2, 4) + ":" + 
                    last6.substring(4, 6);
    return result;
  }
  
  // Fallback: tam MAC
  String result = "";
  for (int i = 0; i < macClean.length(); i += 2) {
    if (i > 0) result += ":";
    if (i + 1 < macClean.length()) {
      result += macClean.substring(i, i+2);
    } else {
      result += macClean.substring(i);
    }
  }
  return result;
}

static void drawTopBar(int wifiRssi, bool is4G, const PowerStatus& power, const char* sensorName) {
  if (!lcd_ok) return;
  
  // Üst kısım için gradient arka plan (koyu maviden açık maviye)
  // Sensör adı barı alt 32px'i kaplayacak, bu yüzden sadece üst 16px için gradient çiz
  const int topBarH = 16;  // WiFi/Saat/Batarya için üst kısım yüksekliği
  for (int i = 0; i < topBarH; i++) {
    uint16_t r = 0;
    uint16_t g = i * 2 + 8;
    uint16_t b = i * 3 + 24;
    uint16_t color = tft.color565(r, g, b);
    tft.drawFastHLine(VIEW_X, VIEW_Y + i, VIEW_W, color);
  }
  
  // Üst kısmın alt çizgisi - KALDIRILDI (kullanıcı istediği için)
  // tft.drawFastHLine(VIEW_X, VIEW_Y + topBarH - 1, VIEW_W, ST77XX_WHITE);
  
  // Bar yüksekliği için merkez hesaplama (sadece üst kısım için) - 2mm (8px) aşağı taşındı
  int barCenterY = VIEW_Y + topBarH / 2 + 8;
  
  // ===== ÇEKİM + MOD (SOL) - Çekim simgesi yanında 4G/WiFi yazısı =====
  int signalX = 8;  // Sol kenardan 8px
  int signalBaseY = barCenterY + 8;  // Merkezin biraz altında
  
  // Çekim gücüne göre sinyal çubukları çiz
  int barCount = 0;
  uint16_t barColor = ST77XX_DARKGREY;
  
  if (wifiRssi > -50) {
    barCount = 4;
    barColor = ST77XX_GREEN;
  } else if (wifiRssi > -70) {
    barCount = 3;
    barColor = ST77XX_YELLOW;
  } else if (wifiRssi > -85) {
    barCount = 2;
    barColor = ST77XX_ORANGE;
  } else if (wifiRssi != 0) {
    barCount = 1;
    barColor = ST77XX_RED;
  }
  
  // 4 çubuk, yukarıdan aşağıya artan yükseklik
  int barX = signalX;
  int barBottomY = signalBaseY;
  int barWidth = 4;
  int barSpacing = 5;
  
  for (int i = 0; i < 4; i++) {
    int barHeight = 4 + (i * 3);  // 4, 7, 10, 13px
    int barY = barBottomY - barHeight;
    
    if (i < barCount) {
      // Aktif çubuk - renkli
      tft.fillRect(barX + i * barSpacing, barY, barWidth, barHeight, barColor);
      // İç highlight
      tft.drawPixel(barX + i * barSpacing + 1, barY + 1, ST77XX_WHITE);
    } else {
      // Pasif çubuk - gri
      tft.fillRect(barX + i * barSpacing, barY, barWidth, barHeight, ST77XX_DARKGREY);
    }
  }
  
  // Çekim simgesinin yanında "4G" veya "WiFi" yazısı
  int textX = signalX + (4 * barSpacing) + 4;  // Çubukların yanına
  String modText = is4G ? "4G" : "WiFi";
  tft.setTextColor(is4G ? ST77XX_CYAN : ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(modText, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(textX, barCenterY - h/2);
  tft.print(modText);
  
  // ===== SAAT (ORTA ÜST) - Büyük ve Belirgin =====
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    
    // Text bounds hesapla
    tft.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    
    // Merkeze yerleştir - 2mm (8px) aşağı taşındı
    int timeX = (VIEW_W - w) / 2;
    int timeY = VIEW_Y + 16;  // Üstten 16px (8px + 8px aşağı)
    
    // Hafif gölge efekti
    tft.setTextColor(ST77XX_BLACK, ST77XX_BLACK);
    tft.setCursor(timeX + 1, timeY + 1);
    tft.print(timeStr);
    
    // Ana text
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(timeX, timeY);
    tft.print(timeStr);
  }
  
  // ===== SENSÖR ADI (ORTA ALT) - Üst bar içinde, Mahal ID barı ile aynı ölçülerde =====
  if (sensorName && strlen(sensorName) > 0) {
    const int nameBarH = 32;  // Mahal ID barı ile aynı yükseklik
    int nameBarY = VIEW_Y + STATUS_BAR_H - nameBarH + 16;  // Üst barın alt kısmı + 4mm aşağı (16px)
    
    // Gradient arka plan (turuncu/amber tonları - Mahal ID ile aynı)
    for (int i = 0; i < nameBarH; i++) {
      uint16_t r = i * 3 + 32;
      uint16_t g = i * 2 + 24;
      uint16_t b = i + 8;
      uint16_t color = tft.color565(r, g, b);
      tft.drawFastHLine(VIEW_X, nameBarY + i, VIEW_W, color);
    }
    
    // Üst ve alt çizgiler (kalın) - Mahal ID ile aynı
    tft.drawFastHLine(VIEW_X, nameBarY, VIEW_W, ST77XX_WHITE);
    tft.drawFastHLine(VIEW_X, nameBarY + 1, VIEW_W, ST77XX_YELLOW);
    tft.drawFastHLine(VIEW_X, nameBarY + nameBarH - 1, VIEW_W, ST77XX_WHITE);
    tft.drawFastHLine(VIEW_X, nameBarY + nameBarH - 2, VIEW_W, ST77XX_YELLOW);
    
    // Text - Mahal ID ile aynı stil (size 2)
    String nameStr = String(sensorName);
    tft.setTextSize(2);
    tft.setTextWrap(false);
    
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(nameStr, 0, 0, &x1, &y1, &w, &h);
    
    // Kısaltma kontrolü
    String showName = nameStr;
    int maxWidth = VIEW_W - 40;  // Margin için
    while (w > maxWidth && showName.length() > 3) {
      showName.remove(showName.length() - 1);
      tft.getTextBounds(showName + "...", 0, 0, &x1, &y1, &w, &h);
    }
    if (showName != nameStr) showName += "...";
    
    // Final bounds
    tft.getTextBounds(showName, 0, 0, &x1, &y1, &w, &h);
    
    // Text arka planını temizle - Mahal ID ile aynı
    int textBgX = (VIEW_W - w) / 2 - 2;
    int textBgY = nameBarY + (nameBarH - h) / 2 - 2;
    uint16_t bgColor = tft.color565(nameBarH * 1.5 + 32, nameBarH + 24, nameBarH / 2 + 8);  // Ortalama gradient rengi
    tft.fillRect(textBgX, textBgY, w + 4, h + 4, bgColor);
    
    // Text çiz - Mahal ID ile aynı pozisyonlama
    int nameX = VIEW_X + (VIEW_W - w) / 2;
    tft.setTextColor(ST77XX_WHITE, bgColor);
    tft.setCursor(nameX, nameBarY + (nameBarH - h) / 2 + 2);
    tft.print(showName);
  }
  
  // ===== BATARYA (SAĞ) - Modern ve Büyük =====
  int battX = VIEW_W - 62;  // Sağdan 62px (3mm = 12px daha sola taşındı)
  int battCenterY = barCenterY;
  
  // Batarya simgesi - BÜYÜTÜLDÜ
  int battIconX = battX;
  int battIconY = battCenterY - 10;  // Merkezden yukarı 10px (büyük ikon için)
  int battIconW = 32;  // 24'ten 32'ye büyütüldü
  int battIconH = 18;  // 14'ten 18'e büyütüldü
  
  // Batarya dış çerçevesi - yuvarlatılmış köşeler için ince çizgiler
  tft.drawRect(battIconX, battIconY, battIconW, battIconH, ST77XX_WHITE);
  tft.drawRect(battIconX + 1, battIconY + 1, battIconW - 2, battIconH - 2, ST77XX_WHITE);
  
  // Batarya pozitif ucu (sağda) - büyük ikon için ayarlandı
  tft.fillRect(battIconX + battIconW, battIconY + 5, 3, 8, ST77XX_WHITE);
  
  // Batarya doluluk - yüzdeye göre renk
  int fillWidth = (battIconW - 4) * power.battPct / 100;
  uint16_t fillColor;
  if (power.battPct > 50) {
    fillColor = ST77XX_GREEN;
  } else if (power.battPct > 20) {
    fillColor = ST77XX_YELLOW;
  } else {
    fillColor = ST77XX_RED;
  }
  
  if (fillWidth > 0) {
    // Gradient efekti için hafif highlight
    tft.fillRect(battIconX + 2, battIconY + 2, fillWidth, battIconH - 4, fillColor);
    // İç highlight çizgisi
    if (fillWidth > 2) {
      tft.drawFastHLine(battIconX + 3, battIconY + 3, fillWidth - 4, ST77XX_WHITE);
    }
  }
  
  // Şarj simgesi (mavi + işareti)
  if (power.charging) {
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds("+", 0, 0, &x1, &y1, &w, &h);
    int plusX = battIconX - w - 4;
    int plusY = battCenterY - h / 2;
    
    // Gölge
    tft.setTextColor(ST77XX_BLACK, ST77XX_BLACK);
    tft.setCursor(plusX + 1, plusY + 1);
    tft.print("+");
    
    // Ana simge
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.setCursor(plusX, plusY);
    tft.print("+");
  }
}

static void drawSensorNameBar(const char* sensorName) {
  if (!lcd_ok) return;
  
  // Sensör adı artık üst bar içinde gösteriliyor (Mahal ID barı ile aynı ölçülerde)
  // Bu fonksiyon artık boş, ama çağrıldığı için boş bırakıyoruz
  // Ayırıcı çizgiler üst bar içindeki sensör adı bölümünde zaten var
}

static void drawInfoBar(int bleCount, bool gpsFix, const char* version, bool mqttConnected) {
  if (!lcd_ok) return;
  
  const int barH = 28;
  // Mahal ID barının üstüne yerleştir (MAC footer 20px + Mahal ID bar 32px = 52px)
  int y = VIEW_Y + VIEW_H - 52 - barH;
  
  // Gradient arka plan (koyu mavi tonları)
  for (int i = 0; i < barH; i++) {
    uint16_t r = i * 1;
    uint16_t g = i * 2 + 16;
    uint16_t b = i * 3 + 32;
    uint16_t color = tft.color565(r, g, b);
    tft.drawFastHLine(VIEW_X, y + i, VIEW_W, color);
  }
  
  // Üst ve alt çizgiler
  tft.drawFastHLine(VIEW_X, y, VIEW_W, ST77XX_WHITE);
  tft.drawFastHLine(VIEW_X, y + barH - 1, VIEW_W, ST77XX_WHITE);
  
  tft.setTextSize(1);
  tft.setTextWrap(false);
  
  int textY = y + (barH - 8) / 2; // Text için Y pozisyonu (size 1 için ~8px yükseklik)
  int xPos = VIEW_X + 8;
  
  // 1) BLE: Bulunan kayıtlı Eddystone sayısı
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setCursor(xPos, textY);
  tft.print("BLE:");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print(bleCount);
  xPos += 42;
  
  // 2) GPS ikonu (dolu daire: fix var = yeşil, fix yok = gri)
  int gpsIconX = xPos + 8;
  int gpsIconY = y + barH / 2;
  if (gpsFix) {
    tft.fillCircle(gpsIconX, gpsIconY, 5, ST77XX_GREEN);
    tft.drawCircle(gpsIconX, gpsIconY, 5, ST77XX_WHITE);
  } else {
    tft.fillCircle(gpsIconX, gpsIconY, 5, ST77XX_DARKGREY);
    tft.drawCircle(gpsIconX, gpsIconY, 5, ST77XX_WHITE);
  }
  // GPS yazısı
  tft.setTextColor(gpsFix ? ST77XX_GREEN : ST77XX_DARKGREY, ST77XX_BLACK);
  tft.setCursor(gpsIconX + 10, textY);
  tft.print("GPS");
  xPos = gpsIconX + 35;
  
  // 3) VR: Versiyon numarası
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(xPos, textY);
  tft.print("VR:");
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print(version);
  xPos += 55;
  
  // 4) MQTT durumu (bağlı = yeşil, değil = kırmızı)
  tft.setCursor(xPos, textY);
  if (mqttConnected) {
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    tft.print("MQTT");
  } else {
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.print("MQTT");
  }
}

static void drawBottomBarWithMahal(const char* mahalId) {
  if (!lcd_ok) return;
  
  const int barH = 32;
  int y = VIEW_Y + VIEW_H - barH - 22;  // MAC barı için yer bırak (22px)
  
  // Renkli gradient arka plan (turuncu/amber tonları)
  for (int i = 0; i < barH; i++) {
    uint16_t r = i * 3 + 32;
    uint16_t g = i * 2 + 24;
    uint16_t b = i + 8;
    uint16_t color = tft.color565(r, g, b);
    tft.drawFastHLine(VIEW_X, y + i, VIEW_W, color);
  }
  
  // Üst ve alt çizgiler (kalın)
  tft.drawFastHLine(VIEW_X, y, VIEW_W, ST77XX_WHITE);
  tft.drawFastHLine(VIEW_X, y + 1, VIEW_W, ST77XX_YELLOW);
  tft.drawFastHLine(VIEW_X, y + barH - 1, VIEW_W, ST77XX_WHITE);
  tft.drawFastHLine(VIEW_X, y + barH - 2, VIEW_W, ST77XX_YELLOW);
  
  tft.setTextSize(2);
  tft.setTextWrap(false);
  
  int16_t x1, y1;
  uint16_t w, h;
  String mahalStr = String(mahalId);
  tft.getTextBounds(mahalStr, 0, 0, &x1, &y1, &w, &h);
  
  // Text arka planını temizle
  int textBgX = (VIEW_W - w) / 2 - 2;
  int textBgY = y + (barH - h) / 2 - 2;
  uint16_t bgColor = tft.color565(barH * 1.5 + 32, barH + 24, barH / 2 + 8);  // Ortalama gradient rengi
  tft.fillRect(textBgX, textBgY, w + 4, h + 4, bgColor);
  
  int mahalX = VIEW_X + (VIEW_W - w) / 2;
  tft.setTextColor(ST77XX_WHITE, bgColor);
  tft.setCursor(mahalX, y + (barH - h) / 2 + 2);
  tft.print(mahalStr);
}

static void drawMacFooter(const char* macAddr) {
  if (!lcd_ok) return;
  
  const int footerH = 20;
  int y = SCREEN_H - footerH;
  
  // Koyu mavi gradient arka plan
  for (int i = 0; i < footerH; i++) {
    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = i * 2 + 16;
    uint16_t color = tft.color565(r, g, b);
    tft.drawFastHLine(VIEW_X, y + i, VIEW_W, color);
  }
  
  // Üst çizgi - KALDIRILDI (kullanıcı istediği için)
  // tft.drawFastHLine(VIEW_X, y, VIEW_W, ST77XX_CYAN);
  
  String macTxt = formatMacPretty(String(macAddr));
  
  tft.setTextSize(2);  // 1'den 2'ye büyütüldü
  tft.setTextWrap(false);
  
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(macTxt, 0, 0, &x1, &y1, &w, &h);
  
  int textY = y + (footerH - h) / 2;
  int textX = (SCREEN_W - w) / 2;
  
  // Text arka planını temizle
  uint16_t bgColor = tft.color565(0, 0, footerH + 16);
  tft.fillRect(textX - 2, textY - 2, w + 4, h + 4, bgColor);
  
  tft.setTextColor(ST77XX_CYAN, bgColor);
  tft.setCursor(textX, textY);
  tft.print(macTxt);
}

static void setBacklightPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint32_t maxDuty = (1UL << LEDC_BLK_RES) - 1; // 255
  uint32_t duty    = (maxDuty * percent) / 100;
  ledc_set_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL, duty);
  ledc_update_duty(LEDC_BLK_MODE, LEDC_BLK_CHANNEL);
}

bool LCD_begin() {
  // SPI
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // BLK LEDC
  ledc_timer_config_t tcfg = {
    .speed_mode      = LEDC_BLK_MODE,
    .duty_resolution = LEDC_BLK_RES,
    .timer_num       = LEDC_BLK_TIMER,
    .freq_hz         = LEDC_BLK_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK
  };
  ledc_timer_config(&tcfg);

  ledc_channel_config_t ccfg = {
    .gpio_num   = TFT_BLK,
    .speed_mode = LEDC_BLK_MODE,
    .channel    = LEDC_BLK_CHANNEL,
    .intr_type  = LEDC_INTR_DISABLE,
    .timer_sel  = LEDC_BLK_TIMER,
    .duty       = 0,
    .hpoint     = 0
  };
  ledc_channel_config(&ccfg);
  setBacklightPercent(15);

  // HW reset
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);  delay(20);
  digitalWrite(TFT_RST, HIGH); delay(120);

  tft.setSPISpeed(27000000);
  tft.init(SCREEN_W, SCREEN_H, SPI_MODE3);
  tft.setRotation(2);
  tft.invertDisplay(false);

  uint8_t madctl;
  tft.readcommand8(0x36);  // mevcut MADCTL değerini oku (bazı sürümlerde 0 dönebilir)
  madctl = 0x00;           // sıfırdan başla
  madctl |= 0x08;          // BGR renk sırası
  madctl |= 0x40;          // MX bitini 1 yap → X mirror kapalı (normal yön)
  tft.sendCommand(0x36, &madctl, 1);

  // Quick test
  tft.fillScreen(ST77XX_BLACK);
  lcd_ok = true;
  return true;
}

void LCD_sleep() {
  // 1) Backlight kapat
  setBacklightPercent(0);       // sende var

  // 2) Display off + sleep in
  tft.sendCommand(0x28, (const uint8_t*)NULL, 0);
  delay(5);
  tft.sendCommand(0x10, (const uint8_t*)NULL, 0);
  delay(120);

  // 3) İstersen BLK pinini low sabitle
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, LOW);
}


void LCD_showTemp(float tempC, const char* reason) {
  if (!lcd_ok) return;

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print("WAKE: ");
  tft.print(reason);

  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setCursor(20, 110);
  tft.print(tempC, 2);

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(190, 118);
  tft.print("C");
}

void LCD_showStatus(float tempC, const PowerStatus& power, const Cfg& cfg, 
                    int wifiRssi, bool is4G, uint8_t alarmState, const char* macAddr,
                    uint32_t bootCount, float latitude, float longitude,
                    float gpsSpeed, float gpsCourse,
                    int gpsSats, float gpsHdop, bool gpsFix,
                    int bleCount, bool mqttConnected, const char* version) {
  if (!lcd_ok) return;

  // Radiuslu ekran için siyah arka plan
  tft.fillScreen(ST77XX_BLACK);
  
  // Üst bar (Çekim | Saat + Sensör Adı | Batarya)
  drawTopBar(wifiRssi, is4G, power, cfg.internalSensorName);
  
  // Sensör adı barı - sadece tam çizgiler
  drawSensorNameBar(cfg.internalSensorName);
  
  // Merkez içerik alanı (düzgün spacing ile)
  // Sensör adı artık üst bar içinde (32px yükseklikte), ekstra bar yok
  const int contentY = VIEW_Y + STATUS_BAR_H + 35; // Üst bar + gap + margin + 5mm aşağı (20px)
  
  // Sıcaklık ikonu
  drawTemperatureIcon(20, contentY + 4);
  
  // Alarm renk analizi
  uint16_t tempColor = ST77XX_GREEN;
  bool showAlarm = false;
  bool isHigh = false;
  
  if (tempC != -99.0f && tempC != 255.0f) {
    if (tempC > cfg.tempHigh) {
      tempColor = ST77XX_RED;
      showAlarm = true;
      isHigh = true;
    } else if (tempC < cfg.tempLow) {
      tempColor = ST77XX_BLUE;
      showAlarm = true;
      isHigh = false;
    }
  }
  
  // Sıcaklık değeri
  int textX = 60;
  int textY = contentY + 12;  // 3mm aşağı (yaklaşık 12px)
  
  tft.setFont();
  tft.setTextSize(4);
  tft.setTextWrap(false);
  tft.setCursor(textX, textY);
  
  if (tempC == -99.0f || tempC == 255.0f) {
    // Sensör yok durumu
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.print("Sensor Yok");
  } else {
    // Normal
    tft.setTextColor(tempColor, ST77XX_BLACK);
    tft.print(tempC, 1);
  }
  
  // Derece sembolü
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(textX + 120, textY + 6);
  tft.print("C");
  
  // Alarm ikonu
  if (showAlarm && tempC != -99.0f && tempC != 255.0f) {
    drawAlarmIcon(SCREEN_W - 36, contentY + 8, isHigh);
  }
  
  // Info bar (BLE, GPS, VR, MQTT durumu) - Sıcaklığın altında, Mahal ID'nin üstünde
  drawInfoBar(bleCount, gpsFix, version, mqttConnected);
  
  // GPS bilgileri (sıcaklığın altında sırayla)
  int gpsY = textY + 45;
  int gpsLineH = 11; // Her satır arası
  
  tft.setTextSize(1);
  
  if (gpsFix && (latitude != 0.0 || longitude != 0.0)) {
    // ===== GPS FIX VAR - TÜM VERİLERİ GÖSTER =====
    
    // Satır 1: Konum (Lat, Lon)
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    char line1[40];
    snprintf(line1, sizeof(line1), "Lat: %.6f", latitude);
    tft.setCursor(10, gpsY);
    tft.print(line1);
    
    // Satır 2: Longitude
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    char line2[40];
    snprintf(line2, sizeof(line2), "Lon: %.6f", longitude);
    tft.setCursor(10, gpsY + gpsLineH);
    tft.print(line2);
    
    // Satır 3: Hız ve Yön
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    char line3[40];
    snprintf(line3, sizeof(line3), "Hiz: %.1f km/h  Yon: %.0f", gpsSpeed, gpsCourse);
    tft.setCursor(10, gpsY + gpsLineH * 2);
    tft.print(line3);
    // Derece sembolü
    tft.drawCircle(180, gpsY + gpsLineH * 2, 2, ST77XX_YELLOW);
    
    // Satır 4: Uydu ve HDOP
    tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    char line4[40];
    snprintf(line4, sizeof(line4), "Sat: %d  HDOP: %.1f", gpsSats, gpsHdop);
    tft.setCursor(10, gpsY + gpsLineH * 3);
    tft.print(line4);
    
  } else {
    // ===== GPS FIX YOK - DEBUG BİLGİSİ =====
    
    // Satır 1: GPS durumu
    tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
    tft.setCursor(10, gpsY);
    tft.print("GPS: Fix bekleniyor...");
    
    // Satır 2: Uydu sayısı
    tft.setTextColor(ST77XX_DARKGREY, ST77XX_BLACK);
    char satStr[32];
    snprintf(satStr, sizeof(satStr), "Uydu: %d  HDOP: %.1f", gpsSats, gpsHdop);
    tft.setCursor(10, gpsY + gpsLineH);
    tft.print(satStr);
    
    // Satır 3: Bilgi
    tft.setTextColor(ST77XX_DARKGREY, ST77XX_BLACK);
    tft.setCursor(10, gpsY + gpsLineH * 2);
    tft.print("Acik gokyuzu gerekli");
  }
  
  // Alt barlar
  drawBottomBarWithMahal(cfg.internalMahalId);
  drawMacFooter(macAddr);
  
  // Font'u sıfırla
  tft.setFont();
  tft.setTextSize(1);
}

// ===== OTA UPDATE EKRANLARI =====

void LCD_showUpdateProgress(int currentBytes, int totalBytes, const char* status) {
  if (!lcd_ok) return;

  tft.fillScreen(ST77XX_BLACK);

  // Başlık
  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(VIEW_X + 20, VIEW_Y + 30);
  tft.print("FIRMWARE UPDATE");

  // Status mesajı (eğer varsa)
  if (status && strlen(status) > 0) {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(VIEW_X + 20, VIEW_Y + 60);
    tft.print(status);
  }

  // Progress bar çerçevesi
  int barX = VIEW_X + 20;
  int barY = VIEW_Y + 120;
  int barW = VIEW_W - 40;
  int barH = 30;
  
  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);
  
  // Progress bar doldurma
  if (totalBytes > 0) {
    int fillW = (barW - 4) * currentBytes / totalBytes;
    if (fillW > 0) {
      tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, ST77XX_GREEN);
    }
  }

  // Yüzde gösterimi
  int percent = (totalBytes > 0) ? (currentBytes * 100 / totalBytes) : 0;
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setTextSize(3);
  
  char percentStr[8];
  snprintf(percentStr, sizeof(percentStr), "%d%%", percent);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(VIEW_X + (VIEW_W - w) / 2, barY + barH + 20);
  tft.print(percentStr);

  // Byte bilgisi
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(1);
  char bytesStr[32];
  snprintf(bytesStr, sizeof(bytesStr), "%d / %d KB", 
           currentBytes / 1024, totalBytes / 1024);
  tft.getTextBounds(bytesStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(VIEW_X + (VIEW_W - w) / 2, barY + barH + 50);
  tft.print(bytesStr);
}

void LCD_showUpdateSuccess() {
  if (!lcd_ok) return;

  tft.fillScreen(ST77XX_BLACK);

  // Başarı mesajı
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(VIEW_X + 30, VIEW_Y + 80);
  tft.print("SUCCESS!");

  // "Restarting..." mesajı
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(VIEW_X + 30, VIEW_Y + 140);
  tft.print("Restarting...");

  // Checkmark simgesi (basit)
  int centerX = VIEW_W / 2;
  int centerY = VIEW_Y + 200;
  tft.drawCircle(centerX, centerY, 25, ST77XX_GREEN);
  tft.drawLine(centerX - 10, centerY, centerX - 2, centerY + 8, ST77XX_GREEN);
  tft.drawLine(centerX - 2, centerY + 8, centerX + 10, centerY - 8, ST77XX_GREEN);
}

void LCD_showUpdateFailed(const char* reason) {
  if (!lcd_ok) return;

  tft.fillScreen(ST77XX_BLACK);

  // Hata başlığı
  tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(VIEW_X + 40, VIEW_Y + 60);
  tft.print("FAILED!");

  // Hata nedeni
  if (reason && strlen(reason) > 0) {
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(VIEW_X + 20, VIEW_Y + 120);
    
    // Uzun mesajları kısalt veya wrap et
    String reasonStr = String(reason);
    if (reasonStr.length() > 30) {
      reasonStr = reasonStr.substring(0, 27) + "...";
    }
    tft.print(reasonStr);
  }

  // X simgesi (basit)
  int centerX = VIEW_W / 2;
  int centerY = VIEW_Y + 200;
  tft.drawCircle(centerX, centerY, 25, ST77XX_RED);
  tft.drawLine(centerX - 10, centerY - 10, centerX + 10, centerY + 10, ST77XX_RED);
  tft.drawLine(centerX + 10, centerY - 10, centerX - 10, centerY + 10, ST77XX_RED);
}

// ===== BLE SENSÖR GÖSTERİMİ =====
void LCD_showBLESensor(const char* sensorName, const char* sensorMac, float temp, 
                       int battPct, int rssi, const PowerStatus& power, 
                       int wifiRssi, bool is4G, const char* gatewayMac,
                       float tempHigh, float tempLow, uint8_t alarmState) {
  if (!lcd_ok) return;
  
  // Ekranı temizle
  tft.fillScreen(ST77XX_BLACK);
  
  // Alarm durumuna göre arka plan rengi
  uint16_t bgColor = ST77XX_BLACK;
  if (alarmState == 1) { // High alarm
    bgColor = tft.color565(40, 0, 0); // Koyu kırmızı
  } else if (alarmState == 2) { // Low alarm
    bgColor = tft.color565(0, 0, 40); // Koyu mavi
  }
  if (bgColor != ST77XX_BLACK) {
    tft.fillScreen(bgColor);
  }
  
  // Üst bar - sensör adı ve gateway batarya
  PowerStatus blePower = power; // Gateway power durumu
  drawTopBar(wifiRssi, is4G, blePower, sensorName);
  
  int16_t x1, y1;
  uint16_t w, h;
  
  // ===== SICAKLIK (Büyük) =====
  int tempY = VIEW_Y + STATUS_BAR_H + 20;
  
  // Termometre ikonu
  drawTemperatureIcon(VIEW_X + 15, tempY + 15);
  
  // Sıcaklık değeri (büyük)
  char tempStr[16];
  if (temp > -40 && temp < 85) {
    snprintf(tempStr, sizeof(tempStr), "%.1f", temp);
  } else {
    snprintf(tempStr, sizeof(tempStr), "--.-");
  }
  
  // Renk: alarm durumuna göre
  uint16_t tempColor = ST77XX_WHITE;
  if (alarmState == 1) tempColor = ST77XX_RED;
  else if (alarmState == 2) tempColor = ST77XX_CYAN;
  
  tft.setTextSize(5);
  tft.setTextColor(tempColor, bgColor);
  tft.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
  int tempX = VIEW_X + (VIEW_W - w - 30) / 2 + 20;
  tft.setCursor(tempX, tempY + 20);
  tft.print(tempStr);
  
  // Derece simgesi
  tft.setTextSize(2);
  tft.setCursor(tempX + w + 2, tempY + 20);
  tft.print("C");
  
  // ===== ALARM LİMİTLERİ + BATARYA & RSSI (sıcaklığın altında, font büyük) =====
  int infoY = tempY + 85;
  tft.setTextSize(2);  // Daha büyük font
  
  // Satır 1: Limitler (H: ve L:)
  // Üst limit (kırmızı)
  char highStr[16];
  snprintf(highStr, sizeof(highStr), "H:%.0f", tempHigh);
  tft.setTextColor(ST77XX_RED, bgColor);
  tft.setCursor(tempX, infoY);
  tft.print(highStr);
  
  // Alt limit (cyan)
  char lowStr[16];
  snprintf(lowStr, sizeof(lowStr), "L:%.0f", tempLow);
  tft.setTextColor(ST77XX_CYAN, bgColor);
  tft.setCursor(tempX + 90, infoY);  // +20px (~5mm) aralık
  tft.print(lowStr);
  
  // Satır 2: Batarya ve RSSI
  int statsY = infoY + 24;
  
  // Sensör bataryası
  char battStr[16];
  snprintf(battStr, sizeof(battStr), "B:%d%%", battPct);
  tft.setTextColor(battPct > 20 ? ST77XX_GREEN : ST77XX_RED, bgColor);
  tft.setCursor(tempX, statsY);
  tft.print(battStr);
  
  // RSSI
  char rssiStr[16];
  snprintf(rssiStr, sizeof(rssiStr), "R:%d", rssi);
  tft.setTextColor(ST77XX_WHITE, bgColor);
  tft.setCursor(tempX + 90, statsY);  // +20px (~5mm) aralık
  tft.print(rssiStr);
  
  // ===== ALT BAR - GATEWAY MAC =====
  int footerY = VIEW_Y + VIEW_H - 20;
  tft.fillRect(VIEW_X, footerY, VIEW_W, 20, ST77XX_DARKGREY);
  tft.setTextColor(ST77XX_WHITE, ST77XX_DARKGREY);
  tft.setTextSize(1);
  
  String gwMacShort = "GW:" + formatMacPretty(String(gatewayMac));
  tft.getTextBounds(gwMacShort, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(VIEW_X + (VIEW_W - w) / 2, footerY + (20 - h) / 2);
  tft.print(gwMacShort);
  
  // Font'u sıfırla
  tft.setFont();
  tft.setTextSize(1);
}