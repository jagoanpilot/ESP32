/*
 * =============================================
 * WIFI WEBSOCKET HANDLER - Implementation
 * Compatible with ESP32 Arduino Core 3.x
 * =============================================
 */
#include "wifi_handler.h"
#include "ota_http.h"
#include <Preferences.h>
#include <BLEDevice.h>

// BLE globals from main firmware (may be null when booting in WiFi mode)
extern BLEServer* pServer;

// External declarations from main .ino
extern Preferences preferences;
extern SemaphoreHandle_t nvsMutex;
extern SemaphoreHandle_t dataMutex;
extern std::atomic<bool> isInjectorEnabled;
extern std::atomic<bool> canDataReady;
extern bool currentTwaiModeNormal;

// For inject status and test panel
extern std::atomic<int> atomicSpeed;
extern std::atomic<int> atomicRPM;
extern std::atomic<int32_t> atomicVoltsRaw;
extern std::atomic<int32_t> atomicAmpereRaw;
extern std::atomic<int32_t> atomicPowerRaw;
extern std::atomic<bool> atomicRegenActive;
extern std::atomic<VehicleMode> atomicMode;
extern bool bmsChargingFlag;
extern bool chargerConnected;
extern bool oriChargerDetected;

// Helper: NVS write with mutex protection
static inline void nvsWriteBool(const char* key, bool val) {
  if (xSemaphoreTake(nvsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    preferences.putBool(key, val);
    xSemaphoreGive(nvsMutex);
  }
}
static inline void nvsWriteString(const char* key, const char* val) {
  if (xSemaphoreTake(nvsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    preferences.putString(key, val);
    xSemaphoreGive(nvsMutex);
  }
}

// Global instances
AsyncWebServer wsServer(WS_PORT);
AsyncWebSocket ws(WS_URL);
bool wifiModeActive = false;
static uint32_t lastWiFiCheck = 0;
static bool wifiEventRegistered = false;
static bool wsHandlersRegistered = false;  // Fix #6: prevent handler stacking

static const char* testPanelHtml = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>VOTOL Test Panel</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #111827;
      color: #e5e7eb;
      margin: 0;
      padding: 24px;
    }
    .wrap {
      max-width: 820px;
      margin: 0 auto;
    }
    .card {
      background: #1f2937;
      border-radius: 14px;
      padding: 18px;
      box-shadow: 0 8px 24px rgba(0,0,0,0.25);
      margin-bottom: 18px;
    }
    h1 {
      margin-top: 0;
      color: #93c5fd;
    }
    .row {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 12px;
    }
    .col {
      flex: 1;
      min-width: 150px;
    }
    label {
      display: block;
      font-size: 12px;
      color: #9ca3af;
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }
    input[type="range"], select, input[type="checkbox"] {
      width: 100%;
    }
    input[type="checkbox"] {
      width: auto;
      transform: scale(1.3);
      margin-top: 6px;
    }
    .buttons {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
      gap: 8px;
    }
    button {
      padding: 12px;
      border-radius: 10px;
      border: none;
      background: #3b82f6;
      color: white;
      font-weight: 700;
      cursor: pointer;
    }
    button.secondary {
      background: #374151;
    }
    .status {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
      gap: 12px;
      margin-top: 16px;
    }
    .status-box {
      background: #111827;
      border-radius: 10px;
      padding: 12px;
      border: 1px solid #374151;
    }
    .status-box small {
      display: block;
      color: #9ca3af;
      margin-bottom: 4px;
    }
    .status-box strong {
      font-size: 20px;
    }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>VOTOL Test Panel</h1>

    <div class="card">
      <div class="row">
        <div class="col">
          <label for="mode">Mode</label>
          <select id="mode">
            <option value="PARK">PARK</option>
            <option value="DRIVE">DRIVE</option>
            <option value="SPORT">SPORT</option>
            <option value="REVERSE">REVERSE</option>
            <option value="BRAKE">BRAKE</option>
            <option value="CHARGING">CHARGING</option>
            <option value="STAND">STAND</option>
          </select>
        </div>
        <div class="col">
          <label for="throttle">Throttle (%)</label>
          <input id="throttle" type="range" min="0" max="100" value="0" />
          <div id="throttleLabel">0%</div>
        </div>
      </div>

      <div class="row">
        <div class="col">
          <label for="speed">Speed (km/h)</label>
          <input id="speed" type="range" min="0" max="120" value="0" />
          <div id="speedLabel">0 km/h</div>
        </div>
        <div class="col">
          <label for="rpm">RPM</label>
          <input id="rpm" type="range" min="0" max="5000" value="0" />
          <div id="rpmLabel">0 rpm</div>
        </div>
      </div>

      <div class="row">
        <div class="col">
          <label for="volts">Voltage (V)</label>
          <input id="volts" type="range" min="30" max="100" value="58" />
          <div id="voltsLabel">58.0 V</div>
        </div>
        <div class="col">
          <label for="amps">Current (A)</label>
          <input id="amps" type="range" min="-80" max="80" value="0" />
          <div id="ampsLabel">0.0 A</div>
        </div>
      </div>

      <div class="row">
        <div class="col">
          <label>Charging</label>
          <input id="charging" type="checkbox" />
        </div>
        <div class="col">
          <label>Regen</label>
          <input id="regen" type="checkbox" />
        </div>
      </div>

      <div class="buttons">
        <button id="apply">Apply Test Values</button>
        <button class="secondary" id="reset">Reset</button>
      </div>
    </div>

    <div class="card">
      <h2>Quick Presets</h2>
      <div class="buttons">
        <button class="secondary" data-mode="PARK">PARK</button>
        <button class="secondary" data-mode="DRIVE">DRIVE</button>
        <button class="secondary" data-mode="SPORT">SPORT</button>
        <button class="secondary" data-mode="BRAKE">BRAKE</button>
        <button class="secondary" data-mode="CHARGING">CHARGING</button>
      </div>
    </div>

    <div class="card">
      <div class="status">
        <div class="status-box">
          <small>Mode</small>
          <strong id="modeStatus">PARK</strong>
        </div>
        <div class="status-box">
          <small>Speed</small>
          <strong id="speedStatus">0</strong>
        </div>
        <div class="status-box">
          <small>RPM</small>
          <strong id="rpmStatus">0</strong>
        </div>
        <div class="status-box">
          <small>Power</small>
          <strong id="powerStatus">0W</strong>
        </div>
      </div>
    </div>
  </div>

  <script>
    const controls = {
      mode: document.getElementById('mode'),
      throttle: document.getElementById('throttle'),
      speed: document.getElementById('speed'),
      rpm: document.getElementById('rpm'),
      volts: document.getElementById('volts'),
      amps: document.getElementById('amps'),
      charging: document.getElementById('charging'),
      regen: document.getElementById('regen')
    };

    const labels = {
      throttle: document.getElementById('throttleLabel'),
      speed: document.getElementById('speedLabel'),
      rpm: document.getElementById('rpmLabel'),
      volts: document.getElementById('voltsLabel'),
      amps: document.getElementById('ampsLabel')
    };

    function syncLabels() {
      labels.throttle.textContent = `${controls.throttle.value}%`;
      labels.speed.textContent = `${controls.speed.value} km/h`;
      labels.rpm.textContent = `${controls.rpm.value} rpm`;
      labels.volts.textContent = `${(Number(controls.volts.value) / 10).toFixed(1)} V`;
      labels.amps.textContent = `${(Number(controls.amps.value) / 10).toFixed(1)} A`;
    }

    function apply() {
      const params = new URLSearchParams({
        mode: controls.mode.value,
        throttle: controls.throttle.value,
        speed: controls.speed.value,
        rpm: controls.rpm.value,
        volts: controls.volts.value,
        amps: controls.amps.value,
        charging: controls.charging.checked ? '1' : '0',
        regen: controls.regen.checked ? '1' : '0'
      });

      const url = '/api/test?' + params.toString();
      fetch(url)
        .then(r => r.text())
        .then(() => {
          document.getElementById('modeStatus').textContent = controls.mode.value;
          document.getElementById('speedStatus').textContent = controls.speed.value;
          document.getElementById('rpmStatus').textContent = controls.rpm.value;
          const power = (Number(controls.volts.value) * Number(controls.amps.value) / 10).toFixed(0);
          document.getElementById('powerStatus').textContent = `${power}W`;
        })
        .catch(err => {
          alert('Gagal mengirim panel test: ' + err);
        });
    }

    function reset() {
      controls.mode.value = 'PARK';
      controls.throttle.value = 0;
      controls.speed.value = 0;
      controls.rpm.value = 0;
      controls.volts.value = 58;
      controls.amps.value = 0;
      controls.charging.checked = false;
      controls.regen.checked = false;
      syncLabels();
      apply();
    }

    Object.values(controls).forEach(control => {
      if (control.type === 'range') {
        control.addEventListener('input', syncLabels);
      }
    });

    document.getElementById('apply').addEventListener('click', apply);
    document.getElementById('reset').addEventListener('click', reset);

    document.querySelectorAll('[data-mode]').forEach(btn => {
      btn.addEventListener('click', () => {
        controls.mode.value = btn.dataset.mode;
        apply();
      });
    });

    syncLabels();
  </script>
</body>
</html>
)HTML";

