#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "index.h"

// uncomment the line below to enable logging into serial
#define DEBUG_SERIAL

namespace {
constexpr uint8_t LED_PIN = 12;
constexpr uint16_t LED_COUNT = 30;
constexpr uint8_t DIGIT_COUNT = 4;
constexpr uint8_t SEGMENTS_PER_DIGIT = 7;
constexpr uint8_t DOT_LEFT_INDEX = 14;
constexpr uint8_t DOT_RIGHT_INDEX = 15;
constexpr uint8_t HOUR_TENS_DIGIT_INDEX = 0;
constexpr char CONFIG_PATH[] = "/config.json";
constexpr uint32_t DISPLAY_REFRESH_MS = 250;
constexpr size_t JSON_CAPACITY = 3072;
constexpr uint8_t NTP_MAX_ATTEMPTS = 40;
constexpr uint16_t NTP_RETRY_DELAY_MS = 250;
constexpr uint32_t NTP_SYNC_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t ALARM_DURATION_MS = 5UL * 60UL * 1000UL;


// Segment encoding order: A, B, C, D, E, F, G (bit 0 = segment A)
constexpr uint8_t DIGIT_SEGMENTS[10] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111  // 9
};

// Custom LED order on the strip (digit wiring: 1=e, 2=d, 3=c, 4=g, 5=f, 6=a, 7=b).
constexpr uint8_t SEGMENT_LED_OFFSET[SEGMENTS_PER_DIGIT] = {
    5, // segment A uses LED 6
    6, // segment B uses LED 7
    2, // segment C uses LED 3
    1, // segment D uses LED 2
    0, // segment E uses LED 1
    4, // segment F uses LED 5
    3  // segment G uses LED 4
};

struct Color {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};

  Color() = default;
  Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

struct PowerSettings {
  bool powerOn{true};
  String startupMode{"clock"};
  String mode{"clock"};
  bool exitSpecialMode{false};
};

struct TimeSettings {
  uint8_t hour{12};
  uint8_t minute{0};
  uint8_t second{0};
};

struct DisplaySettings {
  uint8_t brightness{80};
  Color generalColor{255, 85, 0};
  bool perDigitEnabled{false};
  Color perDigitColor[DIGIT_COUNT];
  struct QuietHoursSettings {
    bool enabled{false};
    uint8_t startHour{23};
    uint8_t startMinute{0};
    uint8_t endHour{7};
    uint8_t endMinute{0};
    uint8_t dimBrightness{0};
  } quietHours;
};

struct DotsSettings {
  bool enabled{true};
  Color leftColor{255, 255, 255};
  Color rightColor{255, 255, 255};
  bool forceOverride{false};
  Color forcedColor{255, 0, 0};
};

struct AlarmSettings {
  bool enabled{false};
  uint8_t hour{7};
  uint8_t minute{0};
  bool active{false};
  unsigned long startMs{0};
  uint8_t lastTriggerHour{255};
  uint8_t lastTriggerMinute{255};
  uint8_t daysMask{0x7F};  // bits 0-6 represent Sunday-Saturday
};

struct NetworkSettings {
  String ntpServer{"pool.ntp.org"};
  int16_t utcOffsetMinutes{0};
};

struct ClockConfig {
  PowerSettings power;
  TimeSettings time;
  DisplaySettings display;
  DotsSettings dots;
  AlarmSettings alarm;
  NetworkSettings network;
};

enum class OperatingMode { Clock, Timer, Weather, Custom, Alarm, Off };

ClockConfig config;
TimeSettings timeAnchor;
unsigned long timeReferenceMs = 0;
unsigned long lastDisplayRefresh = 0;
uint8_t currentAppliedBrightness = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long lastNtpAttemptMs = 0;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);
WiFiManager wifiManager;

bool syncTimeFromNtp();

Color hexToColor(const String &value, const Color &fallback) {
  if (value.length() != 7 || value.charAt(0) != '#') {
    return fallback;
  }
  uint32_t number = strtoul(value.substring(1).c_str(), nullptr, 16);
  return Color(static_cast<uint8_t>((number >> 16) & 0xFF),
               static_cast<uint8_t>((number >> 8) & 0xFF),
               static_cast<uint8_t>(number & 0xFF));
}

String colorToHex(const Color &color) {
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
  return String(buffer);
}

uint32_t asPixelColor(const Color &color) {
  return strip.Color(color.r, color.g, color.b);
}

void attachCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
}

void sendJson(const JsonDocument &doc, int code = 200) {
  String payload;
  serializeJson(doc, payload);
  attachCorsHeaders();
  server.send(code, "application/json", payload);
}

void sendJsonError(const String &message, int code = 400) {
  StaticJsonDocument<256> doc;
  doc["error"] = message;
  sendJson(doc, code);
}

