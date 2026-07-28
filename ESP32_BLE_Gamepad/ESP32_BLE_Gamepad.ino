#include <BleKeyboard.h>

// ピン定義
#define VRX_PIN 35
#define VRY_PIN 34
#define SW_PIN  26

// コントローラーの名前とメーカー名を設定
BleKeyboard bleKeyboard("ESP32 Keyboard", "Custom Maker", 100);

void setup() {
  Serial.begin(115200);
  pinMode(SW_PIN, INPUT_PULLUP);
  
  // キーボードとして初期化
  bleKeyboard.begin();
}

void loop() {
  if (bleKeyboard.isConnected()) {
    int xRaw = analogRead(VRX_PIN);
    int yRaw = analogRead(VRY_PIN);
    int swState = digitalRead(SW_PIN);
    
    // 【シリアルモニタで確認用】
    // ツール ＞ シリアルモニタを開き、右下の通信速度を 115200 に設定してください
    Serial.print("X: "); Serial.print(xRaw);
    Serial.print(" | Y: "); Serial.print(yRaw);
    Serial.print(" | SW: "); Serial.println(swState == LOW ? "PRESSED (0)" : "RELEASED (1)");
    
    // 【X軸 (左右)】 
    // ※もし左右が逆の場合は、KEY_LEFT_ARROW と KEY_RIGHT_ARROW を入れ替えてください
    if (xRaw < 1000) {
      bleKeyboard.press(KEY_LEFT_ARROW);
      bleKeyboard.release(KEY_RIGHT_ARROW);
    } else if (xRaw > 3000) {
      bleKeyboard.press(KEY_RIGHT_ARROW);
      bleKeyboard.release(KEY_LEFT_ARROW);
    } else {
      bleKeyboard.release(KEY_LEFT_ARROW);
      bleKeyboard.release(KEY_RIGHT_ARROW);
    }
    
    // 【Y軸 (上下)】
    // ※もし上下が逆の場合は、KEY_UP_ARROW と KEY_DOWN_ARROW を入れ替えてください
    if (yRaw < 1000) {
      bleKeyboard.press(KEY_UP_ARROW);
      bleKeyboard.release(KEY_DOWN_ARROW);
    } else if (yRaw > 3000) {
      bleKeyboard.press(KEY_DOWN_ARROW);
      bleKeyboard.release(KEY_UP_ARROW);
    } else {
      bleKeyboard.release(KEY_UP_ARROW);
      bleKeyboard.release(KEY_DOWN_ARROW);
    }
    
    // 【決定ボタン (左Shiftキー)】
    if (swState == LOW) {
      bleKeyboard.press(KEY_LEFT_SHIFT); // 左Shiftキー
    } else {
      bleKeyboard.release(KEY_LEFT_SHIFT);
    }
    
    delay(20);
  } else {
    delay(500);
  }
}
