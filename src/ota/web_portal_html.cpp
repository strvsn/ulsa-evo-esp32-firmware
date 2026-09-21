/**
 * @file web_portal_html.cpp
 * @brief WiFi設定ポータルのHTMLテンプレート
 * @date 2025-12-07
 * 
 * キャプティブポータルとOTAアップデート用のWebページ定義
 */

#include "web_portal_html.h"
#include "system/firmware_identity.h"

// ============================================
// 共通スタイル
// ============================================
const char COMMON_STYLE[] PROGMEM = R"rawliteral(
    :root {
      color-scheme: dark;
      --bg: #101418;
      --panel: rgba(18, 22, 26, 0.92);
      --panel-soft: rgba(255, 255, 255, 0.07);
      --line: rgba(255, 255, 255, 0.16);
      --line-strong: rgba(255, 255, 255, 0.28);
      --text: rgba(255, 255, 255, 0.94);
      --muted: rgba(255, 255, 255, 0.62);
      --faint: rgba(255, 255, 255, 0.44);
      --accent: #4ecca3;
      --accent-strong: #6ee7bd;
      --danger: #ff6476;
      --warning: #f5b85d;
    }
    * { box-sizing: border-box; }
    html { min-height: 100%; background: var(--bg); }
    body {
      min-height: 100vh; margin: 0; padding: 18px;
      display: flex; align-items: center; justify-content: center;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif;
      letter-spacing: 0; color: var(--text);
      background:
        linear-gradient(180deg, rgba(255,255,255,.08), rgba(0,0,0,.24)),
        linear-gradient(135deg, #5e676f 0%, #343b42 54%, #20262c 100%);
    }
    .container {
      width: min(100%, 430px); margin: 0; padding: 0;
      overflow: hidden; border-radius: 8px;
      background: var(--panel);
      border: 1px solid var(--line-strong);
      box-shadow: 0 22px 68px rgba(0,0,0,.34);
    }
    .header {
      padding: 18px 18px 14px;
      background: rgba(18,22,26,.62);
      border-bottom: 1px solid var(--line);
    }
    .eyebrow {
      margin: 0 0 4px; font-size: 11px; font-weight: 800;
      color: var(--muted); text-transform: uppercase; letter-spacing: .08em;
    }
    h1 { margin: 0; color: var(--text); font-size: 22px; line-height: 1.15; font-weight: 780; }
    .subhead { margin: 8px 0 0; color: var(--muted); font-size: 13px; line-height: 1.45; }
    .content { padding: 16px 18px 18px; display: grid; gap: 14px; }
    .status-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
    .status-item {
      min-width: 0; padding: 10px; border-radius: 8px;
      background: var(--panel-soft); border: 1px solid var(--line);
    }
    .status-item span { display: block; color: var(--faint); font-size: 10px; font-weight: 800; text-transform: uppercase; letter-spacing: .08em; }
    .status-item strong { display: block; margin-top: 4px; color: var(--text); font-size: 13px; overflow-wrap: anywhere; }
    form { margin: 0; display: grid; gap: 12px; }
    .upload-box {
      padding: 12px; border-radius: 8px;
      background: rgba(0,0,0,.16); border: 1px dashed rgba(255,255,255,.28);
    }
    label { display: block; margin: 0 0 8px; color: var(--muted); font-size: 12px; font-weight: 720; }
    input[type="file"] {
      width: 100%; min-height: 42px; padding: 8px;
      border-radius: 8px; border: 1px solid var(--line);
      background: rgba(255,255,255,.08); color: var(--text); font-size: 13px;
    }
    input[type="file"]::file-selector-button {
      min-height: 32px; margin-right: 10px; padding: 0 12px;
      border: 0; border-radius: 8px;
      background: rgba(255,255,255,.92); color: rgba(16,24,32,.92);
      font-weight: 800;
    }
    .file-hint { margin: 8px 0 0; color: var(--faint); font-size: 12px; line-height: 1.45; }
    input[type="submit"], .button-like {
      width: 100%; min-height: 44px; padding: 0 14px;
      border: 0; border-radius: 8px; cursor: pointer;
      display: inline-flex; align-items: center; justify-content: center; gap: 8px;
      background: linear-gradient(180deg, rgba(110,231,189,.98), rgba(62,190,150,.98));
      color: rgba(12,24,22,.96); font-size: 14px; font-weight: 850; text-align: center;
      box-shadow: 0 10px 26px rgba(78,204,163,.16);
    }
    input[type="submit"]:disabled {
      cursor: wait; background: rgba(255,255,255,.18); color: rgba(255,255,255,.58); box-shadow: none;
    }
    .progress-panel {
      display: none; padding: 12px; border-radius: 8px;
      background: rgba(0,0,0,.18); border: 1px solid var(--line);
    }
    .progress-label { display: flex; justify-content: space-between; gap: 10px; margin-bottom: 8px; color: var(--muted); font-size: 12px; }
    .progress { width: 100%; height: 9px; overflow: hidden; border-radius: 999px; background: rgba(255,255,255,.16); }
    .progress-bar { height: 100%; width: 0%; border-radius: inherit; background: linear-gradient(90deg, var(--accent), var(--accent-strong)); transition: width .18s ease; }
    .notice {
      margin: 0; padding: 11px 12px; border-radius: 8px;
      background: rgba(245,184,93,.12); border: 1px solid rgba(245,184,93,.34);
      color: rgba(255,238,203,.94); font-size: 12px; line-height: 1.5;
    }
    .notice.danger { background: rgba(255,100,118,.12); border-color: rgba(255,100,118,.32); color: rgba(255,220,225,.95); }
    .app-return { text-align: center; }
    .app-return .mark {
      width: 54px; height: 54px; margin: 2px auto 14px; border-radius: 50%;
      display: flex; align-items: center; justify-content: center;
      background: rgba(78,204,163,.14); border: 1px solid rgba(78,204,163,.42);
      color: var(--accent-strong); font-size: 14px; font-weight: 900;
    }
    .app-return .copy { margin: 0; line-height: 1.55; color: var(--muted); font-size: 13px; }
    .app-return .pill {
      display: inline-block; margin-top: 12px; padding: 8px 10px; border-radius: 999px;
      background: var(--panel-soft); border: 1px solid var(--line); color: var(--text); font-size: 12px; font-weight: 760;
    }
    .success h1 { color: var(--accent-strong); }
    @media (max-width: 360px) {
      body { padding: 10px; }
      .header { padding: 16px 14px 12px; }
      .content { padding: 14px; }
      .status-grid { grid-template-columns: 1fr; }
    }
)rawliteral";

