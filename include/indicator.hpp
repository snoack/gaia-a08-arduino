#ifndef _INDICATOR_HPP
#define _INDICATOR_HPP

extern void rgbLedInit();
extern void rgbLedLoop();
extern void ledInit();

extern void indicatorReportAqi(float pm25, float pm10);

#endif
