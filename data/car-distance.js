// car-distance.js

let visualMax = 20;
let cautionThreshold = 3;
let dangerThreshold = 0.5;

let lastPosition = null;
let wheelRotation = 0;

function updateCarPosition() {
  const inp = document.getElementById('distance_to_wall');
  const dist = Math.max(parseFloat(inp.value) || 0, 0);
  const car = document.getElementById('car');
  const wf = document.getElementById('car_wheel-front');
  const wr = document.getElementById('car_wheel-rear');
  const status = document.getElementById('status');
  const warn = document.getElementById('warning');

  const cw = car.getBoundingClientRect().width;
  const cwidth = document.querySelector('.car_container').clientWidth - 20;
  const left = cwidth * (visualMax - dist) / visualMax - cw;

  car.style.left = `${left}px`;
  wf.style.left = `${left + 203}px`;
  wr.style.left = `${left + 41}px`;

  if (lastPosition !== null && lastPosition !== left) {
    const delta = left - lastPosition;
    wheelRotation += delta * 3.3;
    setTimeout(() => {
      wf.style.transform = `rotate(${wheelRotation}deg)`;
      wr.style.transform = `rotate(${wheelRotation}deg)`;
    }, 0);
  }
  lastPosition = left;

  // ⚠️warning only if  out-of-range if dist > visualMax
  warn.classList.toggle('visible', dist > visualMax);

  // status color
  if (dist <= dangerThreshold) status.style.background = 'red';
  else if (dist <= cautionThreshold) status.style.background = 'yellow';
  else status.style.background = 'green';
}

function loadAndApplyConfig() {
  // 1) Pick the right URL based on environment
  const isLocal = window.location.protocol === 'file:' ||
                  window.location.hostname === 'localhost';
  const url     = isLocal ? 'config.json' : '/get_config';

  fetch(url)
    .then(res => {
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      return res.json();
    })
    .then(cfg => applyConfig(cfg))
    .catch(err => {
      console.warn('⚠️ loadAndApplyConfig failed, using defaults:', err);
      // 2) Defaults in feet
      applyConfig({
        distance_max:      20,  // e.g. 20 ft
        distance_warning:  1,
        distance_danger:   0.5
      });
    });
}

function applyConfig(cfg) {
  // 3) Parse floats

  console.log('Applying config:', cfg);

  visualMax        = parseFloat(cfg.distance_max);
  cautionThreshold = parseFloat(cfg.distance_warning);
  dangerThreshold  = parseFloat(cfg.distance_danger);

  const slider = document.getElementById('distance_to_wall');
  slider.max   = visualMax.toFixed(2);
  slider.step  = (visualMax / 10).toFixed(2);  // ten intervals by default
  // slider.step = "1" // set step to 1 ft 

  updateCarPosition();
}

// 4) Wire it all up, including the Update button
window.addEventListener('DOMContentLoaded', () => {
  // initial load
  loadAndApplyConfig();

  // drag the slider
  document.getElementById('distance_to_wall')
    .addEventListener('input', updateCarPosition);

  // when you click “Update” in your settings popup
  document.getElementById('setConfig').addEventListener('click', e => {
    e.preventDefault();
    // assume your script.js already did the POST to /set_config
    // now we re-fetch & redraw with the new values:
    loadAndApplyConfig();
  });
});
