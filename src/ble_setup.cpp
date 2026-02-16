#include "ble_setup.h"
#include "light_control.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

// Global credential storage
String g_ssid = "";
String g_password = "";
String g_client_id = "";
String g_tenant_id = "";
String g_light_type = "0";
String g_light_ip = "";
String g_light_key = "";
String g_light_aux = "1";
String g_client_secret = "";
String g_platform = "0";

// BLE objects
static NimBLEServer* pServer = nullptr;
static NimBLEService* pService = nullptr;
static Preferences nvs_prefs;

// Forward declarations
class CharacteristicCallback;

/**
 * Callback class for BLE characteristic writes
 */
class CharacteristicCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        std::string uuid = pCharacteristic->getUUID().toString();
        std::string value = pCharacteristic->getValue();
        
        Serial.printf("[BLE] Write to %s: \"%s\"\n", uuid.c_str(), value.c_str());
        
        // Route to appropriate handler
        if (uuid == BLE_CHAR_SSID) {
            g_ssid = String(value.c_str());
            Serial.printf("  -> SSID set to: %s\n", g_ssid.c_str());
        } 
        else if (uuid == BLE_CHAR_PASSWORD) {
            g_password = String(value.c_str());
            Serial.printf("  -> PASSWORD set (length: %d)\n", g_password.length());
        } 
        else if (uuid == BLE_CHAR_CLIENT_ID) {
            g_client_id = String(value.c_str());
            Serial.printf("  -> CLIENT_ID set to: %s\n", g_client_id.c_str());
        } 
        else if (uuid == BLE_CHAR_TENANT_ID) {
            g_tenant_id = String(value.c_str());
            Serial.printf("  -> TENANT_ID set to: %s\n", g_tenant_id.c_str());
        } 
        else if (uuid == BLE_CHAR_SAVE) {
            Serial.println("  → SAVE triggered! Storing credentials to NVS...");
            saveCredentialsToNVS();
            // Sync platform to pod_settings
            {
                Preferences sp;
                sp.begin("pod_settings", false);
                sp.putInt("platform", g_platform.toInt());
                sp.end();
            }
            // Also sync light config to pod_light namespace
            LightConfig lc;
            lc.type = (LightType)g_light_type.toInt();
            lc.ip   = g_light_ip;
            lc.brightness = 128;
            lc.key  = g_light_key;
            lc.aux  = g_light_aux;
            saveLightConfig(lc);
            Serial.println("  → Credentials saved. Rebooting in 2s...");
            delay(2000);
            Serial.println("  → Rebooting now!");
            Serial.flush();
            ESP.restart();
        }
        else if (uuid == BLE_CHAR_LIGHT_TYPE) {
            g_light_type = String(value.c_str());
            Serial.printf("  -> LIGHT_TYPE set to: %s\n", g_light_type.c_str());
        }
        else if (uuid == BLE_CHAR_LIGHT_IP) {
            g_light_ip = String(value.c_str());
            Serial.printf("  -> LIGHT_IP set to: %s\n", g_light_ip.c_str());
        }
        else if (uuid == BLE_CHAR_LIGHT_KEY) {
            g_light_key = String(value.c_str());
            Serial.printf("  -> LIGHT_KEY set (length: %d)\n", g_light_key.length());
        }
        else if (uuid == BLE_CHAR_LIGHT_AUX) {
            g_light_aux = String(value.c_str());
            Serial.printf("  -> LIGHT_AUX set to: %s\n", g_light_aux.c_str());
        }
        else if (uuid == BLE_CHAR_CLIENT_SECRET) {
            g_client_secret = String(value.c_str());
            Serial.printf("  -> CLIENT_SECRET set (length: %d)\n", g_client_secret.length());
        }
        else if (uuid == BLE_CHAR_PLATFORM) {
            g_platform = String(value.c_str());
            Serial.printf("  -> PLATFORM set to: %s\n", g_platform.c_str());
        }
    }
    
    void onRead(NimBLECharacteristic* pCharacteristic) override {
        std::string uuid = pCharacteristic->getUUID().toString();
        Serial.printf("[BLE] Read from %s\n", uuid.c_str());
        
        if (uuid == BLE_CHAR_CLIENT_ID) {
            pCharacteristic->setValue(std::string(g_client_id.c_str()));
        } 
        else if (uuid == BLE_CHAR_TENANT_ID) {
            pCharacteristic->setValue(std::string(g_tenant_id.c_str()));
        }
        else if (uuid == BLE_CHAR_LIGHT_TYPE) {
            pCharacteristic->setValue(std::string(g_light_type.c_str()));
        }
        else if (uuid == BLE_CHAR_LIGHT_IP) {
            pCharacteristic->setValue(std::string(g_light_ip.c_str()));
        }
        else if (uuid == BLE_CHAR_LIGHT_KEY) {
            pCharacteristic->setValue(std::string(g_light_key.c_str()));
        }
        else if (uuid == BLE_CHAR_LIGHT_AUX) {
            pCharacteristic->setValue(std::string(g_light_aux.c_str()));
        }
        else if (uuid == BLE_CHAR_CLIENT_SECRET) {
            pCharacteristic->setValue(std::string(g_client_secret.c_str()));
        }
        else if (uuid == BLE_CHAR_PLATFORM) {
            pCharacteristic->setValue(std::string(g_platform.c_str()));
        }
    }
};

