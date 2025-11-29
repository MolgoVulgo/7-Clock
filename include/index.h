#pragma once

#include <pgmspace.h>

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
    .live-clock { font-size: 2.5rem; font-weight: bold; margin: 1rem 0; color: #111827; }
  </style>
</head>
<body>
  <h1>Configuration Horloge ESP8266</h1>
  <p>Utilisez les formulaires ci-dessous pour modifier les paramètres stockés dans <code>config.json</code>. Les modifications sont appliquées immédiatement.</p>
  <div id="live_clock" class="live-clock">--:--:--</div>

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
      <fieldset>
        <legend>Réduction nocturne</legend>
        <label class="inline"><input type="checkbox" id="quiet_enabled"> Activer</label>
        <div class="grid">
          <label>Début - heure<input type="number" id="quiet_start_hour" min="0" max="23"></label>
          <label>Début - minute<input type="number" id="quiet_start_minute" min="0" max="59"></label>
          <label>Fin - heure<input type="number" id="quiet_end_hour" min="0" max="23"></label>
          <label>Fin - minute<input type="number" id="quiet_end_minute" min="0" max="59"></label>
        </div>
        <label>Luminosité réduite (0-255)<input type="number" id="quiet_dim_brightness" min="0" max="255"></label>
      </fieldset>
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
    <h2>Alarme</h2>
    <form id="alarmForm">
      <label class="inline"><input type="checkbox" id="alarm_enabled"> Activer l'alarme</label>
      <div class="grid">
        <label>Heure (0-23)<input type="number" id="alarm_hour" min="0" max="23"></label>
        <label>Minute (0-59)<input type="number" id="alarm_minute" min="0" max="59"></label>
      </div>
      <fieldset>
        <legend>Jours actifs</legend>
        <div class="grid" id="alarm_days_container"></div>
      </fieldset>
      <p>Statut : <span id="alarm_status">-</span></p>
      <button type="submit">Mettre à jour</button>
      <button type="button" class="secondary" id="alarm_stop_btn">Arrêter l'alarme</button>
    </form>
  </section>

  <section>
    <button class="secondary" id="refreshBtn">Rafraîchir les valeurs</button>
  </section>

  <div id="toast"></div>

  <script>
    const $ = (id) => document.getElementById(id);
    const MODES = ["clock","timer","weather","custom","alarm","off"];
    let liveClockSeconds = null;
    let liveClockLastUpdateMs = null;
    const DAY_LABELS = ["Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"];

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
      liveClockSeconds = ((Number(current.hour) || 0) * 3600) + ((Number(current.minute) || 0) * 60) + (Number(current.second) || 0);
      liveClockSeconds %= (24 * 3600);
      liveClockLastUpdateMs = Date.now();
      updateLiveClockDisplay();
    }

    setInterval(updateLiveClockDisplay, 1000);

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
      setLiveClock(data.current);
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
      const quiet = data.quiet_hours || {};
      $("quiet_enabled").checked = quiet.enabled || false;
      $("quiet_start_hour").value = (quiet.start_hour !== undefined) ? quiet.start_hour : 23;
      $("quiet_start_minute").value = (quiet.start_minute !== undefined) ? quiet.start_minute : 0;
      $("quiet_end_hour").value = (quiet.end_hour !== undefined) ? quiet.end_hour : 7;
      $("quiet_end_minute").value = (quiet.end_minute !== undefined) ? quiet.end_minute : 0;
      $("quiet_dim_brightness").value = (quiet.dim_brightness !== undefined) ? quiet.dim_brightness : 0;
    }

    async function loadDots() {
      const data = await fetchJson("/api/dots");
      $("dots_enabled").checked = data.enabled;
      $("dot_left_color").value = data.left_color || "#ffffff";
      $("dot_right_color").value = data.right_color || "#ffffff";
      $("force_override").checked = data.force_override || false;
      $("forced_color").value = data.forced_color || "#ff0000";
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
      const mask = data.days_mask ?? 0x7f;
      DAY_LABELS.forEach((_, index) => {
        const checkbox = $("alarm_day_" + index);
        if (checkbox) {
          checkbox.checked = !!(mask & (1 << index));
        }
      });
    }

    async function loadAll() {
      try {
        await Promise.all([loadPower(), loadTime(), loadDisplay(), loadDots(), loadAlarm()]);
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
          },
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

    $("alarmForm").addEventListener("submit", async (evt) => {
      evt.preventDefault();
      try {
        let mask = 0;
        DAY_LABELS.forEach((_, index) => {
          const checkbox = $("alarm_day_" + index);
          if (checkbox && checkbox.checked) {
            mask |= (1 << index);
          }
        });

        await postJson("/api/alarm", {
          enabled: $("alarm_enabled").checked,
          hour: Number($("alarm_hour").value),
          minute: Number($("alarm_minute").value),
          days_mask: mask
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
