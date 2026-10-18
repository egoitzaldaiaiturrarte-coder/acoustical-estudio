// timeapi.h fake: la API de resolucion de temporizador (mmsystem.dll) que
// usa el worker del driver (timeBeginPeriod/timeEndPeriod balanceados) y
// timeGetTime (el sello de los callbacks ASIO). Solo syntax-check del camino
// #ifdef _WIN32 en Linux.
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
// MMRESULT = unsigned long en el SDK real.
unsigned long timeBeginPeriod(unsigned long dwPeriod);
unsigned long timeEndPeriod(unsigned long dwPeriod);
unsigned long timeGetTime(void);
#ifdef __cplusplus
}
#endif
