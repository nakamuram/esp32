output "api_endpoint" {
  description = "ESP32 が POST するエンドポイント URL（secrets.h の AWS_API_ENDPOINT に設定する）"
  value       = "${aws_apigatewayv2_stage.default.invoke_url}readings"
}

output "dashboard_url" {
  description = "CloudWatch ダッシュボードの URL"
  value       = "https://${var.aws_region}.console.aws.amazon.com/cloudwatch/home?region=${var.aws_region}#dashboards:name=${aws_cloudwatch_dashboard.sensor.dashboard_name}"
}
