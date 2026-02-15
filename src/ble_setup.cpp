#include "ble_setup.h"
#include <NimBLEDevice.h>
#include <Preferences.h>

// Global credential storage
String g_ssid = "";
String g_password = "";
String g_client_id = "";
String g_tenant_id = "";

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
            Serial.println("  → Credentials saved. Rebooting in 2s...");
            delay(2000);
            Serial.println("  → Rebooting now!");
            ESP.restart();
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
 */
void initializeNVS() {
    Serial.println("[NVS] Initializing...");
    nvs_prefs.begin(NVS_NAMESPACE, false);
    Serial.println("[NVS] ✓ Ready");
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
    nvs_prefs.end();
    
    Serial.printf("  SSID: %s\n", g_ssid.c_str());
    Serial.printf("  CLIENT_ID: %s\n", g_client_id.c_str());
    Serial.printf("  TENANT_ID: %s\n", g_tenant_id.c_str());
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
    NimBLEDevice::init("Teams-Puck");
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
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pSave->setCallbacks(pCharCallback);
    
    // Start service
    pService->start();
    
    Serial.println("[BLE] ✓ Service created with 5 characteristics");
}

/**
 * Start BLE advertising
 */
void startBLEAdvertising() {
    if (!NimBLEDevice::getAdvertising()) {
        Serial.println("[BLE] Creating advertising...");
        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        pAdv->addServiceUUID(NimBLEUUID((uint16_t)BLE_SERVICE_UUID));
        pAdv->setScanResponse(true);
        pAdv->setMinPreferred(0x06);
        pAdv->setMaxPreferred(0x12);
    }
    
    Serial.println("[BLE] Starting advertising as 'Teams-Puck'...");
    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] ✓ Advertising active");
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
