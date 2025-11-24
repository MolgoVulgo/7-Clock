#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <time.h>

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

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>ESP8266 Clock</title>
  <style>
    :root { font-family: Arial, sans-serif; color: #1f2a37; background: #f2f4f7; }
    body { margin: 0; padding: 2rem; }
    h1 { margin-top: 0; }
    section { background: #fff; border-radius: 8px; padding: 1.25rem; margin-bottom: 1.5rem; box-shadow: 0 2px 6px rgba(0,0,0,0.08); }
    fieldset { border: 1px solid #d0d7de; border-radius: 6px; margin-bottom: 1rem; }
    legend { font-weight: bold; }
    label { display: flex; flex-direction: column; font-size: 0.9rem; margin-bottom: 0.6rem; }
    input, select { padding: 0.35rem; border-radius: 4px; border: 1px solid #cbd5e1; font-size: 1rem; }
    input[type="checkbox"] { width: auto; margin-right: 0.35rem; }
    .inline { display: flex; align-items: center; gap: 0.4rem; }
    button { padding: 0.45rem 0.9rem; border: none; border-radius: 4px; background: #2563eb; color: #fff; cursor: pointer; font-size: 1rem; }
    button.secondary { background: #64748b; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit,minmax(150px,1fr)); gap: 0.5rem; }
    #toast { position: fixed; top: 1rem; right: 1rem; padding: 0.75rem 1rem; background: #2563eb; color: #fff; border-radius: 6px; opacity: 0; transition: opacity 0.3s ease; }
    #toast.visible { opacity: 1; }
  </style>
</head>
<body>
  <h1>Configuration Horloge ESP8266</h1>
  <p>Utilisez les formulaires ci-dessous pour modifier les paramètres stockés dans <code>config.json</code>. Les modifications sont appliquées immédiatement.</p>

  <section>
    <h2>Alimentation & Modes</h2>
    <form id="powerForm">
      <label class="inline"><input type="checkbox" id="power_on"> Horloge allumée</label>
      <label>Mode courant
        <select id="power_mode">
          <option value="clock">Clock</option>
          <option value="timer">Timer</option>
          <option value="weather">Weather</option>
          <option value="custom">Custom</option>
          <option value="alarm">Alarm</option>
          <option value="off">Off</option>
        </select>
      </label>
      <label>Mode de démarrage
        <select id="startup_mode">
          <option value="clock">Clock</option>
          <option value="timer">Timer</option>
          <option value="weather">Weather</option>
          <option value="custom">Custom</option>
          <option value="alarm">Alarm</option>
          <option value="off">Off</option>
        </select>
      </label>
      <button type="submit">Enregistrer</button>
    </form>
  </section>

  <section>
    <h2>Heure & Réseau</h2>
    <form id="timeForm">
      <div class="grid">
        <label>Heures (0-23)<input type="number" id="time_hour" min="0" max="23"></label>
        <label>Minutes (0-59)<input type="number" id="time_minute" min="0" max="59"></label>
        <label>Secondes (0-59)<input type="number" id="time_second" min="0" max="59"></label>
        <label>Serveur NTP<input type="text" id="ntp_server" placeholder="pool.ntp.org"></label>
        <label>Décalage UTC (minutes)<input type="number" id="utc_offset" min="-720" max="840" step="1"></label>
      </div>
      <button type="submit">Mettre à jour</button>
    </form>
  </section>

  <section>
    <h2>Affichage</h2>
    <form id="displayForm">
      <label>Luminosité (1-255)<input type="number" id="brightness" min="1" max="255"></label>
      <label>Couleur générale<input type="color" id="general_color"></label>
      <label class="inline"><input type="checkbox" id="per_digit_enabled"> Couleur par digit</label>
      <div class="grid">
        <label>Digit 1<input type="color" id="per_digit_0"></label>
        <label>Digit 2<input type="color" id="per_digit_1"></label>
        <label>Digit 3<input type="color" id="per_digit_2"></label>
        <label>Digit 4<input type="color" id="per_digit_3"></label>
      </div>
      <button type="submit">Appliquer</button>
    </form>
  </section>

  <section>
    <h2>Points centraux</h2>
    <form id="dotsForm">
      <label class="inline"><input type="checkbox" id="dots_enabled"> Points actifs</label>
      <div class="grid">
        <label>Couleur gauche<input type="color" id="dot_left_color"></label>
        <label>Couleur droite<input type="color" id="dot_right_color"></label>
      </div>
      <label class="inline"><input type="checkbox" id="force_override"> Forcer les deux points avec une couleur unique</label>
      <label>Couleur forcée<input type="color" id="forced_color"></label>
      <button type="submit">Sauvegarder</button>
    </form>
  </section>

  <section>
    <button class="secondary" id="refreshBtn">Rafraîchir les valeurs</button>
  </section>

  <div id="toast"></div>

  <script>
    const $ = (id) => document.getElementById(id);
    const MODES = ["clock","timer","weather","custom","alarm","off"];

    function showToast(message, error = false) {
      const toast = $("toast");
      toast.textContent = message;
      toast.style.background = error ? "#c0392b" : "#16a34a";
      toast.classList.add("visible");
      setTimeout(() => toast.classList.remove("visible"), 2500);
    }

    async function fetchJson(url) {
      const res = await fetch(url);
      if (!res.ok) throw new Error("HTTP " + res.status);
      return res.json();
    }

    async function postJson(url, payload) {
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || "HTTP " + res.status);
      return data;
    }

    function setSelectValue(selectId, value) {
      const select = $(selectId);
      if (MODES.includes(value)) {
        select.value = value;
      }
    }

    async function loadPower() {
      const data = await fetchJson("/api/power");
      $("power_on").checked = data.power_on;
      setSelectValue("power_mode", data.mode);
      setSelectValue("startup_mode", data.startup_mode);
    }

    async function loadTime() {
      const data = await fetchJson("/api/time");
      $("time_hour").value = data.hour;
      $("time_minute").value = data.minute;
      $("time_second").value = data.second;
      $("ntp_server").value = data.ntp_server || "";
      $("utc_offset").value = data.utc_offset_minutes || 0;
    }

    async function loadDisplay() {
      const data = await fetchJson("/api/display");
      $("brightness").value = data.brightness;
      $("general_color").value = data.general_color || "#ffffff";
      $("per_digit_enabled").checked = !!data.per_digit_color?.enabled;
      const values = data.per_digit_color?.values || [];
    for (let i = 0; i < 4; i++) {
      const color = values[i] || data.general_color || "#ffffff";
      $("per_digit_" + i).value = color;
    }
  }

    async function loadDots() {
      const data = await fetchJson("/api/dots");
    $("dots_enabled").checked = data.enabled;
    $("dot_left_color").value = data.left_color || "#ffffff";
    $("dot_right_color").value = data.right_color || "#ffffff";
    $("force_override").checked = data.force_override || false;
    $("forced_color").value = data.forced_color || "#ff0000";
    }

    async function loadAll() {
      try {
        await Promise.all([loadPower(), loadTime(), loadDisplay(), loadDots()]);
        showToast("Configuration chargée");
      } catch (err) {
        showToast("Erreur de chargement : " + err.message, true);
      }
    }

    $("powerForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        await postJson("/api/power", {
          power_on: $("power_on").checked,
          mode: $("power_mode").value,
          startup_mode: $("startup_mode").value
        });
        showToast("Modes mis à jour");
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("timeForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        await postJson("/api/time", {
          hour: Number($("time_hour").value),
          minute: Number($("time_minute").value),
          second: Number($("time_second").value),
          ntp_server: $("ntp_server").value,
          utc_offset_minutes: Number($("utc_offset").value)
        });
        showToast("Heure synchronisée");
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("displayForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        const perDigitValues = [];
        for (let i = 0; i < 4; i++) {
          perDigitValues.push($("per_digit_" + i).value);
        }
        await postJson("/api/display", {
          brightness: Number($("brightness").value),
          general_color: $("general_color").value,
          per_digit_color: {
            enabled: $("per_digit_enabled").checked,
            values: perDigitValues
          }
        });
        showToast("Affichage mis à jour");
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("dotsForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        await postJson("/api/dots", {
          enabled: $("dots_enabled").checked,
          left_color: $("dot_left_color").value,
          right_color: $("dot_right_color").value,
          force_override: $("force_override").checked,
          forced_color: $("forced_color").value
        });
        showToast("Points mis à jour");
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("refreshBtn").addEventListener("click", (evt) => {
      evt.preventDefault();
      loadAll();
    });

    loadAll();
  </script>
</body>
</html>
)rawliteral";
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
};

struct DotsSettings {
  bool enabled{true};
  Color leftColor{255, 255, 255};
  Color rightColor{255, 255, 255};
  bool forceOverride{false};
  Color forcedColor{255, 0, 0};
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
  NetworkSettings network;
};

enum class OperatingMode { Clock, Timer, Weather, Custom, Alarm, Off };

ClockConfig config;
TimeSettings timeAnchor;
unsigned long timeReferenceMs = 0;
unsigned long lastDisplayRefresh = 0;

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

  JsonObject dots = doc.createNestedObject("dots");
  dots["enabled"] = config.dots.enabled;
  dots["left_color"] = colorToHex(config.dots.leftColor);
  dots["right_color"] = colorToHex(config.dots.rightColor);
  dots["force_override"] = config.dots.forceOverride;
  dots["forced_color"] = colorToHex(config.dots.forcedColor);

  JsonObject network = doc.createNestedObject("network");
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
  }

  JsonObject dots = doc["dots"].as<JsonObject>();
  if (!dots.isNull()) {
    config.dots.enabled = dots["enabled"].as<bool>();
    config.dots.leftColor = hexToColor(dots["left_color"].as<String>(), config.dots.leftColor);
    config.dots.rightColor = hexToColor(dots["right_color"].as<String>(), config.dots.rightColor);
    if (dots.containsKey("force_override")) {
      config.dots.forceOverride = dots["force_override"].as<bool>();
    }
    config.dots.forcedColor = hexToColor(dots["forced_color"].as<String>(), config.dots.forcedColor);
  }

  JsonObject network = doc["network"].as<JsonObject>();
  if (!network.isNull()) {
    String ntp = network["ntp_server"].as<String>();
    if (ntp.length() > 0) {
      config.network.ntpServer = ntp;
    }
    if (network.containsKey("utc_offset_minutes")) {
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
  if (config.display.perDigitEnabled) {
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
  root["ntp_server"] = config.network.ntpServer;
  root["utc_offset_minutes"] = config.network.utcOffsetMinutes;
  sendJson(doc);
}

void handlePostTime() {
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, getRequestBody());
  if (err) {
    sendJsonError("Invalid JSON payload");
    return;
  }

  bool ntpServerUpdated = false;

  if (doc.containsKey("hour")) {
    config.time.hour = constrain(doc["hour"].as<int>(), 0, 23);
  }
  if (doc.containsKey("minute")) {
    config.time.minute = constrain(doc["minute"].as<int>(), 0, 59);
  }
  if (doc.containsKey("second")) {
    config.time.second = constrain(doc["second"].as<int>(), 0, 59);
  }
  if (doc.containsKey("ntp_server")) {
    String ntp = doc["ntp_server"].as<String>();
    if (ntp.length() > 0) {
      config.network.ntpServer = ntp;
      ntpServerUpdated = true;
    }
  }
  if (doc.containsKey("utc_offset_minutes")) {
    config.network.utcOffsetMinutes =
        constrain(doc["utc_offset_minutes"].as<int>(), -720, 840);
    ntpServerUpdated = true;  // re-sync to apply offset change
  }

  timeAnchor = config.time;
  timeReferenceMs = millis();
  if (ntpServerUpdated && WiFi.status() == WL_CONNECTED) {
    syncTimeFromNtp();
  }
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
  root["force_override"] = config.dots.forceOverride;
  root["forced_color"] = colorToHex(config.dots.forcedColor);
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
  if (doc.containsKey("force_override")) {
    config.dots.forceOverride = doc["force_override"].as<bool>();
  }
  if (doc.containsKey("forced_color")) {
    config.dots.forcedColor = hexToColor(doc["forced_color"].as<String>(), config.dots.forcedColor);
  }

  saveConfig();
  updateDisplay();
  handleGetDots();
}

void handleWebUi() {
  server.send_P(200, "text/html", WEB_UI_HTML);
}

void handleInfo() {
  DynamicJsonDocument doc(256);
  doc["project"] = "ESP8266 Clock";
  doc["status"] = "ok";
  doc["endpoints"] = F("/api/power, /api/time, /api/display, /api/dots, /api/info");
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

  server.on("/api/info", HTTP_GET, handleInfo);

  server.onNotFound(handleNotFound);
  server.begin();
}

bool syncTimeFromNtp() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[Clock] Cannot sync time: WiFi not connected"));
    return false;
  }
  if (config.network.ntpServer.length() == 0) {
    Serial.println(F("[Clock] Cannot sync time: NTP server not configured"));
    return false;
  }

  Serial.print(F("[Clock] Syncing time via NTP: "));
  Serial.println(config.network.ntpServer);
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
      Serial.printf("[Clock] NTP sync OK: %02u:%02u:%02u\n", config.time.hour, config.time.minute,
                    config.time.second);
      return true;
    }
    delay(NTP_RETRY_DELAY_MS);
    yield();
  }

  Serial.println(F("[Clock] Failed to sync time via NTP"));
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
  if (syncTimeFromNtp()) {
    saveConfig();
  }
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
