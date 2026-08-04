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
void sendLineMessage(String message, const char* token, const char* userId) {
  if (String(token) == "" || String(userId) == "") return;
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
      "{\"to\":\"" + String(userId) +
      "\",\"messages\":[{\"type\":\"text\",\"text\":\"" +
      safeMessage + "\"}]}";

  // HTTPヘッダーの送信
  client.println("POST /v2/bot/message/push HTTP/1.1");
  client.println("Host: api.line.me");
  client.println("Authorization: Bearer " + String(token));
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
  
  client.stop(); // 明示的にソケットを閉じて連続送信時のリソース枯渇を防ぐ
}

// 両方のボットにメッセージを送信するヘルパー関数
void broadcastLineMessage(String message) {
  sendLineMessage(message, LINE_ACCESS_TOKEN_1, LINE_USER_ID_1);
  sendLineMessage(message, LINE_ACCESS_TOKEN_2, LINE_USER_ID_2);
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

  broadcastLineMessage("【スマートロック】\nESP32が起動し、Wi-Fiに接続しました。\n\n【LINEからの操作方法】\n・unlock:パスワード\n・change:新パスワード\n(例: unlock:142)\n※パスワードはジョイスティックの方向(1〜8)の数字で指定します。");
  sendLineMessage("【システム通知】\nこの端末は「Bot 1」として登録されています。", LINE_ACCESS_TOKEN_1, LINE_USER_ID_1);
  sendLineMessage("【システム通知】\nこの端末は「Bot 2」として登録されています。", LINE_ACCESS_TOKEN_2, LINE_USER_ID_2);
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
    
    const char* urls[] = {GAS_WEBHOOK_URL_1, GAS_WEBHOOK_URL_2};
    for (int i = 0; i < 2; i++) {
      if (String(urls[i]) != "") {
        WiFiClientSecure secureClient;
        secureClient.setInsecure(); // SSL証明書検証をスキップ
        
        HTTPClient http;
        http.begin(secureClient, urls[i]);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // GASはリダイレクトが必須
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          payload.trim();
          
          if (payload.length() > 0) {
            Serial.print("[GAS Polling Bot ");
            Serial.print(i + 1);
            Serial.print("] Received: ");
            Serial.println(payload);
            
            // スマホ特有の自動大文字化や全角コロンを吸収するための正規化
            String normalized = payload;
            normalized.toLowerCase();
            normalized.replace("：", ":");

            if (normalized.startsWith("unlock:")) {
              String pass = normalized.substring(7);
              pass.trim();
              String botName = (i == 0) ? "LINE(Bot 1)" : "LINE(Bot 2)";
              Serial.println("LINE_UNLOCK:" + botName + ":" + pass); 
            } 
            else if (normalized.startsWith("change:")) {
              String pass = normalized.substring(7);
              pass.trim();
              String botName = (i == 0) ? "LINE(Bot 1)" : "LINE(Bot 2)";
              Serial.println("LINE_CHANGE:" + botName + ":" + pass);
            }
            else {
              // 意味不明なメッセージ
              const char* token = (i == 0) ? LINE_ACCESS_TOKEN_1 : LINE_ACCESS_TOKEN_2;
              const char* userId = (i == 0) ? LINE_USER_ID_1 : LINE_USER_ID_2;
              sendLineMessage("⚠️【エラー】\n意味不明なメッセージです。\n\n解錠する場合は「unlock:パスワード」\nパスワードを変更する場合は「change:パスワード」\nと半角で入力してください。", token, userId);
            }
          }
        } else {
          Serial.print("[GAS Polling Bot ");
          Serial.print(i + 1);
          Serial.print("] Failed. HTTP Code: ");
          Serial.println(httpCode);
        }
        http.end();
      }
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

        if (method.startsWith("LINE")) {
            broadcastLineMessage("✅【遠隔操作 成功】\n\nスマートロックを解錠しました。\n操作者：" + method + "\n入力：" + arrowDetail);
        } else {
            broadcastLineMessage("🔓スマートロック解錠\n\n認証方法：" + method + "\n入力：" + arrowDetail);
        }
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

        if (method.startsWith("LINE")) {
            broadcastLineMessage("❌【遠隔操作 失敗】\n\nパスワードが違います。\n操作者：" + method + "\n入力：" + arrowDetail);
        } else {
            broadcastLineMessage("⚠️スマートロック警告\n\n誤ったパスワードが入力されました。\n認証方法：" + method + "\n入力内容：" + arrowDetail);
        }
      }
      // [NEW] パスワード変更時のメッセージに対応
      else if (data.startsWith("PASS_CHANGED")) {
        int colonIdx = data.indexOf(':');
        if (colonIdx != -1) {
            String method = data.substring(colonIdx + 1);
            broadcastLineMessage("✅【遠隔操作 成功】\n\nパスワードが新しく変更・登録されました。\n操作者：" + method);
        } else {
            broadcastLineMessage("🔑スマートロック\n\nパスワードが新しく変更・登録されました。");
        }
      }
      else if (data == "LOCKED") {
        broadcastLineMessage("🔒スマートロックを施錠しました。");
      }
    }
  }
}
