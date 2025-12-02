#pragma once

#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>ESP8266 Clock</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    :root {
      --bg: radial-gradient(circle at top, #1d4ed8 0%, #111827 55%);
      --card-bg: rgba(17, 24, 39, 0.78);
      --accent: #38bdf8;
      font-family: "Inter", "Segoe UI", system-ui, -apple-system, sans-serif;
      color: #f8fafc;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: clamp(1rem, 3vw, 2rem);
      min-height: 100vh;
      background: var(--bg);
      backdrop-filter: blur(12px);
      overflow-x: hidden;
    }
    h1, h2, p { margin: 0; }
    .app-shell { max-width: 1100px; margin: 0 auto; display: flex; flex-direction: column; gap: 1.5rem; }
    header.hero {
      background: linear-gradient(120deg, rgba(56,189,248,0.2), rgba(59,130,246,0.15));
      border-radius: 18px; padding: 2rem; border: 1px solid rgba(148,163,184,0.2);
      display: flex; flex-wrap: wrap; justify-content: space-between; gap: 1rem; align-items: center;
      box-shadow: 0 25px 60px rgba(0,0,0,0.35);
    }
    .badge-row { display: flex; flex-wrap: wrap; gap: 0.5rem; margin-top: 0.75rem; }
    .badge { padding: 0.3rem 0.8rem; border-radius: 999px; background: rgba(15,23,42,0.55); border: 1px solid rgba(148,163,184,0.3); font-size: 0.85rem; }
    .live-clock { font-size: clamp(2.2rem, 5vw, 3rem); font-weight: 700; letter-spacing: 0.08em; color: #fbbf24; text-shadow: 0 0 25px rgba(251,191,36,0.4); }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(240px, 1fr)); gap: 1rem; }
    .cards-grid { grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); }
    .card { background: var(--card-bg); border-radius: 16px; padding: 1.5rem; border: 1px solid rgba(148,163,184,0.2); box-shadow: 0 20px 40px rgba(0,0,0,0.3); display: flex; flex-direction: column; gap: 1rem; }
    fieldset { border: 1px solid rgba(148,163,184,0.2); border-radius: 12px; padding: 1rem; margin: 0; }
    legend { padding: 0 0.75rem; font-size: 0.85rem; color: #94a3b8; }
    label { display: flex; flex-direction: column; gap: 0.35rem; font-size: 0.9rem; color: #cbd5f5; }
    .inline { flex-direction: row; align-items: center; gap: 0.5rem; }
    input, select, button { font-size: 1rem; border-radius: 10px; border: 1px solid rgba(148,163,184,0.35); padding: 0.55rem 0.8rem; background: rgba(15,23,42,0.7); color: #f8fafc; min-height: 44px; }
    input[type="checkbox"] { width: auto; height: auto; }
    button { background: linear-gradient(120deg, #38bdf8, #6366f1); border: none; cursor: pointer; transition: transform 0.2s ease, box-shadow 0.2s ease; box-shadow: 0 10px 25px rgba(59,130,246,0.35); }
    button.secondary { background: rgba(148,163,184,0.25); box-shadow: none; }
    #toast { position: fixed; top: 1.5rem; right: 1.5rem; padding: 0.8rem 1.2rem; border-radius: 999px; background: var(--accent); color: #0f172a; font-weight: 600; opacity: 0; transform: translateY(-20px); transition: opacity 0.25s ease, transform 0.25s ease; }
    #toast.visible { opacity: 1; transform: translateY(0); }
    .color-row { display: flex; align-items: center; gap: 0.6rem; }
    .color-chip { width: 36px; height: 36px; border-radius: 10px; border: 1px solid rgba(148,163,184,0.35); background: #1f2937; }
    .digit-row { display: flex; flex-wrap: wrap; gap: 0.75rem; }
    .digit-row label { flex: 1 1 150px; min-width: 140px; }
    .side-by-side { display: flex; flex-wrap: wrap; gap: 1rem; align-items: flex-end; }
    .side-by-side > label, .side-by-side > div { flex: 1 1 200px; }
    .inline-inputs { display: flex; gap: 0.5rem; align-items: center; }
    .inline-inputs span { font-size: 0.85rem; color: #94a3b8; }
    .day-row { display: flex; flex-wrap: wrap; gap: 0.5rem; }
    .day-row label { flex: 0 0 auto; }
    .range-pair { display: flex; flex-direction: column; gap: 0.3rem; }
    @media (max-width: 640px) {
      .card { padding: 1.1rem; }
      input, select, button { min-height: 40px; }
    }
  </style>
</head>
<body>
  <div class="app-shell">
    <header class="hero">
      <div>
        <h1>ESP8266 Clock</h1>
        <p>Contrôlez votre horloge, les couleurs et l'alarme depuis votre réseau local.</p>
        <div class="badge-row">
          <span class="badge">Wi-Fi STA + AP</span>
          <span class="badge">OTA Ready</span>
          <span class="badge">LittleFS Config</span>
        </div>
      </div>
      <div id="live_clock" class="live-clock">--:--:--</div>
    </header>

    <div class="grid cards-grid">
      <section class="card">
        <h2>NTP</h2>
        <form id="timeForm">
          <div class="grid">
            <label>Serveur NTP<input type="text" id="ntp_server" placeholder="pool.ntp.org"></label>
            <label>Décalage UTC (minutes)<input type="number" id="utc_offset" min="-720" max="840" step="1"></label>
          </div>
          <button type="submit">Mettre à jour</button>
        </form>
      </section>

      <section class="card" style="grid-column: span 2;">
        <h2>Affichage et couleurs</h2>
        <form id="displayForm">
          <div class="grid">
            <label>Luminosité (1-255)<input type="number" id="brightness" min="1" max="255"></label>
            <label>Couleur générale
              <div class="color-row">
                <span class="color-chip" data-chip="general_color"></span>
                <input type="color" id="general_color">
              </div>
            </label>
            <label class="inline" style="grid-column: span 2;"><input type="checkbox" id="per_digit_enabled"> Couleur par digit</label>
          </div>
          <div class="digit-row">
            <label>Digit 1
              <div class="color-row">
                <span class="color-chip" data-chip="per_digit_0"></span>
                <input type="color" id="per_digit_0">
              </div>
            </label>
            <label>Digit 2
              <div class="color-row">
                <span class="color-chip" data-chip="per_digit_1"></span>
                <input type="color" id="per_digit_1">
              </div>
            </label>
            <label>Digit 3
              <div class="color-row">
                <span class="color-chip" data-chip="per_digit_2"></span>
                <input type="color" id="per_digit_2">
              </div>
            </label>
            <label>Digit 4
              <div class="color-row">
                <span class="color-chip" data-chip="per_digit_3"></span>
                <input type="color" id="per_digit_3">
              </div>
            </label>
          </div>
          <fieldset>
            <legend>Réduction nocturne</legend>
            <label class="inline"><input type="checkbox" id="quiet_enabled"> Activer</label>
            <div class="side-by-side">
              <label class="range-pair">Début
                <div class="inline-inputs">
                  <input type="number" id="quiet_start_hour" min="0" max="23" aria-label="Heure début">
                  <span>h</span>
                  <input type="number" id="quiet_start_minute" min="0" max="59" aria-label="Minute début">
                  <span>m</span>
                </div>
              </label>
              <label class="range-pair">Fin
                <div class="inline-inputs">
                  <input type="number" id="quiet_end_hour" min="0" max="23" aria-label="Heure fin">
                  <span>h</span>
                  <input type="number" id="quiet_end_minute" min="0" max="59" aria-label="Minute fin">
                  <span>m</span>
                </div>
              </label>
            </div>
            <label>Luminosité réduite (0-255)<input type="number" id="quiet_dim_brightness" min="0" max="255"></label>
          </fieldset>
          <button type="submit">Appliquer</button>
        </form>
      </section>

      <section class="card">
        <h2>Points centraux</h2>
        <form id="dotsForm">
          <label class="inline"><input type="checkbox" id="dots_enabled"> Points actifs</label>
          <div class="side-by-side">
            <label>Couleur gauche
              <div class="color-row">
                <span class="color-chip" data-chip="dot_left_color"></span>
                <input type="color" id="dot_left_color">
              </div>
            </label>
            <label>Couleur droite
              <div class="color-row">
                <span class="color-chip" data-chip="dot_right_color"></span>
                <input type="color" id="dot_right_color">
              </div>
            </label>
          </div>
          <label class="inline"><input type="checkbox" id="force_override"> Forcer les deux points avec une couleur unique</label>
          <label>Couleur forcée
            <div class="color-row">
              <span class="color-chip" data-chip="forced_color"></span>
              <input type="color" id="forced_color">
            </div>
          </label>
          <button type="submit">Sauvegarder</button>
        </form>
      </section>

      <section class="card">
        <h2>Alarme</h2>
        <form id="alarmForm">
          <label class="inline"><input type="checkbox" id="alarm_enabled"> Activer l'alarme</label>
          <div class="side-by-side">
            <label>Heure / Minutes
              <div class="inline-inputs">
                <input type="number" id="alarm_hour" min="0" max="23" aria-label="Heure alarme">
                <span>:</span>
                <input type="number" id="alarm_minute" min="0" max="59" aria-label="Minute alarme">
              </div>
            </label>
            <label>Durée (minutes)<input type="number" id="alarm_duration" min="1" max="30" step="1"></label>
          </div>
          <fieldset>
            <legend>Jours actifs</legend>
            <div class="day-row" id="alarm_days_container"></div>
          </fieldset>
          <p>Statut : <span id="alarm_status">-</span></p>
          <div class="grid">
            <button type="submit">Mettre à jour</button>
            <button type="button" class="secondary" id="alarm_stop_btn">Arrêter l'alarme</button>
          </div>
        </form>
      </section>
    </div>

    <button class="secondary" id="refreshBtn" style="align-self:flex-end">Rafraîchir les valeurs</button>
  </div>

  <div id="toast"></div>

  <script>
    const $ = (id) => document.getElementById(id);
    const DAY_LABELS = ["Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"];
    const COLOR_FIELDS = [
      "general_color",
      "per_digit_0",
      "per_digit_1",
      "per_digit_2",
      "per_digit_3",
      "dot_left_color",
      "dot_right_color",
      "forced_color"
    ];
    let liveClockSeconds = null;
    let liveClockLastUpdateMs = null;

    function updateLiveClockDisplay() {
      if (liveClockSeconds === null || liveClockLastUpdateMs === null) {
        $("live_clock").textContent = "--:--:--";
        return;
      }
      const now = Date.now();
      const elapsed = Math.floor((now - liveClockLastUpdateMs) / 1000);
      if (elapsed > 0) {
        liveClockSeconds = (liveClockSeconds + elapsed) % (24 * 3600);
        liveClockLastUpdateMs += elapsed * 1000;
      }
      const hours = Math.floor(liveClockSeconds / 3600);
      const minutes = Math.floor((liveClockSeconds % 3600) / 60);
      const seconds = liveClockSeconds % 60;
      $("live_clock").textContent = [hours, minutes, seconds].map((v) => String(v).padStart(2, "0")).join(":");
    }

    function setLiveClock(current) {
      if (!current) {
        liveClockSeconds = null;
        liveClockLastUpdateMs = null;
        $("live_clock").textContent = "--:--:--";
        return;
      }
      liveClockSeconds = ((Number(current.hour) || 0) * 3600) +
                         ((Number(current.minute) || 0) * 60) +
                         (Number(current.second) || 0);
      liveClockSeconds %= (24 * 3600);
      liveClockLastUpdateMs = Date.now();
      updateLiveClockDisplay();
    }

    setInterval(updateLiveClockDisplay, 1000);

    function showToast(message, error = false) {
      const toast = $("toast");
      toast.textContent = message;
      toast.style.background = error ? "#f87171" : "var(--accent)";
      toast.classList.add("visible");
      setTimeout(() => toast.classList.remove("visible"), 2200);
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

    function updateColorChip(id) {
      const chip = document.querySelector(`[data-chip="${id}"]`);
      const input = $(id);
      if (chip && input) chip.style.background = input.value || "#000000";
    }

    function attachColorPreview(id) {
      const input = $(id);
      if (!input) return;
      input.addEventListener("input", () => updateColorChip(id));
      updateColorChip(id);
    }

    COLOR_FIELDS.forEach(attachColorPreview);

    async function loadTime() {
      const data = await fetchJson("/api/time");
      $("ntp_server").value = data.ntp_server || "";
      $("utc_offset").value = data.utc_offset_minutes || 0;
      setLiveClock(data.current);
    }

    async function loadDisplay() {
      const data = await fetchJson("/api/display");
      $("brightness").value = data.brightness;
      $("general_color").value = data.general_color || "#ffffff";
      $("per_digit_enabled").checked = !!data.per_digit_color?.enabled;
      const values = data.per_digit_color?.values || [];
      for (let i = 0; i < 4; i++) $("per_digit_" + i).value = values[i] || data.general_color || "#ffffff";
      const quiet = data.quiet_hours || {};
      $("quiet_enabled").checked = quiet.enabled || false;
      $("quiet_start_hour").value = quiet.start_hour ?? 23;
      $("quiet_start_minute").value = quiet.start_minute ?? 0;
      $("quiet_end_hour").value = quiet.end_hour ?? 7;
      $("quiet_end_minute").value = quiet.end_minute ?? 0;
      $("quiet_dim_brightness").value = quiet.dim_brightness ?? 0;
      ["general_color", "per_digit_0", "per_digit_1", "per_digit_2", "per_digit_3"].forEach(updateColorChip);
    }

    async function loadDots() {
      const data = await fetchJson("/api/dots");
      $("dots_enabled").checked = data.enabled;
      $("dot_left_color").value = data.left_color || "#ffffff";
      $("dot_right_color").value = data.right_color || "#ffffff";
      $("force_override").checked = data.force_override || false;
      $("forced_color").value = data.forced_color || "#ff0000";
      ["dot_left_color", "dot_right_color", "forced_color"].forEach(updateColorChip);
    }

    function buildAlarmDays() {
      const container = $("alarm_days_container");
      container.innerHTML = "";
      DAY_LABELS.forEach((label, index) => {
        const id = "alarm_day_" + index;
        const wrapper = document.createElement("label");
        wrapper.classList.add("inline");
        const input = document.createElement("input");
        input.type = "checkbox";
        input.id = id;
        input.dataset.dayIndex = index;
        wrapper.appendChild(input);
        wrapper.appendChild(document.createTextNode(label));
        container.appendChild(wrapper);
      });
    }

    async function loadAlarm() {
      const data = await fetchJson("/api/alarm");
      $("alarm_enabled").checked = data.enabled;
      $("alarm_hour").value = data.hour;
      $("alarm_minute").value = data.minute;
      $("alarm_status").textContent = data.active ? "Active" : "Inactif";
      $("alarm_duration").value = Math.round((data.duration_ms ?? 300000) / 60000);
      const mask = data.days_mask ?? 0x7f;
      DAY_LABELS.forEach((_, index) => {
        const checkbox = $("alarm_day_" + index);
        if (checkbox) checkbox.checked = !!(mask & (1 << index));
      });
    }

    async function loadAll() {
      try {
        await Promise.all([loadTime(), loadDisplay(), loadDots(), loadAlarm()]);
        showToast("Configuration chargée");
      } catch (err) {
        showToast("Erreur de chargement : " + err.message, true);
      }
    }

    $("timeForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        await postJson("/api/time", {
          ntp_server: $("ntp_server").value,
          utc_offset_minutes: Number($("utc_offset").value)
        });
        showToast("Paramètres NTP mis à jour");
        loadTime();
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("displayForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        const perDigitValues = [];
        for (let i = 0; i < 4; i++) perDigitValues.push($("per_digit_" + i).value);
        await postJson("/api/display", {
          brightness: Number($("brightness").value),
          general_color: $("general_color").value,
          per_digit_color: { enabled: $("per_digit_enabled").checked, values: perDigitValues },
          quiet_hours: {
            enabled: $("quiet_enabled").checked,
            start_hour: Number($("quiet_start_hour").value),
            start_minute: Number($("quiet_start_minute").value),
            end_hour: Number($("quiet_end_hour").value),
            end_minute: Number($("quiet_end_minute").value),
            dim_brightness: Number($("quiet_dim_brightness").value)
          }
        });
        showToast("Affichage mis à jour");
        loadDisplay();
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
        loadDots();
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("alarmForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        let mask = 0;
        DAY_LABELS.forEach((_, index) => {
          const checkbox = $("alarm_day_" + index);
          if (checkbox && checkbox.checked) mask |= (1 << index);
        });
        await postJson("/api/alarm", {
          enabled: $("alarm_enabled").checked,
          hour: Number($("alarm_hour").value),
          minute: Number($("alarm_minute").value),
          days_mask: mask,
          duration_ms: Math.max(1, Number($("alarm_duration").value)) * 60000
        });
        showToast("Alarme mise à jour");
        loadAlarm();
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("alarm_stop_btn").addEventListener("click", async () => {
      try {
        await postJson("/api/alarm", { stop: true });
        showToast("Alarme arrêtée");
        loadAlarm();
      } catch (err) {
        showToast(err.message, true);
      }
    });

    $("refreshBtn").addEventListener("click", (evt) => {
      evt.preventDefault();
      loadAll();
    });

    buildAlarmDays();
    loadAll();
  </script>
</body>
</html>
)rawliteral";
