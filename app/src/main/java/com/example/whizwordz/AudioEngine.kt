package com.example.whizwordz
import android.util.Log
object AudioEngine {
    init {
        Log.i("AudioEngine", "⏳ Loading native library…")
        try {
            System.loadLibrary("whizwordz-lib")
            Log.i("AudioEngine", "✅ Native library loaded")
        } catch (e: UnsatisfiedLinkError) {
            Log.e("AudioEngine", "❌ Failed to load native library:", e)
        }
    }
    external fun startFullDuplex(gainDb: Float)
    external fun stopFullDuplex()

    external fun startPlayRecord(path: String, gainDb: Float)
    external fun stopPlayRecord()

    external fun playLeftChannel(path: String)
    external fun stopPlayback()

    external fun getLastRecordedFilePath(): String?

    /** Returns the AAudio session ID of the *input* stream, or 0 if none. */
    external fun getAudioSessionId(): Int
}
