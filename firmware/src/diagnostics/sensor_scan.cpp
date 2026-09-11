// センサー診断ツール
//
// 型番の分からないモジュールを、推測ではなく実測で特定するためのもの。
// 次の 4 つを順に行う。
//
//   1. I2C バスの電気的状態  … プルアップの有無で「配線されているか」を見る
//   2. I2C スキャン          … 応答の内訳を出す。SDA/SCL の入れ替えも試す
//   3. チップ ID の読み出し  … 同一アドレスの機種を確定させる
//   4. アナログ入力の監視    … フローティングと信号を区別する
//
// 書き込み:
//   pio run -d firmware -e sensor-scan -t upload
//   .venv/bin/python firmware/tools/serial_monitor.py -d 30
#include <Arduino.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

// 試す I2C ピンの組み合わせ。各組は逆順でも試すので、配線の入れ替わりも検出できる。
// ESP32 の I2C は GPIO マトリクスにより任意のピンに割り当てられる。
// ただし入力専用ピン（34-39）は SDA が双方向のため使えない。
struct I2CPins { uint8_t a; uint8_t b; };
constexpr I2CPins I2C_CANDIDATES[] = {
    {26, 25},  // このプロジェクトの配線（SDA=26, SCL=25）
    {21, 22},  // ESP32 の既定
    {32, 33},  // 切り分け用。BMP180 / ESP32 のどちらが壊れているか切り分けるため、
               // 一度も使っていない ADC1 のピンペアを試す
};

// 監視するアナログピン。ADC1 のみを使う。
// ADC2（GPIO 0,2,4,12-15,25-27）は Wi-Fi 使用中に読めなくなる。
constexpr uint8_t ANALOG_PINS[] = {32, 33, 34, 35};

// DHT11 の DATA 線。アイドル時はモジュール上のプルアップで HIGH に
// 保たれているはず。I2C のプルアップ検出と同じ考え方で、配線と電源が
// 生きているかを応答の成否とは別に確認する。
constexpr uint8_t DHT_PIN = 27;

constexpr unsigned long MONITOR_INTERVAL_MS = 1000;
constexpr unsigned long RESCAN_INTERVAL_MS = 30000;

// ---------------------------------------------------------------------------
// I2C アドレスから機種の候補を引く
// ---------------------------------------------------------------------------

static const char *i2cCandidates(uint8_t addr) {
  switch (addr) {
    case 0x0D: return "QMC5883L（磁気）";
    case 0x1E: return "HMC5883L（磁気）";
    case 0x23: return "BH1750（照度）";
    case 0x27: return "PCF8574 / LCD1602 I2C 変換";
    case 0x29: return "TSL2561 / VL53L0X（照度・測距）";
    case 0x38: return "AHT10 / AHT20（温湿度）";
    case 0x39: return "TSL2561（照度）";
    case 0x3C: return "SSD1306 OLED";
    case 0x40: return "Si7021 / HTU21 / INA219（温湿度・電流）";
    case 0x44: return "SHT30 / SHT31（温湿度）";
    case 0x48: return "ADS1115 / LM75（ADC・温度）";
    case 0x53: return "ADXL345（加速度）";
    case 0x57: return "MAX30102（心拍）";
    case 0x5C: return "BH1750（ADDR=H）";
    case 0x68: return "MPU6050 / DS3231 / DS1307（IMU・RTC）";
    case 0x69: return "MPU6050（AD0=H）";
    case 0x76: return "BMP180/280 / BME280（気圧・温湿度）";
    case 0x77: return "BMP180/280 / BME280（SDO=H）";
    default:   return "候補なし（データシート要確認）";
  }
}

// endTransmission の戻り値。2 が返るならバスは正常で、そのアドレスに機器がいないだけ。
static const char *twiError(uint8_t code) {
  switch (code) {
    case 0: return "成功（ACK）";
    case 1: return "データ長オーバー";
    case 2: return "アドレスに NACK";
    case 3: return "データに NACK";
    case 4: return "その他のエラー";
    case 5: return "タイムアウト";
    default: return "不明";
  }
}

// ---------------------------------------------------------------------------
// 1. I2C バスの電気的状態
// ---------------------------------------------------------------------------

