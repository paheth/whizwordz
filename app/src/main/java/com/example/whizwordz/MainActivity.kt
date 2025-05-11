package com.example.whizwordz

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.*
import android.provider.Settings
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import android.widget.Button
import android.widget.EditText
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ToggleButton
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.File
import java.util.*

class MainActivity : AppCompatActivity(), TextToSpeech.OnInitListener {
    private var lastRecordedPath: String? = null
    private lateinit var tts: TextToSpeech
    private lateinit var editTextTts: EditText
    private lateinit var buttonTts: Button
    private lateinit var toggleFullDuplex: ToggleButton
    private lateinit var togglePlayRecord: ToggleButton
    private lateinit var togglePlayLast: ToggleButton
    private lateinit var gainSeekBar: SeekBar
    private lateinit var gainLabel: TextView
    private lateinit var statusTextView: TextView
    private lateinit var toggleTranscribe: ToggleButton
    private lateinit var spinnerModel: androidx.appcompat.widget.AppCompatSpinner

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Initialize TTS
        tts = TextToSpeech(this, this)

        // Request permissions and storage access
        requestPermissions()
        ensureAllFilesAccess()

        // Bind UI
        editTextTts      = findViewById(R.id.editTextTts)
        buttonTts        = findViewById(R.id.buttonTts)
        toggleFullDuplex = findViewById(R.id.toggleFullDuplex)
        togglePlayRecord = findViewById(R.id.togglePlayRecord)
        togglePlayLast   = findViewById(R.id.togglePlayLast)
        gainSeekBar      = findViewById(R.id.gainSeekBar)
        gainLabel        = findViewById(R.id.gainLabel)
        statusTextView   = findViewById(R.id.statusTextView)

        // TTS button action
        buttonTts.setOnClickListener {
            val inputText = editTextTts.text.toString().takeIf { it.isNotBlank() } ?: "Let's play Simon Says!"
            synthesisAndRecord(inputText)
        }

        // Wire up toggle buttons
        toggleFullDuplex.setOnCheckedChangeListener { _, checked ->
            val gain = gainSeekBar.progress.toFloat()
            if (checked) {
                AudioEngine.startFullDuplex(gain)
                appendLog("► Full Duplex ON")
            } else {
                AudioEngine.stopFullDuplex()
                appendLog("■ Full Duplex OFF")
            }
        }

        togglePlayRecord.setOnCheckedChangeListener { _, checked ->
            val path = "/sdcard/Recordings/WhizWordz/whizwordz.wav"
            val gain = gainSeekBar.progress.toFloat()
            if (checked) {
                AudioEngine.startPlayRecord(path, gain)
                appendLog("► Play+Record ON")
                // Automatically stop after WAV duration
                val mmr = MediaMetadataRetriever().apply { setDataSource(path) }
                val durationMs = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
                mmr.release()
                Handler(Looper.getMainLooper()).postDelayed({
                    // auto-stop playback: just uncheck toggle to trigger stop without extra log
                    togglePlayRecord.isChecked = false
                }, durationMs)
            } else {
                AudioEngine.stopPlayRecord()
                appendLog("■ Play+Record OFF")
                lastRecordedPath = AudioEngine.getLastRecordedFilePath()
            }
        }

        // Play last recorded left channel
        togglePlayLast.setOnCheckedChangeListener { _, checked ->
            if (checked) {
                val playPath = lastRecordedPath
                if (!playPath.isNullOrEmpty() && File(playPath).exists()) {
                    AudioEngine.playLeftChannel(playPath)
                    appendLog("► Play Last ON: ${File(playPath).name}")
                    // Automatically stop after file duration
                    val mmr = MediaMetadataRetriever().apply { setDataSource(playPath) }
                    val durationMs = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
                        ?.toLongOrNull() ?: 0L
                    mmr.release()
                    Handler(Looper.getMainLooper()).postDelayed({
                        togglePlayLast.isChecked = false
                    }, durationMs)
                } else {
                    appendLog("⚠ No valid last file to play")
                    togglePlayLast.isChecked = false
                }
            } else {
                AudioEngine.stopPlayback()
                appendLog("■ Play Last OFF")
            }
        }

        gainSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                gainLabel.text = "Gain: $progress dB"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })
    }

    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            tts.language = Locale.US
            tts.setSpeechRate(1.0f)
        } else {
            appendLog("⚠ TTS init failed: $status")
        }
    }

    private fun synthesisAndRecord(text: String) {
        val wavFile = File(externalCacheDir, "temp_tts.wav")
        val params = Bundle().apply {
            putString(TextToSpeech.Engine.KEY_PARAM_UTTERANCE_ID, "TTS_ID")
        }
        tts.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) {}
            override fun onDone(utteranceId: String?) {
                runOnUiThread {
                    val gain = gainSeekBar.progress.toFloat()
                    AudioEngine.startPlayRecord(wavFile.absolutePath, gain)
                    appendLog("► TTS Play+Record ON")
                    // stop after duration
                    val mmr = MediaMetadataRetriever().apply { setDataSource(wavFile.absolutePath) }
                    val duration = mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
                    mmr.release()
                    Handler(Looper.getMainLooper()).postDelayed({
                        AudioEngine.stopPlayRecord()
                        appendLog("► TTS Play+Record OFF")
                        lastRecordedPath = AudioEngine.getLastRecordedFilePath()
                        appendLog("■ TTS Record saved → ${File(lastRecordedPath ?: "").name}")
                    }, duration)
                }
            }
            override fun onError(utteranceId: String?) { appendLog("⚠ TTS error on $utteranceId") }
            override fun onError(utteranceId: String?, errorCode: Int) { appendLog("⚠ TTS error $errorCode on $utteranceId") }
        })
        tts.synthesizeToFile(text, params, wavFile, "TTS_ID")
    }

    private fun appendLog(message: String) {
        statusTextView.append("$message\n")
    }

    private fun requestPermissions() {
        val perms = mutableListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) perms += Manifest.permission.READ_MEDIA_AUDIO
        else { perms += Manifest.permission.READ_EXTERNAL_STORAGE; perms += Manifest.permission.WRITE_EXTERNAL_STORAGE }
        val needed = perms.filter { ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED }
        if (needed.isNotEmpty()) ActivityCompat.requestPermissions(this, needed.toTypedArray(), 123)
    }

    private fun ensureAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                .apply { data = Uri.parse("package:$packageName") })
        }
    }

    override fun onDestroy() {
        tts.shutdown()
        super.onDestroy()
    }
}
