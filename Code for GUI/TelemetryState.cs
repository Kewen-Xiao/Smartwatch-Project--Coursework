using System;

namespace Jdy31OledMonitor
{
    // In-memory snapshot of the latest telemetry values displayed by the dashboard.
    public sealed class TelemetryState
    {
        public DateTime? DeviceTime { get; set; }
        public int? TemperatureC { get; set; }
        public int? HumidityPercent { get; set; }
        public string ClimateComfort { get; set; }
        public int? HeartRateBpm { get; set; }
        public int? Spo2Percent { get; set; }
        public bool? FingerPresent { get; set; }
        public long? StepCount { get; set; }
        public int? LightPercent { get; set; }
        public int? AdcRaw { get; set; }
        public string PositionSource { get; set; }
        public char? PositionFix { get; set; }
        public string Latitude { get; set; }
        public string Longitude { get; set; }
        public int? Roll { get; set; }
        public int? Pitch { get; set; }
        public int? Yaw { get; set; }
        public int? AccelX { get; set; }
        public int? AccelY { get; set; }
        public int? AccelZ { get; set; }
        public int? GyroX { get; set; }
        public int? GyroY { get; set; }
        public int? GyroZ { get; set; }
        public int? TemperatureWarn { get; set; }
        public int? HeartRateWarn { get; set; }
        public DateTime LastReceivedAt { get; set; }
        public string LastRawLine { get; set; }
        public int PacketCount { get; set; }

        public TelemetryState()
        {
            ClimateComfort = string.Empty;
            PositionSource = "NONE";
            Latitude = string.Empty;
            Longitude = string.Empty;
            TemperatureWarn = 30;
            HeartRateWarn = 120;
            LastRawLine = string.Empty;
        }

        // The parser updates a cloned copy so omitted fields can keep their previous value.
        public TelemetryState Clone()
        {
            return new TelemetryState
            {
                DeviceTime = DeviceTime,
                TemperatureC = TemperatureC,
                HumidityPercent = HumidityPercent,
                ClimateComfort = ClimateComfort,
                HeartRateBpm = HeartRateBpm,
                Spo2Percent = Spo2Percent,
                FingerPresent = FingerPresent,
                StepCount = StepCount,
                LightPercent = LightPercent,
                AdcRaw = AdcRaw,
                PositionSource = PositionSource,
                PositionFix = PositionFix,
                Latitude = Latitude,
                Longitude = Longitude,
                Roll = Roll,
                Pitch = Pitch,
                Yaw = Yaw,
                AccelX = AccelX,
                AccelY = AccelY,
                AccelZ = AccelZ,
                GyroX = GyroX,
                GyroY = GyroY,
                GyroZ = GyroZ,
                TemperatureWarn = TemperatureWarn,
                HeartRateWarn = HeartRateWarn,
                LastReceivedAt = LastReceivedAt,
                LastRawLine = LastRawLine,
                PacketCount = PacketCount
            };
        }

        // A packet is considered fresh for a short time window so the UI can show stale-data hints.
        public bool HasFreshPacket
        {
            get { return LastReceivedAt != DateTime.MinValue && (DateTime.Now - LastReceivedAt).TotalSeconds <= 5; }
        }

        // Position is only valid when both coordinates exist and the fix flag reports an active solution.
        public bool HasValidPosition
        {
            get { return !string.IsNullOrWhiteSpace(Latitude) && !string.IsNullOrWhiteSpace(Longitude) && PositionFix == 'A'; }
        }

        // Human-readable labels used by both the metric cards and the OLED preview.
        public string TemperatureComfort
        {
            get
            {
                if (!TemperatureC.HasValue)
                {
                    return "--";
                }

                if (TemperatureC.Value < 20)
                {
                    return "Cold";
                }

                if (TemperatureC.Value > 26)
                {
                    return "Hot";
                }

                return "Comfort";
            }
        }

        public string HumidityComfort
        {
            get
            {
                if (!HumidityPercent.HasValue)
                {
                    return "--";
                }

                if (HumidityPercent.Value < 30)
                {
                    return "Dry";
                }

                if (HumidityPercent.Value > 60)
                {
                    return "Humid";
                }

                return "Comfort";
            }
        }

        public string LightLevel
        {
            get
            {
                if (!LightPercent.HasValue)
                {
                    return "--";
                }

                if (LightPercent.Value >= 70)
                {
                    return "Bright";
                }

                if (LightPercent.Value >= 35)
                {
                    return "Normal";
                }

                return "Dark";
            }
        }

        // Mirror the embedded firmware wording so the desktop preview matches the physical device.
        public string FingerText
        {
            get
            {
                if (!FingerPresent.HasValue)
                {
                    return "--";
                }

                return FingerPresent.Value ? "On" : "Off";
            }
        }
    }
}
