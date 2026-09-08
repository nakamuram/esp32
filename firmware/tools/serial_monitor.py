#!/usr/bin/env python3
"""ESP32 をリセットしてから起動直後のシリアル出力を読み取る。

`pio device monitor` は対話端末を要求するため、スクリプトや CI から使えない。
このツールは pyserial で直接ポートを開き、自動リセット回路を叩いてから読むので、
起動時のチップ情報やブートローダのメッセージを取りこぼさない。

使い方:
    python3 -m venv .venv
    .venv/bin/pip install -r firmware/tools/requirements.txt
    .venv/bin/python firmware/tools/serial_monitor.py            # 自動検出、無限に読む
    .venv/bin/python firmware/tools/serial_monitor.py -d 10      # 10秒で終了
    .venv/bin/python firmware/tools/serial_monitor.py --no-reset # リセットせず途中から読む
"""

from __future__ import annotations

import argparse
import sys
import time

import serial
from serial.tools import list_ports

DEFAULT_BAUD = 115200

# ESP32 開発ボードでよく使われる USB-シリアル変換チップ
KNOWN_BRIDGES = {
    (0x1A86, 0x7523): "CH340",
    (0x1A86, 0x55D4): "CH9102",
    (0x10C4, 0xEA60): "CP210x",
    (0x0403, 0x6001): "FT232R",
}


def find_port() -> str:
    """USB-シリアル変換チップを検出してポート名を返す。"""
    candidates = []
    for p in list_ports.comports():
        if p.vid is None or p.pid is None:
            continue
        name = KNOWN_BRIDGES.get((p.vid, p.pid))
        if name:
            candidates.append((p.device, name))

    if not candidates:
        sys.exit(
            "USB-シリアル変換チップが見つかりません。\n"
            "ボードが接続されているか、充電専用ではないケーブルかを確認してください。"
        )
    if len(candidates) > 1:
        listing = "\n".join(f"  {dev} ({chip})" for dev, chip in candidates)
        sys.exit(f"複数のポートが見つかりました。--port で指定してください:\n{listing}")

    device, chip = candidates[0]
    print(f"# ポート: {device} ({chip})", file=sys.stderr)
    return device


def hard_reset(ser: serial.Serial) -> None:
    """自動リセット回路を叩いて通常起動させる。

    ESP32 開発ボードの割り当て（esptool の実装に準拠）:
        RTS -> EN  (リセット。Low でリセット状態)
        DTR -> IO0 (ブートモード選択。Low でダウンロードモード)

    通常起動させたいので IO0 は High のまま EN だけをパルスさせる。
    DTR と RTS を逆にするとリセットが効かないが、ボードは動き続けるため
    「起動メッセージだけ出ない」という分かりにくい症状になる。
    """
    ser.setDTR(False)  # IO0 = High（通常起動）
    ser.setRTS(True)   # EN  = Low（リセット状態へ）
    time.sleep(0.1)
    ser.setRTS(False)  # EN  = High（起動開始）
    time.sleep(0.05)


def monitor(port: str, baud: int, duration: float, reset: bool) -> int:
    with serial.Serial(port, baud, timeout=0.2) as ser:
        ser.reset_input_buffer()
        if reset:
            hard_reset(ser)

        deadline = time.monotonic() + duration if duration > 0 else None
        try:
            while deadline is None or time.monotonic() < deadline:
                chunk = ser.read(4096)
                if chunk:
                    sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                    sys.stdout.flush()
        except KeyboardInterrupt:
            print(file=sys.stderr)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ESP32 をリセットしてシリアル出力を読み取る",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("-p", "--port", help="シリアルポート（省略時は自動検出）")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD, help="ボーレート")
    parser.add_argument(
        "-d",
        "--duration",
        type=float,
        default=0.0,
        help="読み取り秒数。0 なら Ctrl-C まで読み続ける",
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="リセットせず現在の出力を途中から読む",
    )
    args = parser.parse_args()

    port = args.port or find_port()
    try:
        return monitor(port, args.baud, args.duration, reset=not args.no_reset)
    except serial.SerialException as exc:
        sys.exit(f"シリアルポートを開けません: {exc}")


if __name__ == "__main__":
    raise SystemExit(main())
