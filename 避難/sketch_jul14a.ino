#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// =====================================================
// ジョイスティック・メニュー設定
// =====================================================
constexpr uint8_t JOY_X_PIN = 34; // ADC1
constexpr uint8_t JOY_Y_PIN = 35; // ADC1
constexpr uint8_t JOY_SW_PIN = 32; // 内部プルアップ

enum JoyDirection {
  DIR_CENTER,
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT,
  DIR_UP_LEFT,
  DIR_UP_RIGHT,
  DIR_DOWN_LEFT,
  DIR_DOWN_RIGHT
};

enum SystemMode {
  MODE_NORMAL,
  MODE_REGISTER,
  MODE_DELETE
};

// =====================================================
// ユーザー設定
// =====================================================

// Wi-Fi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// LINE Messaging API
const char* LINE_CHANNEL_ACCESS_TOKEN =
  "YOUR_CHANNEL_ACCESS_TOKEN";

const char* LINE_USER_ID =
  "YOUR_LINE_USER_ID";

// =====================================================
// ピン設定
// =====================================================

// RC522
constexpr uint8_t RFID_SS_PIN   = 5;
constexpr uint8_t RFID_RST_PIN  = 27; // Changed from 22 to avoid I2C conflict
constexpr uint8_t RFID_SCK_PIN  = 18;
constexpr uint8_t RFID_MISO_PIN = 19;
constexpr uint8_t RFID_MOSI_PIN = 23;

// サーボ
constexpr uint8_t SERVO_PIN = 13;

// 必要に応じて実機に合わせて変更
constexpr int LOCK_ANGLE   = 0;
constexpr int UNLOCK_ANGLE = 90;

// 解錠している時間
constexpr unsigned long UNLOCK_TIME_MS = 5000;

// 同じカードの連続読み取り防止時間
constexpr unsigned long CARD_COOLDOWN_MS = 3000;

// Wi-Fi接続待ち時間
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;

// =====================================================
// 登録カード
// =====================================================

// 登録カードの最大数
constexpr int MAX_CARDS = 20;
String authorizedCards[MAX_CARDS];
int authorizedCardCount = 0;

// =====================================================
// オブジェクト
// =====================================================

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo lockServo;
Preferences prefs;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================================================
// 状態管理
// =====================================================

SystemMode currentMode = MODE_NORMAL;
int menuCursor = 0; // 0: Normal, 1: Register, 2: Delete
bool lastBtnState = HIGH;

String lastUID = "";
unsigned long lastCardReadTime = 0;

bool doorUnlocked = false;
unsigned long unlockStartTime = 0;

// =====================================================
// JSON文字列のエスケープ
// =====================================================

String escapeJson(const String& input) {
  String output;
  output.reserve(input.length() + 16);

  for (size_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);

    switch (c) {
      case '"':
        output += "\\\"";
        break;

      case '\\':
        output += "\\\\";
        break;

      case '\n':
        output += "\\n";
        break;

      case '\r':
        output += "\\r";
        break;

      case '\t':
        output += "\\t";
        break;

      default:
        output += c;
        break;
    }
  }

  return output;
}

// =====================================================
// ジョイスティック読み取り
// =====================================================

JoyDirection getJoyDirection() {
  int x = analogRead(JOY_X_PIN);
  int y = analogRead(JOY_Y_PIN);
  
  // 0-4095. Center is around 1800-2200.
  bool left = x < 1000;
  bool right = x > 3000;
  bool up = y < 1000;
  bool down = y > 3000;
  
  if(up && left) return DIR_UP_LEFT;
  if(up && right) return DIR_UP_RIGHT;
  if(down && left) return DIR_DOWN_LEFT;
  if(down && right) return DIR_DOWN_RIGHT;
  if(up) return DIR_UP;
  if(down) return DIR_DOWN;
  if(left) return DIR_LEFT;
  if(right) return DIR_RIGHT;
  
  return DIR_CENTER;
}

// =====================================================
// OLED表示
// =====================================================

void showMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("--- Select Mode ---");
  
  display.setCursor(0, 20);
  display.print(menuCursor == 0 ? "> " : "  ");
  display.println("Normal");
  
  display.setCursor(0, 35);
  display.print(menuCursor == 1 ? "> " : "  ");
  display.println("Register Card");
  
  display.setCursor(0, 50);
  display.print(menuCursor == 2 ? "> " : "  ");
  display.println("Delete Card");
  
  display.display();
}

void showDisplayMessage(const String& line1, const String& line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 15);
  display.println(line1);
  
  display.setCursor(0, 35);
  display.println(line2);
  
  display.display();
}

