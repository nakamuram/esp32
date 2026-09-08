// Wi-Fi 接続とインターネット疎通の確認
//
// 起動時に以下を順に確認する。
//   1. Wi-Fi 接続
//   2. NTP による時刻同期（AWS IoT の TLS で証明書の有効期限検証に必要）
//   3. DNS 解決
//   4. HTTP 接続
//   5. HTTPS 接続（TLS スタックの動作確認）
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "secrets.h"

constexpr uint8_t LED_PIN = 2;
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr unsigned long NTP_TIMEOUT_MS = 15000;
constexpr unsigned long STATUS_INTERVAL_MS = 30000;

// 日本標準時。POSIX の TZ 表記では UTC からの「引く側」の符号になるため JST-9
constexpr const char *TIMEZONE = "JST-9";

// 疎通確認の宛先。example.com は IANA が例示用に予約しているドメイン
constexpr const char *TEST_HOST = "example.com";

static void printChipInfo() {
  Serial.println();
  Serial.println("=== ESP32 起動 ===");
  Serial.printf("チップモデル : %s (リビジョン %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU 周波数   : %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("空きヒープ   : %d bytes\n", ESP.getFreeHeap());
  Serial.println("==================");
}

static const char *wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:   return "SSID が見つからない（SSID の誤り、または 5GHz 帯の可能性）";
    case WL_CONNECT_FAILED:  return "接続失敗（パスワードの誤りの可能性）";
    case WL_CONNECTION_LOST: return "接続が切断された";
    case WL_DISCONNECTED:    return "未接続";
    case WL_IDLE_STATUS:     return "待機中";
    default:                 return "不明な状態";
  }
}

static bool connectWiFi() {
  Serial.printf("\n[1/5] Wi-Fi に接続しています: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      const wl_status_t status = WiFi.status();
      Serial.printf("\n  失敗: %d - %s\n", status, wifiStatusText(status));
      return false;
    }
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.print(".");
    delay(250);
  }

  Serial.printf("\n  成功（%lu ms）\n", millis() - start);
  Serial.printf("  IP: %s / 電波強度: %d dBm / チャンネル: %d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
  return true;
}

// NTP で時刻を合わせる。
// AWS IoT Core への TLS 接続では証明書の有効期限を検証するため、
// 時刻がずれていると原因の分かりにくい handshake 失敗になる。
static bool syncTime() {
  Serial.println("\n[2/5] NTP で時刻を同期しています");

  configTzTime(TIMEZONE, "ntp.nict.jp", "pool.ntp.org", "time.google.com");

  const unsigned long start = millis();
  time_t now = time(nullptr);
  // 2020-01-01 より前なら未同期とみなす
  while (now < 1577836800) {
    if (millis() - start > NTP_TIMEOUT_MS) {
      Serial.printf("  失敗: %lu ms でタイムアウト\n", NTP_TIMEOUT_MS);
      Serial.println("  NTP は UDP 123 番を使う。ルーターで遮断されていないか確認する");
      return false;
    }
    delay(200);
    Serial.print(".");
    now = time(nullptr);
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.printf("\n  成功（%lu ms）: %s JST\n", millis() - start, buf);
  return true;
}

static bool resolveDns() {
  Serial.printf("\n[3/5] DNS を解決しています: %s\n", TEST_HOST);

  IPAddress addr;
  const unsigned long start = millis();
  if (!WiFi.hostByName(TEST_HOST, addr)) {
    Serial.println("  失敗: 名前解決できません");
    return false;
  }

  Serial.printf("  成功（%lu ms）: %s\n", millis() - start, addr.toString().c_str());
  return true;
}

static bool httpGet() {
  Serial.printf("\n[4/5] HTTP で接続しています: http://%s/\n", TEST_HOST);

  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String("http://") + TEST_HOST + "/")) {
    Serial.println("  失敗: HTTPClient の初期化に失敗");
    return false;
  }

  const unsigned long start = millis();
  const int code = http.GET();
  const int length = (code > 0) ? http.getSize() : 0;
  http.end();

  if (code <= 0) {
    Serial.printf("  失敗: %s\n", HTTPClient::errorToString(code).c_str());
    return false;
  }

  Serial.printf("  成功（%lu ms）: HTTP %d / %d bytes\n", millis() - start, code, length);
  return true;
}

static bool httpsGet() {
  Serial.printf("\n[5/5] HTTPS で接続しています: https://%s/\n", TEST_HOST);

  WiFiClientSecure client;
  // ここではサーバ証明書を検証しない。TLS スタックが動くことの確認が目的。
  // AWS IoT Core に接続する際は Amazon のルート CA を設定して検証を有効にする。
  client.setInsecure();

  HTTPClient https;
  https.setTimeout(15000);
  if (!https.begin(client, String("https://") + TEST_HOST + "/")) {
    Serial.println("  失敗: HTTPClient の初期化に失敗");
    return false;
  }

  const unsigned long start = millis();
  const int code = https.GET();
  const int length = (code > 0) ? https.getSize() : 0;
  https.end();

  if (code <= 0) {
    Serial.printf("  失敗: %s\n", HTTPClient::errorToString(code).c_str());
    return false;
  }

  Serial.printf("  成功（%lu ms）: HTTPS %d / %d bytes\n", millis() - start, code, length);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  printChipInfo();

  int passed = 0;
  if (connectWiFi()) {
    passed++;
    if (syncTime())   passed++;
    if (resolveDns()) passed++;
    if (httpGet())    passed++;
    if (httpsGet())   passed++;
  }

  Serial.println();
  Serial.println("==================");
  Serial.printf("疎通確認: %d / 5 項目で成功\n", passed);
  Serial.printf("空きヒープ: %d bytes\n", ESP.getFreeHeap());
  Serial.println("==================");
}

void loop() {
  static unsigned long lastStatus = 0;

  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  delay(1000);

  if (millis() - lastStatus >= STATUS_INTERVAL_MS) {
    lastStatus = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("切断されました。再接続します。");
      connectWiFi();
      return;
    }

    const time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    Serial.printf("%s JST - IP: %s / 電波強度: %d dBm / 空きヒープ: %d bytes\n",
                  buf, WiFi.localIP().toString().c_str(), WiFi.RSSI(), ESP.getFreeHeap());
  }
}
