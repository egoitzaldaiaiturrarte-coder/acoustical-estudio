// jni.h — MINIMAL JNI stub for host-side unit tests of the JNI bridge.
//
// On device the real <jni.h> from the NDK is used. This stub provides just
// enough of the surface that acoustical_jni.cpp compiles and runs on the host
// so we can verify the marshalling without an NDK/Android SDK.
//
// A "jfloatArray" here is a pointer to a struct that owns a real float buffer,
// so the bridge's GetFloatArrayElements/Release calls actually move data.
#pragma once
#include <cstddef>

typedef int jint;
typedef int jsize;
typedef long long jlong;
typedef float jfloat;
typedef int jboolean;

struct _jobject;   typedef _jobject* jobject;
struct _jarray {}; typedef _jarray* jarray;   // complete (empty) so _jfloatArray can inherit

struct FloatArrayData {
    float* data;
    int length;
};
struct _jfloatArray : public _jarray, public FloatArrayData {};
typedef _jfloatArray* jfloatArray;

struct JNIEnv {
    jint GetArrayLength(jarray arr) {
        return static_cast<jint>(static_cast<jfloatArray>(arr)->length);
    }
    float* GetFloatArrayElements(jfloatArray arr, jboolean*) {
        return arr->data;
    }
    void ReleaseFloatArrayElements(jfloatArray arr, float*, jint) {
        (void)arr;
    }
};

#define JNIEXPORT
#define JNICALL
#define JNI_ABORT 2