OperatingMode modeFromString(String value) {
  value.toLowerCase();
  if (value == "clock") {
    return OperatingMode::Clock;
  }
  if (value == "timer") {
    return OperatingMode::Timer;
  }
  if (value == "weather") {
    return OperatingMode::Weather;
  }
  if (value == "custom") {
    return OperatingMode::Custom;
  }
  if (value == "alarm") {
    return OperatingMode::Alarm;
  }
  if (value == "off") {
    return OperatingMode::Off;
  }
  return OperatingMode::Clock;
}

String modeToString(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::Clock:
      return "clock";
    case OperatingMode::Timer:
      return "timer";
    case OperatingMode::Weather:
      return "weather";
    case OperatingMode::Custom:
      return "custom";
    case OperatingMode::Alarm:
      return "alarm";
    case OperatingMode::Off:
      return "off";
  }
  return "clock";
}

String sanitizeMode(const String &value) {
  return modeToString(modeFromString(value));
}

TimeSettings computeCurrentTime();
void applyDisplaySettingsWithTime(const TimeSettings &time);
void stopAlarm();
bool isAlarmScheduledToday(uint8_t weekday);
uint8_t computeWeekdayFromSeconds(uint32_t seconds);

void applyDisplaySettings() {
  applyDisplaySettingsWithTime(computeCurrentTime());
}

void loadDefaultConfig() {
  config.power = PowerSettings();
  config.time = TimeSettings();
  config.display = DisplaySettings();
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    config.display.perDigitColor[i] = config.display.generalColor;
  }
  config.dots = DotsSettings();
  config.alarm = AlarmSettings();
  config.network = NetworkSettings();
}

bool saveConfig() {
  if (!LittleFS.begin()) {
    return false;
  }
  File file = LittleFS.open(CONFIG_PATH, "w");
  if (!file) {
    return false;
  }
  StaticJsonDocument<JSON_CAPACITY> doc;
  JsonObject power = doc["power"].to<JsonObject>();
  power["power_on"] = config.power.powerOn;
  power["startup_mode"] = config.power.startupMode;
  power["mode"] = config.power.mode;
  power["exit_special_mode"] = config.power.exitSpecialMode;

  JsonObject time = doc["time"].to<JsonObject>();
  time["hour"] = config.time.hour;
  time["minute"] = config.time.minute;
  time["second"] = config.time.second;

  JsonObject display = doc["display"].to<JsonObject>();
  display["brightness"] = config.display.brightness;
  display["general_color"] = colorToHex(config.display.generalColor);
  JsonObject perDigit = display["per_digit_color"].to<JsonObject>();
  perDigit["enabled"] = config.display.perDigitEnabled;
  JsonArray perDigitValues = perDigit["values"].to<JsonArray>();
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    perDigitValues.add(colorToHex(config.display.perDigitColor[i]));
  }
  JsonObject quiet = display["quiet_hours"].to<JsonObject>();
  quiet["enabled"] = config.display.quietHours.enabled;
  quiet["start_hour"] = config.display.quietHours.startHour;
  quiet["start_minute"] = config.display.quietHours.startMinute;
  quiet["end_hour"] = config.display.quietHours.endHour;
  quiet["end_minute"] = config.display.quietHours.endMinute;
  quiet["dim_brightness"] = config.display.quietHours.dimBrightness;

  JsonObject dots = doc["dots"].to<JsonObject>();
  dots["enabled"] = config.dots.enabled;
  dots["left_color"] = colorToHex(config.dots.leftColor);
  dots["right_color"] = colorToHex(config.dots.rightColor);
  dots["force_override"] = config.dots.forceOverride;
  dots["forced_color"] = colorToHex(config.dots.forcedColor);

  JsonObject alarm = doc["alarm"].to<JsonObject>();
  alarm["enabled"] = config.alarm.enabled;
  alarm["hour"] = config.alarm.hour;
  alarm["minute"] = config.alarm.minute;
  alarm["days_mask"] = config.alarm.daysMask;

  JsonObject network = doc["network"].to<JsonObject>();
  network["ntp_server"] = config.network.ntpServer;
  network["utc_offset_minutes"] = config.network.utcOffsetMinutes;

  bool ok = serializeJsonPretty(doc, file) > 0;
  file.close();
  return ok;
}

