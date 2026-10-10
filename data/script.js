// One file, no build step beyond gzip. Sections, in order: helpers, signal heads,
// distance sensor, controls, settings dialog, websocket, boot.

const $ = (id) => document.getElementById(id);
// SVG elements have no .hidden property, so everything goes through the attribute.
const show = (el, on) => el.toggleAttribute('hidden', !on);

let cfg = null;          // last /get_config answer
let ped = null;          // last /ped_control answer; stays null on firmware built without the ped signal
let lamp = 'all_off';
let catMode = false;
let lightMode = 'cycle_mode';
let blinkColor = 'none';

// ---- helpers ----

function api(path, body) {
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 6000);
    const opt = { signal: ctl.signal };
    if (body) {
        opt.method = 'POST';
        opt.headers = { 'Content-Type': 'application/json' };
        opt.body = JSON.stringify(body);
    }
    return fetch(path, opt)
        .then((r) => {
            if (!r.ok) {
                const err = new Error(path + ' answered ' + r.status);
                err.status = r.status;
                throw err;
            }
            return r;
        })
        .finally(() => clearTimeout(timer));
}

const apiJson = (path, body) => api(path, body).then((r) => r.json());

// Image URLs built here cannot be stamped by tools/bump_spiffs_version.py, so the
// version comes from the config instead. Without it the week-long max-age would keep
// serving old artwork after a SPIFFS update.
const versioned = (url) => url + (cfg && cfg.version_spiffs ? '?v=' + cfg.version_spiffs : '');

// Resolves once the image has settled either way. img.complete is checked first: off a
// fast serve the load event can fire before a listener is attached.
function imageSettled(img) {
    if (img.complete && img.src) return Promise.resolve();
    return new Promise((resolve) => {
        img.addEventListener('load', resolve, { once: true });
        img.addEventListener('error', resolve, { once: true });
    });
}

let toastTimer = null;

function toast(message, isError) {
    const el = $('toast');
    el.textContent = message;
    el.classList.toggle('error', !!isError);
    show(el, true);
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => show(el, false), 3500);
}

// ---- console easter egg ----

function rainbow() {
    const shadows = ['-1px -1px hsl(0,100%,50%)'];
    for (let i = 1; i < 400; i++) {
        shadows.push(Math.trunc(60 * Math.sin(i * Math.PI / 100)) + 'px ' + i + 'px hsl(' +
            +(i * 5.4).toFixed(1) + ',100%,50%)');
    }
    return 'text-shadow: ' + shadows.join(', ') + '; font-size: 40px;';
}

console.log('%cWhy did I bother making this', rainbow());

function timeWasted() {
    console.log('%cWhy did I waste my time making this', rainbow());
    alert('Why did I waste my time making this');
}

// ---- traffic light ----

const LAMPS = ['red', 'yellow', 'green', 'all_on', 'all_off'];
const catUrl = (name) => versioned('img/cat/' + name + '.webp');
let catFailed = false;
let catPreloaded = false;

// The firmware reports cat mode by suffixing the state: "red_cat".
function updateTrafficLight(state) {
    lamp = state.replace(/_cat$/, '');
    renderLight();
}

function renderLight() {
    const svg = $('light');
    const img = $('lightCat');
    const useCat = catMode && !catFailed;

    svg.dataset.lamp = lamp;
    svg.setAttribute('aria-label', 'Traffic light: ' + lamp.replace('_', ' '));

    // Before the config lands there is no version to stamp the URL with; boot renders again.
    if (useCat && cfg) {
        img.src = catUrl(lamp);
        if (!catPreloaded) {
            // Fetch the other four now, or blink mode stutters on each first swap.
            catPreloaded = true;
            LAMPS.forEach((name) => { new Image().src = catUrl(name); });
        }
    }
    show(img, useCat);
    show(svg, !useCat);
}

// ---- pedestrian head ----
// The board reports phase changes only, so the 1Hz flash and the countdown run
// locally - which is also what the real module does: it watches the hot and times
// the digits itself.

