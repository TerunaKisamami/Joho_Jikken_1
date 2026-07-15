#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// =====================================================
// ピン定義
// =====================================================
// RC522 (SPI)
constexpr uint8_t RFID_SS_PIN = 10;
constexpr uint8_t RFID_RST_PIN = 9;

// Servo
constexpr uint8_t SERVO_PIN = 6;

// SoftwareSerial (ESP32との通信)
constexpr uint8_t ESP_RX_PIN = 2; // ESP32 TX0 -> Uno D2
constexpr uint8_t ESP_TX_PIN = 3; // ESP32 RX0 <- Uno D3

// Joystick
constexpr uint8_t JOY_X_PIN = A0;
constexpr uint8_t JOY_Y_PIN = A1;
constexpr uint8_t JOY_SW_PIN = 7; // SWを追加

// LEDs
constexpr uint8_t LED_SUCCESS = 4;
constexpr uint8_t LED_FAIL = 5;

// =====================================================
// 設定値・定数
// =====================================================
constexpr int LOCK_ANGLE = 90;
constexpr int UNLOCK_ANGLE = 0;
constexpr unsigned long UNLOCK_TIME = 5000;
constexpr int MAX_CARDS = 20;
constexpr int UID_LENGTH = 8;
constexpr int MAX_PASS_LENGTH = 32;

// =====================================================
// オブジェクト
// =====================================================
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo lockServo;
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================================================
// ジョイスティック列挙
// =====================================================
enum JoyDirection {
  DIR_CENTER = 0,
  DIR_UP = 1,
  DIR_DOWN = 2,
  DIR_LEFT = 3,
  DIR_RIGHT = 4,
  DIR_UP_LEFT = 5,
  DIR_UP_RIGHT = 6,
  DIR_DOWN_LEFT = 7,
  DIR_DOWN_RIGHT = 8
};

// =====================================================
// 状態管理
// =====================================================
enum SystemState {
  STATE_BOOT_REGISTER,
  STATE_LOCKED_CARD,
  STATE_LOCKED_PASS,
  STATE_UNLOCKED,
  STATE_ADMIN,
  STATE_ADMIN_PASS1,
  STATE_ADMIN_PASS2
};

SystemState currentState = STATE_BOOT_REGISTER;
unsigned long lastCardReadTime = 0;
unsigned long doorUnlockedTime = 0;
bool doorUnlocked = false;

// カードデータ
String authorizedCards[MAX_CARDS];
int authorizedCardCount = 0;

// パスワードデータ
byte savedPassword[MAX_PASS_LENGTH];
int savedPasswordLen = 0;

byte currentInputPass[MAX_PASS_LENGTH];
int currentInputLen = 0;

byte newPasswordBuf[MAX_PASS_LENGTH];
int newPasswordLen = 0;

// 管理画面
int adminCursor = 0; // 0:Action, 1:Cards, 2:ChangePass
int adminActionMode = 0; // 0:Register, 1:Delete
int adminCardIndex = 0;

// =====================================================
// 関数プロトタイプ
// =====================================================
void refreshOLED();
void setLEDs(bool success, bool fail);
void lockDoor();
void unlockDoor(String method, String detail);
void triggerError(String msg1, String msg2, String logData);
void loadData();
void saveData();
void switchState(SystemState newState);

// =====================================================
// EEPROM
// Byte 0: Card Count
// Byte 1~160: Cards (20 * 8)
// Byte 200: Pass Len
// Byte 201~232: Password bytes
// =====================================================
void loadData() {
  authorizedCardCount = EEPROM.read(0);
  if (authorizedCardCount > MAX_CARDS || authorizedCardCount < 0) authorizedCardCount = 0;
  
  for(int i = 0; i < authorizedCardCount; i++) {
    String uid = "";
    for(int c = 0; c < UID_LENGTH; c++) {
      char ch = EEPROM.read(1 + (i * UID_LENGTH) + c);
      if(ch != 0) uid += ch;
    }
    authorizedCards[i] = uid;
  }

  savedPasswordLen = EEPROM.read(200);
  if (savedPasswordLen <= 0 || savedPasswordLen > MAX_PASS_LENGTH) {
    // デフォルトパスワード: UP, UP, DOWN, DOWN
    savedPasswordLen = 4;
    savedPassword[0] = DIR_UP;
    savedPassword[1] = DIR_UP;
    savedPassword[2] = DIR_DOWN;
    savedPassword[3] = DIR_DOWN;
    saveData();
  } else {
    for(int i = 0; i < savedPasswordLen; i++) {
      savedPassword[i] = EEPROM.read(201 + i);
    }
  }
}

