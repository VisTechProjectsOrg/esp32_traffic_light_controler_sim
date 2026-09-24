var ws = new WebSocket('ws://' + window.location.hostname + '/ws');
let distanceSensorEnabled = false;
let originalDistanceSensorEnabled = false;
let originalDistanceMax = '';
let originalDistanceWarning = '';
let originalDistanceDanger = '';
let originalZonePersistence = '';

function updateTrafficLight(state) {
    document.getElementById('traffic-light').src = '/img/traffic_lt/' + state + '.png';
}

function closePopup(event) {
    if (event) event.preventDefault();

    // Only restore if NOT saving (i.e., if Cancel or overlay)
    if (!event || event.target.id !== 'setConfig') {
        document.getElementById("toggle_distance_sensor_switch").checked = originalDistanceSensorEnabled;
        document.getElementById("distance_max").value = originalDistanceMax;
        document.getElementById("distance_warning").value = originalDistanceWarning;
        document.getElementById("distance_danger").value = originalDistanceDanger;
        document.getElementById("zone_persistence").value = originalZonePersistence;
        toggleDistanceSensorInputs();
    }

    document.getElementById("popup").style.display = "none";
    document.getElementById("overlay").style.display = "none";
}

function openPopup_settings() {
    console.log("Opening settings popup");

    const popup = document.getElementById("popup");
    popup.classList.add("show");

    fetch('/get_config?' + new Date().getTime())
        .then(response => response.json())
        .then(data => {

            console.log("Fetched config values:", data);

            document.getElementById("delay_red").value = data.delay_red;
            document.getElementById("delay_yellow").value = data.delay_yellow;
            document.getElementById("delay_green").value = data.delay_green;
            document.getElementById("toggle_distance_sensor_switch").checked = !!data.distance_sensor_enabled;
            document.getElementById("distance_max").value = data.distance_max;
            document.getElementById("distance_warning").value = data.distance_warning;
            document.getElementById("distance_danger").value = data.distance_danger;
            document.getElementById("zone_persistence").value = data.zone_persistence;
            document.getElementById("version_number_firmware_label").textContent = "FW: v" + (data.version_firmware || "0.0");
            document.getElementById("version_number_spiffs_label").textContent = "SPIFFS: v" + (data.version_spiffs || "0.0");

            // Store original values for cancel
            originalDistanceSensorEnabled = !!data.distance_sensor_enabled;
            originalDistanceMax = data.distance_max;
            originalDistanceWarning = data.distance_warning;
            originalDistanceDanger = data.distance_danger;
            originalZonePersistence = data.zone_persistence;

            const enabled = !!data.distance_sensor_enabled;
            document.getElementById('toggle_distance_sensor_switch').checked = enabled;
            toggleDistanceSensorInputs();

            document.getElementById("popup").style.display = "block";
            document.getElementById("overlay").style.display = "block";

            // Show/hide car distance block on page load
            document.getElementById("carDistanceBlock").style.display = data.distance_sensor_enabled ? "" : "none";
            const diagBlock = document.getElementById("sensorDiagBlock");
            if (diagBlock) diagBlock.style.display = data.distance_sensor_enabled ? "" : "none";


            // Update car distance config values
            window.visualMax = parseInt(data.distance_max);
            window.warningThreshold = parseInt(data.distance_warning);
            window.dangerThreshold = parseInt(data.distance_danger);

            // Update input max
            const distanceInput = document.getElementById("distance_to_wall");
            distanceInput.max = window.visualMax;

            // Update car immediately
            updateCarPosition();

        })
        .catch(error => {
            console.error("Error fetching config values:", error);
            originalDistanceSensorEnabled = false;
            document.getElementById("toggle_distance_sensor_switch").checked = false;
            toggleDistanceSensorInputs();
            // now show the popup anyway
            document.getElementById("popup").style.display = "block";
            document.getElementById("overlay").style.display = "block";

        });
}

function openPopup_time_waisted() {
    console.log("%cWhy did I waste my time making this", css_rainbow);
    alert('Why did I waste my time making this');
}

