# ProGuard / R8 rules for Acoustical Estudio.
#
# Keep the JNI bridge entry points — R8 would otherwise rename/remove the
# external methods that libacoustical_dsp.so resolves by name at runtime.
-keepclassmembers class com.rork.acoustical.domain.audio.NativeDsp {
    native <methods>;
}

# Keep the Kotlinx-serializable model classes (field names must survive).
-keepattributes *Annotation*
-keepclassmembers class com.rork.acoustical.domain.model.** {
    <fields>;
}
