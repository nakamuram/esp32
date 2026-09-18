import json
import os
import time
from decimal import Decimal

import boto3

TABLE_NAME = os.environ["TABLE_NAME"]
METRIC_NAMESPACE = os.environ["METRIC_NAMESPACE"]
DEVICE_SECRET = os.environ["DEVICE_SECRET"]

# 受け付ける数値フィールドと CloudWatch メトリクス名の対応
# （infra/cloudwatch.tf のダッシュボードと一致させる。light_raw は不要と判断し対象外）
METRIC_NAME_MAP = {
    "pressure_hpa": "Pressure",
    "temperature_c": "Temperature",
}

dynamodb = boto3.resource("dynamodb")
cloudwatch = boto3.client("cloudwatch")
table = dynamodb.Table(TABLE_NAME)


def _response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body, ensure_ascii=False),
    }


def handler(event, context):
    headers = {k.lower(): v for k, v in (event.get("headers") or {}).items()}
    if headers.get("x-device-secret") != DEVICE_SECRET:
        return _response(401, {"message": "unauthorized"})

    try:
        payload = json.loads(event.get("body") or "{}")
    except json.JSONDecodeError:
        return _response(400, {"message": "invalid json"})

    device_id = payload.get("device_id")
    if not isinstance(device_id, str) or not device_id:
        return _response(400, {"message": "device_id is required"})

    values = {}
    for key in METRIC_NAME_MAP:
        value = payload.get(key)
        if value is None:
            continue
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            return _response(400, {"message": f"{key} must be a number"})
        values[key] = float(value)

    if not values:
        return _response(400, {"message": "at least one sensor value is required"})

    now_ms = int(time.time() * 1000)

    # DynamoDB (boto3 resource) は float を受け付けないため Decimal に変換する
    item = {"device_id": device_id, "timestamp": now_ms}
    item.update({key: Decimal(str(value)) for key, value in values.items()})
    table.put_item(Item=item)

    cloudwatch.put_metric_data(
        Namespace=METRIC_NAMESPACE,
        MetricData=[
            {
                "MetricName": METRIC_NAME_MAP[key],
                "Value": value,
                "Unit": "None",
                "Dimensions": [{"Name": "DeviceId", "Value": device_id}],
            }
            for key, value in values.items()
        ],
    )

    return _response(200, {"message": "ok"})