bool loadConfig() {
  if (!LittleFS.begin()) {
    LittleFS.format();
    if (!LittleFS.begin()) {
      return false;
    }
  }

  if (!LittleFS.exists(CONFIG_PATH)) {
    loadDefaultConfig();
    saveConfig();
    return true;
  }

  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) {
    loadDefaultConfig();
    return false;
  }

  StaticJsonDocument<JSON_CAPACITY> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    loadDefaultConfig();
    saveConfig();
    return false;
  }

  JsonObject power = doc["power"].as<JsonObject>();
  if (!power.isNull()) {
    config.power.powerOn = power["power_on"].as<bool>();
    config.power.startupMode = sanitizeMode(power["startup_mode"].as<String>());
    config.power.mode = sanitizeMode(power["mode"].as<String>());
    config.power.exitSpecialMode = false;
  }

  JsonObject time = doc["time"].as<JsonObject>();
  if (!time.isNull()) {
    config.time.hour = constrain(time["hour"].as<int>(), 0, 23);
    config.time.minute = constrain(time["minute"].as<int>(), 0, 59);
    config.time.second = constrain(time["second"].as<int>(), 0, 59);
  }

  JsonObject display = doc["display"].as<JsonObject>();
  if (!display.isNull()) {
    config.display.brightness = constrain(display["brightness"].as<int>(), 1, 255);
    config.display.generalColor = hexToColor(display["general_color"].as<String>(), config.display.generalColor);

    JsonObject perDigit = display["per_digit_color"].as<JsonObject>();
    if (!perDigit.isNull()) {
      config.display.perDigitEnabled = perDigit["enabled"].as<bool>();
      JsonArray values = perDigit["values"].as<JsonArray>();
      for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
        if (!values.isNull() && i < values.size()) {
          config.display.perDigitColor[i] = hexToColor(values[i].as<String>(), config.display.generalColor);
        } else {
          config.display.perDigitColor[i] = config.display.generalColor;
        }
      }
    }
    JsonObject quiet = display["quiet_hours"].as<JsonObject>();
    if (!quiet.isNull()) {
      config.display.quietHours.enabled = quiet["enabled"].as<bool>();
      config.display.quietHours.startHour = constrain(quiet["start_hour"].as<int>(), 0, 23);
      config.display.quietHours.startMinute = constrain(quiet["start_minute"].as<int>(), 0, 59);
      config.display.quietHours.endHour = constrain(quiet["end_hour"].as<int>(), 0, 23);
      config.display.quietHours.endMinute = constrain(quiet["end_minute"].as<int>(), 0, 59);
      config.display.quietHours.dimBrightness =
          constrain(quiet["dim_brightness"].as<int>(), 0, 255);
    }
  }

  JsonObject dots = doc["dots"].as<JsonObject>();
  if (!dots.isNull()) {
    config.dots.enabled = dots["enabled"].as<bool>();
    config.dots.leftColor = hexToColor(dots["left_color"].as<String>(), config.dots.leftColor);
    config.dots.rightColor = hexToColor(dots["right_color"].as<String>(), config.dots.rightColor);
    if (!dots["force_override"].isNull()) {
      config.dots.forceOverride = dots["force_override"].as<bool>();
    }
    config.dots.forcedColor = hexToColor(dots["forced_color"].as<String>(), config.dots.forcedColor);
  }

  JsonObject alarm = doc["alarm"].as<JsonObject>();
  if (!alarm.isNull()) {
    config.alarm.enabled = alarm["enabled"].as<bool>();
    config.alarm.hour = constrain(alarm["hour"].as<int>(), 0, 23);
    config.alarm.minute = constrain(alarm["minute"].as<int>(), 0, 59);
    if (!alarm["days_mask"].isNull()) {
      config.alarm.daysMask = alarm["days_mask"].as<uint8_t>();
    }
  }

  JsonObject network = doc["network"].as<JsonObject>();
  if (!network.isNull()) {
    String ntp = network["ntp_server"].as<String>();
    if (ntp.length() > 0) {
      config.network.ntpServer = ntp;
    }
    if (!network["utc_offset_minutes"].isNull()) {
      config.network.utcOffsetMinutes =
          constrain(network["utc_offset_minutes"].as<int>(), -720, 840);  // -12h to +14h
    }
  }

  return true;
}

uint32_t timeToSeconds(const TimeSettings &time) {
  return static_cast<uint32_t>(time.hour) * 3600UL + static_cast<uint32_t>(time.minute) * 60UL + time.second;
}

TimeSettings secondsToTime(uint32_t seconds) {
  seconds %= 24UL * 3600UL;
  TimeSettings result;
  result.hour = seconds / 3600UL;
  seconds %= 3600UL;
  result.minute = seconds / 60UL;
  result.second = seconds % 60UL;
  return result;
}

TimeSettings computeCurrentTime() {
  uint32_t elapsed = (millis() - timeReferenceMs) / 1000UL;
  uint32_t anchorSeconds = timeToSeconds(timeAnchor);
  return secondsToTime(anchorSeconds + elapsed);
}

uint16_t minutesFromComponents(uint8_t hour, uint8_t minute) {
  return static_cast<uint16_t>(hour) * 60 + static_cast<uint16_t>(minute);
}

bool isInRangeWrap(uint16_t current, uint16_t start, uint16_t end) {
  if (start == end) {
    return true;
  }
  if (start < end) {
    return current >= start && current < end;
  }
  return current >= start || current < end;
}

bool isQuietHoursActive(const TimeSettings &time) {
  if (!config.display.quietHours.enabled) {
    return false;
  }
  const uint16_t start = minutesFromComponents(config.display.quietHours.startHour,
                                               config.display.quietHours.startMinute);
  const uint16_t end = minutesFromComponents(config.display.quietHours.endHour,
                                             config.display.quietHours.endMinute);
  const uint16_t current = minutesFromComponents(time.hour, time.minute);
  return isInRangeWrap(current, start, end);
}

