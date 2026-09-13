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

} // extern "C"
