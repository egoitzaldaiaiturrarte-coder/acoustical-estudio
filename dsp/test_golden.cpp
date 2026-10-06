// test_golden.cpp — Test del vector dorado de agregación en bandas.
//
// dsp/golden_bands.json fija, de forma documentada y reproducible, la
// salida de acoustical_aggregate_bands() sobre una entrada concreta (32 bins
// a 500 Hz, rampa de magnitudes -8-4i dB con los bins 28..31 en/bajo el piso
// -120 dB, y 6 bandas a 1/6 de octava elegidas para cubrir cada rama del
// algoritmo: banda vacía -> piso, medias de 1/2/4 bins, y banda mixta donde
// solo cuentan los bins por encima del piso — el bin 28 cae EXACTAMENTE en
// el piso y la comparación es estricta '>').
//
// El test reconstruye la entrada desde el propio JSON (parseo a mano, sin
// dependencias), vuelve a llamar a la función del núcleo C y compara contra
// los out_band_levels del archivo con una tolerancia de 0.01 dB (los
// valores van a 3 decimales). Si alguien cambia el algoritmo de agregación
// sin regenerar el JSON, este test falla — el archivo es el contrato con la
// app Android, que consume el mismo vector.
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                             \
        if (!(cond)) {                                                         \
            ++g_failures;                                                       \
            std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);       \
        }                                                                      \
    } while (0)

// === Parseo plano del JSON, sin dependencias ===
//
// El archivo es "plano" a propósito (solo un objeto con campos escalar y
// arrays de números, sin objetos anidados): se puede leer con find/strtof
// desde cualquier plataforma, incluida la app Android, sin librería JSON.