uint8_t computeWeekdayFromSeconds(uint32_t seconds) {
  (void)seconds;
  if (lastNtpSyncMs == 0) {
    return 7;
  }
  time_t now = time(nullptr);
  if (now <= 0) {
    return 7;
  }
  struct tm timeInfo;
#if defined(ESP8266)
  if (gmtime_r(&now, &timeInfo) == nullptr) {
    return 7;
  }
#else
  struct tm *tmp = gmtime(&now);
  if (!tmp) {
    return 7;
  }
  timeInfo = *tmp;
#endif
  return static_cast<uint8_t>(timeInfo.tm_wday);
}

void applyDisplaySettingsWithTime(const TimeSettings &time) {
  uint8_t desired = constrain(config.display.brightness, static_cast<uint8_t>(1), static_cast<uint8_t>(255));
  if (config.display.quietHours.enabled && isQuietHoursActive(time)) {
    desired =
        constrain(config.display.quietHours.dimBrightness, static_cast<uint8_t>(0), static_cast<uint8_t>(255));
  }
  if (desired != currentAppliedBrightness) {
    strip.setBrightness(desired);
    currentAppliedBrightness = desired;
  }
}

bool isAlarmScheduledToday(uint8_t weekday) {
  if (weekday > 6) {
    return true;
  }
  return config.alarm.daysMask & (1 << weekday);
}

void startAlarm(const TimeSettings &time) {
  config.alarm.active = true;
  config.alarm.startMs = millis();
  config.alarm.lastTriggerHour = time.hour;
  config.alarm.lastTriggerMinute = time.minute;
#ifdef DEBUG_SERIAL
  Serial.println(F("[Alarm] Triggered"));
#endif
}

void stopAlarm() {
  if (!config.alarm.active) {
    return;
  }
  config.alarm.active = false;
  config.alarm.startMs = 0;
#ifdef DEBUG_SERIAL
  Serial.println(F("[Alarm] Cleared"));
#endif
  applyDisplaySettings();
}

void updateAlarmState(const TimeSettings &time) {
  if (config.alarm.active) {
    if (millis() - config.alarm.startMs >= ALARM_DURATION_MS) {
      stopAlarm();
    }
    return;
  }

  if (!config.alarm.enabled) {
    config.alarm.lastTriggerHour = 255;
    config.alarm.lastTriggerMinute = 255;
    return;
  }

  uint8_t weekday = computeWeekdayFromSeconds(timeToSeconds(time));
  if (time.hour == config.alarm.hour && time.minute == config.alarm.minute &&
      isAlarmScheduledToday(weekday)) {
    bool alreadyTriggered = (config.alarm.lastTriggerHour == time.hour &&
                             config.alarm.lastTriggerMinute == time.minute);
    if (!alreadyTriggered) {
      startAlarm(time);
    }
  } else {
    config.alarm.lastTriggerHour = 255;
    config.alarm.lastTriggerMinute = 255;
  }
}

Color resolveDigitColor(uint8_t index) {
  if (config.display.perDigitEnabled) {
    return config.display.perDigitColor[index];
  }
  return config.display.generalColor;
}

void writeDigit(uint8_t digitIndex, uint8_t number, const Color &color,
                bool suppressLeadingZero = false) {
  const uint8_t baseIndex = (digitIndex < 2)
                                ? digitIndex * SEGMENTS_PER_DIGIT
                                : (digitIndex == 2 ? 16 : 23);
  uint8_t segments = (number < 10) ? DIGIT_SEGMENTS[number] : 0;
  if (suppressLeadingZero && number == 0) {
    segments = 0;
  }

  for (uint8_t segment = 0; segment < SEGMENTS_PER_DIGIT; ++segment) {
    uint8_t ledIndex = baseIndex + SEGMENT_LED_OFFSET[segment];
    if (ledIndex >= LED_COUNT) {
      continue;
    }
    bool enabled = segments & (1 << segment);
    if (enabled) {
      strip.setPixelColor(ledIndex, asPixelColor(color));
    } else {
      strip.setPixelColor(ledIndex, 0);
    }
  }
}

void renderDots(OperatingMode mode) {
  bool showDots = config.dots.enabled && mode != OperatingMode::Off;
  bool blink = (mode == OperatingMode::Timer) || (mode == OperatingMode::Alarm);
  bool dotsVisible = !blink || ((millis() / 500UL) % 2 == 0);

  uint32_t leftColor = 0;
  uint32_t rightColor = 0;

  if (showDots && dotsVisible) {
    Color left = config.dots.forceOverride ? config.dots.forcedColor : config.dots.leftColor;
    Color right = config.dots.forceOverride ? config.dots.forcedColor : config.dots.rightColor;
    leftColor = asPixelColor(left);
    rightColor = asPixelColor(right);
  }

  strip.setPixelColor(DOT_LEFT_INDEX, leftColor);
  strip.setPixelColor(DOT_RIGHT_INDEX, rightColor);
}

