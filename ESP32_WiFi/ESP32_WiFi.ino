#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// =====================================================
// Wi-Fi & LINE API 設定
// (実際のパスワードやトークンは secrets.h に記述されています)
// =====================================================
#include "secrets.h"

const char* HOST = "api.line.me";

// =====================================================
// LINE Messaging API へメッセージ送信
// =====================================================
void sendLineMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure(); // SSL証明書の検証をスキップ

  Serial.println("\n[LINE] Resolving DNS for " + String(HOST) + "...");
  IPAddress lineIP;
  if (WiFi.hostByName(HOST, lineIP)) {
    Serial.print("[LINE] IP Address resolved to: ");
    Serial.println(lineIP);
  } else {
    Serial.println("[ERROR] DNS resolution failed! Check your tethering internet connection.");
  }

  Serial.println("[LINE] Connecting to LINE Messaging API (HTTPS)...");
  if (!client.connect(HOST, 443)) {
    Serial.println("[ERROR] Connection to HOST failed! Retrying in 3 seconds...");
    delay(3000);
    if (!client.connect(HOST, 443)) {
      Serial.println("[FATAL ERROR] 2nd Connection attempt failed. SSL or Network issue.");
      return;
    }
  }

  // エスケープ処理 (改行やダブルクォーテーションをJSON用に変換)
  String safeMessage = message;
  safeMessage.replace("\"", "\\\"");
  safeMessage.replace("\n", "\\n");

  // JSON形式で送信データ(Push Message)を構築
  String body =
      "{\"to\":\"" + String(LINE_USER_ID) +
      "\",\"messages\":[{\"type\":\"text\",\"text\":\"" +
      safeMessage + "\"}]}";

  // HTTPヘッダーの送信
  client.println("POST /v2/bot/message/push HTTP/1.1");
  client.println("Host: api.line.me");
  client.println("Authorization: Bearer " + String(LINE_ACCESS_TOKEN));
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);

  // レスポンスヘッダの読み取り
  while(client.connected()) {
    String line = client.readStringUntil('\n');
    if(line == "\r") break;
  }

  // レスポンスボディの読み取り（デバッグ用）
  Serial.print("[LINE] Response: ");
  while(client.available()) {
    Serial.print(client.readStringUntil('\n'));
  }
  Serial.println();
}

// =====================================================
// 初期化
// =====================================================
void setup() {
  // Unoとの通信用 兼 デバッグ用 (RX0, TX0)
  Serial.begin(9600);
  delay(1000);
  
  Serial.println("\n\n===========================");
  Serial.println(" ESP32 Smart Lock System ");
  Serial.println("===========================");

  // Wi-Fi接続
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 20) { // 10秒繋がらなかったら
      Serial.println("\n[ERROR] Wi-Fi Connection Timeout! Please check SSID/PASS.");
      retry = 0;
    }
  }
  
  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());

  sendLineMessage("【スマートロック】\nESP32が起動し、Wi-Fiに接続しました。");
}

// =====================================================
// メインループ
// =====================================================
void loop() {
  static unsigned long lastPollTime = 0;
  unsigned long now = millis();

  // 5秒ごとにGAS (LINEからのコマンド) をポーリング
  if (now - lastPollTime > 5000) {
    lastPollTime = now;
    
    if (String(GAS_WEBHOOK_URL) != "") {
      WiFiClientSecure secureClient;
      secureClient.setInsecure(); // SSL証明書検証をスキップ
      
      HTTPClient http;
      http.begin(secureClient, GAS_WEBHOOK_URL);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // GASはリダイレクトが必須
      
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        payload.trim();
        
        if (payload.startsWith("unlock:")) {
          String pass = payload.substring(7); // 例: "063"
          // ESP32のSerialはUnoと繋がっているため、printlnでそのままUnoに送信される
          Serial.println("LINE_UNLOCK:" + pass); 
        } 
        else if (payload.startsWith("change:")) {
          String pass = payload.substring(7); // 例: "063"
          Serial.println("LINE_CHANGE:" + pass);
        }
      }
      http.end();
    }
  }

  // Unoからのメッセージ受信
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      Serial.println("[Uno] Received: " + data);
      
      // ジョイスティック版の新しいメッセージフォーマットに対応
      if (data.startsWith("UNLOCKED:")) {
        // 例: UNLOCKED:Joystick:1 4 2 
        int firstColon = data.indexOf(':');
        int secondColon = data.indexOf(':', firstColon + 1);
        String method = data.substring(firstColon + 1, secondColon);
        String detail = data.substring(secondColon + 1);
        
        // 入力履歴(数字)を綺麗な絵文字の矢印に変換する
        String arrowDetail = "";
        for (int i = 0; i < detail.length(); i++) {
          char c = detail.charAt(i);
          if (c == '1') arrowDetail += "⬆️";
          else if (c == '2') arrowDetail += "⬇️";
          else if (c == '3') arrowDetail += "⬅️";
          else if (c == '4') arrowDetail += "➡️";
          else if (c == '5') arrowDetail += "↖️";
          else if (c == '6') arrowDetail += "↗️";
          else if (c == '7') arrowDetail += "↙️";
          else if (c == '8') arrowDetail += "↘️";
        }
        
        // 矢印への変換が行われなかった場合（カードリーダー復旧時など）は元の文字をそのまま表示
        if (arrowDetail == "") {
           arrowDetail = detail;
        }

        sendLineMessage(
            "🔓スマートロック解錠\n\n"
            "認証方法：" + method +
            "\n入力：" + arrowDetail);
      } 
      // [NEW] 入力失敗時のメッセージに対応
      else if (data.startsWith("FAIL:")) {
        // 例: FAIL:Joystick:142
        int firstColon = data.indexOf(':');
        int secondColon = data.indexOf(':', firstColon + 1);
        String method = data.substring(firstColon + 1, secondColon);
        String detail = data.substring(secondColon + 1);
        
        String arrowDetail = "";
        for (int i = 0; i < detail.length(); i++) {
          char c = detail.charAt(i);
          if (c == '1') arrowDetail += "⬆️";
          else if (c == '2') arrowDetail += "⬇️";
          else if (c == '3') arrowDetail += "⬅️";
          else if (c == '4') arrowDetail += "➡️";
          else if (c == '5') arrowDetail += "↖️";
          else if (c == '6') arrowDetail += "↗️";
          else if (c == '7') arrowDetail += "↙️";
          else if (c == '8') arrowDetail += "↘️";
        }
        if (arrowDetail == "") arrowDetail = detail;

        sendLineMessage(
            "⚠️スマートロック警告\n\n"
            "誤ったパスワードが入力されました。\n"
            "認証方法：" + method + "\n"
            "入力内容：" + arrowDetail);
      }
      // [NEW] パスワード変更時のメッセージに対応
      else if (data == "PASS_CHANGED") {
        sendLineMessage("🔑スマートロック\n\nパスワードが新しく変更・登録されました。");
      }
      else if (data == "LOCKED") {
        sendLineMessage("🔒スマートロックを施錠しました。");
      }
    }
  }
}
