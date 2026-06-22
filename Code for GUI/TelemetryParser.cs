using System;
using System.Collections.Generic;
using System.Globalization;

namespace Jdy31OledMonitor
{
    // Parses one telemetry sentence produced by the STM32 firmware and merges it into the previous state.
    public static class TelemetryParser
    {
        public static bool TryParseLine(string rawLine, TelemetryState previousState, out TelemetryState nextState, out string errorMessage)
        {
            // Start from the previous snapshot so partial packets only need to update the fields they actually carry.
            nextState = previousState != null ? previousState.Clone() : new TelemetryState();
            errorMessage = string.Empty;

            if (string.IsNullOrWhiteSpace(rawLine))
            {
                errorMessage = "Empty line.";
                return false;
            }

            string line = rawLine.Trim();
            string[] tokens = line.Split(new[] { ',' }, StringSplitOptions.None);
            bool recognizedAnyField = false;

            string tailLatitude = null;
            string tailLongitude = null;

            // The firmware appends latitude/longitude as the last two comma-separated items without explicit keys.
            if (tokens.Length >= 2)
            {
                string maybeLat = tokens[tokens.Length - 2].Trim();
                string maybeLon = tokens[tokens.Length - 1].Trim();
                if (maybeLat.IndexOf('=') < 0 && maybeLon.IndexOf('=') < 0)
                {
                    tailLatitude = maybeLat;
                    tailLongitude = maybeLon;
                }
            }

            for (int i = 0; i < tokens.Length; i++)
            {
                string token = tokens[i].Trim();
                if (token.Length == 0)
                {
                    continue;
                }

                if (i >= tokens.Length - 2 && token.IndexOf('=') < 0 && tailLatitude != null)
                {
                    continue;
                }

                int index = token.IndexOf('=');
                if (index <= 0)
                {
                    continue;
                }

                string key = token.Substring(0, index).Trim().ToUpperInvariant();
                string value = token.Substring(index + 1).Trim();

                // Accept multiple aliases so the desktop tool remains compatible with older packet formats.
                switch (key)
                {
                    case "TIME":
                        DateTime parsedTime;
                        if (DateTime.TryParseExact(value, "yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture, DateTimeStyles.None, out parsedTime) ||
                            DateTime.TryParse(value, CultureInfo.InvariantCulture, DateTimeStyles.None, out parsedTime))
                        {
                            nextState.DeviceTime = parsedTime;
                            recognizedAnyField = true;
                        }
                        break;

                    case "TEMP":
                    case "TEMPERATURE":
                        nextState.TemperatureC = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "HUMI":
                    case "HUMIDITY":
                        nextState.HumidityPercent = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "COMFORT":
                        nextState.ClimateComfort = ParseNullableText(value);
                        recognizedAnyField = true;
                        break;

                    case "HR":
                    case "HEARTRATE":
                        nextState.HeartRateBpm = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "SPO2":
                        nextState.Spo2Percent = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "FINGER":
                        nextState.FingerPresent = ParseNullableBool(value);
                        recognizedAnyField = true;
                        break;

                    case "STEP":
                    case "STEPS":
                        nextState.StepCount = ParseNullableLong(value);
                        recognizedAnyField = true;
                        break;

                    case "LIGHT":
                    case "LIGHT_PERCENT":
                        nextState.LightPercent = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "ADC_RAW":
                    case "LIGHT_RAW":
                    case "ADC":
                        nextState.AdcRaw = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "POSSRC":
                    case "GPSSRC":
                    case "SRC":
                        nextState.PositionSource = NormalizePositionSource(value);
                        recognizedAnyField = true;
                        break;

                    case "POSFIX":
                    case "FIX":
                        nextState.PositionFix = ParseNullableChar(value);
                        recognizedAnyField = true;
                        break;

                    case "LAT":
                    case "LATITUDE":
                        nextState.Latitude = ParseNullableText(value);
                        recognizedAnyField = true;
                        break;

                    case "LON":
                    case "LONGITUDE":
                        nextState.Longitude = ParseNullableText(value);
                        recognizedAnyField = true;
                        break;

                    case "ROLL":
                        nextState.Roll = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "PITCH":
                        nextState.Pitch = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "YAW":
                        nextState.Yaw = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "AX":
                    case "ACCEL_X":
                        nextState.AccelX = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "AY":
                    case "ACCEL_Y":
                        nextState.AccelY = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "AZ":
                    case "ACCEL_Z":
                        nextState.AccelZ = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "GX":
                    case "GYRO_X":
                        nextState.GyroX = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "GY":
                    case "GYRO_Y":
                        nextState.GyroY = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "GZ":
                    case "GYRO_Z":
                        nextState.GyroZ = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "TEMP_WARN":
                    case "TEMPWARNING":
                        nextState.TemperatureWarn = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;

                    case "HR_WARN":
                    case "HRWARNING":
                        nextState.HeartRateWarn = ParseNullableInt(value);
                        recognizedAnyField = true;
                        break;
                }
            }

            if (tailLatitude != null)
            {
                nextState.Latitude = ParseNullableText(tailLatitude);
                nextState.Longitude = ParseNullableText(tailLongitude);
                recognizedAnyField = true;
            }

            // Clear placeholder coordinates when the packet explicitly says that no active fix is available.
            if (nextState.PositionFix.HasValue && nextState.PositionFix.Value != 'A')
            {
                if (string.IsNullOrWhiteSpace(nextState.Latitude) || nextState.Latitude == "--")
                {
                    nextState.Latitude = string.Empty;
                }

                if (string.IsNullOrWhiteSpace(nextState.Longitude) || nextState.Longitude == "--")
                {
                    nextState.Longitude = string.Empty;
                }
            }

            if (!recognizedAnyField)
            {
                errorMessage = "No supported JDY-31 telemetry field was found in line.";
                return false;
            }

            nextState.LastReceivedAt = DateTime.Now;
            nextState.LastRawLine = line;
            nextState.PacketCount = nextState.PacketCount + 1;
            return true;
        }

        // Helper parsers normalize the placeholder text emitted by the firmware into nullable .NET values.
        private static int? ParseNullableInt(string value)
        {
            int parsed;
            if (string.IsNullOrWhiteSpace(value) || value == "--" || value == "---" || value == "N/A")
            {
                return null;
            }

            if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed))
            {
                return parsed;
            }

