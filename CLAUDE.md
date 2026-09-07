# esp32 プロジェクト設定

最終更新: 2026-09-07

グローバル設定（`~/.claude/CLAUDE.md`）の基本原則を継承しつつ、本プロジェクト固有のルールを記載する。

---

## プロジェクト概要

ESP32 でセンサーデータを取得し、AWS を使って可視化する。
最終的には写真を多めに掲載した初心者向けチュートリアル記事として公開したい。

### 技術スタック

| 領域 | 採用技術 |
| --- | --- |
| ボード | Freenove ESP32 WROOM Board（FNK0090 / ESP32-WROOM-32） |
| ファームウェア | PlatformIO + Arduino framework（C++） |
| ビルド環境 | PlatformIO Core 6.2.0（Homebrew 導入） |
| AWS 側 | 未定（AWS IoT Core / API Gateway + Lambda を検討中） |
| IaC | Terraform（予定） |

### ディレクトリ構成

```
esp32/
├── firmware/          PlatformIO プロジェクト（ESP32 ファームウェア）
│   ├── platformio.ini ボード・依存ライブラリのバージョン固定
│   ├── src/           ソース
│   ├── include/       ヘッダ（secrets.h は .gitignore で除外）
│   └── lib/           プロジェクト固有ライブラリ
├── infra/             Terraform（AWS 側。未作成）
└── docs/
    └── learning-logs/ 作業記録
```

---

## 重要な設計決定

### 1. ビルド・書き込みはホストで実行する（グローバル設定の例外）

**グローバル設定では「スクリプト実行は常にコンテナ環境で行う」と定めているが、本プロジェクトのファームウェア領域はこれを適用しない。**

理由: macOS の podman は `applehv` の VM 経由で動作しており、USB シリアルデバイス（`/dev/cu.usbserial-*`）をコンテナに渡せない。書き込みとシリアルモニタは原理的にホスト実行が必須。ビルドだけコンテナ化しても書き込みが分離され、デバッグループが遅くなるため一括してホストで実行する。

再現性は `platformio.ini` でボード定義・フレームワーク・ライブラリのバージョンを固定することで担保する。

**AWS 側（Terraform / Lambda）は従来どおりコンテナ実行の原則を適用する。**

### 2. ファームウェアを `firmware/` に分離

AWS 側の Terraform コードを `infra/` に並置するため、リポジトリ直下を PlatformIO プロジェクトにせず 1 階層下げた。
PlatformIO のコマンドは `-d firmware` で実行するか、`firmware/` に移動してから実行する。

### 3. 認証情報の管理

Wi-Fi パスワードや AWS IoT 証明書は `firmware/include/secrets.h` に置き、`.gitignore` で除外する。
テンプレートとして `firmware/include/secrets.h.example` をコミットしている。

---

## よく使うコマンド集

すべてリポジトリルートから実行する想定。

```bash
# ビルド
pio run -d firmware

# ビルド＋書き込み
pio run -d firmware -t upload

# シリアルモニタ（115200 bps）
pio device monitor -b 115200

# 接続中のシリアルポートを確認
pio device list

# ビルド成果物の削除
pio run -d firmware -t clean
```

---

## 環境構築手順

```bash
# 1. PlatformIO Core の導入
brew install platformio

# 2. 認証情報ファイルの用意
cp firmware/include/secrets.h.example firmware/include/secrets.h
# secrets.h を編集して Wi-Fi 情報を記入

# 3. ボードを USB 接続してポートを確認
pio device list

# 4. ビルド＋書き込み
pio run -d firmware -t upload
```

VS Code を使う場合は PlatformIO IDE 拡張（`platformio.platformio-ide`）を導入する。

---

## トラブルシューティング

### ボードが `pio device list` に出てこない

USB-シリアル変換チップのドライバが不足している可能性がある。

```bash
ls /dev/cu.*
```

`/dev/cu.usbserial-*` や `/dev/cu.wchusbserial-*` が現れない場合:

- **CP2102 搭載機**: macOS の標準ドライバで認識されるはず。USB ケーブルが充電専用でないか確認する
- **CH340 搭載機**: ドライバの追加導入が必要な場合がある

データ通信対応の USB ケーブルを使っているかをまず疑うこと。

### 書き込み時に `Failed to connect to ESP32` が出る

ボード上の BOOT ボタンを押しながら書き込みを開始し、接続確立後に離す。

### LED が点滅しない

`firmware/src/main.cpp` の `LED_PIN` が実機と合っていない可能性がある。基板のシルク印刷を確認して調整する。

---

## 参考ドキュメント

- [Freenove FNK0090 チュートリアル](https://docs.freenove.com/projects/fnk0090/en/latest/)
- [PlatformIO ドキュメント](https://docs.platformio.org/)
- [PlatformIO espressif32 プラットフォーム](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Arduino ESP32 リファレンス](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