function toggleLightMode() {
    console.log("Toggling light mode");

    fetch('/toggle_light_mode')
        .then(response => response.text())
        .then(data => {
            console.log("Server Response (toggle light mode):", data);

            fetch('/get_current_state')
                .then(response => response.json())
                .then(data => {
                    const toggleLightModeSwitch = document.getElementById("toggleLightModeSwitch");
                    const toggleLightModeLabel = document.getElementById("toggleLightModeLabel");
                    toggleLightModeSwitch.checked = (data.light_mode === "blink_mode");
                    toggleLightModeLabel.textContent = data.light_mode === "blink_mode" ? "Blink Mode" : "Cycle Mode";
                })
                .catch(error => console.error("Error fetching current state:", error, "color: red; font-weight: bold;"));
        })
        .catch(error => console.error("Error toggling light mode:", error, "color: red; font-weight: bold;"));
}

function toggleThemeMode() {
    console.log("Toggling theme mode");

    fetch('/toggle_theme_mode')
        .then(response => response.text())
        .then(data => {
            console.log("Server Response (toggle theme mode):", data);

            fetch('/get_current_state')
                .then(response => response.json())
                .then(data => {
                    const toggleThemeModeSwitch = document.getElementById("toggleThemeModeSwitch");
                    const toggleThemeModeLabel = document.getElementById("toggleThemeModeLabel");
                    toggleThemeModeSwitch.checked = (data.theme_mode === "cat_mode");
                    toggleThemeModeLabel.textContent = data.theme_mode === "cat_mode" ? "Cat Mode" : "Normal Mode";
                })
                .catch(error => console.error("Error fetching current state:", error, "color: red; font-weight: bold;"));
        })
        .catch(error => console.error("Error toggling theme mode:", error, "color: red; font-weight: bold;"));
}

function toggleDistanceSensorInputs() {
    const on = document.getElementById('toggle_distance_sensor_switch').checked;
    const distanceBox = document.getElementById('distanceSettings');
    const wrapper = document.querySelector('.settings-wrapper');
    const secondPanel = document.getElementById('popup-second');

    // Panel show/hide
    distanceBox.style.display = on ? 'block' : 'none';
    distanceBox.classList.toggle('active-box', on);

    // Layout: two‑column vs single
    wrapper.classList.toggle('single-column', !on);
    wrapper.style.justifyContent = on ? 'space-between' : 'center';

    // Entire second column
    secondPanel.style.display = on ? '' : 'none';
}


