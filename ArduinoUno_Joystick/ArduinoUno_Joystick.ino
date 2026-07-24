#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// =====================================================
// ピン定義
// =====================================================
// Servo
constexpr uint8_t SERVO_PIN = 6;

// SoftwareSerial (ESP32との通信)
constexpr uint8_t ESP_RX_PIN = 2; // ESP32 TX0 -> Uno D2
constexpr uint8_t ESP_TX_PIN = 3; // ESP32 RX0 <- Uno D3

// Joystick
constexpr uint8_t JOY_X_PIN = A0;
constexpr uint8_t JOY_Y_PIN = A1;
constexpr uint8_t JOY_SW_PIN = 7; 

// LEDs
constexpr uint8_t LED_SUCCESS = 5; // 緑LED
constexpr uint8_t LED_FAIL = 4;    // 赤LED

// =====================================================
// 設定値・定数
// =====================================================
constexpr int LOCK_ANGLE = 90;
constexpr int UNLOCK_ANGLE = 0;
constexpr unsigned long UNLOCK_TIME = 5000;
constexpr int MAX_PASS_LENGTH = 16;
constexpr byte EEPROM_MAGIC_BYTE = 0xAA; // 登録済み判定フラグ

// =====================================================
// オブジェクト
// =====================================================
Servo lockServo;
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
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
  STATE_REGISTER_1,      // パスワード新規登録 (1回目入力)
  STATE_REGISTER_2,      // パスワード新規登録 (2段階認証・確認用)
  STATE_LOCKED,          // ロック画面 (入力待機)
  STATE_UNLOCKED,        // 解錠状態
  STATE_ADMIN,           // 管理画面 (パスワード確認・初期化)
  STATE_CHANGE_PASS_1,   // パスワード変更 (1回目入力)
  STATE_CHANGE_PASS_2    // パスワード変更 (2段階認証・確認用)
};

SystemState currentState = STATE_REGISTER_1;
unsigned long doorUnlockedTime = 0;
bool doorUnlocked = false;
int adminCursor = 0; // 0: Change Pass, 1: Initialize

// パスワードデータ (EEPROM保存用)
byte savedPassword[MAX_PASS_LENGTH];
int savedPasswordLen = 0;

// 入力バッファ (ユーザーが現在入力中のパス)
byte currentInputPass[MAX_PASS_LENGTH];
int currentInputLen = 0;

// 一時バッファ (2段階認証の確認用)
byte tempPasswordBuf[MAX_PASS_LENGTH];
int tempPasswordLen = 0;

// ジョイスティックのチャタリング対策
JoyDirection lastJoyDir = DIR_CENTER;
bool swPressed = false;
bool lastSwState = HIGH; // PULLUP
unsigned long lastSwTime = 0;

// =====================================================
// 関数プロトタイプ
// =====================================================
void refreshOLED();
void setLEDs(bool success, bool fail);
void lockDoor();
void unlockDoor(String method, String detail);
void loadData();
void saveData();
void switchState(SystemState newState);
void handleJoystick();
JoyDirection readJoystick();

// =====================================================
// EEPROM制御
// Byte 0: 0xAA ならパスワード登録済み、それ以外は未登録
// Byte 1: パスワードの長さ
// Byte 2~17: パスワードの方向データ
// =====================================================
void loadData() {
  if (EEPROM.read(0) == EEPROM_MAGIC_BYTE) {
    savedPasswordLen = EEPROM.read(1);
    if (savedPasswordLen > 0 && savedPasswordLen <= MAX_PASS_LENGTH) {
      for(int i = 0; i < savedPasswordLen; i++) {
        savedPassword[i] = EEPROM.read(2 + i);
      }
      switchState(STATE_LOCKED);
    } else {
      switchState(STATE_REGISTER_1);
    }
  } else {
    switchState(STATE_REGISTER_1);
  }
}

void saveData() {
  EEPROM.write(0, EEPROM_MAGIC_BYTE);
  EEPROM.write(1, savedPasswordLen);
  for(int i = 0; i < savedPasswordLen; i++) {
    EEPROM.write(2 + i, savedPassword[i]);
  }
}

void factoryReset() {
  Serial.println(F("--- FACTORY RESET ---"));
  EEPROM.write(0, 0x00); // 登録フラグを消去
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("EEPROM Cleared!");
  display.setCursor(0,10);
  display.print("Settings Reset.");
  display.display();
  delay(2000);
}

