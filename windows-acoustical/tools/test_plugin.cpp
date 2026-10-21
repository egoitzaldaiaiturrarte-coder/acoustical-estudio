// test_plugin.cpp — el procesador VST REAL (PluginProcessor.cpp + el motor +
// el core C) ejecutado sin host, contra los módulos JUCE reales.
//
// Cubre lo que ningún otro gate veía (el plugin solo se compilaba en el job
// de Windows y nadie lo ejecutaba):
//   1. Elegir un modo del catálogo deja en cada parámetro EXACTAMENTE el
//      valor del catálogo (antes setValueNotifyingHost recibía valores
//      crudos como si fueran normalizados 0-1: SPL 103 → 110, intervalo
//      100 ms → 2000, apoyos a 20 kHz y −12 dB, x = −1…).
//   2. Un proyecto guardado con un modo + ajustes propios se recarga con
//      los ajustes propios (antes el modo se re-aplicaba al cargar y los
//      pisaba).
//   3. Con el generador en passthrough y la posición por defecto, el
//      plugin sale a ganancia unidad (antes +17.5 dB por la ley espacial
//      absoluta).
//   4. La línea de retardo no suelta audio viejo al volver a usarse
//      (antes, tras pasar por 0 ms, reproducía lo que quedó en el anillo).
//
// Compila con tools/build_plugintest.sh (Linux, sin CMake ni X11 en runtime).
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace {

int g_checks = 0;
int g_fail = 0;

void check(bool cond, const juce::String& msg) {
    ++g_checks;
    if (cond) {
        std::printf("ok:   %s\n", msg.toRawUTF8());
    } else {
        ++g_fail;
        std::printf("FAIL: %s\n", msg.toRawUTF8());
    }
}

bool nearF(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

float raw(AcousticalAudioProcessor& p, const char* id) {
    return p.parameters().getRawParameterValue(id)->load();
}

// Como lo haría el host o la ventana del plugin: valor del parámetro en sus
// unidades, convertido a la escala normalizada que espera el host.
void setRaw(AcousticalAudioProcessor& p, const char* id, float value) {
    auto* prm = p.parameters().getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

// Deja correr los AsyncUpdater (applyParameters corre en el hilo de mensajes).
void pump() {
    for (int i = 0; i < 4; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}

float rmsDb(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
    const double r = std::sqrt(s / std::max(1, n));
    return r > 0.0 ? static_cast<float>(20.0 * std::log10(r)) : -200.0f;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// Lo que hace el wrapper del host antes de procesar: fija tasa y bloque
// (getSampleRate() los lee de ahí) y llama a prepareToPlay.
void prepare(AcousticalAudioProcessor& p) {
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
}

// Procesa `blocks` bloques con un seno de 1 kHz (o silencio si amp = 0) y
// devuelve el RMS (dB) de la salida del canal L en el último bloque.
float runBlocks(AcousticalAudioProcessor& p, int blocks, float amp, double& phase,
                float* maxAbsOut = nullptr) {
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    float last = -200.0f;
    for (int b = 0; b < blocks; ++b) {
        for (int s = 0; s < kBlock; ++s) {
            const float v = amp * static_cast<float>(std::sin(phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / kRate;
            buf.setSample(0, s, v);
            buf.setSample(1, s, v);
        }
        p.processBlock(buf, midi);
        last = rmsDb(buf.getReadPointer(0), kBlock);
        if (maxAbsOut != nullptr)
            *maxAbsOut = std::max(*maxAbsOut, buf.getMagnitude(0, 0, kBlock));
    }
    return last;
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI init;

    // ---- 1. Modo del catálogo → valores exactos en los parámetros ----
    {
        AcousticalAudioProcessor p;
        const int modeIndex = 5;  // "PAs" (el combo tiene "Actual" en el 0)
        const auto& m = acoustical_mode_profiles()[modeIndex];
        setRaw(p, "mode", static_cast<float>(modeIndex + 1));
        pump();

        check(nearF(raw(p, "mode"), static_cast<float>(modeIndex + 1)), "modo PAs seleccionado");
        check(nearF(raw(p, "targetSpl"), m.targetSpl), "modo -> SPL objetivo = " + juce::String(m.targetSpl)
              + " (hay " + juce::String(raw(p, "targetSpl")) + ")");
        check(nearF(raw(p, "maxGain"), m.maxGainDb), "modo -> ganancia maxima");
        check(nearF(raw(p, "smoothing"), m.smoothingFactor, 0.006f),
              "modo -> suavizado = " + juce::String(m.smoothingFactor) + " (hay " + juce::String(raw(p, "smoothing")) + ")");
        check(nearF(raw(p, "correction"), m.correctionEnabled ? 1.0f : 0.0f), "modo -> correccion");
        check(nearF(raw(p, "noiseSub"), m.noiseSubtractionEnabled ? 1.0f : 0.0f), "modo -> resta de ruido");
        check(nearF(raw(p, "bandCount"), static_cast<float>(m.bandCountIndex)), "modo -> bandas (124)");
        check(nearF(raw(p, "fftSize"), static_cast<float>(m.fftSizeIndex)), "modo -> FFT (8192)");
        check(nearF(raw(p, "analysisInterval"), static_cast<float>(m.analysisIntervalIndex)), "modo -> intervalo (25 ms)");

        bool eqOk = true, supportOk = true;
        juce::String firstBad;
        for (int i = 0; i < 3; ++i) {
            const juce::String n(i + 1);
            const auto& e = m.eqs[i];
            auto want = [&](const juce::String& id, float v, float eps) {
                const float got = p.parameters().getRawParameterValue(id)->load();
                if (!nearF(got, v, eps)) {
                    if (firstBad.isEmpty())
                        firstBad = id + " = " + juce::String(got) + " (catalogo " + juce::String(v) + ")";
                    return false;
                }
                return true;
            };
            eqOk &= want("eq" + n + "Enabled", e.enabled ? 1.0f : 0.0f, 1e-3f);
            eqOk &= want("eq" + n + "Interval", static_cast<float>(e.intervalMs), 0.5f);
            eqOk &= want("eq" + n + "Gain", e.maxGainDb, 0.06f);
            eqOk &= want("eq" + n + "Mixer", e.mixerLevel, 0.006f);
            eqOk &= want("eq" + n + "Speed", e.speedMultiplier, 0.06f);
            eqOk &= want("eq" + n + "Extras", static_cast<float>(e.extraSweeps), 0.5f);
            eqOk &= want("eq" + n + "Start", static_cast<float>(e.startFrom), 1e-3f);
            for (int k = 0; k < 4; ++k) {
                const juce::String s(k + 1);
                supportOk &= want("eq" + n + "Sb" + s + "Freq", e.support[k].frequencyHz, 1.0f);
                supportOk &= want("eq" + n + "Sb" + s + "Gain", e.support[k].gainDb, 0.06f);
                supportOk &= want("eq" + n + "Sb" + s + "Q", e.support[k].q, 0.06f);
            }
        }
        check(eqOk, "modo -> los 3 ecuas con los valores del catalogo" + (eqOk ? juce::String() : "  " + firstBad));
        check(supportOk, "modo -> las 12 bandas de apoyo (Hz, dB, Q) del catalogo"
              + (supportOk ? juce::String() : "  " + firstBad));
        check(nearF(raw(p, "spatialX"), m.spatialX) && nearF(raw(p, "spatialY"), m.spatialY)
                  && nearF(raw(p, "spatialZ"), m.spatialZ) && nearF(raw(p, "spatialSize"), m.spatialSize),
              "modo -> posicion espacial (x " + juce::String(raw(p, "spatialX")) + ", y "
                  + juce::String(raw(p, "spatialY")) + ", z " + juce::String(raw(p, "spatialZ")) + ")");
    }

    // ---- 2. Proyecto con modo + ajuste propio: se recarga TAL CUAL ----
    {
        juce::MemoryBlock state;
        {
            AcousticalAudioProcessor p;
            setRaw(p, "mode", 3.0f);    // "Estudio - Escenario 1"
            pump();
            setRaw(p, "targetSpl", 84.0f);   // el usuario lo afina dentro del modo
            setRaw(p, "eq2Interval", 1200.0f);
            pump();
            check(nearF(raw(p, "targetSpl"), 84.0f), "ajuste dentro del modo aplicado (SPL 84)");
            p.getStateInformation(state);
        }
        AcousticalAudioProcessor q;
        q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        pump();
        check(nearF(raw(q, "mode"), 3.0f), "recarga: el modo sigue seleccionado");
        check(nearF(raw(q, "targetSpl"), 84.0f),
              "recarga: el SPL afinado (84) sobrevive (hay " + juce::String(raw(q, "targetSpl")) + ")");
        check(nearF(raw(q, "eq2Interval"), 1200.0f, 0.5f),
              "recarga: el intervalo afinado del ecu 2 (1200) sobrevive (hay " + juce::String(raw(q, "eq2Interval")) + ")");
    }

    // ---- 3. Passthrough a ganancia unidad con la posición por defecto ----
    {
        AcousticalAudioProcessor p;
        setRaw(p, "genSource", 1.0f);    // "Apagado": la entrada pasa tal cual
        setRaw(p, "correction", 0.0f);   // sin bucle de corrección: solo la cadena
        for (int i = 1; i <= 3; ++i) setRaw(p, ("eq" + juce::String(i) + "Enabled").toRawUTF8(), 0.0f);
        pump();
        prepare(p);
        double phase = 0.0;
        const float inDb = 20.0f * std::log10(0.25f / std::sqrt(2.0f));
        const float outDb = runBlocks(p, 40, 0.25f, phase);
        check(std::fabs(outDb - inDb) < 1.0f,
              "posicion por defecto: sale a ganancia unidad (entrada " + juce::String(inDb, 1)
                  + " dB, salida " + juce::String(outDb, 1) + " dB)");
        p.releaseResources();
    }

    // ---- 4. La línea de retardo no reproduce audio viejo ----
    {
        AcousticalAudioProcessor p;
        setRaw(p, "genSource", 1.0f);
        setRaw(p, "correction", 0.0f);
        for (int i = 1; i <= 3; ++i) setRaw(p, ("eq" + juce::String(i) + "Enabled").toRawUTF8(), 0.0f);
        setRaw(p, "delayMs", 40.0f);
        pump();
        prepare(p);
        double phase = 0.0;
        runBlocks(p, 20, 0.5f, phase);          // seno con 40 ms de retardo
        setRaw(p, "delayMs", 0.0f);
        runBlocks(p, 2, 0.5f, phase);           // seno sin retardo (unos bloques)
        runBlocks(p, 40, 0.0f, phase);          // silencio durante casi medio segundo
        setRaw(p, "delayMs", 20.0f);            // el usuario vuelve a poner retardo
        float peak = 0.0f;
        runBlocks(p, 4, 0.0f, phase, &peak);    // la entrada sigue en silencio
        check(peak < 1e-4f, "tras medio segundo de silencio, volver a poner retardo no suelta audio viejo (pico "
                                + juce::String(peak, 4) + ")");
        p.releaseResources();
    }

    // ---- 5. Menú "Generador": cada opción hace lo que dice ----
    // El valor crudo de un AudioParameterChoice en el APVTS es el ÍNDICE
    // (0..5), no 0..1: la v1.9 lo multiplicaba por 5 y todas las opciones
    // salvo Silencio acababan en "Seno".
    {
        // Salida (entrada en silencio) de una opción del menú: RMS dB del
        // último bloque, cruces por cero por bloque y pico.
        struct GenOut { float rmsDb; int crossings; float peak; };
        auto genOut = [](int menuIndex) {
            AcousticalAudioProcessor p;
            setRaw(p, "genSource", static_cast<float>(menuIndex));
            setRaw(p, "correction", 0.0f);
            for (int i = 1; i <= 3; ++i) setRaw(p, ("eq" + juce::String(i) + "Enabled").toRawUTF8(), 0.0f);
            pump();
            prepare(p);
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            GenOut r{-200.0f, 0, 0.0f};
            for (int b = 0; b < 30; ++b) {
                buf.clear();
                p.processBlock(buf, midi);
                r.peak = std::max(r.peak, buf.getMagnitude(0, 0, kBlock));
            }
            const float* x = buf.getReadPointer(0);
            r.rmsDb = rmsDb(x, kBlock);
            for (int s = 1; s < kBlock; ++s)
                if ((x[s - 1] < 0.0f) != (x[s] < 0.0f)) ++r.crossings;
            p.releaseResources();
            return r;
        };
        const GenOut silencio = genOut(0), apagado = genOut(1), rosa = genOut(2),
                     blanco = genOut(3), barrido = genOut(4), seno = genOut(5);
        check(silencio.rmsDb < -150.0f, "Silencio: la salida es silencio");
        check(apagado.rmsDb < -150.0f, "Apagado: con la entrada en silencio no suena nada (no hay generador)"
              " (hay " + juce::String(apagado.rmsDb, 1) + " dB)");
        // 1 kHz a 48 kHz = ~21 cruces por bloque de 512
        check(seno.crossings >= 19 && seno.crossings <= 23 && std::fabs(seno.rmsDb - (-12.0f - 3.01f)) < 1.0f,
              "Seno: 1 kHz a -12 dB (" + juce::String(seno.crossings) + " cruces, "
                  + juce::String(seno.rmsDb, 1) + " dB RMS)");
        check(rosa.rmsDb > -40.0f && std::abs(rosa.crossings - seno.crossings) > 5,
              "Ruido rosa: suena y no es el seno (" + juce::String(rosa.crossings) + " cruces)");
        check(blanco.rmsDb > -40.0f && blanco.crossings > 3 * seno.crossings,
              "Ruido blanco: suena y es mucho mas denso que el seno (" + juce::String(blanco.crossings) + " cruces)");
        check(barrido.rmsDb > -40.0f && barrido.crossings < seno.crossings,
              "Barrido: suena y arranca grave, por debajo de 1 kHz (" + juce::String(barrido.crossings) + " cruces)");
        float worst = 0.0f;
        for (const auto* g : {&silencio, &apagado, &rosa, &blanco, &barrido, &seno}) worst = std::max(worst, g->peak);
        check(worst < 1.0f, "ninguna opcion satura con el nivel por defecto (pico max " + juce::String(worst, 2) + ")");
    }

    // ---- 6. Migración de proyectos v1.8 (menú de 5 opciones) ----
    // Estado guardado SIN el marcador genSourceMenu: el índice k del menú
    // viejo (Apagado, Rosa, Blanco, Barrido, Seno) es la opción k+1 del nuevo.
    {
        AcousticalAudioProcessor src;
        juce::MemoryBlock fresh;
        src.getStateInformation(fresh);
        auto xml = juce::AudioProcessor::getXmlFromBinary(fresh.getData(), static_cast<int>(fresh.getSize()));
        const char* oldNames[5] = {"Apagado", "Ruido rosa", "Ruido blanco", "Barrido", "Seno"};
        for (int k = 0; k < 5; ++k) {
            juce::ValueTree tree = juce::ValueTree::fromXml(*xml);
            tree.removeProperty("genSourceMenu", nullptr);
            for (auto child : tree)
                if (child.getProperty("id").toString() == "genSource")
                    child.setProperty("value", k, nullptr);
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), legacy);
            AcousticalAudioProcessor dst;
            dst.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
            pump();
            const juce::String now = dst.parameters().getParameter("genSource")->getCurrentValueAsText();
            check(now == juce::String::fromUTF8(oldNames[k]),
                  juce::String("migracion v1.8: \"") + juce::String::fromUTF8(oldNames[k])
                      + juce::String::fromUTF8("\" sigue siendo \"") + juce::String::fromUTF8(oldNames[k])
                      + juce::String::fromUTF8("\" (hay \"") + now + "\")");
        }
        // Un estado nuevo (con marcador) no se toca
        {
            AcousticalAudioProcessor a;
            setRaw(a, "genSource", 4.0f);  // Barrido
            pump();
            juce::MemoryBlock st;
            a.getStateInformation(st);
            AcousticalAudioProcessor b;
            b.setStateInformation(st.getData(), static_cast<int>(st.getSize()));
            pump();
            check(nearF(raw(b, "genSource"), 4.0f), "estado v1.9+ (con marcador): el menu se recarga sin migrar");
        }
    }

    // ---- Informativo: el generador por defecto ----
    {
        AcousticalAudioProcessor p;
        prepare(p);
        double phase = 0.0;
        const float outDb = runBlocks(p, 10, 0.25f, phase);
        std::printf("info: instancia nueva (generador por defecto: \"%s\"): salida %.1f dB con un seno de entrada a -15 dB RMS\n",
                    p.parameters().getParameter("genSource")->getCurrentValueAsText().toRawUTF8(), outDb);
        p.releaseResources();
    }

    std::printf("\nPLUGIN: %d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
