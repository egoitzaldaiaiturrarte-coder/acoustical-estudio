// acoustical_jni.cpp — JNI bridge: Android Kotlin <-> shared C DSP core.
//
// Gives the Android app the EXACT same FFT / band-aggregation code the Windows
// app uses (dsp/acoustical_dsp.c). The Kotlin side (NativeDsp.kt) loads this
// library and prefers it over the pure-Kotlin fallback when the native lib is
// present (on device). On the JVM (unit tests) the library is absent, so the
// pure-Kotlin path runs instead — both paths are correct, the native one is
// the shared reference implementation.
//
// Handles are raw pointers to the C core objects, passed to Kotlin as Long.
// A failed create returns 0, which the Kotlin side treats as "fall back".
#include <jni.h>
#include "acoustical_dsp.h"

namespace {
inline acoustical_fft* fftFromHandle(jlong h) {
    return reinterpret_cast<acoustical_fft*>(h);
}
} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCreate(JNIEnv*, jobject, jint size) {
    acoustical_fft* fft = acoustical_fft_create(size);
    return reinterpret_cast<jlong>(fft); // 0 on failure -> Kotlin falls back
}

JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftFree(JNIEnv*, jobject, jlong handle) {
    acoustical_fft_free(fftFromHandle(handle));
}

JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftCompute(
        JNIEnv* env, jobject, jlong handle,
        jfloatArray input, jint sampleRate, jfloatArray out) {
    acoustical_fft* fft = fftFromHandle(handle);
    if (!fft) return;
    float* in = env->GetFloatArrayElements(input, nullptr);
    float* outp = env->GetFloatArrayElements(out, nullptr);
    acoustical_fft_compute_magnitudes_db(fft, in, sampleRate, outp);
    env->ReleaseFloatArrayElements(out, outp, 0);        // 0 = commit to Java
    env->ReleaseFloatArrayElements(input, in, JNI_ABORT); // read-only
}

JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeFftBinFrequencies(
        JNIEnv* env, jobject, jlong handle, jint sampleRate, jfloatArray out) {
    acoustical_fft* fft = fftFromHandle(handle);
    if (!fft) return;
    float* outp = env->GetFloatArrayElements(out, nullptr);
    acoustical_fft_bin_frequencies(fft, sampleRate, outp);
    env->ReleaseFloatArrayElements(out, outp, 0);
}

JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeAggregateBands(
        JNIEnv* env, jobject,
        jfloatArray bandFrequencies, jfloatArray magnitudesDb,
        jfloatArray binFrequencies, jfloat noiseFloorDb, jfloatArray out) {
    jsize bandCount = env->GetArrayLength(bandFrequencies);
    jsize magCount = env->GetArrayLength(magnitudesDb);
    jsize binCount = env->GetArrayLength(binFrequencies);
    float* bands = env->GetFloatArrayElements(bandFrequencies, nullptr);
    float* mags = env->GetFloatArrayElements(magnitudesDb, nullptr);
    float* bins = env->GetFloatArrayElements(binFrequencies, nullptr);
    float* outp = env->GetFloatArrayElements(out, nullptr);
    acoustical_aggregate_bands(bands, bandCount, mags, magCount, bins, binCount,
                               noiseFloorDb, outp);
    env->ReleaseFloatArrayElements(out, outp, 0);
    env->ReleaseFloatArrayElements(bandFrequencies, bands, JNI_ABORT);
    env->ReleaseFloatArrayElements(magnitudesDb, mags, JNI_ABORT);
    env->ReleaseFloatArrayElements(binFrequencies, bins, JNI_ABORT);
}

// --- Generador de señal (núcleo C: seno, barrido, rosa/blanco, silencio) ---

namespace {
inline acoustical_siggen* siggenFromHandle(jlong h) {
    return reinterpret_cast<acoustical_siggen*>(h);
}
} // namespace

JNIEXPORT jlong JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeSiggenCreate(JNIEnv*, jobject) {
    acoustical_siggen* g = acoustical_siggen_create();
    return reinterpret_cast<jlong>(g); // 0 on failure -> Kotlin falls back
}

JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeSiggenFree(JNIEnv*, jobject, jlong handle) {
    acoustical_siggen_free(siggenFromHandle(handle));
}

// Los parámetros se pasan en CADA llamada (el caller toma su snapshot), igual
// que en el wrapper C++ de Windows: el handle solo conserva el estado de
// fases/filtro rosa/PRNG, nada compartido entre hilos.
JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeSiggenFill(
        JNIEnv* env, jobject, jlong handle, jint waveform,
        jfloat frequencyHz, jfloat amplitude,
        jfloat sweepStartHz, jfloat sweepEndHz, jfloat sweepSeconds,
        jint sampleRate, jfloatArray out) {
    acoustical_siggen* g = siggenFromHandle(handle);
    if (!g || out == nullptr) return;
    float* outp = env->GetFloatArrayElements(out, nullptr);
    if (outp == nullptr) return;  // sin memoria: la JVM ya lanzó OutOfMemoryError
    acoustical_siggen_fill(g, waveform,
                           static_cast<float>(frequencyHz),
                           static_cast<float>(amplitude),
                           static_cast<float>(sweepStartHz),
                           static_cast<float>(sweepEndHz),
                           static_cast<float>(sweepSeconds),
                           static_cast<float>(sampleRate),
                           outp, env->GetArrayLength(out));
    env->ReleaseFloatArrayElements(out, outp, 0);
}

// --- Compensación espacial (ganancia dB + retardo ms; ley única C) --------

// Rellena `out` (longitud >= 2) con [0]=ganancia dB, [1]=retardo ms.
JNIEXPORT void JNICALL
Java_com_rork_acoustical_domain_audio_NativeDsp_nativeSpatialCompensate(
        JNIEnv* env, jobject,
        jfloat x, jfloat y, jfloat z, jfloat size, jfloatArray out) {
    float gainDb = 0.0f, delayMs = 0.0f;
    acoustical_spatial_compensate(static_cast<float>(x), static_cast<float>(y),
                                  static_cast<float>(z), static_cast<float>(size),
                                  &gainDb, &delayMs);
    // out debe tener al menos 2 huecos: con uno más corto se escribiría
    // fuera del array de la JVM (corrupción de heap, no una excepción).
    if (out == nullptr || env->GetArrayLength(out) < 2) return;
    float* outp = env->GetFloatArrayElements(out, nullptr);
    if (outp == nullptr) return;
    outp[0] = gainDb;
    outp[1] = delayMs;
    env->ReleaseFloatArrayElements(out, outp, 0);
}

} // extern "C"
