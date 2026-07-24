#include <BleGamepad.h>

// =====================================================
// ピン定義 (ESP32のピンに合わせてください)
// =====================================================
#define VRX_PIN 32 // ジョイスティックのX軸 (VRX)
#define VRY_PIN 33 // ジョイスティックのY軸 (VRY)
#define SW_PIN  25 // ジョイスティックの押し込みボタン (SW)

// コントローラーの名前とメーカー名を設定
BleGamepad bleGamepad("ESP32 Gamepad", "Custom Maker", 100);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Gamepad!");
  
  // スイッチピンをプルアップ入力に設定
  pinMode(SW_PIN, INPUT_PULLUP);
  
  // BLEゲームパッドの初期化
  bleGamepad.begin();
}

void loop() {
  // PCやスマホとBluetooth接続されている時だけ動作する
  if (bleGamepad.isConnected()) {
    
    // 1. アナログスティックの読み取り
    // ESP32のADC(アナログ入力)は 0 〜 4095 の範囲で取得される
    int xRaw = analogRead(VRX_PIN);
    int yRaw = analogRead(VRY_PIN);
    
    // BleGamepadライブラリの標準形式 (-32767 〜 32767) に変換(マッピング)する
    // ジョイスティックの向きが逆の場合は、ここの 32767 と -32767 を入れ替えてください
    int xMapped = map(xRaw, 0, 4095, -32767, 32767);
    int yMapped = map(yRaw, 0, 4095, -32767, 32767);
    
    // 左のアナログスティックとしてPCに送信
    bleGamepad.setLeftThumb(xMapped, yMapped);
    
    // 2. ボタンの読み取り
    // スイッチはLOW(GNDと繋がった状態)で「押された」と判定
    if (digitalRead(SW_PIN) == LOW) {
      bleGamepad.press(BUTTON_1); // Aボタンなどを押す
    } else {
      bleGamepad.release(BUTTON_1); // ボタンを離す
    }
    
    // PCの負荷を減らすため少し待機 (50FPS相当)
    delay(20);
  } else {
    // 接続待機中は負荷を下げる
    delay(500);
  }
}