const SEGMENTS = {
    0: 'abcdef', 1: 'bc', 2: 'abdeg', 3: 'abcdg', 4: 'bcfg',
    5: 'acdfg', 6: 'acdefg', 7: 'abc', 8: 'abcdefg', 9: 'abcdfg'
};

let pedFlashTimer = null;
let pedCountTimer = null;
let pedZeroTimer = null;
let pedState = 'off';

function setDigits(value) {
    const text = (value === null) ? '  ' : String(value).padStart(2, ' ');
    for (let i = 0; i < 2; i++) {
        const lit = SEGMENTS[text.charAt(i)] || '';
        for (const seg of 'abcdefg') {
            $('d' + i + seg).classList.toggle('on', lit.includes(seg));
        }
    }
}

function setSymbols(hand, man) {
    $('pedHand').classList.toggle('lit-hand', hand);
    $('pedMan').classList.toggle('lit-man', man);
}

function setPedVisual(state) {
    clearInterval(pedFlashTimer);
    clearInterval(pedCountTimer);
    clearTimeout(pedZeroTimer);
    const wasCounting = pedState === 'fdw';
    pedState = state;

    if (state === 'walk') {
        setSymbols(false, true);
        setDigits(null);

    } else if (state === 'fdw') {
        let on = true;
        setSymbols(true, false);
        pedFlashTimer = setInterval(() => {
            on = !on;
            setSymbols(on, false);
        }, 500);

        let count = ped ? ped.fdw : 15;
        setDigits(count);
        pedCountTimer = setInterval(() => {
            count -= 1;
            setDigits(Math.max(count, 0));
            if (count <= 0) clearInterval(pedCountTimer);
        }, 1000);

    } else {
        setSymbols(state === 'dont_walk', false);
        // The countdown and the phase end on the same tick, so without this the last
        // thing shown would be a 1. Land on 0 for a beat, as the real module does.
        if (state === 'dont_walk' && wasCounting) {
            setDigits(0);
            pedZeroTimer = setTimeout(() => setDigits(null), 1000);
        } else {
            setDigits(null);
        }
    }
}

// ---- distance sensor ----

let lastDistance = null;
let lastP = 0;
let wheelRotation = 0;

const clamp01 = (n) => Math.min(Math.max(n, 0), 1);

function fmtFt(value) {
    const n = Number.parseFloat(value);
    return (Number.isNaN(n) || n < 0) ? '--' : n.toFixed(2) + ' ft';
}

function loadCarImages() {
    const imgs = Array.from(document.querySelectorAll('#car img'));
    imgs.forEach((img) => {
        if (!img.src) img.src = versioned(img.dataset.src);
    });
    return Promise.all(imgs.map(imageSettled));
}

function drawScale() {
    const track = $('track');
    const max = cfg.distance_max;
    track.style.setProperty('--wz', clamp01(1 - cfg.distance_warning / max));
    track.style.setProperty('--dz', clamp01(1 - cfg.distance_danger / max));

    const ticks = $('ticks');
    ticks.textContent = '';
    for (let i = 0; i <= 5; i++) {
        const tick = document.createElement('span');
        tick.textContent = +(max * (1 - i / 5)).toFixed(1);
        tick.style.left = (i * 20) + '%';
        ticks.appendChild(tick);
    }
}

// null means the sensor sees nothing in range: the car parks at max, off the scale.
function setDistance(distance) {
    lastDistance = distance;
    if (!cfg || !cfg.distance_sensor_enabled) return;

    const max = cfg.distance_max;
    const dist = (distance === null) ? max : Math.max(distance, 0);
    let zone = '';
    if (distance !== null) {
        zone = dist <= cfg.distance_danger ? 'danger' : dist <= cfg.distance_warning ? 'warning' : 'clear';
    }

    $('distance_to_wall_display').textContent = (distance === null) ? '--' : dist.toFixed(1);
    $('distanceReadout').dataset.zone = zone;
    show($('warning'), dist > max);

    const track = $('track');
    const p = clamp01(1 - dist / max);
    track.style.setProperty('--p', p);

    const wheels = document.querySelectorAll('.car-wheel');
    const diameter = wheels[0].offsetWidth;
    if (diameter) {
        wheelRotation += (p - lastP) * track.clientWidth / (Math.PI * diameter) * 360;
        wheels.forEach((w) => { w.style.transform = 'rotate(' + wheelRotation + 'deg)'; });
    }
    lastP = p;
}