// =====================================================
// 基本制御
// =====================================================
void setLEDs(bool success, bool fail) {
  digitalWrite(LED_SUCCESS, success ? HIGH : LOW);
  digitalWrite(LED_FAIL, fail ? HIGH : LOW);
}

void lockDoor() {
  lockServo.write(LOCK_ANGLE);
  setLEDs(false, false);
  doorUnlocked = false;
  espSerial.println("LOCKED");
  Serial.println(F("Door Locked."));
}

void unlockDoor(String method, String detail) {
  lockServo.write(UNLOCK_ANGLE);
  setLEDs(true, false);
  doorUnlocked = true;
  doorUnlockedTime = millis();
  
  espSerial.println("UNLOCKED:" + method + ":" + detail);
  Serial.println("Door Unlocked via " + method);
  switchState(STATE_UNLOCKED);
}

void switchState(SystemState newState) {
  currentState = newState;
  currentInputLen = 0; // 状態遷移時は入力バッファをクリア
  refreshOLED();
}

// =====================================================
// UI更新 (OLED)
// =====================================================
void refreshOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  if (currentState == STATE_REGISTER_1) {
    display.setCursor(0,0); display.print("[Set New Pass]");
    display.setCursor(0,10); display.print("Input & Press SW");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
  }
  else if (currentState == STATE_REGISTER_2) {
    display.setCursor(0,0); display.print("[Confirm Pass]");
    display.setCursor(0,10); display.print("Input Again & SW");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
  }
  else if (currentState == STATE_LOCKED) {
    display.setCursor(0,0); display.print("Status: LOCKED");
    display.setCursor(0,10); display.print("Enter Password:");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
  }
  else if (currentState == STATE_UNLOCKED) {
    display.setCursor(0,0); display.print("Status: UNLOCKED");
    display.setCursor(0,10); display.print("Push SW to Admin");
    display.setCursor(0,20); display.print("Closing in ");
    int remain = 5 - ((millis() - doorUnlockedTime) / 1000);
    if(remain < 0) remain = 0;
    display.print(remain); display.print("s");
  }
  else if (currentState == STATE_ADMIN) {
    String passStr = getCurrentPasswordString();
    display.setCursor(0,0); 
    display.print("Pass:"); 
    display.print(passStr); 
    
    bool isWrapped = (5 + passStr.length() > 21);
    int menuY = isWrapped ? 16 : 8;
    
    display.setCursor(0, menuY);
    display.print("--- Admin Menu ---");
    
    String options[3] = {"Change Pass", "Initialize", "Exit (Lock)"};
    
    // 現在選択されている項目を描画
    display.setCursor(0, menuY + 8);
    display.print("-> ");
    display.print(options[adminCursor]);
    
    // 画面下部(Y=24)に空きがある場合は、次の項目も薄く(矢印なしで)表示する
    if (!isWrapped) {
      int nextCursor = (adminCursor + 1) % 3;
      display.setCursor(0, menuY + 16); // Y=24
      display.print("   ");
      display.print(options[nextCursor]);
    }
  }
  else if (currentState == STATE_CHANGE_PASS_1) {
    display.setCursor(0,0); display.print("[Change Pass]");
    display.setCursor(0,10); display.print("Input & Press SW");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
  }
  else if (currentState == STATE_CHANGE_PASS_2) {
    display.setCursor(0,0); display.print("[Confirm Change]");
    display.setCursor(0,10); display.print("Input Again & SW");
    display.setCursor(0,20);
    for(int i=0; i<currentInputLen; i++) display.print("*");
  }
  
  display.display();
}

void triggerError(String msg1, String msg2) {
  setLEDs(false, true);
  display.clearDisplay();
  display.setCursor(0,0); display.print(msg1);
  display.setCursor(0,10); display.print(msg2);
  display.display();
  delay(1500);
  setLEDs(false, false);
  currentInputLen = 0;
  refreshOLED();
}

void triggerSuccess(String msg1, String msg2) {
  setLEDs(true, false);
  display.clearDisplay();
  display.setCursor(0,0); display.print(msg1);
  display.setCursor(0,10); display.print(msg2);
  display.display();
  delay(1500);
  setLEDs(false, false);
}

