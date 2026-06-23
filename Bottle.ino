#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <HTTPUpdate.h>
#include <esp_now.h>  // 🚀 ไลบรารีวิทยุสื่อสาร

// 🚀 เปลี่ยนเป็น MAC Address ของบอร์ดเสียงที่คุณจดไว้
uint8_t audioBoardAddress[] = {0xE0, 0x72, 0xA1, 0xD6, 0xDE, 0xE4};

// โครงสร้างข้อความวิทยุ
typedef struct struct_message {
  int command;
} struct_message;
struct_message audioMsg;
esp_now_peer_info_t peerInfo;

// ฟังก์ชันยิงคำสั่งเสียง
void sendAudioCommand(int cmd) {
  audioMsg.command = cmd;
  esp_now_send(audioBoardAddress, (uint8_t*)&audioMsg, sizeof(audioMsg));
}

// ==========================================
// การตั้งค่าความจุ และ Wi-Fi
// ==========================================
const int MAX_BIN_CAPACITY = 150;
int currentBinCount = 0;

struct WiFiCreds {
  const char* ssid;
  const char* password;
};
WiFiCreds networks[] = {
  { "Hans", "12345678" },
  { "Thanan's iPhone", "88888888" },
  { "SARAWUT_2.4GHz", "88628458" },
  { "99/1257", "0843767150" }
};
const int numNetworks = 4;

String lineToken = "H8KDzT3fuhnwiy6AUvuXmE+ECnophjFhYjF8scPAa7ixQBXHeyCoMFAe4GORuXNOEawZ83YoGmshZFjuoYvfj1i50O3o2ZQfm/gHuPKl3xN6/jUF+8ydBVg8n3U9eXxEXBoKldCoMrr4pjLoteqjPAdB04t89/1O/w1cDnyilFU=";
String userId = "U267b24baf4568cdeb1946ebf318d1e38";
String googleScriptURL = "https://script.google.com/macros/s/AKfycbxI8vNijOMuvZ4K6rptEq7fsJElWefXLgkldHTr3Mxnp22u92Qp4lwqTJ0fBOXF4ZD9/exec?";
String githubFirmwareURL = "https://raw.githubusercontent.com/YOUR_USERNAME/YOUR_REPO/main/firmware.bin";

// ==========================================
// Pins & Hardware
// ==========================================
#define TFT_DC 9
#define TFT_CS 10
#define TFT_MOSI 11
#define TFT_CLK 12
#define TFT_MISO 13
#define TFT_RST 14

#define ECO_DARK 0x0000
#define ECO_LIME 0xDEFB
#define ECO_CYAN 0x07FF
#define ECO_GREY 0x3186

SPIClass customSPI(FSPI);
Adafruit_ILI9341 tft = Adafruit_ILI9341(&customSPI, TFT_DC, TFT_CS, TFT_RST);
Preferences prefs;

