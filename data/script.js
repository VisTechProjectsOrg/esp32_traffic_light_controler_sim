var ws = new WebSocket('ws://' + window.location.hostname + '/ws');
let distanceSensorEnabled = false;
let originalDistanceSensorEnabled = false;
let originalDistanceMax = '';
let originalDistanceWarning = '';
let originalDistanceDanger = '';

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
        toggleDistanceSensorInputs();
    }

    document.getElementById("popup").style.display = "none";
    document.getElementById("overlay").style.display = "none";
}

function openPopup_settings() {
    console.log("Opening settings popup");

    document.getElementById("popup").style.display = "block";
    document.getElementById("overlay").style.display = "block";

    const switch_element = document.getElementById('toggle_distance_sensor_switch');

    fetch('/get_config?' + new Date().getTime())
        .then(response => response.json())
        .then(data => {
            document.getElementById("red_delay").value = data.red_delay;
            document.getElementById("yellow_delay").value = data.yellow_delay;
            document.getElementById("green_delay").value = data.green_delay;
            document.getElementById("toggle_distance_sensor_switch").checked = !!data.distance_sensor_enabled;
            document.getElementById("distance_max").value = data.distance_max;
            document.getElementById("distance_warning").value = data.distance_warning;
            document.getElementById("distance_danger").value = data.distance_danger;
            document.getElementById("version_number_label").textContent = data.version;

            // Store original values for cancel
            originalDistanceSensorEnabled = !!data.distance_sensor_enabled;
            originalDistanceMax = data.distance_max;
            originalDistanceWarning = data.distance_warning;
            originalDistanceDanger = data.distance_danger;

            const enabled = !!data.distance_sensor_enabled;
            document.getElementById('toggle_distance_sensor_switch').checked = enabled;
            toggleDistanceSensorInputs();

            // Show/hide car distance block on page load
            document.getElementById("carDistanceBlock").style.display = data.distance_sensor_enabled ? "" : "none";
        })
        .catch(error => console.error("Error fetching config values:", error));
}

function openPopup_time_waisted() {
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
    const switchElement = document.getElementById('toggle_distance_sensor_switch');
    const distanceSettings = document.getElementById('distanceSettings');
    const settingsWrapper = document.querySelector('.settings-wrapper');

    distanceSensorEnabled = switchElement.checked;

    if (distanceSensorEnabled) {
        distanceSettings.style.display = 'block';
        distanceSettings.classList.add('active-box');
        settingsWrapper.classList.remove('single-column');
        settingsWrapper.style.justifyContent = 'space-between';
    } else {
        distanceSettings.style.display = 'none';
        distanceSettings.classList.remove('active-box');
        settingsWrapper.classList.add('single-column');
        settingsWrapper.style.justifyContent = 'center';
    }
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

        } else if (data.state) {
            updateTrafficLight(data.state);

        } else if (data.distance_cm !== undefined && data.distance_cm !== null) {
            const distanceInput = document.getElementById("distance_to_wall");
            const distanceValue = parseInt(data.distance_cm, 10);

            if (distanceInput && distanceInput.tagName === "INPUT") {
                distanceInput.value = distanceValue;
                distanceInput.dispatchEvent(new Event('input', { bubbles: true }));
                console.log('setting "distance_to_wall" to', distanceValue, 'cm');
                updateCarPosition();
            }
        } else {
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

    document.getElementById("setConfig").addEventListener("click", function (event) {
        const distanceSensorWasEnabled = document.getElementById("toggle_distance_sensor_switch").checked;

        sendRequest("set_config").then(() => {
            // After saving, re-fetch latest config and update the form values
            fetch('/get_config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById("red_delay").value = data.red_delay;
                    document.getElementById("yellow_delay").value = data.yellow_delay;
                    document.getElementById("green_delay").value = data.green_delay;
                    document.getElementById("toggle_distance_sensor_switch").checked = !!data.distance_sensor_enabled;
                    document.getElementById("distance_max").value = data.distance_max;
                    document.getElementById("distance_warning").value = data.distance_warning;
                    document.getElementById("distance_danger").value = data.distance_danger;

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
            red_delay: parseFloat(document.getElementById("red_delay").value),
            yellow_delay: parseFloat(document.getElementById("yellow_delay").value),
            green_delay: parseFloat(document.getElementById("green_delay").value),
            distance_sensor_enabled: document.getElementById("toggle_distance_sensor_switch").checked,
            distance_max: document.getElementById("distance_max").value,
            distance_warning: document.getElementById("distance_warning").value,
            distance_danger: document.getElementById("distance_danger").value
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
});