function updateSensorDiagnostics(p) {
    // Nothing on the wire beats whatever the state machine thinks it is doing.
    if (p.connected === false) p = { state: 'disconnected' };
    const state = $('diag_state');
    state.textContent = p.state || '--';
    state.dataset.state = p.state || '';

    $('diag_filtered').textContent = fmtFt(p.filtered);
    $('diag_raw').textContent = fmtFt(p.raw);
    $('diag_strength').textContent = (p.strength === undefined || p.strength === null) ? '--' : p.strength;
    $('diag_baseline').textContent = p.baseline_valid ? fmtFt(p.baseline) : 'learning...';
    $('diag_zone').textContent = p.zone || '--';
}

const queryProximity = () => apiJson('/proximity_control', { action: 'query' }).then(updateSensorDiagnostics);

$('relearnBaseline').addEventListener('click', () => {
    const btn = $('relearnBaseline');
    btn.disabled = true;
    btn.textContent = 'Relearning...';
    apiJson('/proximity_control', { action: 'relearn' })
        .then(updateSensorDiagnostics)
        .catch(() => toast('Relearn failed - controller did not respond', true))
        .finally(() => {
            btn.disabled = false;
            btn.textContent = 'Relearn baseline';
        });
});

// ---- config and state ----

function applyConfig(c) {
    cfg = c;
    $('version_number_firmware_label').textContent = 'FW: v' + c.version_firmware;
    $('version_number_spiffs_label').textContent = 'SPIFFS: v' + c.version_spiffs;

    show($('sensorBlock'), c.distance_sensor_enabled);
    if (!c.distance_sensor_enabled) return Promise.resolve();

    drawScale();
    setDistance(lastDistance);
    queryProximity().catch(() => { });
    return loadCarImages();
}

const loadConfig = () => apiJson('/get_config').then(applyConfig);

function applyState(s) {
    lightMode = s.light_mode;
    catMode = s.theme_mode === 'cat_mode';
    blinkColor = s.blink_color;
    if (s.state) updateTrafficLight(s.state);
    renderLight();
    renderControls();
}

const syncState = () => apiJson('/get_current_state').then(applyState);

// A board built without PED_SIGNAL_ENABLED has no such route; hide the head and its
// settings there rather than showing a dead signal.
function loadPed() {
    return apiJson('/ped_control', { action: 'query' })
        .catch((err) => {
            if (err.status === 404) return null;
            throw err;
        })
        .then((p) => {
            ped = p;
            show($('pedHead'), !!p);
            show($('pedSettings'), !!p);
            $('pedSettings').disabled = !p;
            if (p) setPedVisual(p.ped_state);
        });
}

// ---- controls ----

const modeButtons = Array.from(document.querySelectorAll('[data-mode]'));
const colorButtons = Array.from(document.querySelectorAll('[data-color]'));
const themeSwitch = $('toggleThemeModeSwitch');
const controls = modeButtons.concat(colorButtons, themeSwitch);

function renderControls() {
    modeButtons.forEach((b) => b.setAttribute('aria-pressed', b.dataset.mode === lightMode));
    colorButtons.forEach((b) => b.setAttribute('aria-pressed',
        lightMode === 'blink_mode' && b.dataset.color === blinkColor));
    themeSwitch.checked = catMode;
}

