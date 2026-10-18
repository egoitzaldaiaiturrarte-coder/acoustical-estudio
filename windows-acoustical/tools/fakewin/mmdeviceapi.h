// mmdeviceapi.h fake: el driver ASIO solo INCLUYE este header (la audio
// real viaja por el contrato IASIO + memoria compartida, no por el core
// audio de Windows). Se deja vacio para que el include resuelva; si en el
// futuro el driver usara IMMDevice/IAudioClient, añadir aqui las firmas
// REALES de mmdeviceapi.h (que el check debe validar, no inventar).
#pragma once
#include <windows.h>
