# 照度センサーの代わりに、既存データから日当たり傾向を推定する。
# - 気象庁(宮城島相当=jma_reference_id_2)の日照時間(過去24h合計)
# - センサー実測気温 と 同地点の気温 の日中帯(8-18時)平均差分
# 土壌温度センサー導入後、日較差を3変数目として追加する予定。

data "archive_file" "estimate_light" {
  type        = "zip"
  source_file = "${path.module}/light_estimate_lambda_src/estimate_light.py"
  output_path = "${path.module}/build/estimate_light.zip"
}

resource "aws_iam_role" "lambda_estimate_light" {
  name = "${var.project_name}-lambda-estimate-light-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy" "lambda_estimate_light" {
  name = "${var.project_name}-lambda-estimate-light-policy"
  role = aws_iam_role.lambda_estimate_light.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["dynamodb:Query", "dynamodb:PutItem"]
        Resource = aws_dynamodb_table.sensor_readings.arn
      },
      {
        Effect   = "Allow"
        Action   = ["cloudwatch:PutMetricData"]
        Resource = "*"
      },
      {
        Effect = "Allow"
        Action = [
          "logs:CreateLogGroup",
          "logs:CreateLogStream",
          "logs:PutLogEvents",
        ]
        Resource = "${aws_cloudwatch_log_group.lambda_estimate_light.arn}:*"
      }
    ]
  })
}

resource "aws_cloudwatch_log_group" "lambda_estimate_light" {
  name              = "/aws/lambda/${var.project_name}-estimate-light"
  retention_in_days = 14
}

resource "aws_lambda_function" "estimate_light" {
  function_name    = "${var.project_name}-estimate-light"
  role             = aws_iam_role.lambda_estimate_light.arn
  handler          = "estimate_light.handler"
  runtime          = "python3.12"
  architectures    = ["arm64"]
  filename         = data.archive_file.estimate_light.output_path
  source_code_hash = data.archive_file.estimate_light.output_base64sha256
  timeout          = 15
  memory_size      = 128

  environment {
    variables = {
      TABLE_NAME        = aws_dynamodb_table.sensor_readings.name
      METRIC_NAMESPACE  = var.project_name
      DEVICE_ID         = var.device_id
      JMA_REFERENCE_ID  = var.jma_reference_id_2
      LIGHT_ESTIMATE_ID = "light-estimate"
    }
  }

  depends_on = [aws_cloudwatch_log_group.lambda_estimate_light]
}

# 気象庁データ取得(12:05 JST)の後、12:15 JST に実行
resource "aws_cloudwatch_event_rule" "estimate_light_daily" {
  name                = "${var.project_name}-estimate-light-daily"
  schedule_expression = "cron(15 3 * * ? *)"
}

resource "aws_cloudwatch_event_target" "estimate_light_daily" {
  rule = aws_cloudwatch_event_rule.estimate_light_daily.name
  arn  = aws_lambda_function.estimate_light.arn
}

resource "aws_lambda_permission" "allow_eventbridge_estimate_light" {
  statement_id  = "AllowEventBridgeInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.estimate_light.function_name
  principal     = "events.amazonaws.com"
  source_arn    = aws_cloudwatch_event_rule.estimate_light_daily.arn
}
