// Syntax-check del camino _WIN32 de AsioBridgeClient.h (las funciones Win32
// se validan contra firmas reales vía windows.h fake).
#define _WIN32
#include "AsioBridgeClient.h"

int main() {
    AsioBridgeClient c;
    if (!c.connect()) return 1;
    (void)c.isConnected();
    (void)c.sampleRate();
    (void)c.bufferSize();
    // Getters de vivos/sesión del nuevo BridgeShared.h (front 4B):
    (void)c.driverAlive();
    (void)c.lastDriverActivityMs();
    (void)c.peerSessionMismatch();
    (void)c.appDeadFlag();
    float l[16], r[16];
    const int n = c.pullCapture(l, r, 16, 48000.0);
    c.pushPlayback(l, r, n > 0 ? n : 16, 44100.0);
    c.disconnect();
    return 0;
}
