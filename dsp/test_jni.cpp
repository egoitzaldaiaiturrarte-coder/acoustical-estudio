// test_jni.cpp — host-side test of the JNI bridge (no NDK required).
//
// Compiles acoustical_jni.cpp against the minimal jni.h stub and checks that
// the JNI marshalling produces results identical to calling the shared C core
// directly. This proves the bridge passes the right data with the right sizes.
#include "jni.h"          // stub (host)
#include "acoustical_dsp.h"

#include <cmath>
#include <cstdio>
#include <vector>

extern "C" {
jlong Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCreate(JNIEnv*, jobject, jint);
void  Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftFree(JNIEnv*, jobject, jlong);
void  Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCompute(JNIEnv*, jobject, jlong, jfloatArray, jint, jfloatArray);
void  Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftBinFrequencies(JNIEnv*, jobject, jlong, jint, jfloatArray);
void  Java_com_rork_acoustical_domain_audio_NativeDsp_nativeAggregateBands(JNIEnv*, jobject, jfloatArray, jfloatArray, jfloatArray, jfloat, jfloatArray);
}

static int g_checks = 0;
static int g_fail = 0;
#define CHECK(cond, msg) do { g_checks++; if (!(cond)) { g_fail++; std::printf("FAIL: %s\n", msg); } } while (0)

static bool nearlyEqual(const std::vector<float>& a, const std::vector<float>& b, float eps = 1e-4f) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::fabs(a[i] - b[i]) > eps) return false;
    return true;
}

int main() {
    JNIEnv env;
    const int N = 2048;
    const int sampleRate = 48000;

    // Tones placed exactly on 10-band centers: 250 (idx 3, dominant), 1000 (idx 5), 4000 (idx 7).
    std::vector<float> input(N, 0.0f);
    for (int i = 0; i < N; ++i) {
        float t = i / (float)sampleRate;
        input[i] = 0.4f * std::sin(2.0f * (float)M_PI * 250.0f * t)
                 + 0.3f * std::sin(2.0f * (float)M_PI * 1000.0f * t)
                 + 0.1f * std::sin(2.0f * (float)M_PI * 4000.0f * t);
    }

    // --- FFT via JNI ---
    jlong h = Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCreate(&env, nullptr, N);
    CHECK(h != 0, "fftCreate returned non-zero handle");

    std::vector<float> magViaJni(N / 2), magDirect(N / 2);
    std::vector<float> binViaJni(N / 2), binDirect(N / 2);

    _jfloatArray inArr;  inArr.data = input.data();  inArr.length = N;
    _jfloatArray magJni; magJni.data = magViaJni.data(); magJni.length = N / 2;
    Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCompute(&env, nullptr, h, &inArr, sampleRate, &magJni);

    acoustical_fft* fft = acoustical_fft_create(N);
    acoustical_fft_compute_magnitudes_db(fft, input.data(), sampleRate, magDirect.data());
    CHECK(nearlyEqual(magViaJni, magDirect), "FFT magnitudes: JNI == direct C");

    _jfloatArray binJni; binJni.data = binViaJni.data(); binJni.length = N / 2;
    Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftBinFrequencies(&env, nullptr, h, sampleRate, &binJni);
    acoustical_fft_bin_frequencies(fft, sampleRate, binDirect.data());
    CHECK(nearlyEqual(binViaJni, binDirect), "FFT bin frequencies: JNI == direct C");

    // --- Aggregate via JNI (reuses the JNI-produced magnitudes + bin freqs) ---
    std::vector<float> bandFreqs = {31.25f, 62.5f, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    std::vector<float> aggViaJni(10), aggDirect(10);
    _jfloatArray bandArr; bandArr.data = bandFreqs.data(); bandArr.length = 10;
    _jfloatArray aggJni;  aggJni.data = aggViaJni.data();  aggJni.length = 10;
    Java_com_rork_acoustical_domain_audio_NativeDsp_nativeAggregateBands(&env, nullptr, &bandArr, &magJni, &binJni, -120.0f, &aggJni);
    acoustical_aggregate_bands(bandFreqs.data(), 10, magDirect.data(), N / 2, binDirect.data(), N / 2, -120.0f, aggDirect.data());
    CHECK(nearlyEqual(aggViaJni, aggDirect), "aggregateBands: JNI == direct C");

    // Sanity: the 250 Hz band (idx 3, dominant tone) should be the strongest.
    int maxBand = 0;
    for (int i = 1; i < 10; ++i) if (aggViaJni[i] > aggViaJni[maxBand]) maxBand = i;
    CHECK(maxBand == 3, "strongest band is the 250 Hz band (idx 3)");

    acoustical_fft_free(fft);
    Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftFree(&env, nullptr, h);

    std::printf("JNI BRIDGE: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
