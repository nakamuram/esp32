import datetime
import os
from decimal import Decimal

import boto3
from boto3.dynamodb.conditions import Key

TABLE_NAME = os.environ["TABLE_NAME"]
METRIC_NAMESPACE = os.environ["METRIC_NAMESPACE"]
DEVICE_ID = os.environ["DEVICE_ID"]
JMA_REFERENCE_ID = os.environ["JMA_REFERENCE_ID"]
ESTIMATE_ID = os.environ["LIGHT_ESTIMATE_ID"]

JST = datetime.timezone(datetime.timedelta(hours=9))
LOOKBACK_HOURS = 24
# 日中帯（JST）。低い太陽高度による気温差のノイズを避けるため朝夕を除く
DAYTIME_HOURS = range(8, 18)

dynamodb = boto3.resource("dynamodb")
cloudwatch = boto3.client("cloudwatch")
table = dynamodb.Table(TABLE_NAME)


def _query_recent(device_id, start_ms, end_ms):
    resp = table.query(
        KeyConditionExpression=Key("device_id").eq(device_id) & Key("timestamp").between(start_ms, end_ms)
    )
    return resp["Items"]


def handler(event, context):
    now = datetime.datetime.now(JST)
    start = now - datetime.timedelta(hours=LOOKBACK_HOURS)
    start_ms = int(start.timestamp() * 1000)
    end_ms = int(now.timestamp() * 1000)

    sensor_items = _query_recent(DEVICE_ID, start_ms, end_ms)
    reference_items = _query_recent(JMA_REFERENCE_ID, start_ms, end_ms)

    # センサー実測を JST の時間帯（0-23）ごとに平均化しておく
    sensor_temp_by_hour = {}
    for item in sensor_items:
        if "temperature_c" not in item:
            continue
        hour = datetime.datetime.fromtimestamp(int(item["timestamp"]) / 1000, tz=JST).hour
        sensor_temp_by_hour.setdefault(hour, []).append(float(item["temperature_c"]))
    sensor_temp_by_hour = {h: sum(v) / len(v) for h, v in sensor_temp_by_hour.items()}

    sunshine_hours_24h = 0.0
    temp_diffs = []
    for item in reference_items:
        hour = datetime.datetime.fromtimestamp(int(item["timestamp"]) / 1000, tz=JST).hour

        if "sunshine_1h_hour" in item:
            sunshine_hours_24h += float(item["sunshine_1h_hour"])

        if hour in DAYTIME_HOURS and "temperature_c" in item and hour in sensor_temp_by_hour:
            temp_diffs.append(sensor_temp_by_hour[hour] - float(item["temperature_c"]))

    metric_data = [
        {
            "MetricName": "SunshineHours24h",
            "Value": sunshine_hours_24h,
            "Unit": "None",
            "Dimensions": [{"Name": "DeviceId", "Value": ESTIMATE_ID}],
        }
    ]

    item = {
        "device_id": ESTIMATE_ID,
        "timestamp": end_ms,
        "observed_at": now.isoformat(),
        "sunshine_hours_24h": Decimal(str(round(sunshine_hours_24h, 2))),
    }

    if temp_diffs:
        temp_diff_avg = sum(temp_diffs) / len(temp_diffs)
        item["temp_diff_avg_c"] = Decimal(str(round(temp_diff_avg, 2)))
        metric_data.append(
            {
                "MetricName": "TempDiffAvg",
                "Value": temp_diff_avg,
                "Unit": "None",
                "Dimensions": [{"Name": "DeviceId", "Value": ESTIMATE_ID}],
            }
        )
    else:
        temp_diff_avg = None

    table.put_item(Item=item)
    cloudwatch.put_metric_data(Namespace=METRIC_NAMESPACE, MetricData=metric_data)

    return {
        "message": "ok",
        "sunshine_hours_24h": sunshine_hours_24h,
        "temp_diff_avg_c": temp_diff_avg,
        "daytime_hours_matched": len(temp_diffs),
    }
