#include "wifi_handler.h"

// =====================================================
// GLOBAL OBJECTS & STATE VARIABLES
// =====================================================
AsyncWebServer wsServer(WS_PORT);
AsyncWebSocket ws(WS_URL);

bool wifiModeActive = false;
static bool wsHandlersRegistered = false;
static uint32_t lastWiFiCheck = 0;

// External variable fallback if not declared in main .ino
extern std::atomic<uint8_t> atomicMode;
extern std::atomic<int32_t> atomicSpeed;
extern std::atomic<int32_t> atomicRPM;
extern std::atomic<int32_t> atomicVoltsRaw;
extern std::atomic<int32_t> atomicAmpereRaw;
extern std::atomic<int32_t> atomicPowerRaw;
extern std::atomic<bool> isInjectorEnabled;
extern std::atomic<bool> atomicRegenActive;
extern bool bmsChargingFlag;

// Standard NVS weak functions (akan ditimpa jika ada fungsi NVS di .ino)
__attribute__((weak)) void nvsWriteString(const char* key, const char* value) {}
__attribute__((weak)) void nvsWriteBool(const char* key, bool value) {}

// =====================================================
// HTML TEST PANEL (Stored in PROGMEM)
// =====================================================
const char testPanelHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>VOTOL EV Dashboard</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 20px; }
        .container { max-width: 480px; margin: 0 auto; }
        h1 { color: #38bdf8; margin-bottom: 20px; font-size: 1.5rem; text-transform: uppercase; letter-spacing: 1px; }
        .card { background: #1e293b; border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #334155; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .label { font-size: 0.8rem; color: #94a3b8; text-transform: uppercase; }
        .val { font-size: 1.4rem; font-weight: bold; color: #38bdf8; margin-top: 4px; }
        .mode-badge { display: inline-block; padding: 4px 12px; border-radius: 20px; font-weight: bold; background: #0284c7; color: #fff; font-size: 0.9rem; }
        button { width: 100%; background: #0284c7; border: none; padding: 12px; color: #fff; font-size: 1rem; font-weight: bold; border-radius: 8px; cursor: pointer; transition: 0.2s; }
        button:active { transform: scale(0.98); background: #0369a1; }
        .status-dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; background: #ef4444; margin-right: 6px; }
        .status-dot.online { background: #22c55e; }
    </style>
</head>
<body>
    <div class="container">
        <h1><span id="dot" class="status-dot"></span>VOTOL Dashboard</h1>
        
        <div class="card">
            <span class="label">Vehicle Mode</span><br>
            <div style="margin-top: 8px;"><span id="mode" class="mode-badge">PARK</span></div>
        </div>

        <div class="card grid">
            <div>
                <div class="label">Speed</div>
                <div class="val"><span id="speed">0</span> <small style="font-size: 0.8rem;">km/h</small></div>
            </div>
            <div>
                <div class="label">RPM</div>
                <div class="val" id="rpm">0</div>
            </div>
            <div>
                <div class="label">Voltage</div>
                <div class="val"><span id="volts">0.0</span> <small style="font-size: 0.8rem;">V</small></div>
            </div>
            <div>
                <div class="label">Current</div>
                <div class="val"><span id="amps">0.0</span> <small style="font-size: 0.8rem;">A</small></div>
            </div>
        </div>

        <div class="card">
            <div class="label" style="margin-bottom: 8px;">Power Output</div>
            <div class="val"><span id="power" style="font-size: 2rem; color: #4ade80;">0</span> <small style="font-size: 1rem;">W</small></div>
        </div>

        <div class="card">
            <button onclick="toggleInjector()">Toggle Injector</button>
            <p style="margin-top: 10px; font-size: 0.85rem; color: #94a3b8;">Injector Status: <strong id="injStatus" style="color: #fff;">-</strong></p>
        </div>
    </div>

    <script>
        let ws;
        function connectWS() {
            ws = new WebSocket('ws://' + window.location.host + '/ws');
            ws.onopen = () => { document.getElementById('dot').classList.add('online'); };
            ws.onclose = () => { 
                document.getElementById('dot').classList.remove('online'); 
                setTimeout(connectWS, 2000); 
            };
        }
        
        function updateData() {
            fetch('/api/status')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('mode').innerText = d.mode;
                    document.getElementById('speed').innerText = d.speed;
                    document.getElementById('rpm').innerText = d.rpm;
                    document.getElementById('volts').innerText = d.volts;
                    document.getElementById('amps').innerText = d.amps;
                    document.getElementById('power').innerText = d.power;
                    document.getElementById('injStatus').innerText = d.injector ? "ENABLED" : "DISABLED";
                }).catch(() => {});
        }

        function toggleInjector() {
            fetch('/api/injector', { method: 'POST' })
                .then(r => r.text())
                .then(txt => { document.getElementById('injStatus').innerText = txt; });
        }

        connectWS();
        setInterval(updateData, 300);
    </script>
</body>
</html>
)rawliteral";

// =====================================================
// WEBSOCKET EVENTS
// =====================================================
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                data[len] = 0; // Null terminate string
                char buffer[256];
                strncpy(buffer, (char*)data, sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';

                char *token = strtok(buffer, ",");
                if (token != NULL && strcmp(token, "TEST") == 0) {

                    // Mode parsing
                    token = strtok(NULL, ",");
                    if (token != NULL) {
                        if (strcmp(token, "PARK") == 0)        atomicMode.store(MODE_PARK);
                        else if (strcmp(token, "STAND") == 0)   atomicMode.store(MODE_STAND);
                        else if (strcmp(token, "CHARGING") == 0)atomicMode.store(MODE_CHARGING);
                        else if (strcmp(token, "DRIVE") == 0)   atomicMode.store(MODE_DRIVE);
                        else if (strcmp(token, "SPORT") == 0)   atomicMode.store(MODE_SPORT);
                        else if (strcmp(token, "REVERSE") == 0) atomicMode.store(MODE_REVERSE);
                        else if (strcmp(token, "BRAKE") == 0)   atomicMode.store(MODE_BRAKE);
                    }

                    // Speed
                    token = strtok(NULL, ",");
                    if (token != NULL) atomicSpeed.store(atoi(token));

                    // RPM
                    token = strtok(NULL, ",");
                    if (token != NULL) atomicRPM.store(atoi(token));

                    // Volts
                    token = strtok(NULL, ",");
                    if (token != NULL) {
                        float v = atof(token);
                        atomicVoltsRaw.store((int32_t)(v * 10.0f));
                    }

                    // Amps & Power
                    token = strtok(NULL, ",");
                    if (token != NULL) {
                        float a = atof(token);
                        atomicAmpereRaw.store((int32_t)(a * 10.0f));

                        float v = (float)atomicVoltsRaw.load() / 10.0f;
                        atomicPowerRaw.store((int32_t)(v * a));
                    }

                    // Throttle (Skip)
                    token = strtok(NULL, ",");

                    // Charging
                    token = strtok(NULL, ",");
                    if (token != NULL) bmsChargingFlag = (atoi(token) == 1);

                    // Brake
                    token = strtok(NULL, ",");
                    if (token != NULL) atomicRegenActive.store(atoi(token) == 1);
                }
            }
            break;
        }

        default:
            break;
    }
}

// =====================================================
// SETUP ROUTES AND HANDLERS
// =====================================================
static void setupWiFiHandlers() {
    if (wsHandlersRegistered) return;

    // Route Root Web Server (/)
    wsServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (const uint8_t*)testPanelHtml, strlen_P(testPanelHtml));
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        request->send(response);
    });

    // API Telemetry Status
    wsServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json;
        json.reserve(256);
        
        VehicleMode currentMode = static_cast<VehicleMode>(atomicMode.load());

        json += "{";
        json += "\"mode\":\"" + String(getModeString(currentMode)) + "\",";
        json += "\"speed\":" + String(atomicSpeed.load()) + ",";
        json += "\"rpm\":" + String(atomicRPM.load()) + ",";
        json += "\"volts\":" + String((float)atomicVoltsRaw.load() / 10.0f, 1) + ",";
        json += "\"amps\":" + String((float)atomicAmpereRaw.load() / 10.0f, 1) + ",";
        json += "\"power\":" + String(atomicPowerRaw.load()) + ",";
        json += "\"injector\":" + String(isInjectorEnabled.load() ? "true" : "false");
        json += "}";
        
        request->send(200, "application/json", json);
    });

    // API Toggle Injector
    wsServer.on("/api/injector", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool newState = !isInjectorEnabled.load();
        isInjectorEnabled.store(newState);
        nvsWriteBool("inj_en", newState);
        request->send(200, "text/plain", newState ? "ENABLED" : "DISABLED");
    });

    // Attach WebSocket
    ws.onEvent(onWsEvent);
    wsServer.addHandler(&ws);

    wsHandlersRegistered = true;
}