const char OTA_APP_RETURN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ULSA EVO App OTA</title>
  <style>%STYLE%</style>
</head>
<body>
  <main class="container app-return">
    <section class="header">
      <p class="eyebrow">ULSA Evo App</p>
      <h1>App OTA is ready</h1>
      <p class="subhead">This update session is controlled from the iPhone app.</p>
    </section>
    <section class="content">
      <div class="mark">APP</div>
      <p class="copy">Return to ULSA Evo App to select firmware.bin, start transfer, and watch progress.</p>
      <span class="pill">IP: %IP%</span>
    </section>
  </main>
</body>
</html>
)rawliteral";

// ============================================
// OTAアップデートページ（メインページ）
// ============================================
const char OTA_UPDATE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ULSA EVO Firmware Update</title>
  <style>%STYLE%</style>
</head>
<body>
  <main class="container">
    <section class="header">
      <p class="eyebrow">ULSA EVO Recovery</p>
      <h1>Firmware Update</h1>
      <p class="subhead">Use this page for manual recovery. App-driven OTA should be completed from ULSA Evo App.</p>
    </section>
    <section class="content">
      <div class="status-grid">
        <div class="status-item"><span>Version</span><strong>%VERSION%</strong></div>
        <div class="status-item"><span>Device IP</span><strong>%IP%</strong></div>
      </div>
      <form id="uploadForm" action="/doUpdate?token=%TOKEN%" method="POST" enctype="multipart/form-data">
        <div class="upload-box">
          <label for="firmwareFile">Firmware image</label>
          <input id="firmwareFile" type="file" name="firmware" accept=".bin" required>
          <p class="file-hint" id="fileHint">Select firmware.bin only. Do not use bootloader.bin or partitions.bin here.</p>
        </div>
        <input type="submit" value="Upload firmware.bin" id="submitBtn">
      </form>
      <div id="progressDiv" class="progress-panel">
        <div class="progress-label"><span id="progressState">Uploading firmware</span><strong id="progressText">0%</strong></div>
        <div class="progress"><div class="progress-bar" id="progressBar"></div></div>
      </div>
      <p class="notice danger">Keep the device powered until the update finishes and the ESP32 restarts.</p>
    </section>
  </main>
  <script>
    var form = document.getElementById('uploadForm');
    var fileInput = document.getElementById('firmwareFile');
    var fileHint = document.getElementById('fileHint');
    var submitBtn = document.getElementById('submitBtn');
    var progressDiv = document.getElementById('progressDiv');
    var progressBar = document.getElementById('progressBar');
    var progressText = document.getElementById('progressText');
    var progressState = document.getElementById('progressState');

    fileInput.addEventListener('change', function() {
      var file = fileInput.files && fileInput.files[0];
      fileHint.textContent = file ? file.name + ' / ' + Math.ceil(file.size / 1024) + ' KB' : 'Select firmware.bin only.';
    });

    form.addEventListener('submit', function(e) {
      e.preventDefault();
      var file = fileInput.files && fileInput.files[0];
      if (!file || !file.name.toLowerCase().endsWith('.bin')) {
        alert('Select a .bin firmware file.');
        return;
      }

      var formData = new FormData(form);
      var xhr = new XMLHttpRequest();
      submitBtn.disabled = true;
      submitBtn.value = 'Uploading...';
      progressDiv.style.display = 'block';
      progressBar.style.width = '0%';
      progressText.textContent = '0%';
      progressState.textContent = 'Uploading firmware';

      xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
          var pct = Math.round((e.loaded / e.total) * 100);
          progressBar.style.width = pct + '%';
          progressText.textContent = pct + '%';
        }
      });

      xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
          progressBar.style.width = '100%';
          progressText.textContent = 'Complete';
          progressState.textContent = 'Rebooting ESP32';
          submitBtn.value = 'Complete';
        } else {
          alert('Update failed: ' + xhr.responseText);
          submitBtn.disabled = false;
          submitBtn.value = 'Upload firmware.bin';
          progressDiv.style.display = 'none';
        }
      });

      xhr.addEventListener('error', function() {
        alert('Connection error');
        submitBtn.disabled = false;
        submitBtn.value = 'Upload firmware.bin';
        progressDiv.style.display = 'none';
      });

      xhr.open('POST', form.getAttribute('action'));
      xhr.send(formData);
    });
  </script>
</body>
</html>
)rawliteral";

// ============================================
// OTAアップデート成功ページ
// ============================================
const char OTA_SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ULSA EVO - Update Success</title>
  <style>%STYLE%</style>
</head>
<body>
  <main class="container success">
    <section class="header">
      <p class="eyebrow">ULSA EVO</p>
      <h1>Update Complete</h1>
      <p class="subhead">Firmware was written successfully.</p>
    </section>
    <section class="content app-return">
      <div class="mark">OK</div>
      <p class="copy">The device is rebooting. Reconnect from ULSA Evo App after the ESP32 starts again.</p>
    </section>
  </main>
</body>
</html>
)rawliteral";

// ============================================
// HTMLにスタイルを埋め込むヘルパー関数
// ============================================
String applyStyle(const char* html) {
  String result = FPSTR(html);
  result.replace("%STYLE%", FPSTR(COMMON_STYLE));
  return result;
}

// ============================================
// バージョン文字列取得
// ============================================
const char* getPortalFirmwareVersion() {
  return getEsp32FirmwareIdentity().version;
}
