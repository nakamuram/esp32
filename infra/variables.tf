variable "aws_region" {
  description = "リソースを作成するリージョン"
  type        = string
  default     = "ap-northeast-1"
}

variable "project_name" {
  description = "リソース名の接頭辞（[project_name]-[function] の形式で命名）"
  type        = string
  default     = "esp32-sensor"
}

variable "device_secret" {
  description = "ESP32 からのリクエストを検証する共有シークレット（ヘッダー x-device-secret と照合）"
  type        = string
  sensitive   = true
}

variable "device_id" {
  description = "CloudWatch ダッシュボードで参照するデバイス ID（firmware/include/secrets.h の DEVICE_ID と一致させる）"
  type        = string
  default     = "esp32-sensor-01"
}

variable "amedas_station_code" {
  description = "比較対象とする気象庁アメダス観測所コード・1地点目（設置場所に非公開のため既定値は設定しない。terraform.tfvars で指定する）"
  type        = string
  sensitive   = true
}

variable "amedas_station_code_2" {
  description = "比較対象とする気象庁アメダス観測所コード・2地点目（設置場所に非公開のため既定値は設定しない。terraform.tfvars で指定する）"
  type        = string
  sensitive   = true
}

variable "jma_reference_id" {
  description = "気象庁データ(1地点目)をDynamoDB/CloudWatchで区別するための識別子（設置場所を推測されないよう地名を含めない）"
  type        = string
  default     = "jma-reference"
}

variable "jma_reference_id_2" {
  description = "気象庁データ(2地点目)をDynamoDB/CloudWatchで区別するための識別子（設置場所を推測されないよう地名を含めない）"
  type        = string
  default     = "jma-reference-2"
}
