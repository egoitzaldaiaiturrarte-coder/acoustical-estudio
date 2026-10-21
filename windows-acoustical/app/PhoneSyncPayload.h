// PhoneSyncPayload.h — lectura de la respuesta de sync del móvil (solo
// juce_core: la usan la consola y el test de protocolo, tools/test_lan_link).
//
// El servidor de la app Android contesta a pull/push con un SOBRE:
//
//   {"type":"sync","ok":true,"payload":{ ...lo último que empujó el PC...,
//                                        "config":{ ...config en vivo del
//                                                   móvil: escalares,
//                                                   dynamicEqs, modeId,
//                                                   spatial... },
//                                        "sendToPc": true|false }}
//
// Hasta la v1.10.0 la consola leía "config"/"spatial"/"modeId" en el nivel
// de ARRIBA del sobre (donde no están): "Recibir del móvil" no aplicaba
// nada y el modo/espacio del móvil nunca llegaban al PC. Además el móvil
// pone "spatial" y "modeId" DENTRO de "config", no al lado.
#pragma once

#include <juce_core/juce_core.h>

namespace phonesync {

// El objeto que lleva "config"/"sendToPc": el "payload" del sobre o, si lo
// que llega ya es el payload desnudo, el propio objeto.
inline juce::DynamicObject* payloadOf(const juce::var& reply) {
    auto* o = reply.getDynamicObject();
    if (o == nullptr) return nullptr;
    if (auto* p = o->getProperty("payload").getDynamicObject()) return p;
    return o;
}

// La config en vivo del móvil (payload.config), o nullptr.
inline juce::DynamicObject* configOf(const juce::var& reply) {
    if (auto* p = payloadOf(reply)) return p->getProperty("config").getDynamicObject();
    return nullptr;
}

// ¿El móvil pulsó "Enviar mis ajustes al PC"? (payload.sendToPc)
inline bool stagedOf(const juce::var& reply) {
    if (auto* p = payloadOf(reply)) return static_cast<bool>(p->getProperty("sendToPc"));
    return false;
}

// Id del modo de trabajo de una config ("modeId"; el PC lo escribía como
// "activeModeId" en su settings.json, se aceptan los dos). Vacío = sin modo.
inline juce::String modeIdOf(const juce::DynamicObject* cfg) {
    if (cfg == nullptr) return {};
    const auto v = cfg->getProperty("modeId");
    if (!v.isVoid()) return v.toString();
    return cfg->getProperty("activeModeId").toString();
}

struct Spatial {
    bool present = false;
    float x = 0.0f, y = 0.0f, z = 0.5f, size = 0.5f;
};

// Posición espacial de una config ("spatial": {x, y, z, size}). Solo es
// válida si vienen las cuatro coordenadas y son números finitos.
inline Spatial spatialOf(const juce::DynamicObject* cfg) {
    Spatial s;
    if (cfg == nullptr) return s;
    auto* sp = cfg->getProperty("spatial").getDynamicObject();
    if (sp == nullptr) return s;
    float v[4];
    const char* keys[4] = {"x", "y", "z", "size"};
    for (int i = 0; i < 4; ++i) {
        const auto p = sp->getProperty(keys[i]);
        if (p.isVoid() || !(p.isDouble() || p.isInt() || p.isInt64())) return s;
        v[i] = static_cast<float>(static_cast<double>(p));
        if (!std::isfinite(v[i])) return s;
    }
    s.present = true;
    s.x = v[0]; s.y = v[1]; s.z = v[2]; s.size = v[3];
    return s;
}

}  // namespace phonesync
