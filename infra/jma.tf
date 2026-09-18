# 気象庁アメダスの実測値を1日1回取得し、センサー値と比較できるよう
# 既存のDynamoDBテーブル・CloudWatch namespaceに書き込む。
# 観測地点コード(amedas_station_code)は設置場所を推測されないよう
# terraform.tfvars（gitignore対象）でのみ指定し、コードにはハードコードしない。

data "archive_file" "fetch_jma" {
  type        = "zip"
  source_file = "${path.module}/jma_lambda_src/fetch_jma.py"
  output_path = "${path.module}/build/fetch_jma.zip"
}

resource "aws_iam_role" "lambda_fetch_jma" {
  name = "${var.project_name}-lambda-fetch-jma-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy" "lambda_fetch_jma" {
  name = "${var.project_name}-lambda-fetch-jma-policy"
  role = aws_iam_role.lambda_fetch_jma.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["dynamodb:PutItem"]
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
        Resource = "${aws_cloudwatch_log_group.lambda_fetch_jma.arn}:*"
      }
    ]
  })
}

resource "aws_cloudwatch_log_group" "lambda_fetch_jma" {
  name              = "/aws/lambda/${var.project_name}-fetch-jma"
  retention_in_days = 14
}

resource "aws_lambda_function" "fetch_jma" {
  function_name    = "${var.project_name}-fetch-jma"
  role             = aws_iam_role.lambda_fetch_jma.arn
  handler          = "fetch_jma.handler"
  runtime          = "python3.12"
  architectures    = ["arm64"]
  filename         = data.archive_file.fetch_jma.output_path
  source_code_hash = data.archive_file.fetch_jma.output_base64sha256
  timeout          = 60
  memory_size      = 128

  environment {
    variables = {
      TABLE_NAME            = aws_dynamodb_table.sensor_readings.name
      METRIC_NAMESPACE      = var.project_name
      AMEDAS_STATION_CODE   = var.amedas_station_code
      AMEDAS_STATION_CODE_2 = var.amedas_station_code_2
      JMA_REFERENCE_ID      = var.jma_reference_id
      JMA_REFERENCE_ID_2    = var.jma_reference_id_2
    }
  }

  depends_on = [aws_cloudwatch_log_group.lambda_fetch_jma]
}

# 1日1回、12:05 JST (03:05 UTC) に実行
resource "aws_cloudwatch_event_rule" "fetch_jma_daily" {
  name                = "${var.project_name}-fetch-jma-daily"
  schedule_expression = "cron(5 3 * * ? *)"
}

resource "aws_cloudwatch_event_target" "fetch_jma_daily" {
  rule = aws_cloudwatch_event_rule.fetch_jma_daily.name
  arn  = aws_lambda_function.fetch_jma.arn
}

resource "aws_lambda_permission" "allow_eventbridge_fetch_jma" {
  statement_id  = "AllowEventBridgeInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.fetch_jma.function_name
  principal     = "events.amazonaws.com"
  source_arn    = aws_cloudwatch_event_rule.fetch_jma_daily.arn
}