// =====================================================
// ジョイスティック入力処理
// =====================================================
JoyDirection readJoystick() {
  int x = analogRead(JOY_X_PIN);
  int y = analogRead(JOY_Y_PIN);
  
  bool isUp = (y < 300);
  bool isDown = (y > 700);
  bool isLeft = (x < 300);
  bool isRight = (x > 700);

  if (isUp && isLeft) return DIR_UP_LEFT;
  if (isUp && isRight) return DIR_UP_RIGHT;
  if (isDown && isLeft) return DIR_DOWN_LEFT;
  if (isDown && isRight) return DIR_DOWN_RIGHT;
  if (isUp) return DIR_UP;
  if (isDown) return DIR_DOWN;
  if (isLeft) return DIR_LEFT;
  if (isRight) return DIR_RIGHT;
  return DIR_CENTER;
}

String dirToString(byte dir) {
  switch(dir) {
    case DIR_UP: return "U";
    case DIR_DOWN: return "D";
    case DIR_LEFT: return "L";
    case DIR_RIGHT: return "R";
    case DIR_UP_LEFT: return "UL";
    case DIR_UP_RIGHT: return "UR";
    case DIR_DOWN_LEFT: return "DL";
    case DIR_DOWN_RIGHT: return "DR";
    default: return "?";
  }
}

// OLED表示用の矢印記号 (CP437文字コードを利用)
String dirToArrow(byte dir) {
  switch(dir) {
    case DIR_UP: return "\x18";         // ↑
    case DIR_DOWN: return "\x19";       // ↓
    case DIR_LEFT: return "\x1B";       // ←
    case DIR_RIGHT: return "\x1A";      // →
    case DIR_UP_LEFT: return "\x18\x1B"; // ↑←
    case DIR_UP_RIGHT: return "\x18\x1A"; // ↑→
    case DIR_DOWN_LEFT: return "\x19\x1B"; // ↓←
    case DIR_DOWN_RIGHT: return "\x19\x1A"; // ↓→
    default: return "?";
  }
}

String getCurrentPasswordString() {
  String s = "";
  for(int i = 0; i < savedPasswordLen; i++) {
    s += dirToArrow(savedPassword[i]);
  }
  return s;
}

unsigned long lastStrokeEndTime = 0;
bool inStroke = false;
JoyDirection strokeDir = DIR_CENTER;
unsigned long centerStartTime = 0;

void handleJoystick() {
  JoyDirection currentDir = readJoystick();
  bool swCurr = digitalRead(JOY_SW_PIN);

  // 押し込みボタン(SW)が押された処理
  if (swCurr == LOW && lastSwState == HIGH && millis() - lastSwTime > 50) {
    swPressed = true;
    lastSwTime = millis();
    
    // 【誤入力防止ハック】
    if (millis() - lastStrokeEndTime < 300 && currentInputLen > 0) {
      currentInputLen--; // 最後の入力をキャンセル
      refreshOLED();
    }
  } else {
    swPressed = false;
  }
  lastSwState = swCurr;

  // 方向入力の処理（ストロークベース）
  if (currentDir != DIR_CENTER) {
    centerStartTime = 0; // センタリングのタイマーをリセット
    if (!inStroke) {
      // ストローク開始
      inStroke = true;
      strokeDir = currentDir;
    } else {
      // ストローク中の更新（斜め入力を優先して記録）
      bool isCurrentDiag = (currentDir >= DIR_UP_LEFT && currentDir <= DIR_DOWN_RIGHT);
      bool isStrokeDiag = (strokeDir >= DIR_UP_LEFT && strokeDir <= DIR_DOWN_RIGHT);
      
      if (isCurrentDiag) {
        strokeDir = currentDir; // 斜めなら無条件で上書き
      } else if (!isStrokeDiag) {
        strokeDir = currentDir; // どちらも四方向なら上書き
      }
    }
  } else {
    // ニュートラル(中央)
    if (inStroke) {
      if (centerStartTime == 0) centerStartTime = millis();
      
      // 50ms以上中央に留まったらストローク終了として確定
      if (millis() - centerStartTime > 50) {
        inStroke = false;
        lastStrokeEndTime = millis(); // 最後に方向入力された時間を記録
        
        if (currentInputLen < MAX_PASS_LENGTH) {
          currentInputPass[currentInputLen++] = strokeDir;
          refreshOLED();
        }
      }
    }
  }
}

// パスワードの一致確認
bool checkPasswordMatch(byte* pass1, int len1, byte* pass2, int len2) {
  if (len1 != len2 || len1 == 0) return false;
  for(int i = 0; i < len1; i++) {
    if (pass1[i] != pass2[i]) return false;
  }
  return true;
}