void renderClock(const TimeSettings &now) {
  const uint8_t hourTens = now.hour / 10;
  const uint8_t hourUnits = now.hour % 10;
  const uint8_t minuteTens = now.minute / 10;
  const uint8_t minuteUnits = now.minute % 10;

  uint8_t digits[DIGIT_COUNT] = {hourTens, hourUnits, minuteTens, minuteUnits};

  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    const bool suppressLeadingZero = (i == HOUR_TENS_DIGIT_INDEX);
    writeDigit(i, digits[i], resolveDigitColor(i), suppressLeadingZero);
  }
}

void renderClockWithColor(const TimeSettings &time, const Color &color) {
  const uint8_t hourTens = time.hour / 10;
  const uint8_t hourUnits = time.hour % 10;
  const uint8_t minuteTens = time.minute / 10;
  const uint8_t minuteUnits = time.minute % 10;
  uint8_t digits[DIGIT_COUNT] = {hourTens, hourUnits, minuteTens, minuteUnits};
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    const bool suppressLeadingZero = (i == HOUR_TENS_DIGIT_INDEX);
    writeDigit(i, digits[i], color, suppressLeadingZero);
  }
}

void renderSolidColor(const Color &color) {
  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    strip.setPixelColor(i, asPixelColor(color));
  }
}

void renderCustomMode(const TimeSettings &time) {
  if (config.display.perDigitEnabled) {
    renderClock(time);
  } else {
    renderSolidColor(config.display.generalColor);
  }
}

void updateDisplay() {
  OperatingMode mode = config.power.powerOn ? modeFromString(config.power.mode) : OperatingMode::Off;
  TimeSettings now = computeCurrentTime();
  updateAlarmState(now);
  if (config.alarm.active) {
    if (currentAppliedBrightness != 255) {
      strip.setBrightness(255);
      currentAppliedBrightness = 255;
    }
  } else {
    applyDisplaySettingsWithTime(now);
  }
  if ((mode == OperatingMode::Off || !config.power.powerOn) && !config.alarm.active) {
    strip.clear();
    strip.show();
    return;
  }

  if (config.alarm.active) {
    bool visible = ((millis() / 500UL) % 2 == 0);
    if (visible) {
      strip.clear();
      renderClockWithColor(now, Color(255, 255, 255));
      strip.setPixelColor(DOT_LEFT_INDEX, strip.Color(255, 255, 255));
      strip.setPixelColor(DOT_RIGHT_INDEX, strip.Color(255, 255, 255));
    } else {
      strip.clear();
    }
  } else {
    switch (mode) {
      case OperatingMode::Clock:
        renderClock(now);
        break;
      case OperatingMode::Timer:
        renderClock(now);
        break;
      case OperatingMode::Weather:
        renderSolidColor(config.display.generalColor);
        break;
      case OperatingMode::Custom:
        renderCustomMode(now);
        break;
      case OperatingMode::Alarm:
        renderClock(now);
        break;
      case OperatingMode::Off:
        strip.clear();
        break;
    }
    renderDots(mode);
  }

  strip.show();
}

String getRequestBody() {
  if (server.hasArg("plain")) {
    return server.arg("plain");
  }
  return String();
}

void handleCorsPreflight() {
  attachCorsHeaders();
  server.send(204);
}

void handleGetPower() {
  StaticJsonDocument<512> doc;
  JsonObject root = doc.to<JsonObject>();
  root["power_on"] = config.power.powerOn;
  root["mode"] = config.power.mode;
  root["startup_mode"] = config.power.startupMode;
  root["exit_special_mode"] = config.power.exitSpecialMode;
  sendJson(doc);
}

void handlePostPower() {
  StaticJsonDocument<JSON_CAPACITY / 4> doc;
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }
  if (!doc["power_on"].isNull()) {
    config.power.powerOn = doc["power_on"].as<bool>();
  }
  if (!doc["mode"].isNull()) {
    config.power.mode = sanitizeMode(doc["mode"].as<String>());
  }
  if (!doc["startup_mode"].isNull()) {
    config.power.startupMode = sanitizeMode(doc["startup_mode"].as<String>());
  }
  if (doc["exit_special_mode"].as<bool>()) {
    config.power.mode = config.power.startupMode;
    config.power.exitSpecialMode = false;
  }
  saveConfig();
  updateDisplay();
  handleGetPower();
}

void handleGetTime() {
  StaticJsonDocument<384> doc;
  JsonObject root = doc.to<JsonObject>();
  root["hour"] = config.time.hour;
  root["minute"] = config.time.minute;
  root["second"] = config.time.second;
  root["ntp_server"] = config.network.ntpServer;
  root["utc_offset_minutes"] = config.network.utcOffsetMinutes;
  TimeSettings now = computeCurrentTime();
  JsonObject current = root.createNestedObject("current");
  current["hour"] = now.hour;
  current["minute"] = now.minute;
  current["second"] = now.second;
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", now.hour, now.minute, now.second);
  current["formatted"] = buffer;
  sendJson(doc);
}