// 内部プルアップを切って電位を読む。正しく配線され電源が入っていれば、
// モジュール側のプルアップ抵抗によって SDA/SCL は HIGH に保たれる。
// これで「配線されているか」を、アドレス応答の有無とは独立に判定できる。
//
// 注意: SDA と SCL が逆でも両方 HIGH に見えるため、この検査では入れ替わりは分からない。
static void probeBusLevels(uint8_t p1, uint8_t p2) {
  pinMode(p1, INPUT);  // 内部プルアップは使わない
  pinMode(p2, INPUT);
  delay(5);

  constexpr int N = 50;
  int h1 = 0, h2 = 0;
  for (int i = 0; i < N; i++) {
    h1 += digitalRead(p1);
    h2 += digitalRead(p2);
    delayMicroseconds(200);
  }

  const char *verdict;
  if (h1 == N && h2 == N)                    verdict = "プルアップ検出 → 配線と電源は生きている";
  else if (h1 == 0 && h2 == 0)               verdict = "両方 LOW → GND と短絡、または電源なし";
  else if (h1 > N * 3 / 4 && h2 > N * 3 / 4) verdict = "ほぼ HIGH だが不安定";
  else                                       verdict = "フローティング → 未接続";

  Serial.printf("  GPIO%-2d/%-2d  HIGH率 %3d%% / %3d%%  %s\n",
                p1, p2, h1 * 100 / N, h2 * 100 / N, verdict);
}

// ---------------------------------------------------------------------------
// 3. チップ ID の読み出し
// ---------------------------------------------------------------------------

// Bosch のセンサーはレジスタ 0xD0 にチップ ID を持つ。
// BMP180 / BMP280 / BME280 は同じアドレスを共有し外見もほぼ同一なので、
// ここを読まなければ機種を確定できない。
static void identifyBosch(uint8_t addr, uint8_t sda, uint8_t scl) {
  Wire.end();
  delay(20);
  Wire.begin(sda, scl);
  Wire.setClock(50000);
  delay(50);

  Wire.beginTransmission(addr);
  Wire.write(0xD0);
  if (Wire.endTransmission(false) != 0) {  // リピートスタートのため stop を送らない
    Serial.println("  チップ ID: レジスタ指定に失敗");
    return;
  }
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) {
    Serial.println("  チップ ID: 読み出しに失敗");
    return;
  }

  const uint8_t id = Wire.read();
  const char *name, *measures;
  switch (id) {
    case 0x55: name = "BMP180"; measures = "気圧・温度"; break;
    case 0x58: name = "BMP280"; measures = "気圧・温度"; break;
    case 0x60: name = "BME280"; measures = "気圧・温度・湿度"; break;
    case 0x61: name = "BME680"; measures = "気圧・温度・湿度・ガス"; break;
    case 0x50: name = "BMP388"; measures = "気圧・温度"; break;
    default:   name = "不明";   measures = "-"; break;
  }
  Serial.printf("  チップ ID 0x%02X → %s（%s）\n", id, name, measures);
}

// ---------------------------------------------------------------------------
// 2. I2C スキャン
// ---------------------------------------------------------------------------

// 応答の内訳まで出す。全アドレスが NACK ならバスは正常で機器がいないだけ、
// タイムアウトが多ければバスが掴まれている、と切り分けられる。
static int scanOnPins(uint8_t sda, uint8_t scl) {
  Wire.end();
  delay(30);
  Wire.begin(sda, scl);
  Wire.setClock(50000);
  Wire.setTimeOut(100);
  delay(150);  // センサーの起動待ち

  int counts[6] = {0};
  int found = 0;
  uint8_t boschAddr = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();
    if (err < 6) counts[err]++;
    if (err == 0) {
      Serial.printf("  ★ SDA=%-2d SCL=%-2d  0x%02X  %s\n", sda, scl, addr, i2cCandidates(addr));
      found++;
      if (addr == 0x76 || addr == 0x77) boschAddr = addr;
    }
    delay(2);
  }

  if (found == 0) {
    Serial.printf("    SDA=%-2d SCL=%-2d  応答なし（", sda, scl);
    bool first = true;
    for (int i = 0; i < 6; i++) {
      if (counts[i] == 0) continue;
      if (!first) Serial.print(", ");
      Serial.printf("%s %d", twiError(i), counts[i]);
      first = false;
    }
    Serial.println("）");
  } else if (boschAddr != 0) {
    identifyBosch(boschAddr, sda, scl);
  }
  return found;
}

static void runI2CDiagnostics() {
  Serial.println();
  Serial.println("=== 1. I2C バスの電気的状態 ===");
  for (const auto &p : I2C_CANDIDATES) probeBusLevels(p.a, p.b);

  Serial.println();
  Serial.println("=== 2-3. I2C スキャンとチップ ID ===");
  int total = 0;
  for (const auto &p : I2C_CANDIDATES) {
    total += scanOnPins(p.a, p.b);
    total += scanOnPins(p.b, p.a);  // SDA/SCL の入れ替わりも試す
  }

  if (total == 0) {
    Serial.println();
    Serial.println("  I2C 機器は見つかりませんでした。確認事項:");
    Serial.println("   ・GY 系の基板は VCC ではなく 3.3 ピンに 3.3V を入れる");
    Serial.println("     （VCC は 5V 入力前提。3.3V ではレギュレータの降下で IC が起動しない）");
    Serial.println("   ・ブレッドボードの電源レールは中央で分断されていることがある");
  }
}

