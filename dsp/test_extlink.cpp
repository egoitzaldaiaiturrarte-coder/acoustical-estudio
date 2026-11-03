// test_extlink.cpp — v1.11.1: el enlace móvil del VST (ExtLink).
//
// Dos bloques:
//  1) Parser del datagrama (parseExtPacket): el protocolo es un JSON plano
//     con esquema fijo; se valida que lo válido se acepte y lo hostil
//     (truncado, code no numérico, array desbordado, app/type incorrectos,
//     basura al final) se rechace sin tocar la memoria.
//  2) End-to-end en loopback: un ExtLink real arrancado en un puerto de
//     test y un "móvil falso" (un socket UDP en el mismo proceso) envía
//     datagramas: el receptor debe (a) ignorar el code equivocado,
//     (b) disparar el callback con el code correcto y (c) reflejarlo en el
//     status que lee el editor.
//
// ExtLink.h es C++17 puro (sin JUCE): este test corre en el gate dsp
// (g++ Linux) con el MISMO código que compila el build de Windows
// (allí el camino _WIN32, syntax-checked aparte contra el stub winsock2).
#include "ExtLink.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using test_socklen_t = socklen_t;
#else
#include <winsock2.h>
using test_socklen_t = int;
#endif

namespace {

int gFails = 0;

void expect(bool cond, const char* what) {
    if (cond) {
        std::printf("  ok  %s\n", what);
    } else {
        std::printf("  FAIL %s\n", what);
        ++gFails;
    }
}

// === 1) Parser =============================================================

void testParser() {
    std::printf("[parseExtPacket]\n");
    using acoustical::ExtAnalysis;
    using acoustical::parseExtPacket;

    {
        const char* good =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"341207\","
            "\"bands\":[ -6.25, -1.1, 0.5, 12.0 ],\"spl\":78.4,\"peakSpl\":80.0,"
            "\"rt60\":310,\"bandsCount\":4,\"ts\":1699999999}";
        ExtAnalysis a;
        const bool ok = parseExtPacket(good, (int)std::strlen(good), a);
        expect(ok, "paquete válido se acepta");
        expect(a.code == 341207u, "code parseado");
        expect(a.bands.size() == 4, "4 bandas");
        if (a.bands.size() == 4) {
            expect(a.bands[0] < -6.0f && a.bands[0] > -6.6f, "banda 0 = -6.25");
            expect(a.bands[3] > 11.0f && a.bands[3] < 13.0f, "banda 3 = 12.0");
        }
        expect(a.spl > 78.0f && a.spl < 79.0f, "spl = 78.4");
        expect(a.rt60Ms > 309.0f && a.rt60Ms < 311.0f, "rt60 = 310");
        expect(a.ts == 1699999999LL, "ts parseado");
    }
    {
        const char* badApp =
            "{\"app\":\"otro\",\"type\":\"ext\",\"code\":\"123456\",\"bands\":[1.0]}";
        ExtAnalysis a;
        expect(!parseExtPacket(badApp, (int)std::strlen(badApp), a),
               "app incorrecto se rechaza");
    }
    {
        const char* badType =
            "{\"app\":\"acoustical\",\"type\":\"beacon\",\"code\":\"123456\",\"bands\":[1.0]}";
        ExtAnalysis a;
        expect(!parseExtPacket(badType, (int)std::strlen(badType), a),
               "type incorrecto se rechaza");
    }
    {
        const char* codeZero =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"0\",\"bands\":[1.0]}";
        ExtAnalysis a;
        expect(!parseExtPacket(codeZero, (int)std::strlen(codeZero), a),
               "code 0 se rechaza (0 = sin código)");
    }
    {
        const char* codeAlpha =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"12ab56\",\"bands\":[1.0]}";
        ExtAnalysis a;
        expect(!parseExtPacket(codeAlpha, (int)std::strlen(codeAlpha), a),
               "code no numérico se rechaza");
    }
    {
        const char* noBands =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\"}";
        ExtAnalysis a;
        expect(!parseExtPacket(noBands, (int)std::strlen(noBands), a),
               "sin bandas se rechaza");
    }
    {
        const char* emptyBands =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\",\"bands\":[]}";
        ExtAnalysis a;
        expect(!parseExtPacket(emptyBands, (int)std::strlen(emptyBands), a),
               "bandas vacías se rechaza");
    }
    {
        // Truncado en mitad del array: debe rechazar (no crash).
        const char* trunc =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\","
            "\"bands\":[1.0,2.0,3.0,";
        ExtAnalysis a;
        expect(!parseExtPacket(trunc, (int)std::strlen(trunc), a),
               "JSON truncado se rechaza");
    }
    {
        // Basura después del } : se rechaza.
        const char* junk =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\",\"bands\":[1.0]}x";
        ExtAnalysis a;
        expect(!parseExtPacket(junk, (int)std::strlen(junk), a),
               "basura tras el } se rechaza");
    }
    {
        // Array desbordado (> 2048): se rechaza.
        std::string big = "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\",\"bands\":[";
        for (int i = 0; i < 2049; ++i)
            big += (i ? ",1.0" : "1.0");
        big += "]}";
        ExtAnalysis a;
        expect(!parseExtPacket(big.data(), (int)big.size(), a),
               "array de 2049 bandas se rechaza (límite 2048)");
    }
    {
        const char* ok2048 =
            "{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"123456\",\"bands\":[";
        std::string v = ok2048;
        for (int i = 0; i < 2048; ++i)
            v += (i ? ",1.0" : "1.0");
        v += "]}";
        ExtAnalysis a;
        expect(parseExtPacket(v.data(), (int)v.size(), a) && a.bands.size() == 2048,
               "array de 2048 bandas se acepta");
    }
    {
        // Valida también el límite de tamaño de datagrama (64 KB).
        std::string big = std::string(64 * 1024 + 10, 'x');
        ExtAnalysis a;
        expect(!parseExtPacket(big.data(), (int)big.size(), a),
               "datagrama > 64 KB se rechaza");
    }
}

// === 2) End-to-end en loopback =============================================

void testLoopback() {
    std::printf("[ExtLink loopback]\n");
    const int port = 37373;  // fuera de los puertos del sistema (41041-41047)
    const acoustical::ExtAnalysis expected{
        424242u, std::vector<float>{-3.0f, -1.5f, 2.5f, 0.0f, 8.0f},
        72.5f, 75.0f, 250.0f, 1699999999LL};

    acoustical::ExtLink link;
    std::atomic<int> rxCount{0};
    std::atomic<float> lastSpl{-1.0f};
    std::atomic<int> lastBands{-1};
    std::atomic<std::int64_t> lastTs{0};
    link.onAnalysis = [&](const acoustical::ExtAnalysis& a) {
        ++rxCount;
        lastSpl.store(a.spl);
        lastBands.store((int)a.bands.size());
        lastTs.store(a.ts);
    };
    link.setCode(424242);
    link.start(port);
    // El arranque del hilo es asíncrono (bind+thread): dejarlo asentarse.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Móvil falso.
    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in to {};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons((uint16_t)port);
    auto sendTo = [&](const char* msg) {
        sendto(fd, msg, (int)std::strlen(msg), 0, (sockaddr*)&to, sizeof to);
    };

    // (a) code equivocado: no llega al callback.
    sendTo("{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"111111\","
           "\"bands\":[1.0,2.0],\"spl\":60.0}");
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    expect(rxCount.load() == 0, "code equivocado no dispara el callback");

    // (b) code correcto: el receptor dispara el callback con el análisis.
    sendTo("{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"424242\","
           "\"bands\":[ -3.0, -1.5, 2.5, 0.0, 8.0 ],\"spl\":72.5,\"peakSpl\":75.0,"
           "\"rt60\":250,\"ts\":1699999999}");
    for (int i = 0; i < 40 && rxCount.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(rxCount.load() == 1, "code correcto dispara el callback");
    expect(lastBands.load() == 5, "5 bandas recibidas");
    expect(lastSpl.load() > 72.0f && lastSpl.load() < 73.0f, "spl recibido = 72.5");
    expect(lastTs.load() == 1699999999LL, "ts recibido");

    // (c) status (lo que lee el editor): refleja la última recepción.
    const auto st = link.status();
    expect(st.lastRxMs > 0, "status.lastRxMs > 0");
    expect(st.lastCode == 424242u, "status.lastCode = 424242");
    expect(st.bandsCount == 5, "status.bandsCount = 5");

    // (d) un segundo paquete actualiza (la frecuencia de envío real del
    // móvil es 4 paquetes/s; aquí solo se comprueba el segundo).
    sendTo("{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"424242\","
           "\"bands\":[ 0.5, 0.5, 0.5, 0.5, 0.5 ],\"spl\":80.0}");
    for (int i = 0; i < 40 && rxCount.load() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    expect(rxCount.load() == 2, "segundo paquete recibido");
    expect(lastSpl.load() > 79.5f && lastSpl.load() < 80.5f, "spl actualizado = 80.0");

    // (e) code -> 0: el hilo se para y no recibe más.
    link.setCode(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    sendTo("{\"app\":\"acoustical\",\"type\":\"ext\",\"code\":\"424242\","
           "\"bands\":[9.0]}");
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    expect(rxCount.load() == 2, "con code 0 (desactivado) no se recibe nada");

    link.stop();
    (void)fd;  // el SO lo libera al terminar
}

}  // namespace

int main() {
    testParser();
    testLoopback();
    if (gFails == 0) {
        std::printf("test_extlink: OK\n");
        return 0;
    }
    std::printf("test_extlink: %d fallos\n", gFails);
    return 1;
}