const int IRPin = 7;
const int RGBPin = 48;
Servo topServo;
#define SERVO_PIN 6

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = { { '1', '2', '3' }, { '4', '5', '6' }, { '7', '8', '9' }, { '*', '0', '#' } };
byte rowPins[ROWS] = { 15, 3, 17, 18 };
byte colPins[COLS] = { 8, 16, 46 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ==========================================
// FSM & Data
// ==========================================
enum SystemState { STATE_IDLE,
                   STATE_VERIFYING,
                   STATE_ACTIVE,
                   STATE_SAVING,
                   STATE_FULL,
                   STATE_RESETTING,
                   STATE_UPDATING };
SystemState currentState = STATE_IDLE;
bool isFullScreenDrawn = false;

String enteredPhone = "";
int sessionBottles = 0;
int villageTotal = 850;
const int VILLAGE_GOAL = 1500;

volatile bool triggerVerify = false;
volatile bool triggerSave = false;
volatile bool triggerAlert = false;
volatile bool triggerReset = false;
volatile bool triggerUpdate = false;
volatile int networkResult = 0;
TaskHandle_t TaskCore0;

const int MAX_VIP = 25;
String knownUsers[MAX_VIP];
int numKnownUsers = 0;
String currentGuess = "";
volatile bool triggerSyncVIP = true;

void parseVIPData(String payload) {
  numKnownUsers = 0;
  knownUsers[numKnownUsers++] = "0000000000";
  knownUsers[numKnownUsers++] = "8888888888";
  knownUsers[numKnownUsers++] = "9999999999";

  int start = 0;
  int end = payload.indexOf(',');
  while (end != -1 && numKnownUsers < MAX_VIP) {
    String p = payload.substring(start, end);
    p.trim();
    if (p.length() > 5) knownUsers[numKnownUsers++] = p;
    start = end + 1;
    end = payload.indexOf(',', start);
  }
  if (start < payload.length() && numKnownUsers < MAX_VIP) {
    String p = payload.substring(start);
    p.trim();
    if (p.length() > 5) knownUsers[numKnownUsers++] = p;
  }
}

// ==========================================
// CORE 0: Network
// ==========================================
void networkTask(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(2000));
  bool otaSetup = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      maintainWiFi();
      otaSetup = false;
      triggerSyncVIP = true;
    }
    if (WiFi.status() == WL_CONNECTED) {
      if (!otaSetup) {
        ArduinoOTA.setHostname("EcoBottles-BangKhian");
        ArduinoOTA.setPassword("admin1234");
        ArduinoOTA.begin();
        otaSetup = true;
      }
      ArduinoOTA.handle();
    }

    if (triggerSyncVIP && WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, googleScriptURL + "action=getVIPs");
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      if (http.GET() > 0) { parseVIPData(http.getString()); }
      http.end();
      triggerSyncVIP = false;
    }

    if (triggerVerify) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, googleScriptURL + "action=verify&phone=" + enteredPhone);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        networkResult = (http.GET() > 0 && http.getString().indexOf("VERIFIED") >= 0) ? 1 : 2;
        http.end();
      } else networkResult = 2;
      triggerVerify = false;
    }

    if (triggerSave) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, googleScriptURL + "action=save&phone=" + enteredPhone + "&bottles=" + String(sessionBottles));
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.GET();
        http.end();
      }
      networkResult = 1;
      triggerSave = false;
    }

    if (triggerAlert) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin("https://api.line.me/v2/bot/message/push");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + lineToken);
        String msg = "⚠️ [แจ้งเตือนด่วน] ตู้ Eco Bottles เต็มแล้ว! (" + String(currentBinCount) + "/" + String(MAX_BIN_CAPACITY) + " ขวด)";
        http.POST("{\"to\":\"" + userId + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + msg + "\"}]}");
        http.end();
      }
      networkResult = 1;
      triggerAlert = false;
    }

    if (triggerReset) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin("https://api.line.me/v2/bot/message/push");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + lineToken);
        String msg = "✅ [ระบบแอดมิน] ทำการล้างข้อมูลขวดในตู้เรียบร้อย เริ่มนับ 0 ใหม่ พร้อมให้บริการแล้วครับ";
        http.POST("{\"to\":\"" + userId + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + msg + "\"}]}");
        http.end();
      }
      networkResult = 1;
      triggerReset = false;
    }

    if (triggerUpdate) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();
        httpUpdate.rebootOnUpdate(true);
        networkResult = (httpUpdate.update(client, githubFirmwareURL) == HTTP_UPDATE_FAILED) ? 2 : 1;
      } else networkResult = 2;
      triggerUpdate = false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(200));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  for (int i = 0; i < numNetworks; i++) {
    WiFi.begin(networks[i].ssid, networks[i].password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      vTaskDelay(pdMS_TO_TICKS(500));
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ==========================================
// UI Functions
// ==========================================
void drawVillageProgress() {
  int barWidth = 280;
  int progress = map(villageTotal, 0, VILLAGE_GOAL, 0, barWidth);
  if (progress > barWidth) progress = barWidth;
  tft.drawRoundRect(20, 165, barWidth, 15, 8, ECO_LIME);
  tft.fillRoundRect(22, 167, progress - 4, 11, 6, ECO_CYAN);
  tft.setCursor(20, 150);
  tft.setTextColor(ECO_LIME);
  tft.setTextSize(1);
  tft.print("VILLAGE GOAL: ");
  tft.print(villageTotal);
  tft.print("/");
  tft.print(VILLAGE_GOAL);
}

void drawLockedScreen() {
  tft.fillScreen(ECO_DARK);
  tft.fillRect(0, 0, 320, 4, ECO_LIME);
  tft.setCursor(15, 15);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print("BANG KHIAN ");
  tft.setTextColor(ECO_LIME);
  tft.print("ECO-HUB");
  tft.fillRoundRect(15, 50, 290, 80, 15, ECO_GREY);
  tft.drawRoundRect(15, 50, 290, 80, 15, ECO_LIME);
  tft.setCursor(35, 65);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.print("IDENTIFICATION REQUIRED");
  drawVillageProgress();
  tft.fillRect(0, 215, 320, 25, 0x0841);
  tft.setCursor(65, 222);
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  tft.print("PRESS NUMBER TO START SESSION");
  updatePhoneDisplay();
}

void updatePhoneDisplay() {
  tft.fillRect(10, 85, 300, 50, ECO_DARK);
  tft.fillRect(30, 85, 260, 30, ECO_GREY);
  tft.setCursor(35, 90);
  tft.setTextSize(3);
  currentGuess = "";
  if (enteredPhone == "") {
    tft.setTextColor(ECO_LIME);
    tft.print("___-___-____");
    return;
  }

  if (enteredPhone.length() == 10) {
    String displayStr = enteredPhone;
    if (enteredPhone == "0000000000") displayStr = "[ADMIN: RESET]";
    else if (enteredPhone == "9999999999") displayStr = "[ADMIN: UPDATE]";
    else if (enteredPhone == "8888888888") displayStr = "[ADMIN: TEST FULL]";
    else displayStr = enteredPhone.substring(0, 3) + "-XXX-" + enteredPhone.substring(7);
    tft.setTextColor(ECO_LIME);
    tft.print(displayStr);
  } else {
    for (int i = 0; i < numKnownUsers; i++) {
      if (knownUsers[i].startsWith(enteredPhone)) {
        currentGuess = knownUsers[i];
        break;
      }
    }
    if (currentGuess != "") {
      tft.setTextColor(ECO_LIME);
      tft.print(enteredPhone);
      tft.setTextColor(ILI9341_DARKGREY);
      tft.print(currentGuess.substring(enteredPhone.length()));
      tft.setCursor(75, 122);
      tft.setTextColor(ECO_CYAN);
      tft.setTextSize(1);
      tft.print("Press [#] to Auto-Complete");
    } else {
      tft.setTextColor(ECO_LIME);
      tft.print(enteredPhone);
    }
  }
}

void drawUnlockedScreen() {
  tft.fillScreen(ECO_DARK);
  tft.fillRect(0, 0, 320, 40, ECO_CYAN);
  tft.setCursor(15, 12);
  tft.setTextColor(ECO_DARK);
  tft.setTextSize(2);
  tft.print("HERO SESSION ACTIVE");
  tft.drawRoundRect(10, 55, 300, 140, 15, ECO_CYAN);
  tft.setCursor(25, 75);
  tft.setTextColor(ECO_GREY);
  tft.setTextSize(1);
  tft.print("RECYCLER ID: ");
  tft.setTextColor(ILI9341_WHITE);
  tft.print(enteredPhone.substring(0, 3) + "-XXX-" + enteredPhone.substring(7));
  tft.setCursor(25, 110);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(4);
  tft.print("BOTTLES:");
  updateBottleCountOnly();
  tft.setCursor(45, 210);
  tft.setTextColor(ECO_CYAN);
  tft.setTextSize(1);
  tft.print("INSERT BOTTLES NOW  |  [*] FINISH");
}

void updateBottleCountOnly() {
  tft.fillRect(210, 105, 80, 40, ECO_DARK);
  tft.setCursor(215, 110);
  tft.setTextColor(ECO_LIME);
  tft.setTextSize(4);
  tft.print(sessionBottles);
}

void drawFullScreen() {
  tft.fillScreen(0xF800);
  tft.drawRect(10, 10, 300, 220, ILI9341_WHITE);
  tft.setCursor(20, 60);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(4);
  tft.print("BIN FULL");
  tft.setCursor(20, 110);
  tft.setTextSize(2);
  tft.print("Out of Service.");
  tft.setCursor(20, 140);
  tft.print("Capacity: " + String(currentBinCount) + "/" + String(MAX_BIN_CAPACITY));
  tft.setCursor(20, 190);
  tft.setTextSize(1);
  tft.print("Admin: Enter 0000000000 to Reset Bin");
}

// ==========================================
// CORE 1: MAIN LOOP
// ==========================================
void loop() {
  static unsigned long lastColorUpdate = 0;
  if (millis() - lastColorUpdate > 1000) {
    if (currentState == STATE_IDLE) {
      if (WiFi.status() == WL_CONNECTED) {
        if (triggerSyncVIP) neopixelWrite(RGBPin, 0, 40, 0);
        else neopixelWrite(RGBPin, 0, 10, 0);
      } else neopixelWrite(RGBPin, 20, 5, 0);
    } else if (currentState == STATE_FULL) {
      neopixelWrite(RGBPin, 20, 0, 0);
    }
    lastColorUpdate = millis();
  }

  switch (currentState) {
    case STATE_IDLE:
      {
        if (currentBinCount >= MAX_BIN_CAPACITY) {
          enteredPhone = "";
          isFullScreenDrawn = false;
          networkResult = 0;
          triggerAlert = true;
          sendAudioCommand(9);  // 🚀 สั่งวิทยุ: เสียงไซเรนตู้เต็ม
          currentState = STATE_FULL;
          break;
        }
        char key = keypad.getKey();
        if (key) {
          if (key == '*') {
            if (enteredPhone.length() > 0) enteredPhone.remove(enteredPhone.length() - 1);
            updatePhoneDisplay();
          } else if (key == '#') {
            if (currentGuess != "") {
              enteredPhone = currentGuess;
              updatePhoneDisplay();
            }
          } else if (enteredPhone.length() < 10) {
            enteredPhone += key;
            updatePhoneDisplay();
          }

          if (enteredPhone.length() == 10) {
            vTaskDelay(pdMS_TO_TICKS(200));
            if (enteredPhone == "9999999999") {
              tft.fillScreen(ECO_CYAN);
              tft.drawRect(10, 10, 300, 220, ECO_DARK);
              tft.setCursor(30, 80);
              tft.setTextColor(ECO_DARK);
              tft.setTextSize(3);
              tft.print("UPDATING FIRMWARE");
              tft.setCursor(30, 130);
              tft.setTextSize(2);
              tft.print("Downloading from GitHub...");
              networkResult = 0;
              triggerUpdate = true;
              currentState = STATE_UPDATING;
            } else if (enteredPhone == "8888888888") {
              tft.fillScreen(0xF800);
              tft.setCursor(40, 110);
              tft.setTextColor(ILI9341_WHITE);
              tft.setTextSize(2);
              tft.print("FORCING BIN FULL...");
              currentBinCount = MAX_BIN_CAPACITY;
              prefs.putInt("binCount", currentBinCount);
              isFullScreenDrawn = false;
              networkResult = 0;
              triggerAlert = true;
              sendAudioCommand(9);  // 🚀 สั่งวิทยุ: เสียงไซเรน
              currentState = STATE_FULL;
            } else {
              tft.fillScreen(ECO_DARK);
              tft.setCursor(70, 110);
              tft.setTextColor(ECO_CYAN);
              tft.setTextSize(3);
              tft.print("VERIFYING...");
              sendAudioCommand(1);  // 🚀 สั่งวิทยุ: เสียงกำลังตรวจสอบ
              networkResult = 0;
              triggerVerify = true;
              currentState = STATE_VERIFYING;
            }
          }
        }
        break;
      }

    case STATE_FULL:
      {
        if (!isFullScreenDrawn) {
          drawFullScreen();
          isFullScreenDrawn = true;
        }
        char key = keypad.getKey();
        if (key) {
          if (key == '*') {
            if (enteredPhone.length() > 0) enteredPhone.remove(enteredPhone.length() - 1);
          } else if (key != '#' && enteredPhone.length() < 10) {
            enteredPhone += key;
            tft.fillRect(20, 185, 280, 25, ECO_DARK);
            tft.setCursor(25, 190);
            tft.setTextColor(ILI9341_WHITE);
            tft.setTextSize(2);
            String mask = "";
            for (int i = 0; i < enteredPhone.length(); i++) mask += "*";
            tft.print(mask);

            if (enteredPhone == "0000000000") {
              tft.fillScreen(ECO_CYAN);
              tft.setCursor(30, 100);
              tft.setTextColor(ECO_DARK);
              tft.setTextSize(3);
              tft.print("RESETTING BIN...");
              currentBinCount = 0;
              prefs.putInt("binCount", 0);
              networkResult = 0;
              triggerReset = true;
              currentState = STATE_RESETTING;
            } else if (enteredPhone.length() == 10) {
              enteredPhone = "";
              drawFullScreen();
            }
          }
        }
        break;
      }

    case STATE_VERIFYING:
      {
        if (networkResult == 1) {
          sessionBottles = 0;
          topServo.attach(SERVO_PIN);
          topServo.write(85);
          drawUnlockedScreen();
          sendAudioCommand(2);  // 🚀 สั่งวิทยุ: เสียงสำเร็จ ยืนยันตัวตนผ่าน
          currentState = STATE_ACTIVE;
        } else if (networkResult == 2) {
          tft.fillScreen(0x8000);
          tft.setCursor(40, 100);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(3);
          tft.print("USER NOT FOUND");
          sendAudioCommand(5);  // 🚀 สั่งวิทยุ: เสียง Error หาผู้ใช้ไม่พบ
          delay(3000);
          enteredPhone = "";
          drawLockedScreen();
          currentState = STATE_IDLE;
        }
        break;
      }

case STATE_ACTIVE:
      {
        // 🚀 แก้ไข: ลดเวลา Debounce ลงเหลือ 5ms ให้เซนเซอร์จับตาดูขวดแบบตาไม่กะพริบ!
        if (digitalRead(IRPin) == LOW) {
          delay(5); 
          if (digitalRead(IRPin) == LOW) {
            
            sessionBottles++;
            villageTotal++;
            currentBinCount++;
            prefs.putInt("binCount", currentBinCount);
            updateBottleCountOnly();
            sendAudioCommand(3); 

            if(!topServo.attached()) {
              topServo.attach(SERVO_PIN);
            }
            
            // สั่งปิดฝาแบบนุ่มนวล
            for (int pos = 85; pos >= 15; pos -= 2) {
              topServo.write(pos);
              vTaskDelay(pdMS_TO_TICKS(15)); 
            }
            
            vTaskDelay(pdMS_TO_TICKS(3000)); 

            // รอจนกว่าขวดจะพ้นหน้าเซนเซอร์จริงๆ
            int irTimeout = 0;
            while (digitalRead(IRPin) == LOW && irTimeout < 200) { 
                vTaskDelay(pdMS_TO_TICKS(10)); 
                irTimeout++; 
            } 

            // เปิดฝากลับเพื่อรอรับขวดใหม่
            if (currentBinCount < MAX_BIN_CAPACITY) {
              for (int pos = 15; pos <= 85; pos += 2) {
                topServo.write(pos);
                vTaskDelay(pdMS_TO_TICKS(15));
              }
            }
          }
        }
        
        char key = keypad.getKey();
        if (key == '*' || currentBinCount >= MAX_BIN_CAPACITY) {
          if(!topServo.attached()) topServo.attach(SERVO_PIN);
          
          for (int pos = 85; pos >= 15; pos -= 2) {
            topServo.write(pos);
            vTaskDelay(pdMS_TO_TICKS(15));
          }
          
          delay(500);
          topServo.detach(); 
          
          tft.fillScreen(ECO_CYAN);
          tft.setCursor(50, 110);
          tft.setTextColor(ECO_DARK);
          tft.setTextSize(3);
          tft.print("SAVING...");
          sendAudioCommand(4); 
          networkResult = 0;
          triggerSave = true;
          currentState = STATE_SAVING;
        }
        break;
      }

    case STATE_SAVING:
      {
        if (networkResult == 1) {
          delay(1000);
          enteredPhone = "";
          sessionBottles = 0;
          drawLockedScreen();
          currentState = STATE_IDLE;
        }
        break;
      }

    case STATE_RESETTING:
      {
        if (networkResult == 1) {
          delay(2000);
          enteredPhone = "";
          drawLockedScreen();
          currentState = STATE_IDLE;
        }
        break;
      }

    case STATE_UPDATING:
      {
        if (networkResult == 2) {
          tft.fillScreen(0xF800);
          tft.setCursor(40, 100);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(3);
          tft.print("UPDATE FAILED!");
          neopixelWrite(RGBPin, 20, 0, 0);
          delay(4000);
          enteredPhone = "";
          drawLockedScreen();
          currentState = STATE_IDLE;
        }
        break;
      }
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}

void setup() {
  Serial.begin(115200);
  pinMode(IRPin, INPUT_PULLUP);
  topServo.attach(SERVO_PIN);
  topServo.write(2);
  delay(500);
  topServo.detach();

  customSPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, -1);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ECO_DARK);
  tft.setCursor(50, 110);
  tft.setTextColor(ECO_LIME);
  tft.setTextSize(3);
  tft.print("SYSTEM READY");

  prefs.begin("ecoDB", false);
  currentBinCount = prefs.getInt("binCount", 0);

  // 🚀 เปิดระบบวิทยุ ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) { Serial.println("Error initializing ESP-NOW"); }
  memcpy(peerInfo.peer_addr, audioBoardAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  delay(1000);
  drawLockedScreen();
  xTaskCreatePinnedToCore(networkTask, "NetTask", 20000, NULL, 1, &TaskCore0, 0);
}