// ---------------------------------------------------------------------------
// 4. アナログ入力の監視
// ---------------------------------------------------------------------------

// 未接続のピンは電荷が保持されず値が乱高下する。連続サンプルのばらつきで判定する。
// GPIO34-39 は内部プルアップ/プルダウンを持たないため、この方法で見るしかない。
// DHT11 の起動信号を送り、応答（80us LOW + 80us HIGH の ACK パルス）が
// 返ってくるかをライブラリを介さず直接見る。エッジが一切なければ
// センサー自体が応答していない可能性が高い。
static void rawPulseDHT() {
  // 起動信号: DATA を 18ms 以上 LOW にしてからリリースする
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT);

  // 直後 1ms のレベル変化（エッジ）を数える。ACK があれば数回、
  // データ転送まで進めば 40bit 分のエッジが観測されるはず。
  int edges = 0;
  int last = digitalRead(DHT_PIN);
  const unsigned long start = micros();
  while (micros() - start < 5000) {  // 5ms 観測
    const int now = digitalRead(DHT_PIN);
    if (now != last) {
      edges++;
      last = now;
    }
  }

  Serial.printf("  起動信号後 5ms のレベル変化: %d 回  %s\n", edges,
                edges == 0 ? "→ 応答なし。センサー自体が反応していない疑い"
                           : "→ 何らかの応答あり");
}

static void probeDHT() {
  pinMode(DHT_PIN, INPUT);
  delay(5);

  constexpr int N = 50;
  int high = 0;
  for (int i = 0; i < N; i++) {
    high += digitalRead(DHT_PIN);
    delayMicroseconds(200);
  }

  const char *verdict;
  if (high == N)      verdict = "プルアップ検出 → 配線と電源は生きている（応答するかは別）";
  else if (high == 0) verdict = "常時 LOW → GND と短絡、または電源なし";
  else                verdict = "不安定 → フローティングの疑い（DATA 未接続の可能性）";

  Serial.printf("  GPIO%-2d (DHT11 DATA)  HIGH率 %3d%%  %s\n", DHT_PIN, high * 100 / N, verdict);
}

static void monitorAnalog() {
  Serial.println("--- 4. DHT11 DATA線 と アナログ入力 ---");
  probeDHT();
  rawPulseDHT();
  for (uint8_t pin : ANALOG_PINS) {
    constexpr int N = 64;
    long sum = 0;
    int lo = 4095, hi = 0;
    for (int i = 0; i < N; i++) {
      const int v = analogRead(pin);
      sum += v;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      delayMicroseconds(150);
    }
    const int avg = sum / N;
    const int spread = hi - lo;

    const char *verdict;
    if (spread > 500)     verdict = "乱高下 → 未接続";
    else if (spread > 60) verdict = "★ 変動あり → 信号";
    else if (avg < 80)    verdict = "ほぼ 0V";
    else if (avg > 4000)  verdict = "ほぼ電源電圧";
    else                  verdict = "★ 安定 → 接続あり";

    Serial.printf("  GPIO%-2d  %4d (%4d mV)  ばらつき %4d  %s\n",
                  pin, avg, avg * 3300 / 4095, spread, verdict);
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("################ センサー診断ツール ################");
  Serial.printf("チップ: %s / 空きヒープ: %d bytes\n", ESP.getChipModel(), ESP.getFreeHeap());

  runI2CDiagnostics();

  analogReadResolution(12);  // 0-4095
  Serial.println();
  Serial.println("アナログ入力の監視を開始します。");
  Serial.println("センサーに刺激を与えて、値が変化するピンを探してください。");
  Serial.println("  光: ライトを当てる / 覆う   音: 手を叩く   可変抵抗: つまみを回す");
  Serial.println();
}

void loop() {
  static unsigned long lastMonitor = 0;
  static unsigned long lastRescan = 0;
  const unsigned long now = millis();

  if (now - lastMonitor >= MONITOR_INTERVAL_MS) {
    lastMonitor = now;
    monitorAnalog();
  }

  if (now - lastRescan >= RESCAN_INTERVAL_MS) {
    lastRescan = now;
    runI2CDiagnostics();
  }
}
