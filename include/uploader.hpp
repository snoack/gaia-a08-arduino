#ifndef _UPLOADER_HPP
#define _UPLOADER_HPP

#if defined(CONF_AQICN) || defined(CONF_SENSOR_COMMUNITY) || defined(CONF_OPENSENSEMAP)
#define ANY_UPLOADER_ENABLED
extern void uploaderInit();
#endif

#endif
