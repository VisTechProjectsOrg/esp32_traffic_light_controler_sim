fetch('/config')
  .then(r => r.json())
  .then(cfg => {
    window.dangerThreshold = cfg.dangerThreshold;
    window.cautionThreshold = cfg.cautionThreshold;
    window.visualMax = cfg.visualMax;
    // Now use these in your code!
  });

const visualMax = 150;
const warningThreshold = visualMax;

let lastPosition = null;
let wheelRotation = 0;

function updateCarPosition() {
  const input = document.getElementById('distance_to_wall');
  const car = document.getElementById('car');
  const wheelFront = document.getElementById('car_wheel-front');
  const wheelRear = document.getElementById('car_wheel-rear');
  const status = document.getElementById('status');
  const warning = document.getElementById('warning');

  // Define thresholds for status colors
  const cautionThreshold = 40;
  const dangerThreshold = 10;
  
  const wallWidth = 20;

  const container = document.querySelector('.car_container');
  const carWidth = car.getBoundingClientRect().width;
  const containerWidth = container.clientWidth - wallWidth;

  let distance_to_wall = parseInt(input.value);
  const adjustedDistance = Math.max(distance_to_wall, 0);
  const leftPos = containerWidth * (visualMax - adjustedDistance) / visualMax - carWidth;

  car.style.left = `${leftPos}px`;
  wheelFront.style.left = `${leftPos + 203}px`;
  wheelRear.style.left = `${leftPos + 41}px`;

  if (lastPosition !== null && lastPosition !== leftPos) {
    const delta = leftPos - lastPosition;
    wheelRotation += delta * 3.3;
    setTimeout(() => {
      wheelFront.style.transform = `rotate(${wheelRotation}deg)`;
      wheelRear.style.transform = `rotate(${wheelRotation}deg)`;
    });
  }
  lastPosition = leftPos;

  // Show warning only when the car's front is completely out of frame (off the left edge)
  if (distance_to_wall >= warningThreshold) {
    warning.classList.add('visible');
  } else {
    warning.classList.remove('visible');
  }

  if (distance_to_wall <= dangerThreshold) {
    status.style.background = 'red';
  } else if (distance_to_wall <= cautionThreshold) {
    status.style.background = 'yellow';
  } else {
    status.style.background = 'green';
  }
}
// Call updateCarPosition only after all images are loaded
window.addEventListener('DOMContentLoaded', function() {
  document.getElementById('distance_to_wall').max = warningThreshold;
  updateCarPosition();
});