// =====================================================
// Wi-Fi接続
// =====================================================

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println();
  Serial.print("Wi-Fi接続中: ");
  Serial.println(WIFI_SSID);

  showDisplayMessage("Wi-Fi Connecting", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (millis() - startTime >= WIFI_TIMEOUT_MS) {
      Serial.println();
      Serial.println("Wi-Fi接続タイムアウト");
      return false;
    }
  }

  Serial.println();
  Serial.println("Wi-Fi接続成功");
  Serial.print("IPアドレス: ");
  Serial.println(WiFi.localIP());

  return true;
}

// =====================================================
// LINE通知
// =====================================================

bool sendLineMessage(const String& message) {
  if (!connectWiFi()) {
    Serial.println("LINE送信中止: Wi-Fi未接続");
    return false;
  }

  WiFiClientSecure secureClient;

  // 試作用の簡易設定です。
  // サーバー証明書の検証を省略します。
  secureClient.setInsecure();

  HTTPClient https;

  const char* endpoint =
    "https://api.line.me/v2/bot/message/push";

  if (!https.begin(secureClient, endpoint)) {
    Serial.println("HTTPS接続の初期化に失敗");
    return false;
  }

  https.setTimeout(10000);

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  String authorization =
    "Bearer " + String(LINE_CHANNEL_ACCESS_TOKEN);

  https.addHeader(
    "Authorization",
    authorization
  );

  String payload =
    "{"
      "\"to\":\"" + escapeJson(String(LINE_USER_ID)) + "\","
      "\"messages\":["
        "{"
          "\"type\":\"text\","
          "\"text\":\"" + escapeJson(message) + "\""
        "}"
      "]"
    "}";

  Serial.println("LINEへ通知を送信します");
  Serial.println(payload);

  int httpCode = https.POST(payload);

  String response = https.getString();

  Serial.print("HTTPステータス: ");
  Serial.println(httpCode);

  if (response.length() > 0) {
    Serial.print("LINE応答: ");
    Serial.println(response);
  }

  https.end();

  // LINE Messaging APIのPush送信成功時は通常200
  if (httpCode == 200) {
    Serial.println("LINE通知成功");
    return true;
  }

  Serial.println("LINE通知失敗");
  return false;
}

// =====================================================
// RC522からUIDを取得
// =====================================================

String getCardUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  return uid;
}

// =====================================================
// 登録カードの検索
// =====================================================

void loadCards() {
  prefs.begin("smartlock", false);
  authorizedCardCount = prefs.getInt("count", 0);
  for(int i = 0; i < authorizedCardCount; i++) {
    authorizedCards[i] = prefs.getString(String(i).c_str(), "");
  }
}

void saveCards() {
  prefs.putInt("count", authorizedCardCount);
  for(int i = 0; i < authorizedCardCount; i++) {
    prefs.putString(String(i).c_str(), authorizedCards[i]);
  }
}

int findAuthorizedCard(const String& uid) {
  for (int i = 0; i < authorizedCardCount; i++) {
    if (uid.equalsIgnoreCase(authorizedCards[i])) {
      return i;
    }
  }
  return -1;
}

// =====================================================
// 施錠
// =====================================================

void lockDoor(bool sendNotification) {
  Serial.println("施錠します");

  lockServo.write(LOCK_ANGLE);
  doorUnlocked = false;

  showMenu();

  if (sendNotification) {
    sendLineMessage(
      "【スマートロック】\n"
      "自動施錠しました。"
    );
  }
}

// =====================================================
// 解錠
// =====================================================

void unlockDoor(const String& userName, const String& uid) {
  Serial.println("解錠します");

  lockServo.write(UNLOCK_ANGLE);

  doorUnlocked = true;
  unlockStartTime = millis();

  showDisplayMessage("Unlocked!", "User: " + userName);

  String message =
    "【スマートロック】\n"
    "カード認証に成功しました。\n"
    "解錠者: " + userName + "\n"
    "UID: " + uid + "\n"
    "状態: 解錠";

  sendLineMessage(message);
}

// =====================================================
// 未登録カード処理
// =====================================================

void unauthorizedCard(const String& uid) {
  Serial.println("未登録カードです");

  showDisplayMessage("Access Denied", "Unregistered");

  String message =
    "【スマートロック警告】\n"
    "未登録カードを検出しました。\n"
    "UID: " + uid + "\n"
    "解錠は実行されていません。";

  sendLineMessage(message);

  delay(2000);
  showMenu();
}

// =====================================================
// カード読み取り処理
// =====================================================