void saveData() {
  EEPROM.write(0, authorizedCardCount);
  for(int i = 0; i < authorizedCardCount; i++) {
    for(int c = 0; c < UID_LENGTH; c++) {
      if (c < authorizedCards[i].length()) {
        EEPROM.write(1 + (i * UID_LENGTH) + c, authorizedCards[i].charAt(c));
      } else {
        EEPROM.write(1 + (i * UID_LENGTH) + c, 0);
      }
    }
  }

  EEPROM.write(200, savedPasswordLen);
  for(int i = 0; i < savedPasswordLen; i++) {
    EEPROM.write(201 + i, savedPassword[i]);
  }
}

int findCard(const String& uid) {
  for (int i = 0; i < authorizedCardCount; i++) {
    if (uid.equalsIgnoreCase(authorizedCards[i])) return i;
  }
  return -1;
}

// =====================================================
// OLED / UI
// =====================================================
void refreshOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  if (currentState == STATE_BOOT_REGISTER) {
    display.setCursor(0,0); display.println("[BOOT REGISTER]");
    display.setCursor(0,15); display.println("Scan Card to Add");
    display.setCursor(0,30); display.println("-------------");
    display.setCursor(0,45); display.println("(SW to Skip)");
  }
  else if (currentState == STATE_LOCKED_CARD) {
    display.setCursor(0,0); display.println("[CARD UNLOCK]");
    display.setCursor(0,15); display.println("Scan RFID Card");
    display.setCursor(0,30); display.println("-------------");
    display.setCursor(0,45); display.println("(SW to Pass Mode)");
  }
  else if (currentState == STATE_LOCKED_PASS) {
    display.setCursor(0,0); display.println("[PASS UNLOCK]");
    display.setCursor(0,15); display.println("Input sequence:");
    display.setCursor(0,30); 
    for(int i=0; i<currentInputLen; i++) display.print("*");
    display.println();
    display.setCursor(0,45); display.println("(SW to Card Mode)");
  }
  else if (currentState == STATE_UNLOCKED) {
    display.setCursor(0,0); display.println("UNLOCKED!");
    display.setCursor(0,15); display.println("Welcome.");
    display.setCursor(0,30); display.println("-------------");
    display.setCursor(0,45); display.println("(SW for Admin)");
  }
  else if (currentState == STATE_ADMIN) {
    display.setCursor(0,0);
    display.print(adminCursor==0 ? "> " : "  ");
    display.print("Mode: < ");
    display.print(adminActionMode==0 ? "Register" : "Delete");
    display.println(" >");

    display.setCursor(0,15);
    display.print(adminCursor==1 ? "> " : "  ");
    display.print("Card: ");
    if (authorizedCardCount > 0) {
      display.print(adminCardIndex + 1); display.print("/"); display.print(authorizedCardCount);
      display.println(); display.print("   "); display.println(authorizedCards[adminCardIndex]);
    } else {
      display.println("No Cards");
    }

    display.setCursor(0,35);
    display.print(adminCursor==2 ? "> " : "  ");
    display.println("Change Password");

    display.setCursor(0,45);
    display.print(adminCursor==3 ? "> " : "  ");
    display.println("Exit Admin");
  }
  else if (currentState == STATE_ADMIN_PASS1) {
    display.setCursor(0,0); display.println("Enter New Pass:");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
    display.setCursor(0,40); display.println("Press SW to confirm");
  }
  else if (currentState == STATE_ADMIN_PASS2) {
    display.setCursor(0,0); display.println("Re-enter Pass:");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
    display.setCursor(0,40); display.println("Press SW to verify");
  }

  display.display();
}

void setLEDs(bool success, bool fail) {
  digitalWrite(LED_SUCCESS, success ? HIGH : LOW);
  digitalWrite(LED_FAIL, fail ? HIGH : LOW);
}

void switchState(SystemState newState) {
  setLEDs(false, false);
  currentState = newState;
  currentInputLen = 0;
  refreshOLED();
}