// =====================================================
// ESP32(LINE)からのコマンド処理
// =====================================================
void handleEspSerial() {
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;
    
    if (cmd.startsWith("LINE_UNLOCK:")) {
      String linePass = cmd.substring(12);
      
      int tempLen = 0;
      byte tempBuf[MAX_PASS_LENGTH];
      for (int i=0; i<linePass.length() && i<MAX_PASS_LENGTH; i++) {
        char c = linePass.charAt(i);
        if (c >= '0' && c <= '7') {
          byte dir = DIR_CENTER;
          if (c == '0') dir = DIR_UP;
          else if (c == '1') dir = DIR_UP_RIGHT;
          else if (c == '2') dir = DIR_RIGHT;
          else if (c == '3') dir = DIR_DOWN_RIGHT;
          else if (c == '4') dir = DIR_DOWN;
          else if (c == '5') dir = DIR_DOWN_LEFT;
          else if (c == '6') dir = DIR_LEFT;
          else if (c == '7') dir = DIR_UP_LEFT;
          tempBuf[tempLen++] = dir;
        }
      }
      
      if (checkPasswordMatch(savedPassword, savedPasswordLen, tempBuf, tempLen)) {
        String pd = "";
        for(int i=0; i<tempLen; i++) pd += String(tempBuf[i]);
        unlockDoor("LINE", pd);
      } else {
        String pd = "";
        for(int i=0; i<tempLen; i++) pd += String(tempBuf[i]);
        espSerial.println("FAIL:LINE:" + pd);
        triggerError("LINE Error", "Wrong Pass");
        switchState(STATE_LOCKED);
      }
    } 
    else if (cmd.startsWith("LINE_CHANGE:")) {
      String linePass = cmd.substring(12);
      
      int tempLen = 0;
      byte tempBuf[MAX_PASS_LENGTH];
      for (int i=0; i<linePass.length() && i<MAX_PASS_LENGTH; i++) {
        char c = linePass.charAt(i);
        if (c >= '0' && c <= '7') {
          byte dir = DIR_CENTER;
          if (c == '0') dir = DIR_UP;
          else if (c == '1') dir = DIR_UP_RIGHT;
          else if (c == '2') dir = DIR_RIGHT;
          else if (c == '3') dir = DIR_DOWN_RIGHT;
          else if (c == '4') dir = DIR_DOWN;
          else if (c == '5') dir = DIR_DOWN_LEFT;
          else if (c == '6') dir = DIR_LEFT;
          else if (c == '7') dir = DIR_UP_LEFT;
          tempBuf[tempLen++] = dir;
        }
      }
      
      if (tempLen > 0) {
        savedPasswordLen = tempLen;
        for (int i=0; i<tempLen; i++) savedPassword[i] = tempBuf[i];
        saveData();
        triggerSuccess("Success!", "LINE Pass Saved");
        espSerial.println("PASS_CHANGED");
        lockDoor();
        switchState(STATE_LOCKED);
      }
    }
  }
}

// =====================================================
// セットアップとメインループ
// =====================================================
void setup() {
  Serial.begin(115200);
  espSerial.begin(9600);
  
  pinMode(LED_SUCCESS, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  
  setLEDs(false, false);

  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed!"));
    for(;;);
  }
  display.clearDisplay(); display.display();

  lockServo.attach(SERVO_PIN);
  lockServo.write(LOCK_ANGLE);

  // 初回起動時のリセット確認
  // 電源を入れる前からボタンを押しっぱなしにしていると、EEPROMが初期化される
  if (digitalRead(JOY_SW_PIN) == LOW) {
    factoryReset();
  }

  // EEPROMの読み込み（ここで登録状態ならLOCKED、未登録ならREGISTER_1へ）
  loadData();
}