// The toggle routes flip state rather than set it, so a double tap would undo itself.
// Everything is locked until the board has answered and been re-read.
function command(path) {
    controls.forEach((c) => { c.disabled = true; });
    api(path)
        .then(syncState)
        .catch(() => {
            renderControls();
            toast('Controller did not respond', true);
        })
        .finally(() => controls.forEach((c) => { c.disabled = false; }));
}

modeButtons.forEach((b) => b.addEventListener('click', () => {
    if (b.dataset.mode !== lightMode) command('/toggle_light_mode');
}));

colorButtons.forEach((b) => b.addEventListener('click', () => command('/blink_mode?color=' + b.dataset.color)));

themeSwitch.addEventListener('change', () => command('/toggle_theme_mode'));

$('pageTitle').addEventListener('click', timeWasted);
$('light').addEventListener('click', timeWasted);
$('lightCat').addEventListener('click', timeWasted);
$('lightCat').addEventListener('error', () => {
    // Never leave a broken image where the signal should be.
    catFailed = true;
    renderLight();
});

// ---- settings dialog ----

const dialog = $('settings');
const form = $('setConfigForm');
const fieldsets = Array.from(form.querySelectorAll('fieldset'));
const DISTANCE_IDS = ['distance_danger', 'distance_warning', 'distance_max', 'approach_min'];
let settingsBusy = false;

// While loading or saving, nothing may dismiss the dialog or edit the form.
function setSettingsBusy(message) {
    settingsBusy = !!message;
    $('settingsStatus').textContent = message || '';
    show($('settingsStatus'), settingsBusy);
    fieldsets.forEach((f) => { f.disabled = settingsBusy || (f.id === 'pedSettings' && !ped); });
    ['setConfig', 'cancelSettings', 'closeSettings'].forEach((id) => { $(id).disabled = settingsBusy; });
}

function settingsError(message) {
    $('settingsError').textContent = message || '';
    show($('settingsError'), !!message);
}

function toggleDistanceSensorInputs() {
    const sw = $('toggle_distance_sensor_switch');
    const on = sw.checked;
    show($('distanceSettings'), on);
    // Disabled inputs are skipped by validation, which a hidden invalid field would block.
    DISTANCE_IDS.forEach((id) => { $(id).disabled = !on; });

    // Older firmware does not report presence; treat that as connected.
    const connected = !cfg || cfg.distance_sensor_connected !== false;
    // Switching it off is always allowed. Switching it on with nothing attached is not.
    sw.disabled = !connected && !on;
    const hint = $('sensorHint');
    hint.textContent = connected ? '' : on
        ? 'No sensor detected - check the wiring. It picks up again when the sensor answers.'
        : 'No sensor detected - check the wiring.';
    show(hint, !connected);
}

// With the light, the only ped number that can clash with the light is the countdown:
// the walk symbol simply gets whatever the light has left over. So the countdown is
// capped at the light's length, and nothing ever has to be stretched or skipped.
const PED_MODE_HINTS = {
    own: 'The crossing repeats on its own timer and ignores the traffic light.',
    light: 'The crossing runs during the light you pick: walk symbol first, then the countdown, ending as the light changes.'
};
const FDW_MIN = 3;
const FDW_MAX = 99;

// A value the form lowered by itself remembers what it was, so it can be put back the
// moment the cap no longer bites.
function releaseAuto(input, restore) {
    if (restore && input.dataset.before !== undefined) input.value = input.dataset.before;
    delete input.dataset.before;
    input.classList.remove('auto');
}