// =====================================================
// PUBLIC FUNCTIONS IMPLEMENTATION
// =====================================================

void initWiFiMode() {
    if (wifiModeActive) return;

    WiFi.persistent(false);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    // Dynamic configuration IP Static SoftAP (192.168.4.1)
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);

    bool apCreated = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_MAX_CLIENTS);

    if (!apCreated) {
        Serial.println("[WiFi] Gagal membuat Access Point!");
        return;
    }

    setupWiFiHandlers();
    wsServer.begin();

    wifiModeActive = true;
    transportMode.store(TRANSPORT_WIFI);
    nvsWriteString("comm_mode", "WIFI");

    Serial.printf("[WiFi] Access Point Aktif!\n");
    Serial.printf("[WiFi] SSID     : %s\n", WIFI_AP_SSID);
    Serial.printf("[WiFi] Dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());
}

void stopWiFiMode() {
    if (!wifiModeActive) return;

    ws.closeAll();
    wsServer.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    wifiModeActive = false;
    Serial.println("[WiFi] Access Point Dinonaktifkan.");
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
    if (wifiModeActive && ws.count() > 0) {
        ws.textAll(data);
    }
}

void wsBroadcast(uint8_t* data, size_t len) {
    if (wifiModeActive && ws.count() > 0) {
        ws.binaryAll(data, len);
    }
}

bool isWiFiModeActive() {
    return wifiModeActive;
}

bool isWsClientConnected() {
    return (wifiModeActive && ws.count() > 0);
}