void handlePostTime() {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  bool ntpServerUpdated = false;

  if (!doc["hour"].isNull()) {
    config.time.hour = constrain(doc["hour"].as<int>(), 0, 23);
  }
  if (!doc["minute"].isNull()) {
    config.time.minute = constrain(doc["minute"].as<int>(), 0, 59);
  }
  if (!doc["second"].isNull()) {
    config.time.second = constrain(doc["second"].as<int>(), 0, 59);
  }
  if (!doc["ntp_server"].isNull()) {
    String ntp = doc["ntp_server"].as<String>();
    if (ntp.length() > 0) {
      config.network.ntpServer = ntp;
      ntpServerUpdated = true;
    }
  }
  if (!doc["utc_offset_minutes"].isNull()) {
    config.network.utcOffsetMinutes =
        constrain(doc["utc_offset_minutes"].as<int>(), -720, 840);
    ntpServerUpdated = true;  // re-sync to apply offset change
  }

  timeAnchor = config.time;
  timeReferenceMs = millis();
  if (ntpServerUpdated && WiFi.status() == WL_CONNECTED) {
    syncTimeFromNtp();
  }
  applyDisplaySettings();
  saveConfig();
  handleGetTime();
}

void handleGetDisplay() {
  StaticJsonDocument<JSON_CAPACITY / 2> doc;
  JsonObject root = doc.to<JsonObject>();
  root["brightness"] = config.display.brightness;
  root["general_color"] = colorToHex(config.display.generalColor);
  JsonObject perDigit = root.createNestedObject("per_digit_color");
  perDigit["enabled"] = config.display.perDigitEnabled;
  JsonArray values = perDigit.createNestedArray("values");
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    values.add(colorToHex(config.display.perDigitColor[i]));
  }
  JsonObject quiet = root.createNestedObject("quiet_hours");
  quiet["enabled"] = config.display.quietHours.enabled;
  quiet["start_hour"] = config.display.quietHours.startHour;
  quiet["start_minute"] = config.display.quietHours.startMinute;
  quiet["end_hour"] = config.display.quietHours.endHour;
  quiet["end_minute"] = config.display.quietHours.endMinute;
  quiet["dim_brightness"] = config.display.quietHours.dimBrightness;
  sendJson(doc);
}

void handlePostDisplay() {
  StaticJsonDocument<JSON_CAPACITY / 2> doc;
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (!doc["brightness"].isNull()) {
    config.display.brightness = constrain(doc["brightness"].as<int>(), 1, 255);
  }
  if (!doc["general_color"].isNull()) {
    config.display.generalColor = hexToColor(doc["general_color"].as<String>(), config.display.generalColor);
  }

  if (!doc["per_digit_color"].isNull()) {
    if (doc["per_digit_color"].is<JsonObject>()) {
      JsonObject perDigit = doc["per_digit_color"].as<JsonObject>();
      if (!perDigit["enabled"].isNull()) {
        config.display.perDigitEnabled = perDigit["enabled"].as<bool>();
      }
      if (perDigit["values"].is<JsonArray>()) {
        JsonArray values = perDigit["values"].as<JsonArray>();
        for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
          if (!values.isNull() && i < values.size()) {
            config.display.perDigitColor[i] = hexToColor(values[i].as<String>(), config.display.perDigitColor[i]);
          }
        }
      }
    } else if (doc["per_digit_color"].is<JsonArray>()) {
      config.display.perDigitEnabled = true;
      JsonArray values = doc["per_digit_color"].as<JsonArray>();
      for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
        if (!values.isNull() && i < values.size()) {
          config.display.perDigitColor[i] = hexToColor(values[i].as<String>(), config.display.perDigitColor[i]);
        }
      }
    }
  }

  if (!doc["quiet_hours"].isNull()) {
    JsonObject quiet = doc["quiet_hours"].as<JsonObject>();
    if (!quiet.isNull()) {
      if (!quiet["enabled"].isNull()) {
        config.display.quietHours.enabled = quiet["enabled"].as<bool>();
      }
      if (!quiet["start_hour"].isNull()) {
        config.display.quietHours.startHour = constrain(quiet["start_hour"].as<int>(), 0, 23);
      }
      if (!quiet["start_minute"].isNull()) {
        config.display.quietHours.startMinute = constrain(quiet["start_minute"].as<int>(), 0, 59);
      }
      if (!quiet["end_hour"].isNull()) {
        config.display.quietHours.endHour = constrain(quiet["end_hour"].as<int>(), 0, 23);
      }
      if (!quiet["end_minute"].isNull()) {
        config.display.quietHours.endMinute = constrain(quiet["end_minute"].as<int>(), 0, 59);
      }
      if (!quiet["dim_brightness"].isNull()) {
        config.display.quietHours.dimBrightness =
            constrain(quiet["dim_brightness"].as<int>(), 0, 255);
      }
    }
  }

  applyDisplaySettings();
  saveConfig();
  updateDisplay();
  handleGetDisplay();
}