void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getCardUID();

  Serial.println();
  Serial.print("カードUID: ");
  Serial.println(uid);

  unsigned long currentTime = millis();

  // 同じカードを短時間に連続検出した場合は無視
  if (
    uid == lastUID &&
    currentTime - lastCardReadTime < CARD_COOLDOWN_MS
  ) {
    Serial.println("連続読み取りのため無視します");

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  lastUID = uid;
  lastCardReadTime = currentTime;

  int cardIndex = findAuthorizedCard(uid);

  if (currentMode == MODE_NORMAL) {
    if (cardIndex >= 0) {
      unlockDoor("User", uid); // Modified to User since name is removed
    } else {
      unauthorizedCard(uid);
    }
  } 
  else if (currentMode == MODE_REGISTER) {
    if (cardIndex >= 0) {
      showDisplayMessage("Already Registered", uid);
      delay(2000);
    } else {
      if (authorizedCardCount < MAX_CARDS) {
        authorizedCards[authorizedCardCount] = uid;
        authorizedCardCount++;
        saveCards();
        showDisplayMessage("Registered!", uid);
        sendLineMessage("新しいカードを登録しました: " + uid);
      } else {
        showDisplayMessage("Error", "Storage Full");
      }
      delay(2000);
    }
    showMenu();
  }
  else if (currentMode == MODE_DELETE) {
    if (cardIndex >= 0) {
      // Remove card by shifting array
      for(int i = cardIndex; i < authorizedCardCount - 1; i++) {
        authorizedCards[i] = authorizedCards[i+1];
      }
      authorizedCardCount--;
      saveCards();
      showDisplayMessage("Deleted!", uid);
      sendLineMessage("カードを削除しました: " + uid);
      delay(2000);
    } else {
      showDisplayMessage("Not Found", uid);
      delay(2000);
    }
    showMenu();
  }

  // カードとの通信を終了
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// =====================================================
// 自動施錠処理
// =====================================================

void checkAutoLock() {
  if (!doorUnlocked) {
    return;
  }

  if (millis() - unlockStartTime >= UNLOCK_TIME_MS) {
    lockDoor(true);
  }
}

// =====================================================
// 初期化
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 RFID Smart Lock");
  Serial.println("============================");

  // ジョイスティックピン初期化
  pinMode(JOY_SW_PIN, INPUT_PULLUP);

  // OLED初期化
  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.display();
  }
  
  showDisplayMessage("System Starting", "Please wait...");

  // SPI・RC522初期化
  SPI.begin(
    RFID_SCK_PIN,
    RFID_MISO_PIN,
    RFID_MOSI_PIN,
    RFID_SS_PIN
  );

  rfid.PCD_Init();
  delay(50);

  Serial.print("RC522バージョン: 0x");
  byte version = rfid.PCD_ReadRegister(
    MFRC522::VersionReg
  );
  Serial.println(version, HEX);

  if (version == 0x00 || version == 0xFF) {
    Serial.println(
      "RC522を認識できません。配線と3.3V電源を確認してください。"
    );
  } else {
    Serial.println("RC522初期化完了");
  }

  // サーボ初期化
  lockServo.setPeriodHertz(50);
  lockServo.attach(SERVO_PIN, 500, 2400);

  // 起動時は施錠位置
  lockServo.write(LOCK_ANGLE);
  doorUnlocked = false;

  Serial.println("サーボ初期化完了");

  // Wi-Fi接続
  bool wifiConnected = connectWiFi();

  if (wifiConnected) {
    sendLineMessage(
      "【スマートロック】\n"
      "ESP32が起動しました。\n"
      "カードを読み取れる状態です。"
    );
  }

  Serial.println();
  Serial.println("カードをRC522にかざしてください");
  
  loadCards();
  showMenu();
}

// =====================================================
// ジョイスティック制御
// =====================================================

unsigned long lastJoyTime = 0;

void handleJoystick() {
  unsigned long now = millis();
  if (now - lastJoyTime < 200) return; // デバウンス

  JoyDirection dir = getJoyDirection();

  if (dir == DIR_UP) {
    menuCursor--;
    if (menuCursor < 0) menuCursor = 2;
    currentMode = static_cast<SystemMode>(menuCursor);
    showMenu();
    lastJoyTime = now;
  } 
  else if (dir == DIR_DOWN) {
    menuCursor++;
    if (menuCursor > 2) menuCursor = 0;
    currentMode = static_cast<SystemMode>(menuCursor);
    showMenu();
    lastJoyTime = now;
  }
}

// =====================================================
// メインループ
// =====================================================

void loop() {
  handleJoystick();
  checkRFID();
  checkAutoLock();

  delay(20);
}