document.addEventListener("DOMContentLoaded", function () {

    ws.onmessage = function (event) {
        let data = JSON.parse(event.data);

        if (data.light_mode) {
            console.log("Light mode:", data);
            const toggleSwitch = document.getElementById("toggleLightModeSwitch");
            const toggleSwitchLabel = document.getElementById("toggleLightModeLabel");

            if (data.light_mode === "cycle_mode") {
                toggleSwitch.checked = false;
                toggleSwitchLabel.textContent = "Cycle Mode";
            } else if (data.light_mode === "blink_mode") {
                toggleSwitch.checked = true;
                toggleSwitchLabel.textContent = "Blink Mode";
            } else {
                console.error("%cUnknown light mode received:", data.light_Mode, "color: orange; font-weight: bold;");
            }

        } else if (data.theme_mode) {
            console.log("Theme mode:", data);
            const toggleSwitch = document.getElementById("toggleThemeModeSwitch");
            const toggleSwitchLabel = document.getElementById("toggleThemeModeLabel");

            if (data.theme_mode === "normal_mode") {
                toggleSwitch.checked = false;
                toggleSwitchLabel.textContent = "Normal Mode";
            } else if (data.theme_mode === "cat_mode") {
                toggleSwitch.checked = true;
                toggleSwitchLabel.textContent = "Cat Mode";
            } else {
                console.error("Unknown theme mode received:", data.theme_mode);
            }

        } else if (data.blink_color) {
            console.log("Blink color:", data);
            const blinkColorSelect = document.getElementById("blinkColorSelect");
            blinkColorSelect.value = data.blink_color;

        } else if (data.proximity) {
            updateSensorDiagnostics(data.proximity);

        } else if (data.ped_state) {
            console.log("Ped state:", data.ped_state);

        } else if (data.state) {
            updateTrafficLight(data.state);

        } else if (data.distance === null) {
            // Out of range - show car at max distance
            const distanceInput = document.getElementById("distance_to_wall");
            const distanceDisplay = document.getElementById("distance_to_wall_display");
            if (distanceInput && window.visualMax) {
                distanceInput.value = window.visualMax;
                if (distanceDisplay) distanceDisplay.textContent = "--";
                updateCarPosition();
            }

        } else if (data.distance !== undefined) {
            const distanceInput = document.getElementById("distance_to_wall");
            const distanceDisplay = document.getElementById("distance_to_wall_display");

            if (!isNaN(data.distance)) {
                const distanceValue = Number.parseFloat(data.distance);
                console.log("Distance: " + distanceValue.toFixed(2) + " ft, Temp: " + (data.sensor_temp || "--") + " C");

                if (distanceInput) {
                    distanceInput.value = distanceValue;
                    if (distanceDisplay) distanceDisplay.textContent = distanceValue.toFixed(1);
                    updateCarPosition();
                }
            } else {
                console.warn("Distance value is NaN (possibly null):", data);
            }
        }
        else {
            console.error("Unknown data received:", data);
        }

        if (data?.state !== undefined && data?.state !== null && data?.state !== "") {
            updateTrafficLight(data.state);
        }
    };

    ws.onclose = function () {
        console.log("WebSocket disconnected, attempting to reconnect...", "color: red; font-weight: bold;");
    };

    ws.onopen = function () {
        console.log("%cConnected to WebSocket server", "color: green; font-weight: bold;");
    };

    ws.onerror = function (error) {
        console.error("WebSocket Error:", error);
    };

    fetch('/get_current_state')
        .then(response => response.json())
        .then(data => {
            console.log("Fetched Current State:", data);

            const toggleLightModeSwitch = document.getElementById("toggleLightModeSwitch");
            const toggleLightModeLabel = document.getElementById("toggleLightModeLabel");
            toggleLightModeSwitch.checked = (data.light_mode === "blink_mode");
            toggleLightModeLabel.textContent = data.light_mode === "blink_mode" ? "Blink Mode" : "Cycle Mode";

            const toggleThemeModeSwitch = document.getElementById("toggleThemeModeSwitch");
            const toggleThemeModeLabel = document.getElementById("toggleThemeModeLabel");
            toggleThemeModeSwitch.checked = (data.theme_mode === "cat_mode");
            toggleThemeModeLabel.textContent = data.theme_mode === "cat_mode" ? "Cat Mode" : "Normal Mode";

            if (data.state) {
                updateTrafficLight(data.state);
            }

            const blinkColorSelect = document.getElementById("blinkColorSelect");
            if (data.blink_color) {
                blinkColorSelect.value = data.blink_color;
            }
        })
        .catch(error => console.error("Error fetching current state:", error));

    // Set initial visibility of car distance block on first load
    fetch('/get_config')
        .then(res => res.json())
        .then(cfg => {
            const enabled = cfg.distance_sensor_enabled;
            const block = document.getElementById("carDistanceBlock");
            block.style.display = enabled ? "" : "none";
        })
        .catch(err => console.error("Error loading initial distance sensor config:", err));

    loadPedConfig();

    document.getElementById("setConfig").addEventListener("click", function (event) {
        const distanceSensorWasEnabled = document.getElementById("toggle_distance_sensor_switch").checked;

        savePedConfig();

        sendRequest("set_config").then(() => {
            // After saving, re-fetch latest config and update the form values
            fetch('/get_config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById("delay_red").value = data.delay_red;
                    document.getElementById("delay_yellow").value = data.delay_yellow;
                    document.getElementById("delay_green").value = data.delay_green;
                    document.getElementById("toggle_distance_sensor_switch").checked = !!data.distance_sensor_enabled;
                    document.getElementById("distance_max").value = data.distance_max;
                    document.getElementById("distance_warning").value = data.distance_warning;
                    document.getElementById("distance_danger").value = data.distance_danger;
                    document.getElementById("zone_persistence").value = data.zone_persistence;

                    toggleDistanceSensorInputs();
                });
        });

        // Update car block visibility immediately
        document.getElementById("carDistanceBlock").style.display = distanceSensorWasEnabled ? "" : "none";

        closePopup(event); // Close after saving
    });

    function sendRequest(action) {
        const data = {
            action: action,
            delay_red: parseFloat(document.getElementById("delay_red").value),
            delay_yellow: parseFloat(document.getElementById("delay_yellow").value),
            delay_green: parseFloat(document.getElementById("delay_green").value),
            distance_sensor_enabled: document.getElementById("toggle_distance_sensor_switch").checked,
            distance_max: document.getElementById("distance_max").value,
            distance_warning: document.getElementById("distance_warning").value,
            distance_danger: document.getElementById("distance_danger").value,
            zone_persistence: parseInt(document.getElementById("zone_persistence").value)
        };

        return fetch("/set_config", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(data)
        })
            .then(response => response.json())
            .then(data => {
                console.log("Server Response (set config values):", data);
                console.log("%cSubmitted successfully new config values", "color: green; font-weight: bold;");
                alert("Settings updated successfully!");
            })
            .catch(error => console.error("Error:", error));
    }

    document.getElementById('blinkColorSelect').addEventListener('change', function () {
        var color = this.value;
        fetch('/blink_mode?color=' + color)
            .then(response => response.text())
            .then(data => {
                var toggleLightModeSwitch = document.getElementById('toggleLightModeSwitch');
                var toggleLightModeLabel = document.getElementById('toggleLightModeLabel');

                if (!toggleLightModeSwitch.checked) {
                    toggleLightModeSwitch.checked = true;
                    toggleLightModeLabel.textContent = "Blink Mode";
                }
            })
            .catch(error => console.error("Error selecting blink mode:", error, "color: red; font-weight: bold;"));
    });

    function sendSliderUpdate(type, value) {
        let message = `${type}:${value}`;
        console.log("%cSending:", "color: orange; font-weight: bold;", message);
        ws.send(message);
    }



    document.getElementById("toggleLightModeSwitch").addEventListener("input", function () {
        sendSliderUpdate("light_mode", this.value);
    });

    document.getElementById("toggleThemeModeSwitch").addEventListener("input", function () {
        sendSliderUpdate("theme_mode", this.value);
    });

    // Reset OTA state when main page becomes visible
    document.addEventListener('visibilitychange', function () {
        if (!document.hidden) {
            // Page is now visible, reset OTA state
            fetch('/reset_ota_state', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                }
            }).catch(err => console.log('Failed to reset OTA state:', err));
        }
    });

    // Also reset when page loads
    fetch('/reset_ota_state', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        }
    }).catch(err => console.log('Failed to reset OTA state:', err));
});