void handleGetDots() {
  StaticJsonDocument<512> doc;
  JsonObject root = doc.to<JsonObject>();
  root["enabled"] = config.dots.enabled;
  root["left_color"] = colorToHex(config.dots.leftColor);
  root["right_color"] = colorToHex(config.dots.rightColor);
  root["force_override"] = config.dots.forceOverride;
  root["forced_color"] = colorToHex(config.dots.forcedColor);
  sendJson(doc);
}

void handlePostDots() {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (!doc["enabled"].isNull()) {
    config.dots.enabled = doc["enabled"].as<bool>();
  }
  if (!doc["left_color"].isNull()) {
    config.dots.leftColor = hexToColor(doc["left_color"].as<String>(), config.dots.leftColor);
  }
  if (!doc["right_color"].isNull()) {
    config.dots.rightColor = hexToColor(doc["right_color"].as<String>(), config.dots.rightColor);
  }
  if (!doc["force_override"].isNull()) {
    config.dots.forceOverride = doc["force_override"].as<bool>();
  }
  if (!doc["forced_color"].isNull()) {
    config.dots.forcedColor = hexToColor(doc["forced_color"].as<String>(), config.dots.forcedColor);
  }

  saveConfig();
  updateDisplay();
  handleGetDots();
}

void handleGetAlarm() {
  StaticJsonDocument<256> doc;
  JsonObject root = doc.to<JsonObject>();
  root["enabled"] = config.alarm.enabled;
  root["hour"] = config.alarm.hour;
  root["minute"] = config.alarm.minute;
  root["days_mask"] = config.alarm.daysMask;
  root["active"] = config.alarm.active;
  root["remaining_ms"] = config.alarm.active
                             ? max(0L, static_cast<long>(ALARM_DURATION_MS - (millis() - config.alarm.startMs)))
                             : 0;
  sendJson(doc);
}

void handlePostAlarm() {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (!doc["enabled"].isNull()) {
    config.alarm.enabled = doc["enabled"].as<bool>();
  }
  if (!doc["hour"].isNull()) {
    config.alarm.hour = constrain(doc["hour"].as<int>(), 0, 23);
  }
  if (!doc["minute"].isNull()) {
    config.alarm.minute = constrain(doc["minute"].as<int>(), 0, 59);
  }
  if (!doc["days_mask"].isNull()) {
    config.alarm.daysMask = doc["days_mask"].as<uint8_t>();
  }
  bool requestStop = doc["stop"].as<bool>();
  if (requestStop || !config.alarm.enabled) {
    stopAlarm();
  }

  saveConfig();
  handleGetAlarm();
}

void handleWebUi() {
  server.send_P(200, "text/html", WEB_UI_HTML);
}

void handleInfo() {
  StaticJsonDocument<256> doc;
  doc["project"] = "ESP8266 Clock";
  doc["status"] = "ok";
  doc["endpoints"] = F("/api/power, /api/time, /api/display, /api/dots, /api/alarm, /api/info");
  sendJson(doc);
}

void handleNotFound() {
  sendJsonError("Endpoint not found", 404);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleWebUi);
  server.on("/index.html", HTTP_GET, handleWebUi);

  server.on("/api/power", HTTP_GET, handleGetPower);
  server.on("/api/power", HTTP_POST, handlePostPower);
  server.on("/api/power", HTTP_OPTIONS, handleCorsPreflight);

  server.on("/api/time", HTTP_GET, handleGetTime);
  server.on("/api/time", HTTP_POST, handlePostTime);
  server.on("/api/time", HTTP_OPTIONS, handleCorsPreflight);

  server.on("/api/display", HTTP_GET, handleGetDisplay);
  server.on("/api/display", HTTP_POST, handlePostDisplay);
  server.on("/api/display", HTTP_OPTIONS, handleCorsPreflight);

  server.on("/api/dots", HTTP_GET, handleGetDots);
  server.on("/api/dots", HTTP_POST, handlePostDots);
  server.on("/api/dots", HTTP_OPTIONS, handleCorsPreflight);

  server.on("/api/alarm", HTTP_GET, handleGetAlarm);
  server.on("/api/alarm", HTTP_POST, handlePostAlarm);
  server.on("/api/alarm", HTTP_OPTIONS, handleCorsPreflight);

  server.on("/api/info", HTTP_GET, handleInfo);

  server.onNotFound(handleNotFound);
  server.begin();
}

