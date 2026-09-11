// BMP180（気圧・温度）と Grove Light Sensor（照度）の実測値を継続的に取得する。
//
// #4 で特定したセンサーとその配線:
//   BMP180              (I2C)      気圧・温度   VCC ではなく 3.3 ピンから給電
//   Grove Light Sensor  (アナログ) 照度         SIG は白線
//
// DHT11 も一時追加を試みたが、電気的には配線・電源とも正常（プルアップ検出）
// なのに起動信号に一切応答せず、モジュール自体の故障と判断して撤去した。
// この切り分けの過程で、BMP180 の SDA/SCL を当初の GPIO26/25 から
// GPIO32/33 に変更している（診断の過程で GPIO26/25 が不安定になったため）。
//
// Wi-Fi 接続の詳細な確認は #3 で済ませているため、ここでは簡略化し、
// 接続できなくてもセンサーの読み取りは継続する（オフラインでも動作を見たいため）。
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

#include "secrets.h"

constexpr uint8_t LED_PIN = 2;

// BMP180 (I2C)。GPIO32/33 で動作確認済み。
constexpr uint8_t I2C_SDA = 32;
constexpr uint8_t I2C_SCL = 33;

// Grove Light Sensor (アナログ)。#4 で判明: SIG は黄ではなく白、GPIO35。
// ADC1 のピンなので Wi-Fi 使用中でも読める。
constexpr uint8_t LIGHT_PIN = 35;

constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long READ_INTERVAL_MS = 3000;

Adafruit_BMP085 bmp;
static bool bmpReady = false;

static void connectWiFi() {
  Serial.printf("Wi-Fi に接続しています: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println("  タイムアウト。オフラインのままセンサーの読み取りを続けます。");
      return;
    }
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(250);
  }
  Serial.printf("  接続しました: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("=== センサー実測 ===");

  connectWiFi();

  Wire.begin(I2C_SDA, I2C_SCL);
  bmpReady = bmp.begin();
  Serial.println(bmpReady ? "BMP180 初期化成功"
                           : "BMP180 初期化失敗。配線（3.3ピン給電・SDA/SCL）を確認してください");

  analogReadResolution(12);  // 0-4095

  Serial.println("====================");
  Serial.println();
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);

  Serial.println("--- 実測値 ---");

  // BMP180: 気圧・温度・海面高度からの相対高度
  if (bmpReady) {
    const float pressure = bmp.readPressure() / 100.0f;  // Pa → hPa
    const float bmpTemp = bmp.readTemperature();
    const float altitude = bmp.readAltitude();
    Serial.printf("気圧        : %.1f hPa\n", pressure);
    Serial.printf("高度        : %.1f m\n", altitude);
    Serial.printf("温度        : %.1f C\n", bmpTemp);
  } else {
    Serial.println("BMP180      : 初期化に失敗しているため読み取りをスキップ");
  }

  // Grove Light Sensor: 生値の平均でノイズを均す。
  // 未接続のピンは電荷が保持されず値が乱高下するため、サンプル間の
  // ばらつき（最大-最小）が大きければ未接続と判定する。これが無いと、
  // 浮いたピンのノイズをもっともらしい照度として出力してしまう。
  long sum = 0;
  int lightMin = 4095, lightMax = 0;
  constexpr int N = 16;
  for (int i = 0; i < N; i++) {
    const int v = analogRead(LIGHT_PIN);
    sum += v;
    if (v < lightMin) lightMin = v;
    if (v > lightMax) lightMax = v;
    delayMicroseconds(200);
  }
  const int lightRaw = sum / N;
  const int lightSpread = lightMax - lightMin;
  if (lightSpread > 500) {
    Serial.printf("照度        : 未接続（ばらつき %d、フローティングの疑い）\n", lightSpread);
  } else {
    Serial.printf("照度        : %4d (raw, 0-4095)\n", lightRaw);
  }

  Serial.println();
  delay(READ_INTERVAL_MS);
}
