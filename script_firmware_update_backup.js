// script_firmware_update.js

let uploadInProgress = false;

document.addEventListener("DOMContentLoaded", () => {
  // Add fade-in effect to the container
  document.querySelectorAll('.container').forEach(el => el.classList.add('fade-in'));

  const dropArea = document.getElementById("drop-area");
  const fileInput = document.getElementById("file_input");
  const progressBar = document.getElementById("progress_bar");
  const progressContainer = document.getElementById("progress_container");
  const statusText = document.getElementById("status");
  const firmwareBtn = document.getElementById("githubFirmware");
  const spiffsBtn = document.getElementById("githubSPIFFS");
  const versionInfo = document.getElementById("versionInfo");

  dropArea.addEventListener("click", () => fileInput.click());

  dropArea.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropArea.classList.add("dragover");
  });
  dropArea.addEventListener("dragleave", () => dropArea.classList.remove("dragover"));
  dropArea.addEventListener("drop", (e) => {
    e.preventDefault();
    dropArea.classList.remove("dragover");
    fileInput.value = ""; // prevent change event from re-triggering
    const file = e.dataTransfer.files[0];
    handleUpload(file);
  });

  fileInput.addEventListener("change", () => {
    const file = fileInput.files[0];
    handleUpload(file);
  });

  fetch("/get_config")
    .then(res => res.json())
    .then(device => {
      const currentFirmware = device.version_firmware || "0.0";
      const currentSPIFFS = device.version_spiffs || "0.0";
      versionInfo.textContent = `Current Firmware: v${currentFirmware}, SPIFFS: v${currentSPIFFS}`;

      fetch("https://raw.githubusercontent.com/VisTechProjects/esp32_traffic_light_controler_sim/firmware_prod/version.json")
        .then(res => res.json())
        .then(github => {
          const githubFirmware = github.firmware || "0.0";
          const githubSPIFFS = github.spiffs || "0.0";

          if (currentFirmware !== githubFirmware) {
            firmwareBtn.classList.add("update-available");
            firmwareBtn.disabled = false;
            firmwareBtn.textContent = `⬇️ Update Firmware to v${githubFirmware}`;
          } else {
            firmwareBtn.disabled = true;
            firmwareBtn.textContent = `✅ Firmware is Up-to-Date`;
          }

          if (currentSPIFFS !== githubSPIFFS) {
            spiffsBtn.classList.add("update-available");
            spiffsBtn.disabled = false;
            spiffsBtn.textContent = `⬇️ Update SPIFFS to v${githubSPIFFS}`;
          } else {
            spiffsBtn.disabled = true;
            spiffsBtn.textContent = `✅ SPIFFS is Up-to-Date`;
          }
        })
        .catch(err => {
          console.error("Failed to fetch version info from GitHub:", err);

          firmwareBtn.textContent = "⚠️ Firmware check failed";
          spiffsBtn.textContent = "⚠️ SPIFFS check failed";

          firmwareBtn.classList.add("error");
          spiffsBtn.classList.add("error");

          firmwareBtn.disabled = true;
          spiffsBtn.disabled = true;
        });
    }).catch(err => {
      console.error("Failed to fetch version info from GitHub:", err);

      firmwareBtn.textContent = "⚠️ Firmware check failed";
      spiffsBtn.textContent = "⚠️ SPIFFS check failed";

      firmwareBtn.classList.add("error");
      spiffsBtn.classList.add("error");

      firmwareBtn.disabled = true;
      spiffsBtn.disabled = true;
    });;

  firmwareBtn.addEventListener("click", () => {
    fetchAndUploadFromGitHub("https://raw.githubusercontent.com/VisTechProjects/esp32_traffic_light_controler_sim/firmware_prod/build/firmware.bin", "firmware.bin");
  });

  spiffsBtn.addEventListener("click", () => {
    fetchAndUploadFromGitHub("https://raw.githubusercontent.com/VisTechProjects/esp32_traffic_light_controler_sim/firmware_prod/build/spiffs.bin", "spiffs.bin");
  });


  function handleUpload(file) {
    if (uploadInProgress) return;

    // 1) Reset UI state
    progressContainer.style.display = "block";
    progressBar.style.width = "0%";
    progressBar.classList.remove("error");
    statusText.textContent = "";

    uploadInProgress = true;

    // 2) Kick off the POST
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/update");

    // 3) Single onload handler
    xhr.onload = function () {
      uploadInProgress = false;
      progressBar.classList.remove("uploading");

      if (xhr.status === 200 && xhr.responseText === "OK") {
        showSuccessModal();
      }
      else if (xhr.status === 400 && xhr.responseText === "INVALID_FILE") {
        statusText.textContent = "❌ Invalid file: must be firmware.bin or spiffs.bin";
        progressBar.classList.add("error");
      }
      else {
        statusText.textContent = "❌ OTA update failed. Please try again.";
        progressBar.classList.add("error");
      }
    };

    // 4) Network errors
    xhr.onerror = function () {
      uploadInProgress = false;
      statusText.textContent = "❌ Upload failed (network error)";
      progressBar.classList.add("error");
    };

    // 5) Progress bar
    xhr.upload.onprogress = function (e) {
      const pct = Math.round((e.loaded / e.total) * 100);
      progressBar.style.width = pct + "%";
    };

    // 6) Send raw Blob so the server sees the right filename
    //    (no FormData!)
    xhr.send(file);
  }


  // Call this when the user selects a file and clicks “Upload” NEW
  // function handleUpload(file) {
  //   if (uploadInProgress) return;

  //   // reset UI state every time
  //   progressContainer.style.display = "block";
  //   progressBar.style.width         = "0%";
  //   progressBar.classList.remove("error");
  //   statusText.textContent          = "";

  //   uploadInProgress = true;

  //   const xhr = new XMLHttpRequest();
  //   xhr.open("POST", "/update");

  //   xhr.onload = function () {
  //     uploadInProgress = false;
  //     progressBar.classList.remove("uploading");

  //     if (xhr.status === 200 && xhr.responseText === "OK") {
  //       showSuccessModal();  // your existing success dialog
  //     }
  //     else if (xhr.status === 400 && xhr.responseText === "INVALID_FILE") {
  //       statusText.textContent = "❌ Invalid file: must be firmware.bin or spiffs.bin";
  //       progressBar.classList.add("error");
  //     }
  //     else {
  //       statusText.textContent = "❌ OTA update failed. Please try again.";
  //       progressBar.classList.add("error");
  //     }
  //   };

  //   xhr.upload.onprogress = function (e) {
  //     const pct = Math.round((e.loaded / e.total) * 100);
  //     progressBar.style.width = pct + "%";
  //   };

  //   xhr.send(file);
  // }


  // function handleUpload(file) {
  //   if (uploadInProgress) return;

  //   // --- reset UI state every time ---
  //   progressContainer.style.display = "block";
  //   progressBar.style.width = "0%";
  //   progressBar.classList.remove("error");
  //   statusText.textContent = "";

  //   uploadInProgress = true;

  //   if (!file || !file.name.endsWith(".bin")) {
  //     statusText.textContent = "❌ Only .bin files are allowed.";
  //     uploadInProgress = false;
  //     return;
  //   }

  //   const isSPIFFS = file.name.toLowerCase().includes("spiffs");
  //   statusText.textContent = `Uploading ${file.name} (${isSPIFFS ? "SPIFFS" : "Firmware"})...`;
  //   progressContainer.style.display = "block";
  //   progressBar.style.width = "0%";

  //   const xhr = new XMLHttpRequest();
  //   xhr.open("POST", "/update");
  //   xhr.onload = function () {
  //     uploadInProgress = false;
  //     progressBar.classList.remove("uploading");

  //     if (xhr.status === 200 && xhr.responseText === "OK") {
  //       showSuccessModal();  // your existing success popup
  //     }
  //     else if (xhr.status === 400 && xhr.responseText === "INVALID_FILE") {
  //       statusText.textContent = "❌ Invalid file: must be firmware.bin or spiffs.bin";
  //       progressBar.classList.add("error");
  //     }
  //     else {
  //       statusText.textContent = "❌ OTA update failed. Please try again.";
  //       progressBar.classList.add("error");
  //     }
  //   };

  //   xhr.upload.onprogress = function (e) {
  //     const pct = Math.round((e.loaded / e.total) * 100);
  //     progressBar.style.width = pct + "%";
  //   };

  //   xhr.send(file);
  // }

  // xhr.onload = function () {
  //   uploadInProgress = false;
  //   progressBar.classList.remove("uploading");

  //   if (xhr.status === 200 && xhr.responseText === "OK") {
  //     // ✅ success
  //     progressBar.style.width = "100%";
  //     showModal("success", "Update Successful", "Your device is now rebooting.");
  //     // showSuccessModal();
  //   }
  //   else if (xhr.status === 400 && xhr.responseText === "INVALID_FILE") {
  //     // 🛑 invalid file
  //     showModal("error", "Invalid File",
  //       "Please upload exactly “firmware.bin” or “spiffs.bin”.");
  //   }
  //   else {
  //     // 🚨 other OTA error
  //     showModal("error", "Upload Failed",
  //       "An error occurred during OTA update. Please try again.");
  //   }
  // };
  // xhr.onload = function () {
  //   if (xhr.status === 200) {
  //     showSuccessModal();   // must be your existing success popup
  //   }
  //   else if (xhr.status === 400) {
  //     // invalid file
  //     statusText.textContent = "❌ Invalid file: upload firmware.bin or spiffs.bin";
  //     progressBar.classList.add("error");
  //   }
  //   else {
  //     // other OTA error
  //     statusText.textContent = "❌ OTA update failed. Try again.";
  //     progressBar.classList.add("error");
  //   }
  // };


  xhr.onerror = function () {
    uploadInProgress = false;
    statusText.textContent = "❌ Upload failed (network error)";
    progressBar.classList.add("error");
  };

  const form = new FormData();
  form.append("file", file, file.name);
  xhr.send(form);
  // }

  function fetchAndUploadFromGitHub(url, name) {
    statusText.textContent = `Fetching ${name} from GitHub...`;
    progressContainer.style.display = "block";
    progressBar.style.width = "0%";

    fetch(url)
      .then(response => {
        if (!response.ok) throw new Error("Failed to fetch " + name);
        return response.blob();
      })
      .then(blob => {
        const file = new File([blob], name, { type: "application/octet-stream" });
        handleUpload(file);
      })
      .catch(err => {
        statusText.textContent = "❌ " + err.message;
      });
  }

  function showSuccessModal() {
    document.getElementById("successModal").style.display = "flex";
  }

  /**
 * @param {"success"|"error"} type
 * @param {string} title   — e.g. "Update Successful" or "Invalid File"
 * @param {string} message — descriptive text
 */
  function showModal(type, title, message) {
    // create backdrop
    const backdrop = document.createElement("div");
    backdrop.classList.add("modal-backdrop");
    document.body.appendChild(backdrop);

    // create modal box
    const modal = document.createElement("div");
    modal.classList.add("modal-box", type);       // you can use CSS to style .modal-box.success vs .error
    modal.innerHTML = `
    <h3>${title}</h3>
    <p>${message}</p>
    <button>OK</button>
  `;
    document.body.appendChild(modal);

    // center & show
    backdrop.style.display = modal.style.display = "flex";

    // dismiss
    modal.querySelector("button").addEventListener("click", () => {
      backdrop.remove();
      modal.remove();
      if (type === "success") {
        window.location.href = "/";  // optional: go back to main page
      }
    });
  }

  window.closeModal = function () {
    document.getElementById("successModal").style.display = "none";
    window.location.href = "/";
  };
});
