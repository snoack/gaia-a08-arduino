
#ifndef _GAIA_NETWORK
#define _GAIA_NETWORK

void wifiInit();

// Service provisioning and connection recovery.
void wifiHandle();

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