            double parsedDouble;
            if (double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out parsedDouble))
            {
                return (int)Math.Round(parsedDouble, MidpointRounding.AwayFromZero);
            }

            return null;
        }

        private static long? ParseNullableLong(string value)
        {
            long parsed;
            if (string.IsNullOrWhiteSpace(value) || value == "--" || value == "---" || value == "N/A")
            {
                return null;
            }

            if (long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed))
            {
                return parsed;
            }

            return null;
        }

        private static bool? ParseNullableBool(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value == "--")
            {
                return null;
            }

            string normalized = value.Trim().ToUpperInvariant();
            if (normalized == "ON" || normalized == "1" || normalized == "TRUE" || normalized == "YES")
            {
                return true;
            }

            if (normalized == "OFF" || normalized == "0" || normalized == "FALSE" || normalized == "NO")
            {
                return false;
            }

            return null;
        }

        private static char? ParseNullableChar(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value == "--")
            {
                return null;
            }

            return char.ToUpperInvariant(value.Trim()[0]);
        }

        private static string ParseNullableText(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value == "--" || value == "---" || value == "N/A")
            {
                return string.Empty;
            }

            return value.Trim();
        }

        // Keep well-known source tags normalized while still preserving unfamiliar strings for debugging.
        private static string NormalizePositionSource(string value)
        {
            string text = ParseNullableText(value);
            if (text.Length == 0)
            {
                return "NONE";
            }

            text = text.ToUpperInvariant();
            if (text == "GNSS" || text == "GPS" || text == "BT" || text == "NONE")
            {
                return text;
            }

            return value.Trim();
        }
    }
}
