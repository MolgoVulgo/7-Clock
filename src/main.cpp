#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiManager.h>

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
  bool singleDigitOverride{false};
  uint8_t singleDigitIndex{0};
  Color singleDigitColor{255, 255, 255};
};

struct DotsSettings {
  bool enabled{true};
  Color leftColor{255, 255, 255};
  Color rightColor{255, 255, 255};
  bool forceLeft{false};
  bool forceRight{false};
  Color forcedLeftColor{255, 0, 0};
  Color forcedRightColor{255, 0, 0};
};

struct ClockConfig {
  PowerSettings power;
  TimeSettings time;
  DisplaySettings display;
  DotsSettings dots;
};

enum class OperatingMode { Clock, Timer, Weather, Custom, Alarm, Off };

ClockConfig config;
TimeSettings timeAnchor;
unsigned long timeReferenceMs = 0;
unsigned long lastDisplayRefresh = 0;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(80);
WiFiManager wifiManager;

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
  DynamicJsonDocument doc(256);
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

void applyDisplaySettings() {
  strip.setBrightness(constrain(config.display.brightness, static_cast<uint8_t>(1), static_cast<uint8_t>(255)));
}

void loadDefaultConfig() {
  config.power = PowerSettings();
  config.time = TimeSettings();
  config.display = DisplaySettings();
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    config.display.perDigitColor[i] = config.display.generalColor;
  }
  config.dots = DotsSettings();
}

bool saveConfig() {
  if (!LittleFS.begin()) {
    return false;
  }
  File file = LittleFS.open(CONFIG_PATH, "w");
  if (!file) {
    return false;
  }
  DynamicJsonDocument doc(JSON_CAPACITY);
  JsonObject power = doc.createNestedObject("power");
  power["power_on"] = config.power.powerOn;
  power["startup_mode"] = config.power.startupMode;
  power["mode"] = config.power.mode;
  power["exit_special_mode"] = config.power.exitSpecialMode;

  JsonObject time = doc.createNestedObject("time");
  time["hour"] = config.time.hour;
  time["minute"] = config.time.minute;
  time["second"] = config.time.second;

  JsonObject display = doc.createNestedObject("display");
  display["brightness"] = config.display.brightness;
  display["general_color"] = colorToHex(config.display.generalColor);
  JsonObject perDigit = display.createNestedObject("per_digit_color");
  perDigit["enabled"] = config.display.perDigitEnabled;
  JsonArray perDigitValues = perDigit.createNestedArray("values");
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    perDigitValues.add(colorToHex(config.display.perDigitColor[i]));
  }
  JsonObject singleOverride = display.createNestedObject("single_digit_override");
  singleOverride["enabled"] = config.display.singleDigitOverride;
  singleOverride["index"] = config.display.singleDigitIndex;
  singleOverride["color"] = colorToHex(config.display.singleDigitColor);

  JsonObject dots = doc.createNestedObject("dots");
  dots["enabled"] = config.dots.enabled;
  dots["left_color"] = colorToHex(config.dots.leftColor);
  dots["right_color"] = colorToHex(config.dots.rightColor);
  dots["force_left"] = config.dots.forceLeft;
  dots["force_right"] = config.dots.forceRight;
  dots["forced_left_color"] = colorToHex(config.dots.forcedLeftColor);
  dots["forced_right_color"] = colorToHex(config.dots.forcedRightColor);

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

  DynamicJsonDocument doc(JSON_CAPACITY);
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
    JsonObject singleOverride = display["single_digit_override"].as<JsonObject>();
    if (!singleOverride.isNull()) {
      config.display.singleDigitOverride = singleOverride["enabled"].as<bool>();
      config.display.singleDigitIndex = constrain(singleOverride["index"].as<int>(), 0, DIGIT_COUNT - 1);
      config.display.singleDigitColor = hexToColor(singleOverride["color"].as<String>(), config.display.generalColor);
    }
  }

  JsonObject dots = doc["dots"].as<JsonObject>();
  if (!dots.isNull()) {
    config.dots.enabled = dots["enabled"].as<bool>();
    config.dots.leftColor = hexToColor(dots["left_color"].as<String>(), config.dots.leftColor);
    config.dots.rightColor = hexToColor(dots["right_color"].as<String>(), config.dots.rightColor);
    config.dots.forceLeft = dots["force_left"].as<bool>();
    config.dots.forceRight = dots["force_right"].as<bool>();
    config.dots.forcedLeftColor = hexToColor(dots["forced_left_color"].as<String>(), config.dots.forcedLeftColor);
    config.dots.forcedRightColor = hexToColor(dots["forced_right_color"].as<String>(), config.dots.forcedRightColor);
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

Color resolveDigitColor(uint8_t index) {
  if (config.display.singleDigitOverride && config.display.singleDigitIndex == index) {
    return config.display.singleDigitColor;
  }
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
    Color left = config.dots.forceLeft ? config.dots.forcedLeftColor : config.dots.leftColor;
    Color right = config.dots.forceRight ? config.dots.forcedRightColor : config.dots.rightColor;
    leftColor = asPixelColor(left);
    rightColor = asPixelColor(right);
  }

  strip.setPixelColor(DOT_LEFT_INDEX, leftColor);
  strip.setPixelColor(DOT_RIGHT_INDEX, rightColor);
}

