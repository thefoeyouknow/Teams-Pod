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
#define BLE_CHAR_LIGHT_KEY      "0001ff08-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_LIGHT_AUX      "0001ff09-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_CLIENT_SECRET  "0001ff0a-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_PLATFORM       "0001ff0b-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_TIMEZONE       "0001ff0c-0000-1000-8000-00805f9b34fb"
#define BLE_CHAR_OFFICE_HOURS   "0001ff0d-0000-1000-8000-00805f9b34fb"

// NVS Storage Keys
#define NVS_NAMESPACE           "puck_creds"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASSWORD        "password"
#define NVS_KEY_CLIENT_ID       "client_id"
#define NVS_KEY_TENANT_ID       "tenant_id"

// Function declarations
void initializeBLE();
void deinitBLE();
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
extern String g_light_key;
extern String g_light_aux;
extern String g_client_secret;
extern String g_platform;
extern String g_timezone;
extern String g_office_hours;

#endif
