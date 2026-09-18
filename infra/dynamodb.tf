# センサー実測値の生データ保存先。
# device_id + timestamp の複合キーで、将来デバイスを増やしても流用できる形にしている。
resource "aws_dynamodb_table" "sensor_readings" {
  name         = "${var.project_name}-readings"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "device_id"
  range_key    = "timestamp"

  attribute {
    name = "device_id"
    type = "S"
  }

  attribute {
    name = "timestamp"
    type = "N"
  }
}