void renderClock() {
  TimeSettings now = computeCurrentTime();
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

void renderSolidColor(const Color &color) {
  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    strip.setPixelColor(i, asPixelColor(color));
  }
}

void renderCustomMode() {
  if (config.display.perDigitEnabled || config.display.singleDigitOverride) {
    renderClock();
  } else {
    renderSolidColor(config.display.generalColor);
  }
}

void updateDisplay() {
  OperatingMode mode = config.power.powerOn ? modeFromString(config.power.mode) : OperatingMode::Off;
  if (mode == OperatingMode::Off || !config.power.powerOn) {
    strip.clear();
    strip.show();
    return;
  }

  switch (mode) {
    case OperatingMode::Clock:
      renderClock();
      break;
    case OperatingMode::Timer:
      renderClock();
      break;
    case OperatingMode::Weather:
      renderSolidColor(config.display.generalColor);
      break;
    case OperatingMode::Custom:
      renderCustomMode();
      break;
    case OperatingMode::Alarm:
      renderClock();
      break;
    case OperatingMode::Off:
      strip.clear();
      break;
  }

  renderDots(mode);
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
  DynamicJsonDocument doc(512);
  JsonObject root = doc.to<JsonObject>();
  root["power_on"] = config.power.powerOn;
  root["mode"] = config.power.mode;
  root["startup_mode"] = config.power.startupMode;
  root["exit_special_mode"] = config.power.exitSpecialMode;
  sendJson(doc);
}

void handlePostPower() {
  DynamicJsonDocument doc(JSON_CAPACITY / 4);
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }
  if (doc.containsKey("power_on")) {
    config.power.powerOn = doc["power_on"].as<bool>();
  }
  if (doc.containsKey("mode")) {
    config.power.mode = sanitizeMode(doc["mode"].as<String>());
  }
  if (doc.containsKey("startup_mode")) {
    config.power.startupMode = sanitizeMode(doc["startup_mode"].as<String>());
  }
  if (doc.containsKey("exit_special_mode") && doc["exit_special_mode"].as<bool>()) {
    config.power.mode = config.power.startupMode;
    config.power.exitSpecialMode = false;
  }
  saveConfig();
  updateDisplay();
  handleGetPower();
}

void handleGetTime() {
  DynamicJsonDocument doc(256);
  JsonObject root = doc.to<JsonObject>();
  root["hour"] = config.time.hour;
  root["minute"] = config.time.minute;
  root["second"] = config.time.second;
  sendJson(doc);
}

void handlePostTime() {
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (doc.containsKey("hour")) {
    config.time.hour = constrain(doc["hour"].as<int>(), 0, 23);
  }
  if (doc.containsKey("minute")) {
    config.time.minute = constrain(doc["minute"].as<int>(), 0, 59);
  }
  if (doc.containsKey("second")) {
    config.time.second = constrain(doc["second"].as<int>(), 0, 59);
  }

  timeAnchor = config.time;
  timeReferenceMs = millis();
  saveConfig();
  handleGetTime();
}

void handleGetDisplay() {
  DynamicJsonDocument doc(JSON_CAPACITY / 2);
  JsonObject root = doc.to<JsonObject>();
  root["brightness"] = config.display.brightness;
  root["general_color"] = colorToHex(config.display.generalColor);
  JsonObject perDigit = root.createNestedObject("per_digit_color");
  perDigit["enabled"] = config.display.perDigitEnabled;
  JsonArray values = perDigit.createNestedArray("values");
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    values.add(colorToHex(config.display.perDigitColor[i]));
  }
  JsonObject singleOverride = root.createNestedObject("single_digit_override");
  singleOverride["enabled"] = config.display.singleDigitOverride;
  singleOverride["index"] = config.display.singleDigitIndex;
  singleOverride["color"] = colorToHex(config.display.singleDigitColor);
  sendJson(doc);
}