function updatePhaseTotal(event) {
    const el = $('pedPhaseTotal');
    const num = (id) => Number.parseInt($(id).value, 10) || 0;
    const mode = $('ped_mode').value;
    const countdown = $('ped_fdw');

    // Start from what the user set, then cap again below only if it is still called
    // for. A field being typed in is the user's own value from here on, so it is
    // released without being put back - and never rewritten under the cursor.
    const typed = (event && event.type === 'input') ? event.target : null;
    releaseAuto(countdown, countdown !== typed);

    show($('pedPhaseField'), mode === 'light');
    show($('pedWalkField'), mode === 'own');
    show($('pedDwField'), mode === 'own');
    show($('pedDelayField'), mode === 'light');
    // Hidden fields keep the value they loaded with; disabled only so validation skips them.
    $('ped_walk').disabled = mode !== 'own';
    $('ped_dw').disabled = mode !== 'own';
    $('ped_delay').disabled = mode !== 'light';
    $('pedModeHint').textContent = PED_MODE_HINTS[mode];
    el.classList.remove('held');
    countdown.max = FDW_MAX;

    const fdw = num('ped_fdw');

    if (mode === 'own') {
        $('pedFdwHint').textContent = 'seconds the hand flashes';
        el.textContent = 'Repeats every ' + (num('ped_walk') + fdw + num('ped_dw')) + 's: walk ' +
            num('ped_walk') + 's, countdown ' + fdw + 's, solid hand ' + num('ped_dw') + 's.';
        return;
    }

    const which = $('ped_chain_phase').value;
    const wait = num('ped_delay');
    // What the crossing has to fit in: the light, less the wait at the top of it.
    const light = num(which === 'green' ? 'delay_green' : 'delay_red') - wait;
    const room = wait ? 'what is left of the ' + which + ' after the wait' : 'the length of the ' + which;
    $('pedFdwHint').textContent = 'seconds the hand flashes; at most ' + Math.max(light, 0) + 's';

    if (light < FDW_MIN) {
        el.textContent = 'Crossing skipped: ' + room + ' is shorter than the shortest countdown (' + FDW_MIN + 's).';
        el.classList.add('held');
        return;
    }

    countdown.max = light;
    if (fdw > light) {
        el.classList.add('held');
        if (typed === countdown) {
            el.textContent = 'The countdown can be at most ' + light + 's, ' + room + '.';
            return;
        }
        el.textContent = 'Countdown lowered from ' + fdw + 's to ' + light + 's, ' + room + '.';
        countdown.dataset.before = fdw;
        countdown.value = light;
        countdown.classList.add('auto');
        return;
    }

    el.textContent = 'On ' + which + ': ' + (wait ? 'wait ' + wait + 's, ' : '') +
        (light > fdw ? 'walk ' + (light - fdw) + 's, countdown ' + fdw + 's.'
            : 'countdown ' + fdw + 's for the ' + (wait ? 'rest of the' : 'whole') + ' light, no walk symbol.') +
        ' Then solid hand until the next ' + which + '.';
}

function fillSettings() {
    releaseAuto($('ped_fdw'));
    ['delay_red', 'delay_yellow', 'delay_green'].concat(DISTANCE_IDS)
        .forEach((id) => { $(id).value = cfg[id]; });
    $('toggle_distance_sensor_switch').checked = cfg.distance_sensor_enabled;
    toggleDistanceSensorInputs();
    // Firmware from before the setting always ran without one; it defaults to on.
    $('lamp_test').checked = cfg.lamp_test !== false;

    if (ped) {
        $('ped_walk').value = ped.walk;
        $('ped_fdw').value = ped.fdw;
        $('ped_dw').value = ped.dw;
        $('ped_delay').value = ped.delay || 0;
        $('ped_mode').value = ped.chained ? 'light' : 'own';
        $('ped_chain_phase').value = ped.chain_phase;
    }
    updatePhaseTotal();
}

function openSettings() {
    settingsError('');
    $('setConfig').hidden = false;
    dialog.showModal();
    document.body.style.overflow = 'hidden';
    setSettingsBusy('Loading settings...');

    // Always re-read: another phone may have changed things since this page loaded.
    Promise.all([loadConfig(), loadPed()])
        .then(() => {
            setSettingsBusy(null);
            fillSettings();
        })
        .catch(() => {
            setSettingsBusy(null);
            fieldsets.forEach((f) => { f.disabled = true; });
            $('setConfig').hidden = true;
            settingsError('Could not load the settings - the controller did not respond. Close and try again.');
        });
}

