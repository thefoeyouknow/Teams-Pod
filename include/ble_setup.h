#ifndef BLE_SETUP_H
#define BLE_SETUP_H

#include <Arduino.h>

// BLE Service and Characteristic UUIDs
#define BLE_SERVICE_UUID        0x00FF
#define BLE_CHAR_SSID           "0001ff01-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_PASSWORD       "0001ff02-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_CLIENT_ID      "0001ff03-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_TENANT_ID      "0001ff04-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_SAVE           "0001ff05-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_LIGHT_TYPE     "0001ff06-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_LIGHT_IP       "0001ff07-0000-1000-8000-00805f9b34fb"

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
extern String g_light_type;
extern String g_light_ip;

#endif
