resource "aws_cloudwatch_dashboard" "sensor" {
  dashboard_name = "${var.project_name}-dashboard"

  dashboard_body = jsonencode({
    widgets = [
      {
        type   = "metric"
        x      = 0
        y      = 0
        width  = 12
        height = 6
        properties = {
          title  = "気圧 (hPa)"
          view   = "timeSeries"
          region = var.aws_region
          period = 3600
          stat   = "Average"
          metrics = [
            [var.project_name, "Pressure", "DeviceId", var.device_id, { color = "#1f77b4", label = "センサー実測" }],
            [var.project_name, "Pressure", "DeviceId", var.jma_reference_id, { color = "#7f7f7f", label = "気象庁(参考1)" }],
            [var.project_name, "Pressure", "DeviceId", var.jma_reference_id_2, { color = "#bcbd22", label = "気象庁(参考2)" }]
          ]
        }
      },
      {
        type   = "metric"
        x      = 12
        y      = 0
        width  = 12
        height = 6
        properties = {
          title  = "温度 (C)"
          view   = "timeSeries"
          region = var.aws_region
          period = 3600
          stat   = "Average"
          metrics = [
            [var.project_name, "Temperature", "DeviceId", var.device_id, { color = "#d62728", label = "センサー実測" }],
            [var.project_name, "Temperature", "DeviceId", var.jma_reference_id, { color = "#7f7f7f", label = "気象庁(参考1)" }],
            [var.project_name, "Temperature", "DeviceId", var.jma_reference_id_2, { color = "#bcbd22", label = "気象庁(参考2)" }]
          ]
        }
      },
      {
        type   = "metric"
        x      = 0
        y      = 6
        width  = 12
        height = 6
        properties = {
          title  = "日照時間 過去24h (h)"
          view   = "timeSeries"
          region = var.aws_region
          period = 86400
          stat   = "Maximum"
          metrics = [
            [var.project_name, "SunshineHours24h", "DeviceId", "light-estimate", { color = "#ff7f0e", label = "気象庁(参考2)日照時間" }]
          ]
        }
      },
      {
        type   = "metric"
        x      = 12
        y      = 6
        width  = 12
        height = 6
        properties = {
          title  = "気温差 日中平均 (C, センサー-気象庁参考2)"
          view   = "timeSeries"
          region = var.aws_region
          period = 86400
          stat   = "Maximum"
          metrics = [
            [var.project_name, "TempDiffAvg", "DeviceId", "light-estimate", { color = "#9467bd", label = "日当たり傾向(推定)" }]
          ]
        }
      },
      {
        type   = "metric"
        x      = 0
        y      = 12
        width  = 12
        height = 6
        properties = {
          title  = "日照時間 毎時 (h, 気象庁参考2)"
          view   = "timeSeries"
          region = var.aws_region
          period = 3600
          stat   = "Average"
          metrics = [
            [var.project_name, "SunshineHours", "DeviceId", var.jma_reference_id_2, { color = "#ff7f0e", label = "気象庁(参考2)日照時間/h" }]
          ]
        }
      },
    ]
  })
}
