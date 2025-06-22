var ws = new WebSocket('ws://' + window.location.hostname + '/ws');
let distanceSensorEnabled = false; // Add this at the top with other variables

function updateTrafficLight(state) {
    document.getElementById('traffic-light').src = '/img/traffic_lt/' + state + '.png';
}

function closePopup(event) {
    if (event) event.preventDefault();

    // Only apply changes if clicking the Update Settings button
    if (event && event.target.id === 'setDelays') {
        const panel = document.getElementById('carDistancePanel');
        panel.style.display = distanceSensorEnabled ? 'inline-block' : 'none';

        // Store the state
        const switch_element = document.getElementById('toggleDistanceSensorSwitch');
        distanceSensorEnabled = switch_element.checked;
    }

    document.getElementById("popup").style.display = "none";
    document.getElementById("overlay").style.display = "none";
}

function openPopup_settings() {
    console.log("Opening settings popup");

    document.getElementById("popup").style.display = "block";
    document.getElementById("overlay").style.display = "block";

    // Set switch state to match current panel visibility
    const panel = document.getElementById('carDistancePanel');
    const switch_element = document.getElementById('toggleDistanceSensorSwitch');
    // switch_element.checked = panel.style.display !== 'none';
    distanceSensorEnabled = switch_element.checked;

    fetch('/get_delays?' + new Date().getTime())  // Adds a unique timestamp, prevents caching
        .then(response => response.json())
        .then(data => {
            console.log("Fetched delay values:", data); // <-- Add this
            document.getElementById("red_delay").value = data.red_delay / 1000;
            document.getElementById("yellow_delay").value = data.yellow_delay / 1000;
            document.getElementById("green_delay").value = data.green_delay / 1000;
            document.getElementById("toggleDistanceSensorSwitch").checked = !!data.distance_sensor;
        })
        .catch(error => console.error("Error fetching delay values:", error));
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

function toggleDistanceSensor() {
    const switch_element = document.getElementById('toggleDistanceSensorSwitch');
    distanceSensorEnabled = switch_element.checked;
    // Remove the immediate panel visibility change
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
            const distanceValue = parseInt(data.distance_cm, 10); // convert to integer
            if (distanceInput && distanceInput.tagName === "INPUT") {
                distanceInput.value = distanceValue;
                distanceInput.dispatchEvent(new Event('input', { bubbles: true }));
                console.log('setting "distance_to_wall" to ', distanceValue, 'cm');
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

    document.getElementById("setDelays").addEventListener("click", function () {
        sendRequest("set_delays");
    });

    function sendRequest(action) {
        const data = {
            action: action,
            red_delay: parseFloat(document.getElementById("red_delay").value),
            yellow_delay: parseFloat(document.getElementById("yellow_delay").value),
            green_delay: parseFloat(document.getElementById("green_delay").value)
        };

        fetch("/set_delays", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify(data)
        })
            .then(response => response.json())
            .then(data => {
                console.log("Server Response (set delays):", data);
                console.log("%cSubmitted successfully new delay values", "color: green; font-weight: bold;");
                alert("Delays updates successfully!");
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