/**
 * Server callback for connection events
 */
class ServerCallback : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        Serial.printf("[BLE] Client connected (addr: %s)\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    
    void onDisconnect(NimBLEServer* pServer) override {
        Serial.println("[BLE] Client disconnected. Resuming advertising...");
        NimBLEDevice::startAdvertising();
    }
};

/**
 * Initialize the NVS (Non-Volatile Storage)
 * Note: we no longer hold the namespace open permanently.
 * Each read/write function opens and closes its own session.
 */
void initializeNVS() {
    Serial.println("[NVS] Initializing...");
    // Quick open/close to verify NVS partition is accessible
    if (nvs_prefs.begin(NVS_NAMESPACE, true)) {
        nvs_prefs.end();
        Serial.println("[NVS] ✓ Ready");
    } else {
        Serial.println("[NVS] ✗ Failed to open namespace");
    }
}

/**
 * Load credentials from NVS
 */
void loadCredentialsFromNVS() {
    Serial.println("[NVS] Loading credentials...");
    nvs_prefs.begin(NVS_NAMESPACE, true);   // open read-only
    g_ssid = nvs_prefs.getString(NVS_KEY_SSID, "");
    g_password = nvs_prefs.getString(NVS_KEY_PASSWORD, "");
    g_client_id = nvs_prefs.getString(NVS_KEY_CLIENT_ID, "");
    g_tenant_id = nvs_prefs.getString(NVS_KEY_TENANT_ID, "");
    g_light_type = nvs_prefs.getString("light_type", "0");
    g_light_ip = nvs_prefs.getString("light_ip", "");
    g_client_secret = nvs_prefs.getString("client_sec", "");
    g_platform = nvs_prefs.getString("platform_s", "0");
    nvs_prefs.end();
    
    Serial.printf("  SSID: %s\n", g_ssid.c_str());
    Serial.printf("  CLIENT_ID: %s\n", g_client_id.c_str());
    Serial.printf("  TENANT_ID: %s\n", g_tenant_id.c_str());
    Serial.printf("  PLATFORM: %s\n", g_platform.c_str());
    Serial.printf("  LIGHT: type=%s ip=%s\n", g_light_type.c_str(), g_light_ip.c_str());
}

/**
 * Save credentials to NVS
 */
void saveCredentialsToNVS() {
    Serial.println("[NVS] Saving credentials...");
    nvs_prefs.begin(NVS_NAMESPACE, false);  // open read-write
    nvs_prefs.putString(NVS_KEY_SSID, g_ssid);
    nvs_prefs.putString(NVS_KEY_PASSWORD, g_password);
    nvs_prefs.putString(NVS_KEY_CLIENT_ID, g_client_id);
    nvs_prefs.putString(NVS_KEY_TENANT_ID, g_tenant_id);
    nvs_prefs.putString("light_type", g_light_type);
    nvs_prefs.putString("light_ip", g_light_ip);
    nvs_prefs.putString("client_sec", g_client_secret);
    nvs_prefs.putString("platform_s", g_platform);
    nvs_prefs.end();
    Serial.println("[NVS] ✓ Credentials saved");
}

/**
 * Check if valid credentials exist in NVS
 */
bool hasStoredCredentials() {
    nvs_prefs.begin(NVS_NAMESPACE, true); // Read-only
    bool has_creds = !nvs_prefs.getString(NVS_KEY_SSID, "").isEmpty();
    nvs_prefs.end();
    return has_creds;
}

/**
 * Initialize and configure BLE
 */
