// 動作確認用スケッチ
// オンボード LED を点滅させ、チップ情報をシリアルに出力する
#include <Arduino.h>

// Freenove ESP32 WROOM Board のオンボード LED
// 点灯しない場合は基板のシルク印刷を確認してピン番号を調整する
constexpr uint8_t LED_PIN = 2;

void setup() {
  Serial.begin(115200);
  delay(1000);  // シリアルモニタの接続待ち

  pinMode(LED_PIN, OUTPUT);

  Serial.println();
  Serial.println("=== ESP32 起動 ===");
  Serial.printf("チップモデル : %s (リビジョン %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU コア数   : %d\n", ESP.getChipCores());
  Serial.printf("CPU 周波数   : %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash サイズ : %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("空きヒープ   : %d bytes\n", ESP.getFreeHeap());
  Serial.println("==================");
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED: ON");
  delay(1000);

  digitalWrite(LED_PIN, LOW);
  Serial.println("LED: OFF");
  delay(1000);
}
