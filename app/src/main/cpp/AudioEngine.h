#pragma once
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Full-duplex passthrough (mic → speaker)
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_startFullDuplex(JNIEnv* env, jclass clazz, jfloat gainDb);
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_stopFullDuplex(JNIEnv* env, jclass clazz);

// Play a WAV file and record mic+playback into stereo file
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_startPlayRecord(JNIEnv* env, jclass clazz,
                                                       jstring filePath, jfloat gainDb);
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_stopPlayRecord(JNIEnv* env, jclass clazz);

// Play only left channel of last recording
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_playLeftChannel(JNIEnv* env, jclass clazz,
                                                       jstring filePath);
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_stopPlayback(JNIEnv* env, jclass clazz);

// Helpers
JNIEXPORT jstring JNICALL
Java_com_example_whizwordz_AudioEngine_getLastRecordedFilePath(JNIEnv* env, jclass clazz);
JNIEXPORT jint JNICALL
Java_com_example_whizwordz_AudioEngine_getAudioSessionId(JNIEnv* env, jclass clazz);

// Transcription control
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_startTranscription(JNIEnv* env, jclass clazz,
                                                          jstring model);
JNIEXPORT void JNICALL
Java_com_example_whizwordz_AudioEngine_stopTranscription(JNIEnv* env, jclass clazz);

#ifdef __cplusplus
}
#endif
