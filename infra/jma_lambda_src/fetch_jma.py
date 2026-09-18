import datetime
import json
import os
import urllib.request
from decimal import Decimal

import boto3

TABLE_NAME = os.environ["TABLE_NAME"]
METRIC_NAMESPACE = os.environ["METRIC_NAMESPACE"]
STATIONS = [
    (os.environ["AMEDAS_STATION_CODE"], os.environ["JMA_REFERENCE_ID"]),
    (os.environ["AMEDAS_STATION_CODE_2"], os.environ["JMA_REFERENCE_ID_2"]),
]

# 実行は1日1回だが、比較用にセンサー側と粒度を近づけるため過去24時間分を毎時取得する
HOURS_TO_FETCH = 24

LATEST_TIME_URL = "https://www.jma.go.jp/bosai/amedas/data/latest_time.txt"
MAP_DATA_URL = "https://www.jma.go.jp/bosai/amedas/data/map/{ymdhns}.json"

# CloudWatch ダッシュボードに載せる項目（将来的に増やす可能性はある）
DASHBOARD_METRIC_MAP = {
    "pressure_hpa": "Pressure",
    "temperature_c": "Temperature",
    "sunshine_1h_hour": "SunshineHours",
}

# アメダスJSONのキー -> DynamoDBに保存するフィールド名。
# ここに無いキーが将来追加されても取りこぼさないよう、未知のキーはそのまま保存する。
FIELD_NAME_MAP = {
    "temp": "temperature_c",
    "pressure": "pressure_hpa",
    "normalPressure": "sea_level_pressure_hpa",
    "humidity": "humidity_percent",
    "visibility": "visibility_m",
    "sun10m": "sunshine_10min_min",
    "sun1h": "sunshine_1h_hour",
    "precipitation10m": "precipitation_10min_mm",
    "precipitation1h": "precipitation_1h_mm",
    "precipitation3h": "precipitation_3h_mm",
    "precipitation24h": "precipitation_24h_mm",
    "windDirection": "wind_direction_code",
    "wind": "wind_speed_ms",
}

# CloudWatch PutMetricData は1リクエストあたり最大20件まで
PUT_METRIC_BATCH_SIZE = 20

dynamodb = boto3.resource("dynamodb")
cloudwatch = boto3.client("cloudwatch")
table = dynamodb.Table(TABLE_NAME)


def _fetch_json(url):
    with urllib.request.urlopen(url, timeout=10) as res:
        return json.loads(res.read().decode())


def _extract_all_values(station):
    """アメダスの観測項目のうち、有効フラグ(0)が立っているものを全て取り出す"""
    values = {}
    for key, entry in station.items():
        if not (isinstance(entry, list) and len(entry) == 2):
            continue
        value, flag = entry
        if flag != 0 or value is None:
            continue
        field_name = FIELD_NAME_MAP.get(key, key)
        values[field_name] = value
    return values


def handler(event, context):
    latest_time_raw = urllib.request.urlopen(LATEST_TIME_URL, timeout=10).read().decode().strip()
    latest_observed_at = datetime.datetime.fromisoformat(latest_time_raw)
    # 分・秒を切り捨て、直近の毎正時から過去24時間分を対象にする
    latest_hour = latest_observed_at.replace(minute=0, second=0, microsecond=0)

    metric_data = []
    stats = {reference_id: {"written": 0, "skipped": 0} for _, reference_id in STATIONS}

    for hours_ago in range(HOURS_TO_FETCH):
        observed_at = latest_hour - datetime.timedelta(hours=hours_ago)
        ymdhns = observed_at.strftime("%Y%m%d%H%M%S")

        try:
            # 1回の取得で全観測地点のデータが返るため、2地点分をまとめて処理する
            all_stations = _fetch_json(MAP_DATA_URL.format(ymdhns=ymdhns))
        except Exception as exc:  # noqa: BLE001 - 1時間分の失敗で全体を止めない
            print(f"警告: {observed_at.isoformat()} の取得に失敗: {exc}")
            for _, reference_id in STATIONS:
                stats[reference_id]["skipped"] += 1
            continue

        for station_code, reference_id in STATIONS:
            station = all_stations.get(station_code)
            if station is None:
                print(f"警告: {reference_id} {observed_at.isoformat()} に観測地点コードが見つかりません")
                stats[reference_id]["skipped"] += 1
                continue

            values = _extract_all_values(station)
            if not values:
                stats[reference_id]["skipped"] += 1
                continue

            # DynamoDB の device_id + timestamp をキーに冪等に上書きできるため、
            # 同じ時刻を再取得しても重複データにはならない
            item = {
                "device_id": reference_id,
                "timestamp": int(observed_at.timestamp() * 1000),
                "observed_at": observed_at.isoformat(),
            }
            item.update(
                {
                    key: Decimal(str(value)) if isinstance(value, (int, float)) else value
                    for key, value in values.items()
                }
            )
            table.put_item(Item=item)
            stats[reference_id]["written"] += 1

            for key, metric_name in DASHBOARD_METRIC_MAP.items():
                if key not in values:
                    continue
                metric_data.append(
                    {
                        "MetricName": metric_name,
                        "Value": float(values[key]),
                        "Timestamp": observed_at,
                        "Unit": "None",
                        "Dimensions": [{"Name": "DeviceId", "Value": reference_id}],
                    }
                )

    for i in range(0, len(metric_data), PUT_METRIC_BATCH_SIZE):
        cloudwatch.put_metric_data(
            Namespace=METRIC_NAMESPACE, MetricData=metric_data[i : i + PUT_METRIC_BATCH_SIZE]
        )

    return {"message": "ok", "stats": stats}