function closeSettings() {
    if (!settingsBusy) dialog.close();
}

function validateSettings() {
    const danger = $('distance_danger');
    const warning = $('distance_warning');
    const approach = $('approach_min');
    warning.setCustomValidity('');
    danger.setCustomValidity('');
    approach.setCustomValidity('');

    if ($('toggle_distance_sensor_switch').checked) {
        if (Number(warning.value) >= Number($('distance_max').value)) {
            warning.setCustomValidity('The yellow zone must be smaller than the green zone.');
        }
        if (Number(danger.value) >= Number(warning.value)) {
            danger.setCustomValidity('The red zone must be smaller than the yellow zone.');
        }
        // A car is first seen at the green zone, so it has to be able to close this much.
        if (Number(approach.value) >= Number($('distance_max').value)) {
            approach.setCustomValidity('The approach must be smaller than the green zone.');
        }
    }
    return form.reportValidity();
}

function saveSettings(event) {
    event.preventDefault();
    if (settingsBusy || !validateSettings()) return;

    const num = (id) => Number($(id).value);
    const sensorOn = $('toggle_distance_sensor_switch').checked;
    // With the sensor off its fields are hidden, so the stored zones are sent back unchanged.
    const zone = (id) => (sensorOn ? num(id) : cfg[id]);

    const config = {
        action: 'set_config',
        delay_red: num('delay_red'),
        delay_yellow: num('delay_yellow'),
        delay_green: num('delay_green'),
        lamp_test: $('lamp_test').checked,
        distance_sensor_enabled: sensorOn,
        distance_max: zone('distance_max'),
        distance_warning: zone('distance_warning'),
        distance_danger: zone('distance_danger'),
        approach_min: zone('approach_min')
    };
    const pedConfig = ped && {
        action: 'set_config',
        walk: num('ped_walk'),
        fdw: num('ped_fdw'),
        dw: num('ped_dw'),
        delay: num('ped_delay'),
        chained: $('ped_mode').value === 'light',
        // the light's timing always wins; the form keeps the countdown inside it
        fit: true,
        chain_phase: $('ped_chain_phase').value
    };

    settingsError('');
    setSettingsBusy('Saving...');

    api('/set_config', config)
        .then(() => (pedConfig ? apiJson('/ped_control', pedConfig) : null))
        .then((p) => {
            if (p) ped = p;
            return loadConfig();
        })
        .then(() => {
            setSettingsBusy(null);
            dialog.close();
            toast('Settings saved');
        })
        .catch(() => {
            setSettingsBusy(null);
            settingsError('Not saved - the controller did not respond. Try again.');
        });
}

$('openSettings').addEventListener('click', openSettings);
$('closeSettings').addEventListener('click', closeSettings);
$('cancelSettings').addEventListener('click', closeSettings);
form.addEventListener('submit', saveSettings);
$('toggle_distance_sensor_switch').addEventListener('change', toggleDistanceSensorInputs);

['ped_walk', 'ped_fdw', 'ped_dw', 'ped_delay', 'ped_mode', 'ped_chain_phase', 'delay_red', 'delay_green']
    .forEach((id) => {
        $(id).addEventListener('input', updatePhaseTotal);
        $(id).addEventListener('change', updatePhaseTotal);
    });

// Escape
dialog.addEventListener('cancel', (event) => {
    if (settingsBusy) event.preventDefault();
});
// A click that lands on the dialog element itself is on the backdrop; the form covers the rest.
dialog.addEventListener('click', (event) => {
    if (event.target === dialog) closeSettings();
});
dialog.addEventListener('close', () => { document.body.style.overflow = ''; });

// ---- websocket ----

let wasLost = false;

