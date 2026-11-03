// test_extlink_syntax.cpp — syntax-check del camino SOLO-WINDOWS de
// ExtLink.h (el receptor del enlace móvil: socket winsock + thread) contra
// las firmas reales del SDK (stub de fakewin), mismo criterio que los
// syntax-checks del driver ASIO y del filtro de crash. El código del
// receptor se compila y CORRE en el gate dsp (test_extlink.cpp, loopback
// POSIX); aquí solo se valida que el camino _WIN32 no tenga typos de API
// antes de quemar el job de Windows.
#include "ExtLink.h"

int main() {
    acoustical::ExtAnalysis a;
    const char* pkt = "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\","
                      "\"bands\":[1.0]}";
    return acoustical::parseExtPacket(pkt, (int)sizeof(pkt), a) ? 0 : 1;
}