void initializeBLE() {
    Serial.println("\n[BLE] Initializing NimBLE...");
    
    // Initialize NVS first
    initializeNVS();
    
    // Create BLE device
    NimBLEDevice::init("Status-Pod");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Create BLE server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallback());
    
    // Create service
    pService = pServer->createService(NimBLEUUID((uint16_t)BLE_SERVICE_UUID));
    
    // Create characteristics with callbacks
    CharacteristicCallback* pCharCallback = new CharacteristicCallback();
    
    // SSID (Write only)
    NimBLECharacteristic* pSSID = pService->createCharacteristic(
        BLE_CHAR_SSID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pSSID->setCallbacks(pCharCallback);
    
    // PASSWORD (Write only)
    NimBLECharacteristic* pPassword = pService->createCharacteristic(
        BLE_CHAR_PASSWORD,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pPassword->setCallbacks(pCharCallback);
    
    // CLIENT_ID (Write + Read)
    NimBLECharacteristic* pClientID = pService->createCharacteristic(
        BLE_CHAR_CLIENT_ID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pClientID->setCallbacks(pCharCallback);
    
    // TENANT_ID (Write + Read)
    NimBLECharacteristic* pTenantID = pService->createCharacteristic(
        BLE_CHAR_TENANT_ID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pTenantID->setCallbacks(pCharCallback);
    
    // SAVE (Write only - triggers reboot)
    NimBLECharacteristic* pSave = pService->createCharacteristic(
        BLE_CHAR_SAVE,
        NIMBLE_PROPERTY::WRITE
    );
    pSave->setCallbacks(pCharCallback);
    
    // LIGHT_TYPE (Write + Read): "0"=None, "1"=WLED, "2"=Smart Bulb
    NimBLECharacteristic* pLightType = pService->createCharacteristic(
        BLE_CHAR_LIGHT_TYPE,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pLightType->setCallbacks(pCharCallback);
    
    // LIGHT_IP (Write + Read): IP address of the light device
    NimBLECharacteristic* pLightIP = pService->createCharacteristic(
        BLE_CHAR_LIGHT_IP,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pLightIP->setCallbacks(pCharCallback);
    
    // LIGHT_KEY (Write + Read): API key (Hue bridge)
    NimBLECharacteristic* pLightKey = pService->createCharacteristic(
        BLE_CHAR_LIGHT_KEY,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pLightKey->setCallbacks(pCharCallback);

    // LIGHT_AUX (Write + Read): Aux field (Hue light ID)
    NimBLECharacteristic* pLightAux = pService->createCharacteristic(
        BLE_CHAR_LIGHT_AUX,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pLightAux->setCallbacks(pCharCallback);

    // CLIENT_SECRET (Write + Read): Zoom client secret
    NimBLECharacteristic* pClientSecret = pService->createCharacteristic(
        BLE_CHAR_CLIENT_SECRET,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pClientSecret->setCallbacks(pCharCallback);

    // PLATFORM (Write + Read): "0"=Teams, "1"=Zoom
    NimBLECharacteristic* pPlatform = pService->createCharacteristic(
        BLE_CHAR_PLATFORM,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    pPlatform->setCallbacks(pCharCallback);

    // Start service
    pService->start();
    
    Serial.println("[BLE] ✓ Service created with 12 characteristics");
}

/**
 * Start BLE advertising
 */
void startBLEAdvertising() {
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();

    // Stop first if already running (safe no-op if not)
    if (pAdv->isAdvertising()) {
        pAdv->stop();
        delay(100);
    }

    // Configure on every call — idempotent, avoids stale state
    pAdv->addServiceUUID(NimBLEUUID((uint16_t)BLE_SERVICE_UUID));
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    pAdv->setMaxPreferred(0x12);

    Serial.println("[BLE] Starting advertising as 'Status-Pod'...");
    bool ok = pAdv->start();
    Serial.printf("[BLE] %s Advertising %s\n", ok ? "✓" : "✗",
                  ok ? "active" : "FAILED");
}

/**
 * Stop BLE advertising
 */
void stopBLEAdvertising() {
    Serial.println("[BLE] Stopping advertising...");
    NimBLEDevice::stopAdvertising();
    Serial.println("[BLE] ✓ Advertising stopped");
}

/**
 * Clear all stored credentials from NVS (factory reset)
 */
void clearStoredCredentials() {
    nvs_prefs.begin(NVS_NAMESPACE, false);
    nvs_prefs.clear();
    nvs_prefs.end();
    g_ssid = "";
    g_password = "";
    g_client_id = "";
    g_tenant_id = "";
    Serial.println("[NVS] All credentials cleared");
}