function onMessage(data) {
    let known = false;

    if (data.light_mode) {
        lightMode = data.light_mode;
        renderControls();
        known = true;
    }
    if (data.theme_mode) {
        catMode = data.theme_mode === 'cat_mode';
        renderLight();
        renderControls();
        known = true;
    }
    if (data.blink_color) {
        blinkColor = data.blink_color;
        renderControls();
        known = true;
    }
    if (data.state) {
        updateTrafficLight(data.state);
        known = true;
    }
    if (data.ped_state) {
        setPedVisual(data.ped_state);
        known = true;
    }
    if (data.proximity) {
        updateSensorDiagnostics(data.proximity);
        known = true;
    }
    if (data.proximity_zone) {
        $('diag_zone').textContent = data.proximity_zone;
        known = true;
    }
    if (data.proximity_baseline !== undefined) {
        $('diag_baseline').textContent = fmtFt(data.proximity_baseline);
        known = true;
    }
    if (data.sensor_disconnected !== undefined) {
        if (cfg) cfg.distance_sensor_connected = !data.sensor_disconnected;
        if (data.sensor_disconnected) updateSensorDiagnostics({ state: 'disconnected' });
        else if (cfg && cfg.distance_sensor_enabled) queryProximity().catch(() => { });
        if (dialog.open && !settingsBusy) toggleDistanceSensorInputs();
        known = true;
    }
    if (data.distance !== undefined) {
        // null is a real value here: nothing in range
        setDistance(data.distance === null ? null : Number.parseFloat(data.distance));
        if (data.sensor_temp !== undefined) $('diag_temp').textContent = data.sensor_temp + ' C';
        known = true;
    }

    if (!known) console.error('Unknown data received:', data);
}

function connect() {
    const ws = new WebSocket('ws://' + window.location.host + '/ws');

    ws.onopen = () => {
        show($('reconnectPopup'), false);
        if (!wasLost) return;
        wasLost = false;

        // The board has probably rebooted. If that was a SPIFFS update, this page is
        // stale, so take the new one; otherwise just catch up on what was missed.
        const before = cfg && cfg.version_spiffs;
        Promise.all([loadConfig(), syncState(), loadPed()])
            .then(() => { if (before && cfg.version_spiffs !== before) window.location.reload(); })
            .catch(() => { });
    };

    ws.onmessage = (event) => {
        let data;
        try {
            data = JSON.parse(event.data);
        } catch (e) {
            console.error('Unknown data received:', event.data);
            return;
        }
        onMessage(data);
    };

    // A board that reboots never closes the socket, and a browser with nothing to send
    // never finds out. The firmware ignores incoming text, so this exists only to make
    // the dead connection fail fast.
    const heartbeat = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) ws.send('ping');
    }, 5000);

    ws.onclose = () => {
        clearInterval(heartbeat);
        wasLost = true;
        show($('reconnectPopup'), true);
        setTimeout(connect, 2000);
    };
}

$('refreshButton').addEventListener('click', () => window.location.reload());

// Opening the socket already clears the OTA flag on the board; this covers coming back
// to a tab that was left open while the OTA page was used in another.
document.addEventListener('visibilitychange', () => {
    if (!document.hidden) fetch('/reset_ota_state', { method: 'POST' }).catch(() => { });
});

// ---- boot ----
// Hold the splash until the first config and state have landed and the artwork they
// call for has loaded, so nothing fills in or swaps in front of the user. The timeout
// means an unreachable controller can never strand anyone on the splash.

(function boot() {
    const splash = $('bootSplash');
    let finished = false;

    function dismiss(message) {
        if (finished) return;
        finished = true;
        if (message) toast(message, true);
        splash.classList.add('boot-done');
        setTimeout(() => show(splash, false), 400);
    }

    setTimeout(() => dismiss('Controller slow to respond - loading anyway'), 8000);

    setPedVisual('off');
    renderControls();
    connect();

    Promise.all([loadConfig(), loadPed()])
        // state last: a cat-mode image URL needs the version from the config
        .then(syncState)
        .then(() => (catMode ? imageSettled($('lightCat')) : null))
        .then(() => dismiss(), () => dismiss('Could not reach the controller'));
})();