// Extrae el array de floats del campo "key". Devuelve false si el campo no
// existe, no es un array, o algún token no es un número.
static bool extractFloatArray(const std::string& doc, const char* key,
                              std::vector<float>& out) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t kp = doc.find(k);
    if (kp == std::string::npos) return false;
    const size_t open = doc.find('[', kp);
    if (open == std::string::npos || open < kp) return false;
    const size_t close = doc.find(']', open);
    if (close == std::string::npos || close < open) return false;

    std::string inner = doc.substr(open + 1, close - open - 1);
    size_t pos = 0;
    while (pos <= inner.size()) {
        // Token hasta la siguiente coma (o el final).
        size_t comma = inner.find(',', pos);
        const size_t end = (comma == std::string::npos) ? inner.size() : comma;
        // Recortar espacios en los bordes.
        size_t b = pos, e = end;
        while (b < e && (inner[b] == ' ' || inner[b] == '\t' ||
                         inner[b] == '\n' || inner[b] == '\r')) ++b;
        while (e > b && (inner[e - 1] == ' ' || inner[e - 1] == '\t' ||
                         inner[e - 1] == '\n' || inner[e - 1] == '\r')) --e;
        const std::string tok = inner.substr(b, e - b);
        if (!tok.empty()) {
            char* parsed = nullptr;
            const float v = strtof(tok.c_str(), &parsed);
            // El token debe consumirse completo (descarta basura tipo "12x").
            if (parsed == nullptr || *parsed != '\0') return false;
            if (!std::isfinite(v)) return false;
            out.push_back(v);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}

// Extrae un escalar float del campo "key" (p. ej. noise_floor_db).
static bool extractFloat(const std::string& doc, const char* key, float& out) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t kp = doc.find(k);
    if (kp == std::string::npos) return false;
    const size_t colon = doc.find(':', kp);
    if (colon == std::string::npos) return false;
    const size_t comma = doc.find_first_of(",\n", colon + 1);
    const std::string tok = doc.substr(colon + 1,
                                       comma - colon - 1);
    char* parsed = nullptr;
    const float v = strtof(tok.c_str(), &parsed);
    if (parsed == nullptr || *parsed != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

static std::string readWholeFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// El JSON vive junto al test (mismo directorio): se localiza desde __FILE__,
// de modo que el binario corre desde cualquier directorio de trabajo.
static std::string goldenPath() {
    const std::string src = __FILE__;
    const size_t slash = src.find_last_of("/\\");
    // Sin directorio (compilado con un path relativo) => CWD actual.
    const std::string dir = (slash == std::string::npos)
        ? std::string()
        : src.substr(0, slash + 1);
    return dir + "golden_bands.json";
}

static bool isStrictlyAscending(const std::vector<float>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (!(v[i] > v[i - 1])) return false;
    return true;
}

int main() {
    const std::string path = goldenPath();
    const std::string doc = readWholeFile(path.c_str());
    CHECK(!doc.empty(), "golden_bands.json se pudo leer");
    if (doc.empty()) {
        std::printf("GOLDEN: %d checks, %d failures\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }

    // === Estructura del vector ===
    float noiseFloor = 0.0f;
    std::vector<float> bandFreqs, binFreqs, mags, golden;
    CHECK(extractFloat(doc, "noise_floor_db", noiseFloor), "campo noise_floor_db");
    CHECK(extractFloatArray(doc, "band_frequencies", bandFreqs), "campo band_frequencies");
    CHECK(extractFloatArray(doc, "bin_frequencies", binFreqs), "campo bin_frequencies");
    CHECK(extractFloatArray(doc, "magnitudes_db", mags), "campo magnitudes_db");
    CHECK(extractFloatArray(doc, "out_band_levels", golden), "campo out_band_levels");
    if (g_failures) {
        std::printf("GOLDEN: %d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }

    // Coherencia interna del vector (precondiciones de la función + lo que
    // el vector debe fijar: 1/6 de octava <=> band_count <= 40).
    CHECK(bandFreqs.size() == golden.size(), "out_band_levels coincide con la nº de bandas");
    CHECK(!bandFreqs.empty() && bandFreqs.size() <= 40,
          "band_count <= 40 (ratio 2^(1/6), 1/6 de octava)");
    CHECK(binFreqs.size() == mags.size(), "bins y magnitudes, misma longitud");
    CHECK(isStrictlyAscending(bandFreqs), "bandas en frecuencias ascendentes");
    CHECK(isStrictlyAscending(binFreqs), "bins en frecuencias ascendentes");
    CHECK(std::isfinite(noiseFloor), "piso de ruido finito");

    // === Propiedades del vector que debe mantener (es su punto de prueba) ===
    // 1. Banda vacía -> nivel = piso (la primera banda no cubre ningún bin).
    CHECK(std::fabs(golden[0] - noiseFloor) < 1e-6, "banda 0 vacía da el piso");
    // 2. Bin 28 EXACTO en el piso: debe excluirse (comparación estricta '>').
    //    Si alguien la cambiara a >=, la banda mixta (última) variaría.
    const float bin28 = mags.size() > 28 ? mags[28] : 0.0f;
    CHECK(std::fabs(bin28 - noiseFloor) < 1e-6, "el bin 28 cae exactamente en el piso");
    // 3. Bins por debajo del piso: excluidos.
    CHECK(mags.size() > 31 && mags[31] < noiseFloor, "los bins finales están bajo el piso");

    // === La salida de la función REAL coincide con el vector dorado ===
    std::vector<float> computed(golden.size());
    acoustical_aggregate_bands(bandFreqs.data(), static_cast<int>(bandFreqs.size()),
                               mags.data(), static_cast<int>(mags.size()),
                               binFreqs.data(), static_cast<int>(binFreqs.size()),
                               noiseFloor, computed.data());
    double worst = 0.0;
    for (size_t i = 0; i < golden.size(); ++i)
        worst = std::max(worst, std::fabs((double)computed[i] - (double)golden[i]));
    // 0.01 dB: cubre el redondeo a 3 decimales del archivo con margen.
    CHECK(worst <= 0.01, "out_band_levels == acoustical_aggregate_bands (0.01 dB)");
    for (size_t i = 0; i < golden.size(); ++i)
        std::printf("  banda %zu: dorado=%8.3f  calculado=%8.3f\n",
                    i, (double)golden[i], (double)computed[i]);

    std::printf("GOLDEN: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("GOLDEN: ALL PASSED\n");
    return g_failures ? 1 : 0;
}