// ---- sensor diagnostics ----

function fmtFt(value) {
    const n = Number.parseFloat(value);
    return (Number.isNaN(n) || n < 0) ? "--" : n.toFixed(2) + " ft";
}

function updateSensorDiagnostics(p) {
    const stateEl = document.getElementById("diag_state");
    if (!stateEl) return;

    stateEl.textContent = p.state || "--";
    stateEl.setAttribute("data-state", p.state || "");

    document.getElementById("diag_filtered").textContent = fmtFt(p.filtered);
    document.getElementById("diag_raw").textContent = fmtFt(p.raw);
    document.getElementById("diag_strength").textContent =
        (p.strength === undefined || p.strength === null) ? "--" : p.strength;
    document.getElementById("diag_baseline").textContent =
        p.baseline_valid ? fmtFt(p.baseline) : "learning...";
    document.getElementById("diag_zone").textContent = "zone: " + (p.zone || "--");
}

document.addEventListener("DOMContentLoaded", function () {
    const relearn = document.getElementById("relearnBaseline");
    if (!relearn) return;

    relearn.addEventListener("click", function () {
        relearn.disabled = true;
        relearn.textContent = "Relearning...";
        fetch("/proximity_control", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ action: "relearn" })
        })
            .then(function (r) { return r.json(); })
            .then(function (p) { updateSensorDiagnostics(p); })
            .catch(function (e) { console.error("Relearn failed:", e); })
            .finally(function () {
                relearn.disabled = false;
                relearn.textContent = "Relearn baseline";
            });
    });
});