void loop() {
  handleJoystick();
  handleEspSerial();

  // ---------------------------------------------------
  // [1] パスワード入力フェーズ (初回登録 または 変更時の1回目)
  // ---------------------------------------------------
  if (currentState == STATE_REGISTER_1 || currentState == STATE_CHANGE_PASS_1) {
    if (swPressed) {
      if (currentInputLen == 0) {
        triggerError("Error", "Empty Pass!");
      } else {
        // 入力したパスワードをテンポラリバッファに保存して、確認画面へ移行
        tempPasswordLen = currentInputLen;
        for(int i=0; i<currentInputLen; i++) tempPasswordBuf[i] = currentInputPass[i];
        
        if (currentState == STATE_REGISTER_1) switchState(STATE_REGISTER_2);
        else switchState(STATE_CHANGE_PASS_2);
      }
    }
  }
  // ---------------------------------------------------
  // [2] 2段階認証フェーズ (確認のための再入力)
  // ---------------------------------------------------
  else if (currentState == STATE_REGISTER_2 || currentState == STATE_CHANGE_PASS_2) {
    if (swPressed) {
      // 1回目と2回目のパスワードが完全一致するか確認
      if (checkPasswordMatch(tempPasswordBuf, tempPasswordLen, currentInputPass, currentInputLen)) {
        // 一致！ EEPROMに正式に保存
        savedPasswordLen = tempPasswordLen;
        for(int i=0; i<tempPasswordLen; i++) savedPassword[i] = tempPasswordBuf[i];
        saveData();
        triggerSuccess("Success!", "Pass Saved");
        
        // パスワード変更の通知と、確実な施錠(LOCKED送信)を行う
        espSerial.println("PASS_CHANGED");
        lockDoor();
        
        switchState(STATE_LOCKED);
      } else {
        // 不一致！ 最初からやり直し
        triggerError("Mismatch!", "Try Again");
        if (currentState == STATE_REGISTER_2) switchState(STATE_REGISTER_1);
        else switchState(STATE_CHANGE_PASS_1);
      }
    }
  }
  // ---------------------------------------------------
  // [3] ロック画面 (普段の待機画面)
  // ---------------------------------------------------
  else if (currentState == STATE_LOCKED) {
    if (swPressed) {
      if (currentInputLen == 0) {
        // 何も入力せずにボタンを押した場合は無視するか、入力をクリア
        switchState(STATE_LOCKED);
        return;
      }
      
      if (checkPasswordMatch(savedPassword, savedPasswordLen, currentInputPass, currentInputLen)) {
        // 解錠成功
        String pd = "";
        for(int i=0; i<currentInputLen; i++) {
          pd += String(currentInputPass[i]); // 数字ベースで送信
        }
        unlockDoor("Joystick", pd);
      } else {
        // パスワード間違い
        String pd = "";
        for(int i=0; i<currentInputLen; i++) {
          pd += String(currentInputPass[i]); // 数字ベースで送信
        }
        espSerial.println("FAIL:Joystick:" + pd);
        
        triggerError("Wrong Pass!", "Try Again");
        switchState(STATE_LOCKED);
      }
    }
  }
  // ---------------------------------------------------
  // [4] 解錠画面 (ドアが開いている状態)
  // ---------------------------------------------------
  else if (currentState == STATE_UNLOCKED) {
    unsigned long now = millis();
    if (now - doorUnlockedTime >= UNLOCK_TIME) {
      // 5秒経過で自動ロック
      lockDoor();
      switchState(STATE_LOCKED);
    }
    else {
      // 1秒ごとにカウントダウン更新
      static unsigned long lastUpdate = 0;
      if (now - lastUpdate >= 1000) {
        refreshOLED();
        lastUpdate = now;
      }
      
      // 解錠中にボタンを押すと、管理画面へ移行
      if (swPressed) {
        adminCursor = 0; // カーソルを初期位置へ
        switchState(STATE_ADMIN);
      }
    }
  }
  // ---------------------------------------------------
  // [5] 管理画面 (現在のパスワード確認とメニュー)
  // ---------------------------------------------------
  else if (currentState == STATE_ADMIN) {
    JoyDirection currentAdminDir = readJoystick();
    static JoyDirection lastAdminDir = DIR_CENTER;
    
    // カーソルの移動 (0~2をループ)
    if (currentAdminDir != DIR_CENTER && currentAdminDir != lastAdminDir) {
      if (currentAdminDir == DIR_UP || currentAdminDir == DIR_LEFT) {
        adminCursor--;
        if(adminCursor < 0) adminCursor = 2;
      }
      if (currentAdminDir == DIR_DOWN || currentAdminDir == DIR_RIGHT) {
        adminCursor++;
        if(adminCursor > 2) adminCursor = 0;
      }
      refreshOLED();
    }
    lastAdminDir = currentAdminDir;
    
    // メニューの決定
    if (swPressed) {
      if (adminCursor == 0) {
        // パスワード変更へ
        switchState(STATE_CHANGE_PASS_1);
      } else if (adminCursor == 1) {
        // 初期化して再起動
        lockDoor();
        factoryReset();
        switchState(STATE_REGISTER_1);
      } else if (adminCursor == 2) {
        // 戻る (施錠してロック画面へ)
        lockDoor();
        switchState(STATE_LOCKED);
      }
    }
  }
}
