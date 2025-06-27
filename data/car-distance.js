// car-distance.js

let visualMax = 150;
let cautionThreshold = 40;
let dangerThreshold = 10;

let lastPosition = null;
let wheelRotation = 0;

// 1) Build tick marks based on current visualMax
function generateTicks() {
  const container = document.querySelector('.car_ticks');
  if (!container) return;
  container.innerHTML = '';
  const step = Math.max(Math.floor(visualMax / 10), 1);
  for (let v = visualMax; v >= 0; v -= step) {
    const d = document.createElement('div');
    d.className = 'car_tick';
    d.dataset.label = v;
    container.appendChild(d);
  }
}

// 2) Position car, spin wheels, update status & warning
function updateCarPosition() {
  const inp = document.getElementById('distance_to_wall');
  const dist = Math.max(parseInt(inp.value, 10) || 0, 0);
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

// 3) Fetch your config and apply the three values
function loadAndApplyConfig() {
  fetch('/get_config')
    .then(r => r.json())
    .then(cfg => {
      visualMax = +cfg.distance_max;
      cautionThreshold = +cfg.distance_warning;
      dangerThreshold = +cfg.distance_danger;

      // update slider range
      document.getElementById('distance_to_wall').max = visualMax;

      // redraw ticks & car
      generateTicks();
      updateCarPosition();
    })
    .catch(e => console.error('Failed to load /get_config:', e));
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