// ---- pedestrian settings ----

function applyPedConfig(cfg) {
    if (!cfg) return;
    const set = function (id, value) {
        const el = document.getElementById(id);
        if (el && value !== undefined && value !== null) el.value = value;
    };
    set("ped_walk", cfg.walk);
    set("ped_fdw", cfg.fdw);
    set("ped_chain_phase", cfg.chain_phase);

    const chained = document.getElementById("ped_chained");
    if (chained && cfg.chained !== undefined) chained.checked = !!cfg.chained;
}

function loadPedConfig() {
    // no-op body: the board answers a bare set_state with its current config
    fetch("/ped_control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "query" })
    })
        .then(function (r) { return r.json(); })
        .then(applyPedConfig)
        .catch(function (e) { console.error("Error loading ped config:", e); });

    fetch("/proximity_control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "query" })
    })
        .then(function (r) { return r.json(); })
        .then(function (p) {
            const usePed = document.getElementById("prox_use_ped");
            if (usePed && p.use_ped !== undefined) usePed.checked = !!p.use_ped;
            updateSensorDiagnostics(p);
        })
        .catch(function (e) { console.error("Error loading proximity config:", e); });
}

function savePedConfig() {
    const num = function (id, fallback) {
        const el = document.getElementById(id);
        const v = el ? Number.parseInt(el.value, 10) : NaN;
        return Number.isNaN(v) ? fallback : v;
    };

    fetch("/ped_control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            action: "set_config",
            walk: num("ped_walk", 7),
            fdw: num("ped_fdw", 15),
            chained: document.getElementById("ped_chained").checked,
            chain_phase: document.getElementById("ped_chain_phase").value
        })
    }).catch(function (e) { console.error("Error saving ped config:", e); });

    fetch("/proximity_control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            action: "set_config",
            use_ped: document.getElementById("prox_use_ped").checked
        })
    }).catch(function (e) { console.error("Error saving proximity config:", e); });
}


// ---- boot splash ----
// The page used to render with empty fields that /get_config then overwrote in
// front of the user. Hold the splash until the first config and state have landed,
// with a timeout so a failing endpoint can never strand you on the splash.

(function () {
    var BOOT_TIMEOUT_MS = 8000;
    var pending = { config: false, state: false };
    var finished = false;

    function dismiss(message) {
        if (finished) return;
        finished = true;
        var splash = document.getElementById("bootSplash");
        if (!splash) return;
        if (message) {
            var text = document.getElementById("bootText");
            if (text) text.textContent = message;
        }
        splash.classList.add("boot-done");
        setTimeout(function () { splash.style.display = "none"; }, 400);
    }

    function mark(key) {
        pending[key] = true;
        if (pending.config && pending.state) dismiss();
    }

    window.bootReady = mark;

    // Never strand the user, even if the controller is unreachable.
    setTimeout(function () {
        if (!finished) dismiss("Controller slow to respond - loading anyway");
    }, BOOT_TIMEOUT_MS);

    document.addEventListener("DOMContentLoaded", function () {
        fetch("/get_config").then(function () { mark("config"); }).catch(function () { mark("config"); });
        fetch("/get_current_state").then(function () { mark("state"); }).catch(function () { mark("state"); });
    });
})();