void handlePostDisplay() {
  DynamicJsonDocument doc(JSON_CAPACITY / 2);
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (doc.containsKey("brightness")) {
    config.display.brightness = constrain(doc["brightness"].as<int>(), 1, 255);
  }
  if (doc.containsKey("general_color")) {
    config.display.generalColor = hexToColor(doc["general_color"].as<String>(), config.display.generalColor);
  }

  if (doc.containsKey("per_digit_color")) {
    if (doc["per_digit_color"].is<JsonObject>()) {
      JsonObject perDigit = doc["per_digit_color"].as<JsonObject>();
      if (perDigit.containsKey("enabled")) {
        config.display.perDigitEnabled = perDigit["enabled"].as<bool>();
      }
      if (perDigit.containsKey("values")) {
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

  if (doc.containsKey("single_digit_override")) {
    JsonObject singleOverride = doc["single_digit_override"].as<JsonObject>();
    if (!singleOverride.isNull()) {
      if (singleOverride.containsKey("enabled")) {
        config.display.singleDigitOverride = singleOverride["enabled"].as<bool>();
      }
      if (singleOverride.containsKey("index")) {
        config.display.singleDigitIndex = constrain(singleOverride["index"].as<int>(), 0, DIGIT_COUNT - 1);
      }
      if (singleOverride.containsKey("color")) {
        config.display.singleDigitColor = hexToColor(singleOverride["color"].as<String>(), config.display.singleDigitColor);
      }
    }
  }

  applyDisplaySettings();
  saveConfig();
  updateDisplay();
  handleGetDisplay();
}

void handleGetDots() {
  DynamicJsonDocument doc(512);
  JsonObject root = doc.to<JsonObject>();
  root["enabled"] = config.dots.enabled;
  root["left_color"] = colorToHex(config.dots.leftColor);
  root["right_color"] = colorToHex(config.dots.rightColor);
  root["force_left"] = config.dots.forceLeft;
  root["force_right"] = config.dots.forceRight;
  root["forced_left_color"] = colorToHex(config.dots.forcedLeftColor);
  root["forced_right_color"] = colorToHex(config.dots.forcedRightColor);
  sendJson(doc);
}

void handlePostDots() {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  if (doc.containsKey("enabled")) {
    config.dots.enabled = doc["enabled"].as<bool>();
  }
  if (doc.containsKey("left_color")) {
    config.dots.leftColor = hexToColor(doc["left_color"].as<String>(), config.dots.leftColor);
  }
  if (doc.containsKey("right_color")) {
    config.dots.rightColor = hexToColor(doc["right_color"].as<String>(), config.dots.rightColor);
  }
  if (doc.containsKey("force_left")) {
    config.dots.forceLeft = doc["force_left"].as<bool>();
  }
  if (doc.containsKey("force_right")) {
    config.dots.forceRight = doc["force_right"].as<bool>();
  }
  if (doc.containsKey("forced_left_color")) {
    config.dots.forcedLeftColor = hexToColor(doc["forced_left_color"].as<String>(), config.dots.forcedLeftColor);
  }
  if (doc.containsKey("forced_right_color")) {
    config.dots.forcedRightColor = hexToColor(doc["forced_right_color"].as<String>(), config.dots.forcedRightColor);
  }

  saveConfig();
  updateDisplay();
  handleGetDots();
}

void handleRoot() {
  DynamicJsonDocument doc(256);
  doc["project"] = "ESP8266 Clock";
  doc["status"] = "ok";
  doc["endpoints"] = F("/api/power, /api/time, /api/display, /api/dots");
  sendJson(doc);
}

void handleNotFound() {
  sendJsonError("Endpoint not found", 404);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);

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

  server.onNotFound(handleNotFound);
  server.begin();
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

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println(F("[Clock] Booting"));

  strip.begin();
  strip.clear();
  strip.show();

  if (!loadConfig()) {
    Serial.println(F("[Clock] Using default configuration"));
  }
  timeAnchor = config.time;
  timeReferenceMs = millis();
  applyDisplaySettings();

  ensureWiFi();
  setupWebServer();
  Serial.println(F("[Clock] Web server ready"));
  updateDisplay();
}

void loop() {
  server.handleClient();
  if (millis() - lastDisplayRefresh >= DISPLAY_REFRESH_MS) {
    lastDisplayRefresh = millis();
    updateDisplay();
  }
}
