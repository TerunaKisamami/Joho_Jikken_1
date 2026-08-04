#ifndef SECRETS_H
#define SECRETS_H

// =====================================================
// 情報テンプレート (GitHubにアップロードされます)
// 実際の使用時は、このファイルを「secrets.h」という名前にコピーして
// トークンを入力してください。
// =====================================================
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";

// LINE Messaging API (Push Message) の認証情報
const char *LINE_ACCESS_TOKEN = "YOUR_CHANNEL_ACCESS_TOKEN";
const char *LINE_USER_ID      = "YOUR_USER_ID";

// Google Apps Script (GAS) Webhook URL (ポーリング用)
const char *GAS_WEBHOOK_URL   = "YOUR_GAS_WEBHOOK_URL";

#endif