// WiFi Event Handler
static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("[WiFi-Event] AP Started");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("[WiFi-Event] AP Stopped");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.printf("[WiFi-Event] Station connected - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
        info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
        info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.printf("[WiFi-Event] Station disconnected - MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
        info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
        info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.printf("[WiFi-Event] Station got IP: %s\n", 
        IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
      break;
    default:
      break;
  }
}

// WebSocket event handler
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WiFi] WebSocket client #%u connected from %s (total: %u)\n", 
                    (unsigned int)client->id(), client->remoteIP().toString().c_str(),
                    (unsigned int)server->count());
      // Disable Nagle's Algorithm: send packets immediately for real-time responsiveness
      // Without this, TCP buffers small packets (~200ms delay) causing dashboard jitter
      client->client()->setNoDelay(true);
      Serial.printf("[WiFi] TCP_NODELAY enabled for client #%u\n", (unsigned int)client->id());
      break;
      
    case WS_EVT_DISCONNECT:
      Serial.printf("[WiFi] WebSocket client #%u disconnected (remaining: %u)\n", 
                    (unsigned int)client->id(), 
                    server->count() > 0 ? (unsigned int)(server->count() - 1) : 0u);
      break;
      
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      
      // DEBUG: Log all incoming frames
      Serial.printf("[WiFi] WS_EVT_DATA received: len=%u, opcode=%d, final=%d, index=%u\n", 
                    (unsigned int)len, info->opcode, info->final, (unsigned int)info->index);
      
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        // Fix #1: Don't write past data buffer. Use length-bounded String constructor.
        String cmd((char*)data, len);
        Serial.printf("[WiFi] Command received: '%s'\n", cmd.c_str());
        
        if (cmd.startsWith("BLE:ON")) {
           Serial.println("[WiFi] Switch to BLE mode requested");
           transportMode.store(TRANSPORT_BLE, std::memory_order_release);
           // Mode saved by loop() auto-save
        }
        else if (cmd.startsWith("WIFI:ON")) {
           // No-op in WiFi mode, but acknowledge for UI consistency
           Serial.println("[WiFi] WIFI:ON received (already in WiFi mode)");
        }
        // Cmd: "TIMESYNC:YY,MM,DD,HH,MI,SS" -> Set BMS clock

      } else if (info->opcode == WS_BINARY) {
        Serial.printf("[WiFi] BINARY frame received (len=%u) - IGNORED\n", (unsigned int)len);
      }
      break;
    }
    
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWiFiMode() {
  if (wifiModeActive) {
    Serial.println("[WiFi] Already active!");
    return;
  }
  
  Serial.println("\n========================================");
  Serial.println("       Starting WiFi AP Mode");
  Serial.println("========================================\n");
  
  // === STEP 1: BLE already stopped by commTask (stopBLE + btStop) ===
  // No need to touch BLE here - commTask handles full teardown
  Serial.println("[WiFi] Step 1: BLE already stopped by commTask");
  
  // === STEP 2: Reset WiFi Completely ===
  Serial.println("[WiFi] Step 2: Resetting WiFi...");
  
  // Stop any existing AP
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(500); // Reduced from 1000ms - sufficient for WiFi reset
  Serial.println("       - WiFi reset complete");
  
  // === STEP 3: Register Event Handler ===
  if (!wifiEventRegistered) {
    WiFi.onEvent(onWiFiEvent);
    wifiEventRegistered = true;
  }
  
  // === STEP 4: Configure and Start AP ===
  Serial.println("[WiFi] Step 3: Configuring AP...");
  
  // Set mode to AP
  WiFi.mode(WIFI_AP);
  delay(300);  // Reduced from 500ms
  
  // Disable power saving
  WiFi.setSleep(false);
  
  // Configure static IP (IMPORTANT: Must be before softAP!)
  IPAddress localIP(192, 168, 8, 1);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  if (!WiFi.softAPConfig(localIP, gateway, subnet)) {
    Serial.println("[WiFi] ERROR: softAPConfig failed!");
    return;
  }
  Serial.println("       - AP IP Config: 192.168.8.1/24");
  
  // Start AP
  Serial.println("[WiFi] Step 4: Starting AP...");
  bool result = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_MAX_CLIENTS);
  if (!result) {
    Serial.println("[WiFi] ERROR: softAP failed!");
    return;
  }
  
  // Wait for AP ready
  delay(800);  // Reduced from 1500ms - usually ready faster
  
  // Verify IP
  IPAddress actualIP = WiFi.softAPIP();
  if (actualIP == IPAddress(0, 0, 0, 0)) {
    Serial.println("[WiFi] ERROR: No IP assigned!");
    return;
  }
  
  Serial.println("       - AP Started successfully");
  Serial.printf("       - AP IP: %s\n", actualIP.toString().c_str());
  
  // === STEP 5: Start WebSocket Server ===
  Serial.println("[WiFi] Step 5: Starting WebSocket server...");
  
  // Fix #6: Only register handlers once to prevent handler stacking
  if (!wsHandlersRegistered) {
    ws.onEvent(onWsEvent);
    wsServer.addHandler(&ws);
    
    // Setup OTA HTTP endpoints
    setupOtaHttp(&wsServer);

    // === HTTP API: Test panel page ===
    wsServer.on("/panel", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/html", testPanelHtml);
    });

    // === HTTP API: Test panel state injection ===
    wsServer.on("/api/test", HTTP_GET, [](AsyncWebServerRequest *request){
      String modeName = request->arg("mode");
      int speed = request->arg("speed").toInt();
      int rpm = request->arg("rpm").toInt();
      int throttle = request->arg("throttle").toInt();
      float volts = request->arg("volts").toFloat();
      float amps = request->arg("amps").toFloat();
      bool charging = request->arg("charging") == "1";
      bool regen = request->arg("regen") == "1";

      if (volts <= 0.0f) volts = 58.0f;
      if (amps == 0.0f && throttle > 0) {
        amps = throttle * 0.8f;
      }

      VehicleMode mode = MODE_PARK;
      if (modeName == "DRIVE") mode = MODE_DRIVE;
      else if (modeName == "SPORT") mode = MODE_SPORT;
      else if (modeName == "REVERSE") mode = MODE_REVERSE;
      else if (modeName == "BRAKE") mode = MODE_BRAKE;
      else if (modeName == "CHARGING") mode = MODE_CHARGING;
      else if (modeName == "STAND") mode = MODE_STAND;

      atomicSpeed.store(constrain(speed, 0, 200), std::memory_order_release);
      atomicRPM.store(constrain(rpm, 0, 5000), std::memory_order_release);
      atomicMode.store(mode, std::memory_order_release);
      atomicRegenActive.store(regen, std::memory_order_release);
      atomicVoltsRaw.store((int32_t)(volts * 10.0f), std::memory_order_release);

      if (charging) {
        atomicAmpereRaw.store((int32_t)((amps * 10.0f) > 0 ? (amps * 10.0f) : 20.0f), std::memory_order_release);
        atomicPowerRaw.store((int32_t)(volts * ((amps > 0.0f) ? amps : 2.0f)), std::memory_order_release);
      } else if (regen) {
        atomicAmpereRaw.store((int32_t)(-fabsf(amps) * 10.0f), std::memory_order_release);
        atomicPowerRaw.store((int32_t)(-fabsf(volts * amps)), std::memory_order_release);
      } else {
        float activeAmps = (amps != 0.0f) ? amps : (throttle * 0.8f);
        atomicAmpereRaw.store((int32_t)(activeAmps * 10.0f), std::memory_order_release);
        atomicPowerRaw.store((int32_t)(volts * activeAmps), std::memory_order_release);
      }

      canDataReady.store(true, std::memory_order_release);
      request->send(200, "text/plain", "OK");
    });
    
    // === HTTP API: Switch to BLE ===
    // More reliable than WebSocket for mode switching
    wsServer.on("/SWITCH_BLE", HTTP_GET, [](AsyncWebServerRequest *request){
      Serial.println("[HTTP] /SWITCH_BLE requested");
      request->send(200, "text/plain", "OK. Switching to BLE...");
      
      transportMode.store(TRANSPORT_BLE, std::memory_order_release);
      nvsWriteString("mode", "BLE");
    });
    
    // === HTTP API: Inject Enable/Disable ===
    // Simple HTTP toggle (alternative to WebSocket INJECT:1/0 and BLE)
    wsServer.on("/inject_on", HTTP_GET, [](AsyncWebServerRequest *request){
      isInjectorEnabled.store(true, std::memory_order_release);
      nvsWriteBool("inj", true);
      Serial.println("[HTTP] Injector: ON");
      request->send(200, "text/plain", "Injector ENABLED");
    });
    wsServer.on("/inject_off", HTTP_GET, [](AsyncWebServerRequest *request){
      isInjectorEnabled.store(false, std::memory_order_release);
      nvsWriteBool("inj", false);
      Serial.println("[HTTP] Injector: OFF");
      request->send(200, "text/plain", "Injector DISABLED");
    });
    
    // === HTTP API: Inject Status ===
    wsServer.on("/inject_status", HTTP_GET, [](AsyncWebServerRequest *request){
      bool injEnabled = isInjectorEnabled.load(std::memory_order_acquire);
      float amp = atomicAmpereRaw.load(std::memory_order_acquire) / 10.0f;
      
      bool localBms = false;
      bool localChrConn = false;
      bool localOriDet = false;
      
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        localBms = bmsChargingFlag;
        localChrConn = chargerConnected;
        localOriDet = oriChargerDetected;
        xSemaphoreGive(dataMutex);
      }
      
      VehicleMode mode = atomicMode.load(std::memory_order_acquire);
      bool modeOK = (mode == MODE_PARK || mode == MODE_CHARGING || mode == MODE_STAND);
      
      char buf[320];
      snprintf(buf, sizeof(buf),
        "{"
        "\"injectorEnabled\":%s,"
        "\"bmsCharging\":%s,"
        "\"chargerConnected\":%s,"
        "\"oriCharger\":%s,"
        "\"ampere\":%.1f,"
        "\"vehicleMode\":\"%s\","
        "\"modeAllowsInject\":%s,"
        "\"twaiMode\":\"%s\""
        "}",
        injEnabled ? "true" : "false",
        localBms ? "true" : "false",
        localChrConn ? "true" : "false",
        localOriDet ? "true" : "false",
        amp,
        getModeString(mode),
        modeOK ? "true" : "false",
        currentTwaiModeNormal ? "NORMAL" : "LISTEN_ONLY"
      );
      
      request->send(200, "application/json", buf);
    });
    
    wsHandlersRegistered = true;
    Serial.println("       - Handlers registered (first time)");
  } else {
    Serial.println("       - Handlers already registered, skipping");
  }
  
  wsServer.begin();
  
  wifiModeActive = true;
  lastWiFiCheck = millis();
  
  Serial.println("\n========================================");
  Serial.printf("  SSID:     %s\n", WIFI_AP_SSID);
  Serial.printf("  Password: %s\n", WIFI_AP_PASSWORD);
  Serial.printf("  AP IP:    %s\n", actualIP.toString().c_str());
  Serial.printf("  WebSocket: ws://%s:%d%s\n", actualIP.toString().c_str(), WS_PORT, WS_URL);
  Serial.println("\n  Status: READY - Connect your phone!");
  Serial.println("========================================\n");
  
  // Debug: Show connected stations periodically
  Serial.println("[WiFi] Waiting for stations to connect...");
  Serial.println("[WiFi] Tip: If phone gets stuck 'Obtaining IP', try:");
  Serial.println("      1. Forget network on phone and reconnect");
  Serial.println("      2. Restart ESP32");
  Serial.println("      3. Use static IP on phone (192.168.4.x)");
}

void stopWiFiMode() {
  if (!wifiModeActive) return;
  
  Serial.println("[WiFi] Stopping WiFi mode...");
  
  ws.closeAll();
  delay(100);
  
  wsServer.end();
  delay(100);
  
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  
  wifiModeActive = false;
  
  Serial.println("[WiFi] WiFi stopped");
}

void handleWiFiLoop() {
  if (!wifiModeActive) return;
  
  uint32_t now = millis();
  if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL_MS) {
    lastWiFiCheck = now;
    ws.cleanupClients();
  }
}

void wsBroadcast(const char* data) {
  // Fix #4: Use ws.count() instead of single bool to support multiple clients
  if (!wifiModeActive || ws.count() == 0) return;
  ws.textAll(data);
}

void wsBroadcast(uint8_t* data, size_t len) {
  if (!wifiModeActive || ws.count() == 0) return;
  ws.binaryAll(data, len);
}

bool isWiFiModeActive() {
  return wifiModeActive;
}

bool isWsClientConnected() {
  return ws.count() > 0;
}
