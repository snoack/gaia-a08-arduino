
#ifndef _GAIA_NETWORK
#define _GAIA_NETWORK

void wifiInit();

// Force a Wi-Fi reconnect when the link has been down too long for the core's
// own auto-reconnect to recover it (see wifi.cpp). Cheap to call every loop();
// no-ops unless a reconnect is due.
void wifiReconnect();

#ifdef CONF_USE_WEB_SERVER
void webServerInit();
void webServerHandle();
#endif

#ifdef CONF_MQTT
void mqttInit();
#endif

#ifdef OTA_PASSWORD
void otaInit();
void otaHandle();
#endif

#endif // _GAIA_NETWORK
