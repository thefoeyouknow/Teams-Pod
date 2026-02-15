#ifndef BLE_SETUP_H
#define BLE_SETUP_H

#include <Arduino.h>

// BLE Service and Characteristic UUIDs
#define BLE_SERVICE_UUID        0x00FF
#define BLE_CHAR_SSID           "0001FF01-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_PASSWORD       "0001FF02-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_CLIENT_ID      "0001FF03-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_TENANT_ID      "0001FF04-0000-1000-8000-00805F9B34FB"
#define BLE_CHAR_SAVE           "0001FF05-0000-1000-8000-00805F9B34FB"

// NVS Storage Keys
#define NVS_NAMESPACE           "puck_creds"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "password"
#define NVS_KEY_CLIENT_ID       "client_id"
#define NVS_KEY_TENANT_ID       "tenant_id"

// Function declarations
void initializeBLE();
void startBLEAdvertising();
void stopBLEAdvertising();
bool hasStoredCredentials();
void loadCredentialsFromNVS();
void saveCredentialsToNVS();
void clearStoredCredentials();

// Credential storage
extern String g_ssid;
extern String g_password;
extern String g_client_id;
extern String g_tenant_id;

#endif
