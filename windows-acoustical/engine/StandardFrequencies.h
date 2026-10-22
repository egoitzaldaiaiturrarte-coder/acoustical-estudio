// StandardFrequencies.h — port de StandardFrequencies (AudioModels.kt).
#pragma once

#include "AcousticalParameters.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace acoustical {

struct StandardFrequencies {
    static const std::vector<float>& thirdOctave() {
        static const std::vector<float> v = {
            20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160,
            200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
            2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000
        };
        return v;
    }

    static const std::vector<float>& tenBand() {
        static const std::vector<float> v = {
            31.25f, 62.5f, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
        };
        return v;
    }

    static const std::vector<float>& eightBand() {
        static const std::vector<float> v = { 60, 170, 310, 600, 1000, 3000, 6000, 12000 };
        return v;
    }

    static const std::vector<float>& sixteenBand() {
        static const std::vector<float> v = {
            40, 63, 100, 160, 250, 400, 630, 1000,
            1600, 2500, 4000, 6300, 8000, 10000, 14000, 16000
        };
        return v;
    }

    // 124 bandas logarítmicas de 20 Hz a 20 kHz (razón constante 1000^(1/123))
    static const std::vector<float>& ultra124() {
        static const std::vector<float> v = [] {
            std::vector<float> out(124);
            for (int i = 0; i < 124; ++i)
                out[i] = static_cast<float>(20.0 * std::pow(1000.0, i / 123.0));
            return out;
        }();
        return v;
    }

    // Q de los filtros del EQ para un juego de bandas: cada filtro tan ancho
    // como la distancia a su vecina (Q de N-ésimo de octava,
    // sqrt(2^b)/(2^b - 1) con b = separación típica en octavas). 10 bandas
    // (una octava) → 1.41, el valor fijo de siempre; 31 (1/3 oct) → 4.32;
    // 124 (~1/12 oct) → ~17.8.
    //
    // v1.10.2: hasta ahora el Q era 1.41 para cualquier número de bandas. Con
    // 124 bandas (los seis modos del catálogo desde la v1.10.0) cada
    // corrección se solapaba con ~12 vecinas, el bucle corregía ~12 veces de
    // más y se disparaba (salida de cientos o miles de veces el fondo de
    // escala; ver tools/test_plugin.cpp, sección 7).
    static float eqQ(const std::vector<float>& freqs) {
        std::vector<double> octaves;
        for (size_t i = 1; i < freqs.size(); ++i)
            if (freqs[i - 1] > 0.0f && freqs[i] > freqs[i - 1])
                octaves.push_back(std::log2(static_cast<double>(freqs[i]) / freqs[i - 1]));
        if (octaves.empty()) return 1.41f;
        // mediana: los juegos de 8 y 16 bandas no son regulares
        std::sort(octaves.begin(), octaves.end());
        const double r = std::pow(2.0, octaves[octaves.size() / 2]);
        const double q = std::sqrt(r) / (r - 1.0);
        return static_cast<float>(std::clamp(q, 0.7, 20.0));
    }

    static std::vector<float> forCount(BandCount count) {
        switch (count) {
            case BandCount::B8:   return eightBand();
            case BandCount::B10:  return tenBand();
            case BandCount::B16:  return sixteenBand();
            case BandCount::B31:  return thirdOctave();
            case BandCount::B124: return ultra124();
        }
        return tenBand();
    }
};

} // namespace acoustical
