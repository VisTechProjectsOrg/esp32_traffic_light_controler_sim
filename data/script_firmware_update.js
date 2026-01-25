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
    const btn_firmware = document.getElementById("githubFirmware");
    const btn_spiffs = document.getElementById("githubSPIFFS");
    const versionInfo = document.getElementById("versionInfo");

    // Fetch device config and GitHub versions (omitted for brevity)
    // …

    // Clear file input on click so selecting the same file re-triggers change event
    fileInput.addEventListener('click', () => {
        fileInput.value = '';
    });

    dropArea.addEventListener("click", () => fileInput.click());

    dropArea.addEventListener("dragover", (e) => {
        e.preventDefault();
        dropArea.classList.add("dragover");
    });

    dropArea.addEventListener("dragleave", () => dropArea.classList.remove("dragover"));

    dropArea.addEventListener("drop", (e) => {
        e.preventDefault();
        dropArea.classList.remove("dragover");
        fileInput.value = ""; // clear prior selection
        const file = e.dataTransfer.files[0];
        handleUpload(file);
    });

    fileInput.addEventListener("change", () => {
        const file = fileInput.files[0];
        handleUpload(file);
    });

    btn_firmware.addEventListener("click", () => {
        fetchAndUploadFromGitHub(
            "https://raw.githubusercontent.com/VisTechProjectsOrg/esp32_traffic_light_controler_sim/firmware_prod/build/firmware.bin",
            "firmware.bin"
        );
    });

    btn_spiffs.addEventListener("click", () => {
        fetchAndUploadFromGitHub(
            "https://raw.githubusercontent.com/VisTechProjectsOrg/esp32_traffic_light_controler_sim/firmware_prod/build/spiffs.bin",
            "spiffs.bin"
        );
    });

    function handleUpload(file) {
        if (uploadInProgress) return;

        // reset UI state every time
        progressContainer.style.display = "block";
        progressBar.style.width = "0%";
        progressBar.classList.remove("error");
        statusText.textContent = "";

        uploadInProgress = true;

        if (!file || !file.name.endsWith(".bin")) {
            statusText.textContent = "❌ Only .BIN files are allowed.";
            uploadInProgress = false;
            fileInput.value = '';
            return;
        }

        if (!["firmware.bin", "spiffs.bin"].includes(file.name)) {
            statusText.innerHTML = `❌ ${file.name} is not a valid file.<br>Must be firmware.bin or spiffs.bin`;
            uploadInProgress = false;
            fileInput.value = '';
            return;
        }

        const isSPIFFS = file.name.toLowerCase().includes("spiffs");
        statusText.textContent = `Uploading ${isSPIFFS ? "SPIFFS" : "Firmware"}...`;

        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/update");

        xhr.upload.onprogress = function (e) {
            const pct = Math.round((e.loaded / e.total) * 100);
            progressBar.style.width = pct + "%";
        };

        xhr.onload = function () {
            uploadInProgress = false;
            progressBar.classList.remove("uploading");
            fileInput.value = '';

            if (xhr.status === 200 && xhr.responseText === "OK") {
                progressBar.style.width = "100%";
                showModal("success", "Update Successful", "Your device is now rebooting.");
            } else if (xhr.status === 400 && xhr.responseText === "INVALID_FILE") {
                statusText.textContent = "❌ Invalid file: must be firmware.bin or spiffs.bin";
                progressBar.style.width = "0%";
                progressBar.classList.add("error");
            } else {
                statusText.textContent = "❌ OTA update failed. Please try again.";
                progressBar.style.width = "0%";
                progressBar.classList.add("error");
            }
        };

        xhr.onerror = function () {
            uploadInProgress = false;
            statusText.textContent = "❌ Upload failed (network error)";
            progressBar.style.width = "0%";
            progressBar.classList.add("error");
            fileInput.value = '';
        };

        const form = new FormData();
        form.append("update", file, file.name);
        xhr.send(form);
    }

    function fetchAndUploadFromGitHub(url, name) {
        statusText.textContent = `Fetching ${name} from GitHub...`;
        progressBar.style.width = "0%";
        progressContainer.style.display = "block";

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
                fileInput.value = '';
            });
    }

    function showModal(type, title, message) {
        const backdrop = document.createElement("div");
        backdrop.classList.add("modal-backdrop");
        document.body.append(backdrop);

        const modal = document.createElement("div");
        modal.classList.add("modal-box", type);
        modal.innerHTML = `
        <h3>${title}</h3>
        <p>${message}</p>
        <button>OK</button>
        `;

        backdrop.append(modal);
        backdrop.style.display = "flex";

        modal.querySelector("button").addEventListener("click", () => {
            backdrop.remove();
            if (type === "success") window.location.href = "/";
        });
    }

    fetch("/get_config")
        .then(res => res.json())
        .then(device => {
            const currentFirmware = device.version_firmware || "0.0";
            const currentSPIFFS = device.version_spiffs || "0.0";
            versionInfo.textContent = `Current Firmware: v${currentFirmware}, SPIFFS: v${currentSPIFFS}`;

            console.info("%cCurrent Firmware: " + currentFirmware, 'color: orange;');
            console.info("%cCurrent SPIFFS: "+ currentSPIFFS, 'color: orange;');
            console.info("Fetching version info from GitHub...");


            fetch("https://raw.githubusercontent.com/VisTechProjectsOrg/esp32_traffic_light_controler_sim/firmware_prod/version.json")
                .then(res => res.json())
                .then(github => {
                    const githubFirmware = github.firmware || "0.0";
                    const githubSPIFFS = github.spiffs || "0.0";

                    const firmwareNeedsUpdate = currentFirmware !== githubFirmware;
                    const spiffsNeedsUpdate = currentSPIFFS !== githubSPIFFS;

                    if (firmwareNeedsUpdate) {
                        btn_firmware.classList.add("update-available");
                        btn_firmware.disabled = false;
                        btn_firmware.textContent = `Update Firmware to v${githubFirmware}`;
                        console.info('%cGitHub Firmware update available: ' + githubFirmware, 'color: green;');
                    } else {
                        btn_firmware.disabled = true;
                        btn_firmware.textContent = `Firmware is Up-to-Date`;
                        console.info('%cGitHub Firmware is up-to-date: ' + githubFirmware, 'color: green;');
                    }

                    if (spiffsNeedsUpdate) {
                        if (firmwareNeedsUpdate) {
                            btn_spiffs.disabled = true;
                            btn_spiffs.textContent = `Update Firmware first`;
                            btn_spiffs.classList.add("waiting");
                            console.info('%cSPIFFS update available but waiting for firmware', 'color: orange;');
                        } else {
                            btn_spiffs.classList.add("update-available");
                            btn_spiffs.disabled = false;
                            btn_spiffs.textContent = `Update SPIFFS to v${githubSPIFFS}`;
                            console.info('%cGitHub SPIFFS update available: ' + githubSPIFFS, 'color: green;');
                        }
                    } else {
                        btn_spiffs.disabled = true;
                        btn_spiffs.textContent = `SPIFFS is Up-to-Date`;
                        console.info("%cGitHub SPIFFS is up-to-date: " + githubSPIFFS, 'color: green;');
                    }
                })
                .catch(err => {
                    console.error("%cFailed to fetch version info from GitHub:" + err,'color: red;');

                    btn_firmware.textContent = "⚠️ Firmware check failed";
                    btn_spiffs.textContent = "⚠️ SPIFFS check failed";

                    btn_firmware.classList.add("error");
                    btn_spiffs.classList.add("error");

                    btn_firmware.disabled = true;
                    btn_spiffs.disabled = true;
                });
        }).catch(err => {
            console.error("%cFailed to fetch version info from GitHub:" + err,'color: red;');

            btn_firmware.textContent = "⚠️ Firmware check failed";
            btn_spiffs.textContent = "⚠️ SPIFFS check failed";

            btn_firmware.classList.add("error");
            btn_spiffs.classList.add("error");

            btn_firmware.disabled = true;
            btn_spiffs.disabled = true;
        });
});