void triggerError(String msg1, String msg2, String logData) {
  display.clearDisplay();
  display.setCursor(0,10); display.println(msg1);
  display.setCursor(0,30); display.println(msg2);
  display.display();
  
  setLEDs(false, true);
  if (logData.length() > 0) espSerial.println("FAIL:" + logData);
  
  delay(2000);
  setLEDs(false, false);
  refreshOLED();
}

// =====================================================
// ドア・RFID
// =====================================================
void lockDoor() {
  doorUnlocked = false;
  lockServo.write(LOCK_ANGLE);
  switchState(STATE_LOCKED_CARD);
}

void unlockDoor(String method, String detail) {
  doorUnlocked = true;
  doorUnlockedTime = millis();
  lockServo.write(UNLOCK_ANGLE);
  
  setLEDs(true, false);
  espSerial.println("SUCCESS:" + detail);
  
  switchState(STATE_UNLOCKED);
}

void checkAutoLock() {
  if (doorUnlocked && currentState == STATE_UNLOCKED) {
    if (millis() - doorUnlockedTime >= UNLOCK_TIME) {
      lockDoor();
    }
  }
}

void checkRFID() {
  if (currentState != STATE_BOOT_REGISTER && currentState != STATE_LOCKED_CARD && currentState != STATE_ADMIN) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  unsigned long now = millis();
  if (now - lastCardReadTime < 2000) return;
  lastCardReadTime = now;

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  int idx = findCard(uid);

  if (currentState == STATE_BOOT_REGISTER) {
    if (idx >= 0) {
      display.clearDisplay(); display.setCursor(0,20); display.print("Already Registered"); display.display();
      delay(1500);
      switchState(STATE_LOCKED_CARD);
    } else if (authorizedCardCount < MAX_CARDS) {
      authorizedCards[authorizedCardCount++] = uid;
      saveData();
      setLEDs(true, false);
      display.clearDisplay(); display.setCursor(0,20); display.print("Registered!"); display.display();
      espSerial.println("REGISTERED:" + uid);
      delay(1500);
      setLEDs(false, false);
      switchState(STATE_LOCKED_CARD);
    } else {
      triggerError("Error", "Storage Full", "");
      switchState(STATE_LOCKED_CARD);
    }
  }
  else if (currentState == STATE_LOCKED_CARD) {
    if (idx >= 0) unlockDoor("RFID", uid);
    else triggerError("Access Denied", "Unregistered", uid);
  }
  else if (currentState == STATE_ADMIN && adminCursor == 0) {
    if (adminActionMode == 0) { // Register
      if (idx >= 0) triggerError("Already", "Registered", "");
      else if (authorizedCardCount < MAX_CARDS) {
        authorizedCards[authorizedCardCount++] = uid;
        saveData();
        setLEDs(true, false);
        display.clearDisplay(); display.setCursor(0,20); display.print("Registered!"); display.display();
        espSerial.println("REGISTERED:" + uid);
        delay(1500);
      } else {
        triggerError("Error", "Storage Full", "");
      }
    } else { // Delete
      if (idx >= 0) {
        if (authorizedCardCount <= 1) {
          triggerError("Error", "Last Card", "last card");
        } else {
          for(int i = idx; i < authorizedCardCount - 1; i++) authorizedCards[i] = authorizedCards[i+1];
          authorizedCardCount--;
          saveData();
          setLEDs(true, false);
          display.clearDisplay(); display.setCursor(0,20); display.print("Deleted!"); display.display();
          espSerial.println("DELETED:" + uid);
          delay(1500);
        }
      } else {
        triggerError("Not Found", uid, "");
      }
    }
    setLEDs(false, false);
    refreshOLED();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// =====================================================
// ジョイスティック
// =====================================================
JoyDirection getJoyDirection() {
  int x = analogRead(JOY_X_PIN);
  int y = analogRead(JOY_Y_PIN);
  
  bool left = x < 300;
  bool right = x > 700;
  bool up = y < 300;
  bool down = y > 700;
  
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

unsigned long lastJoyTime = 0;
bool lastBtnState = HIGH;

void handleJoystick() {
  unsigned long now = millis();
  if (now - lastJoyTime < 250) return;

  JoyDirection dir = getJoyDirection();
  bool btn = digitalRead(JOY_SW_PIN);
  bool btnPressed = (btn == LOW && lastBtnState == HIGH);
  lastBtnState = btn;

  if (dir != DIR_CENTER || btnPressed) {
    lastJoyTime = now;

    if (currentState == STATE_BOOT_REGISTER) {
      if (btnPressed) switchState(STATE_LOCKED_CARD);
    }
    else if (currentState == STATE_LOCKED_CARD) {
      if (btnPressed) switchState(STATE_LOCKED_PASS);
    }
    else if (currentState == STATE_LOCKED_PASS) {
      if (btnPressed) {
        if (currentInputLen == 0) { switchState(STATE_LOCKED_CARD); return; }
        
        bool match = (currentInputLen == savedPasswordLen);
        for(int i=0; i<currentInputLen && match; i++) {
          if (currentInputPass[i] != savedPassword[i]) match = false;
        }
        if (match) unlockDoor("Password", "Matched");
        else {
          triggerError("Access Denied", "Wrong Password", "PASS_ERROR");
          currentInputLen = 0;
          refreshOLED();
        }
      } else if (dir != DIR_CENTER && currentInputLen < MAX_PASS_LENGTH) {
        currentInputPass[currentInputLen++] = dir;
        refreshOLED();
      }
    }
    else if (currentState == STATE_UNLOCKED) {
      if (btnPressed) {
        adminCursor = 0; adminActionMode = 0; adminCardIndex = 0;
        switchState(STATE_ADMIN);
      }
    }
    else if (currentState == STATE_ADMIN) {
      if (dir == DIR_UP) {
        adminCursor--; if(adminCursor < 0) adminCursor = 3; refreshOLED();
      } else if (dir == DIR_DOWN) {
        adminCursor++; if(adminCursor > 3) adminCursor = 0; refreshOLED();
      } else if (dir == DIR_LEFT || dir == DIR_RIGHT) {
        if (adminCursor == 0) {
          adminActionMode = (adminActionMode == 0) ? 1 : 0;
          refreshOLED();
        } else if (adminCursor == 1 && authorizedCardCount > 0) {
          if (dir == DIR_LEFT) adminCardIndex = (adminCardIndex - 1 + authorizedCardCount) % authorizedCardCount;
          else adminCardIndex = (adminCardIndex + 1) % authorizedCardCount;
          refreshOLED();
        }
      } else if (btnPressed) {
        if (adminCursor == 2) {
          switchState(STATE_ADMIN_PASS1);
        } else if (adminCursor == 3) {
          lockDoor();
        }
      }
    }
    else if (currentState == STATE_ADMIN_PASS1) {
      if (btnPressed) {
        if (currentInputLen > 0) {
          newPasswordLen = currentInputLen;
          for(int i=0; i<currentInputLen; i++) newPasswordBuf[i] = currentInputPass[i];
          switchState(STATE_ADMIN_PASS2);
        }
      } else if (dir != DIR_CENTER && currentInputLen < MAX_PASS_LENGTH) {
        currentInputPass[currentInputLen++] = dir;
        refreshOLED();
      }
    }
    else if (currentState == STATE_ADMIN_PASS2) {
      if (btnPressed) {
        bool match = (currentInputLen == newPasswordLen);
        for(int i=0; i<currentInputLen && match; i++) {
          if (currentInputPass[i] != newPasswordBuf[i]) match = false;
        }
        if (match) {
          savedPasswordLen = newPasswordLen;
          for(int i=0; i<newPasswordLen; i++) savedPassword[i] = newPasswordBuf[i];
          saveData();
          triggerError("Success!", "Password Changed", "");
          switchState(STATE_ADMIN);
        } else {
          triggerError("Mismatch!", "Change Cancelled", "");
          switchState(STATE_ADMIN);
        }
      } else if (dir != DIR_CENTER && currentInputLen < MAX_PASS_LENGTH) {
        currentInputPass[currentInputLen++] = dir;
        refreshOLED();
      }
    }
  }
}

// =====================================================
// 初期化・ループ
// =====================================================
void setup() {
  Serial.begin(115200);
  espSerial.begin(9600);

  pinMode(LED_SUCCESS, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  setLEDs(false, false);

  SPI.begin();
  rfid.PCD_Init();
  lockServo.attach(SERVO_PIN);
  lockServo.write(LOCK_ANGLE);

  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  loadData();
  switchState(STATE_LOCKED_CARD);
}

void loop() {
  handleJoystick();
  checkRFID();
  checkAutoLock();
}
