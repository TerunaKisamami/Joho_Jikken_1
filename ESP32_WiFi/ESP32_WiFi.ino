#include <WiFi.h>
#include <WiFiClientSecure.h>

// =====================================================
// Wi-Fi & LINE Notify 設定
// (実際のパスワードやトークンは secrets.h に記述されています)
// =====================================================
#include "secrets.h"
// =====================================================
// LINEへメッセージ送信
// =====================================================
void sendLineMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // SSL証明書の検証をスキップ

  if (!client.connect(LINE_HOST, 443)) {
    return;
  }

  String query = String("message=") + message;
  String request = String("POST /api/notify HTTP/1.1\r\n") +
                   "Host: " + LINE_HOST + "\r\n" + "Authorization: Bearer " +
                   LINE_TOKEN + "\r\n" +
                   "Content-Type: application/x-www-form-urlencoded\r\n" +
                   "Content-Length: " + String(query.length()) + "\r\n" +
                   "Connection: close\r\n\r\n" + query;

  client.print(request);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break; // ヘッダー終了
    }
  }
}

// =====================================================
// 初期化
// =====================================================
void setup() {
  // Unoとの通信用 (RX0, TX0)
  Serial.begin(9600);

  // Wi-Fi接続
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  sendLineMessage("【スマートロック】\nESP32が起動し、Wi-Fiに接続しました。");
}

// =====================================================
// メインループ
// =====================================================
void loop() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      if (data.startsWith("SUCCESS:")) {
        String uid = data.substring(8);
        sendLineMessage(
            "【スマートロック】\nカード認証に成功しました。\nUID: " + uid +
            "\n状態: 解錠");
      } else if (data.startsWith("FAIL:")) {
        String uid = data.substring(5);
        sendLineMessage(
            "【スマートロック警告】\n未登録カードを検出しました。\nUID: " +
            uid);
      } else if (data.startsWith("REGISTERED:")) {
        String uid = data.substring(11);
        sendLineMessage(
            "【スマートロック】\n新しいカードを登録しました。\nUID: " + uid);
      } else if (data.startsWith("DELETED:")) {
        String uid = data.substring(8);
        sendLineMessage("【スマートロック】\nカードを削除しました。\nUID: " +
                        uid);
      }
    }
  }
}
