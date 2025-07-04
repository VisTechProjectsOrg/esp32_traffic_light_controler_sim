const uploadArea = document.getElementById("upload_area");
const fileInput = document.getElementById("file_input");
const status = document.getElementById("status");
const progressBar = document.getElementById("progress_bar");
const progressContainer = document.getElementById("progress_container");
const versionInfo = document.getElementById("versionInfo");
const firmwareBtn = document.getElementById("githubFirmware");
const spiffsBtn = document.getElementById("githubSPIFFS");

const githubFirmwareVersion = "1.2.0";
const githubSpiffsVersion = "1.2.0";

uploadArea.addEventListener("click", () => fileInput.click());
uploadArea.addEventListener("dragover", (e) => {
  e.preventDefault();
  uploadArea.classList.add("dragover");
});
uploadArea.addEventListener("dragleave", () => uploadArea.classList.remove("dragover"));
uploadArea.addEventListener("drop", (e) => {
  e.preventDefault();
  uploadArea.classList.remove("dragover");
  const file = e.dataTransfer.files[0];
  handleUpload(file);
});
fileInput.addEventListener("change", () => handleUpload(fileInput.files[0]));

function handleUpload(file) {
  if (!file || !file.name.endsWith(".bin")) {
    status.textContent = "❌ Only .bin files are allowed.";
    return;
  }

  const isSPIFFS = file.name.toLowerCase().includes("spiffs");
  status.textContent = `Uploading ${file.name} (${isSPIFFS ? "SPIFFS" : "Firmware"})...`;
  progressContainer.style.display = "block";
  progressBar.style.width = "0%";

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/update", true);
  xhr.upload.onprogress = function (e) {
    if (e.lengthComputable) {
      const percent = ((e.loaded / e.total) * 100).toFixed(0);
      status.textContent = `Uploading ${file.name} - ${percent}%`;
      progressBar.style.width = percent + "%";
    }
  };
  xhr.onload = function () {
    if (xhr.status === 200 && xhr.responseText === "OK") {
      status.textContent = "✅ Upload complete.";
      progressBar.style.width = "100%";
      showSuccessModal();
    } else {
      status.textContent = "❌ Upload failed.";
      progressBar.style.backgroundColor = "#f44336";
    }
  };
  const form = new FormData();
  form.append("file", file, file.name);
  xhr.send(form);
}

function fetchAndUploadFromGitHub(url, label) {
  status.textContent = `🔄 Fetching ${label} from GitHub...`;
  progressContainer.style.display = "block";
  progressBar.style.width = "0%";

  fetch(url)
    .then(response => {
      if (!response.ok) throw new Error("Failed to fetch " + label);
      return response.blob();
    })
    .then(blob => {
      const file = new File([blob], label, { type: "application/octet-stream" });
      handleUpload(file);
    })
    .catch(err => {
      status.textContent = `❌ ${err.message}`;
      progressBar.style.backgroundColor = "#f44336";
    });
}

firmwareBtn.addEventListener("click", () => {
  fetchAndUploadFromGitHub("https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/build/firmware.bin", "firmware.bin");
});
spiffsBtn.addEventListener("click", () => {
  fetchAndUploadFromGitHub("https://raw.githubusercontent.com/YOUR_USER/YOUR_REPO/main/build/spiffs.bin", "spiffs.bin");
});

function showSuccessModal() {
  document.getElementById("successModal").style.display = "flex";
}
function closeModal() {
  document.getElementById("successModal").style.display = "none";
  window.location.href = "/";
}

fetch("/get_config")
  .then(res => res.json())
  .then(data => {
    const fw = data.version_firmware || "0.0.0";
    const spiffs = data.version_spiffs || "0.0.0";
    versionInfo.textContent = `Current Firmware: v${fw}, SPIFFS: v${spiffs}`;
    if (fw !== githubFirmwareVersion) firmwareBtn.classList.remove("hidden");
    if (spiffs !== githubSpiffsVersion) spiffsBtn.classList.remove("hidden");
  })
  .catch(() => versionInfo.textContent = "Version info unavailable.");
