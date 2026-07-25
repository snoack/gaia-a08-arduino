
#ifndef _GAIA_NETWORK
#define _GAIA_NETWORK

void wifiInit();
void webServerInit();

#ifdef CONF_MQTT
void mqttInit();
#endif

#ifdef OTA_PASSWORD
void otaInit();
void otaHandle();
#endif

#endif // _GAIA_NETWORK
