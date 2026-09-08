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
│   ├── lib/           プロジェクト固有ライブラリ
│   └── tools/         開発補助スクリプト（Python）
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

### 3. Python は venv で実行する

システムの Python を汚さず依存を隔離するため、Python を使う場面では必ず venv を作る。
Homebrew の Python は PEP 668 により `pip install` が拒否される場合もある。

```bash
python3 -m venv .venv
.venv/bin/pip install -r firmware/tools/requirements.txt
```

グローバル設定はスクリプトのコンテナ実行を求めているが、USB シリアル通信は
コンテナ化が原理的に不可能（設計決定 1 を参照）。その中間として venv を用いる。

`.venv/` は `.gitignore` で除外し、依存は `requirements.txt` に固定する。

### 4. 認証情報の管理

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

# シリアルモニタ（115200 bps、対話用）
pio device monitor -b 115200

# シリアルモニタ（リセット付き。スクリプトから使う場合はこちら）
.venv/bin/python firmware/tools/serial_monitor.py        # Ctrl-C まで読む
.venv/bin/python firmware/tools/serial_monitor.py -d 10  # 10秒で終了

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

- **CH340 搭載機**（本プロジェクトの FNK0090 はこちら。VID `1A86` / PID `7523`）:
  macOS 標準ドライバで認識される。実機で確認済みで、追加ドライバは不要だった
- **CP2102 搭載機**: 同じく macOS の標準ドライバで認識される

データ通信対応の USB ケーブルを使っているかをまず疑うこと。

### 書き込みが `Unable to verify flash chip connection` で失敗する

```
Chip is ESP32-D0WD-V3 (revision v3.1)
Changing baud rate to 921600
A fatal error occurred: Unable to verify flash chip connection
(Serial data stream stopped: Possible serial noise or corruption.)
```

チップの型番まで読めているため配線やドライバの問題に見えるが、原因は書き込み速度。
CH340 は高速転送で不安定になる。`platformio.ini` の `upload_speed` を 460800 に
下げると解決する（実機で確認済み）。

### スクリプトからシリアル出力を読みたい

`pio device monitor` は対話端末を要求するため、パイプやリダイレクトの下では
`UserSideException` で失敗する。`firmware/tools/serial_monitor.py` を使うこと。

なお macOS では `/dev/cu.*` を開き直すたびに termios がリセットされるため、
`stty` で設定してから別プロセスで `cat` する方法は文字化けする。

### 書き込み時に `Failed to connect to ESP32` が出る

ボード上の BOOT ボタンを押しながら書き込みを開始し、接続確立後に離す。

### LED が点滅しない

`firmware/src/main.cpp` の `LED_PIN` が実機と合っていない可能性がある。基板のシルク印刷を確認して調整する。

---

## 記事と Issue の運用

**1記事 = 1 Issue** で管理する。Issue はこのリポジトリ（public）側に作成する。

記事はこのプロジェクトの作業の副産物であり、作業記録は作業リポジトリにあるのが
自然なため。設計判断の過程が公開されること自体が、初心者向けチュートリアルという
目標に対する価値になる。

### 手順

1. 作業開始前に `.github/ISSUE_TEMPLATE/article.md` から Issue を作成する
2. 作業中は Issue に随時追記する（清書は不要）
   - 実行したコマンドと結果
   - ハマった点は「症状・原因・解決方法」をセットで
   - 設計判断は選択肢の比較と採用理由を残す
3. 記事執筆時に Issue の内容を素材として使う
4. **公開前に必ずローカルプレビューで確認する**（記事 #3 以降）
5. 公開後、Issue に公開 URL を記入して close する

### 公開前の確認手順

push すると Cloudflare Pages が自動デプロイして即座に公開されるため、
その前に必ずローカルで内容を確認する。

```bash
# 1. コンテナでビルド検証
podman run --rm -v "$PWD":/app -v nakamuram_blog_node_modules:/app/node_modules \
  -w /app node:lts sh -c "npx astro check && npx astro build"

# 2. dist/ を配信してプレビュー
podman run -d --name blog-preview -p 4321:80 \
  -v "$PWD/dist":/usr/share/nginx/html:ro nginx:alpine
```

`http://localhost:4321/posts/<slug>/` を開いて確認し、
問題がなければコミットと push を行う。

nginx はポートフォワード環境で Location からポート番号を落とすため、
`absolute_redirect off;` を設定した conf をマウントすること。

### 注意

public リポジトリのため、Issue に機密情報を書かないこと。
Wi-Fi 認証情報、AWS の認証情報、個人を特定できる情報は記載しない。

### 記事の置き場所

記事本体は別リポジトリ `nakamuram-blog`（private）の
`src/data/blog/` に置く。写真は `src/assets/images/<slug>/` に配置し、
Markdown からは `@/assets/images/<slug>/xxx.png` で参照する。

---

## 参考ドキュメント

- [Freenove FNK0090 チュートリアル](https://docs.freenove.com/projects/fnk0090/en/latest/)
- [PlatformIO ドキュメント](https://docs.platformio.org/)
- [PlatformIO espressif32 プラットフォーム](https://docs.platformio.org/en/latest/platforms/espressif32.html)
- [Arduino ESP32 リファレンス](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