bool syncTimeFromNtp() {
  if (WiFi.status() != WL_CONNECTED) {
#ifdef DEBUG_SERIAL
    Serial.println(F("[Clock] Cannot sync time: WiFi not connected"));
#endif  // DEBUG_SERIAL
    return false;
  }
  if (config.network.ntpServer.length() == 0) {
#ifdef DEBUG_SERIAL
    Serial.println(F("[Clock] Cannot sync time: NTP server not configured"));
#endif  // DEBUG_SERIAL
    return false;
  }

  lastNtpAttemptMs = millis();
#ifdef DEBUG_SERIAL
  Serial.print(F("[Clock] Syncing time via NTP: "));
  Serial.println(config.network.ntpServer);
#endif  // DEBUG_SERIAL
  configTime(0, 0, config.network.ntpServer.c_str());

  for (uint8_t attempt = 0; attempt < NTP_MAX_ATTEMPTS; ++attempt) {
    time_t now = time(nullptr);
    if (now > 100000) {
      const int32_t offsetSeconds = static_cast<int32_t>(config.network.utcOffsetMinutes) * 60;
      time_t adjusted = now + offsetSeconds;
      struct tm timeInfo;
#if defined(ESP8266)
      if (gmtime_r(&adjusted, &timeInfo) == nullptr) {
        delay(NTP_RETRY_DELAY_MS);
        yield();
        continue;
      }
#else
      struct tm *tmp = gmtime(&adjusted);
      if (!tmp) {
        delay(NTP_RETRY_DELAY_MS);
        yield();
        continue;
      }
      timeInfo = *tmp;
#endif
      config.time.hour = constrain(timeInfo.tm_hour, 0, 23);
      config.time.minute = constrain(timeInfo.tm_min, 0, 59);
      config.time.second = constrain(timeInfo.tm_sec, 0, 59);
      timeAnchor = config.time;
      timeReferenceMs = millis();
      lastNtpSyncMs = millis();
      lastNtpAttemptMs = lastNtpSyncMs;
#ifdef DEBUG_SERIAL
      Serial.printf("[Clock] NTP sync OK: %02u:%02u:%02u\n", config.time.hour, config.time.minute,
                    config.time.second);
#endif  // DEBUG_SERIAL
      return true;
    }
    delay(NTP_RETRY_DELAY_MS);
    yield();
  }

#ifdef DEBUG_SERIAL
  Serial.println(F("[Clock] Failed to sync time via NTP"));
#endif  // DEBUG_SERIAL
  return false;
}

void ensureWiFi() {
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setTimeout(180);
  bool connected = wifiManager.autoConnect("Clock-Setup");
  if (!connected) {
    delay(1000);
    ESP.restart();
  }
}

void setupOta() {
  ArduinoOTA.setHostname("esp8266-clock");
  ArduinoOTA.onStart([]() {
#ifdef DEBUG_SERIAL
    Serial.println(F("[OTA] Start update"));
#endif
  });
  ArduinoOTA.onEnd([]() {
#ifdef DEBUG_SERIAL
    Serial.println(F("[OTA] Update finished"));
#endif
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef DEBUG_SERIAL
    Serial.printf("[OTA] Progress: %u%%\n", (progress * 100) / total);
#endif
  });
  ArduinoOTA.onError([](ota_error_t error) {
#ifdef DEBUG_SERIAL
    Serial.printf("[OTA] Error[%u]\n", error);
#endif
  });
  ArduinoOTA.begin();
#ifdef DEBUG_SERIAL
  Serial.println(F("[OTA] Ready"));
#endif
}

}  // namespace

void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(115200);
#endif  // DEBUG_SERIAL
  delay(50);
#ifdef DEBUG_SERIAL
  Serial.println();
  Serial.println(F("[Clock] Booting"));
#endif  // DEBUG_SERIAL

  strip.begin();
  strip.clear();
  strip.show();

  if (!loadConfig()) {
#ifdef DEBUG_SERIAL
    Serial.println(F("[Clock] Using default configuration"));
#endif  // DEBUG_SERIAL
  }
  timeAnchor = config.time;
  timeReferenceMs = millis();
  applyDisplaySettings();

  ensureWiFi();
  setupOta();
  if (syncTimeFromNtp()) {
    saveConfig();
  }
  applyDisplaySettings();
  setupWebServer();
#ifdef DEBUG_SERIAL
  Serial.println(F("[Clock] Web server ready"));
#endif  // DEBUG_SERIAL
  updateDisplay();
}

void loop() {
  ArduinoOTA.handle();
  unsigned long nowMs = millis();
  if (WiFi.status() == WL_CONNECTED) {
    bool needInitialSync = (lastNtpSyncMs == 0);
    bool dueDailySync = (!needInitialSync) && (nowMs - lastNtpSyncMs >= NTP_SYNC_INTERVAL_MS);
    bool readyForRetry = (nowMs - lastNtpAttemptMs >= NTP_RETRY_INTERVAL_MS);
    if ((needInitialSync || dueDailySync) && readyForRetry) {
      syncTimeFromNtp();
    }
  }
  server.handleClient();
  if (nowMs - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    lastDisplayRefresh = nowMs;
    updateDisplay();
  }
}
