data "archive_file" "ingest" {
  type        = "zip"
  source_file = "${path.module}/lambda_src/ingest.py"
  output_path = "${path.module}/build/ingest.zip"
}

resource "aws_iam_role" "lambda_ingest" {
  name = "${var.project_name}-lambda-ingest-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

# 最小権限: 対象テーブルへの書き込みのみ許可（読み取り・削除は不要）
resource "aws_iam_role_policy" "lambda_ingest" {
  name = "${var.project_name}-lambda-ingest-policy"
  role = aws_iam_role.lambda_ingest.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["dynamodb:PutItem"]
        Resource = aws_dynamodb_table.sensor_readings.arn
      },
      {
        # CloudWatch PutMetricData はリソースレベルの権限指定に対応していないため "*" になる
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
        Resource = "${aws_cloudwatch_log_group.lambda_ingest.arn}:*"
      }
    ]
  })
}

resource "aws_cloudwatch_log_group" "lambda_ingest" {
  name              = "/aws/lambda/${var.project_name}-ingest"
  retention_in_days = 14
}

resource "aws_lambda_function" "ingest" {
  function_name    = "${var.project_name}-ingest"
  role             = aws_iam_role.lambda_ingest.arn
  handler          = "ingest.handler"
  runtime          = "python3.12"
  architectures    = ["arm64"]
  filename         = data.archive_file.ingest.output_path
  source_code_hash = data.archive_file.ingest.output_base64sha256
  timeout          = 10
  memory_size      = 128

  environment {
    variables = {
      TABLE_NAME       = aws_dynamodb_table.sensor_readings.name
      METRIC_NAMESPACE = var.project_name
      DEVICE_SECRET    = var.device_secret
    }
  }

  depends_on = [aws_cloudwatch_log_group.lambda_ingest]
}
