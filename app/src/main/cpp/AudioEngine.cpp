#include "AudioEngine.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <mutex>
#include <vector>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cmath>

//#include "whisper.h"

static const char* TAG = "WhizWordzAAudio";
static constexpr int kSampleRate = 48000;
static constexpr int kChannelCount = 1;
static constexpr int kFramesPerCallback = 192;

class Engine {
public:
    Engine() = default;
    ~Engine() { stopFullDuplex(); stopPlayRecord(); stopPlayback(); }

    void startFullDuplex(float gainDb) {
        std::lock_guard<std::mutex> lock(mutex_);
        gain_ = std::pow(10.0f, gainDb / 20.0f);
        tempBuffer_.assign(kFramesPerCallback, 0.0f);
        openStream(kSampleRate, AAUDIO_DIRECTION_INPUT, nullptr, inStream_, true);
        openStream(kSampleRate, AAUDIO_DIRECTION_OUTPUT, fullDuplexCallback, outStream_, false);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Full-duplex ON (gain=%.1f dB)", gainDb);
    }

    void stopFullDuplex() {
        std::lock_guard<std::mutex> lock(mutex_);
        closeStream(inStream_);
        closeStream(outStream_);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Full-duplex OFF");
    }

    void startPlayRecord(const char* path, float gainDb) {
        std::lock_guard<std::mutex> lock(mutex_);
        gain_ = std::pow(10.0f, gainDb / 20.0f);
        // Load WAV and resample to kSampleRate
        std::vector<float> raw;
        int srcRate = 0;
        loadWav(path, raw, srcRate);
        if (srcRate <= 0) srcRate = kSampleRate;
        resample(raw, srcRate, playData_);
        lastPath_ = makeTimestampedPath("rec_");
        outFile_.open(lastPath_, std::ios::binary);
        writeWavHeader(outFile_, 2, kSampleRate);
        playPos_ = 0;
        tempBuffer_.assign(kFramesPerCallback, 0.0f);
        recBuffer_.assign(kFramesPerCallback, 0.0f);
        openStream(kSampleRate, AAUDIO_DIRECTION_INPUT, nullptr, recStream_, true);
        openStream(kSampleRate, AAUDIO_DIRECTION_OUTPUT, playRecordCallback, playStream_, false);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Play+Record ON -> %s", lastPath_.c_str());
    }

    void stopPlayRecord() {
        std::lock_guard<std::mutex> lock(mutex_);
        closeStream(recStream_);
        closeStream(playStream_);
        finalizeWavHeader(outFile_);
        outFile_.close();
        __android_log_print(ANDROID_LOG_INFO, TAG, "Play+Record OFF");
    }

    void playLeftChannel(const char* path) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<float> raw;
        int srcRate = 0;
        loadWav(path, raw, srcRate);
        resample(raw, srcRate, leftData_);
        playPos_ = 0;
        openStream(kSampleRate, AAUDIO_DIRECTION_OUTPUT, leftCallback, leftStream_, false);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Left-channel ON");
    }

    void stopPlayback() {
        std::lock_guard<std::mutex> lock(mutex_);
        closeStream(leftStream_);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Left-channel OFF");
    }

    int getInputSessionId() const {
        return inStream_ ? AAudioStream_getSessionId(inStream_) : 0;
    }
    std::string getLastPath() const { return lastPath_; }

private:
    std::mutex mutex_;

    AAudioStream* inStream_{};
    AAudioStream* outStream_{};
    AAudioStream* recStream_{};
    AAudioStream* playStream_{};
    AAudioStream* leftStream_{};
    float gain_{1.0f};
    std::vector<float> tempBuffer_, recBuffer_, playData_, leftData_;
    int playPos_{0};
    std::ofstream outFile_;
    std::string lastPath_;

    void openStream(int rate, int direction,
                    aaudio_data_callback_result_t(*cb)(AAudioStream*, void*, void*, int32_t),
                    AAudioStream*& stream, bool isInput) {
        AAudioStreamBuilder* b;
        AAudio_createStreamBuilder(&b);
        AAudioStreamBuilder_setDirection(b, direction);
        if (isInput)
            AAudioStreamBuilder_setInputPreset(b, AAUDIO_INPUT_PRESET_UNPROCESSED);
        AAudioStreamBuilder_setSampleRate(b, rate);
        AAudioStreamBuilder_setChannelCount(b, kChannelCount);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        // ensure callback buffer size
        AAudioStreamBuilder_setFramesPerDataCallback(b, kFramesPerCallback);
        if (cb)
            AAudioStreamBuilder_setDataCallback(b, cb, this);
        AAudioStreamBuilder_openStream(b, &stream);
        AAudioStreamBuilder_delete(b);
        if (stream)
            AAudioStream_requestStart(stream);
    }

    void closeStream(AAudioStream*& s) {
        if (s) {
            AAudioStream_requestStop(s);
            AAudioStream_close(s);
            s = nullptr;
        }
    }

    void loadWav(const char* path, std::vector<float>& out, int& rate) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            rate = 0;
            return;
        }
        // Read header
        char hdr[44];
        in.read(hdr, 44);

        // Channel count (bytes 22–23) and sample-rate (bytes 24–27)
        int channels = (uint8_t)hdr[22] | ((uint8_t)hdr[23] << 8);
        rate = (uint8_t)hdr[24] | ((uint8_t)hdr[25] << 8)
               | ((uint8_t)hdr[26] << 16) | ((uint8_t)hdr[27] << 24);

        // Data‐chunk size at bytes 40–43
        int dataBytes = (uint8_t)hdr[40] | ((uint8_t)hdr[41] << 8)
                        | ((uint8_t)hdr[42] << 16) | ((uint8_t)hdr[43] << 24);
        size_t totalSamples = dataBytes / 2;               // 2 bytes per sample
        size_t frames       = totalSamples / channels;      // number of L/R frames

        out.clear();
        in.seekg(44, std::ios::beg);

        // For each frame, read the left channel sample, skip the rest
        for (size_t f = 0; f < frames; ++f) {
            int16_t sample = 0;
            // read left
            in.read(reinterpret_cast<char*>(&sample), sizeof(sample));
            out.push_back(sample / 32768.0f);
            // skip right (and any extra channels)
            in.seekg(2 * (channels - 1), std::ios::cur);
        }
    }

    void resample(const std::vector<float>& in, int inRate, std::vector<float>& out) {
        if (inRate == kSampleRate) { out = in; return; }
        size_t outSize = in.size() * kSampleRate / inRate;
        out.resize(outSize);
        for (size_t i = 0; i < outSize; ++i) {
            double pos = i * (double)inRate / kSampleRate;
            size_t idx = (size_t)pos;
            double frac = pos - idx;
            float a = in[std::min(idx, in.size()-1)];
            float b = in[std::min(idx+1, in.size()-1)];
            out[i] = a * (1.0 - frac) + b * frac;
        }
    }

    void writeWavHeader(std::ofstream& f, int ch, int rate) {
        f.write("RIFF----WAVEfmt ", 16);
        writeInt(f, 16, 4); writeInt(f, 1, 2); writeInt(f, ch, 2);
        writeInt(f, rate, 4); writeInt(f, rate * ch * 2, 4);
        writeInt(f, ch * 2, 2); writeInt(f, 16, 2);
        f.write("data----", 8);
    }

    void finalizeWavHeader(std::ofstream& f) {
        int sz = (int)f.tellp();
        f.seekp(4); writeInt(f, sz - 8, 4);
        f.seekp(40); writeInt(f, sz - 44, 4);
    }

    void writeInt(std::ofstream& f, int32_t v, int b) {
        for (int i = 0; i < b; ++i)
            f.put((char)((v >> (8 * i)) & 0xFF));
    }

    std::string makeTimestampedPath(const char* prefix) {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{}; localtime_r(&tt, &tm);
        std::ostringstream ss;
        ss << "/sdcard/Recordings/WhizWordz/" << prefix
           << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".wav";
        return ss.str();
    }

    static aaudio_data_callback_result_t fullDuplexCallback(AAudioStream*, void* ud, void* data, int32_t frames) {
        auto* self = reinterpret_cast<Engine*>(ud);
        float* out = reinterpret_cast<float*>(data);
        int32_t cnt = AAudioStream_read(self->inStream_, self->tempBuffer_.data(), frames, 0);
        if (cnt > 0) {
            for (int i = 0; i < cnt; ++i)
                out[i] = self->tempBuffer_[i] * self->gain_;
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    static aaudio_data_callback_result_t playRecordCallback(AAudioStream*, void* ud, void* data, int32_t frames) {
        auto* self = reinterpret_cast<Engine*>(ud);
        float* out = reinterpret_cast<float*>(data);
        int32_t cnt = AAudioStream_read(self->recStream_, self->recBuffer_.data(), frames, 0);
        for (int i = 0; i < frames; ++i) {
            float mic = (i < cnt ? self->recBuffer_[i] : 0.0f) * self->gain_;
            float ply = (self->playPos_ < (int)self->playData_.size()) ? self->playData_[self->playPos_++] : 0.0f;
            out[i] = ply;
            int16_t left = (int16_t)(mic * 32767);
            int16_t right = (int16_t)(ply * 32767);
            self->writeInt(self->outFile_, left, 2);
            self->writeInt(self->outFile_, right, 2);
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

//    static aaudio_data_callback_result_t leftCallback(AAudioStream*, void* ud, void* data, int32_t frames) {
//        auto* self = reinterpret_cast<Engine*>(ud);
//        float* out = reinterpret_cast<float*>(data);
//        for (int i = 0; i < frames; ++i) {
//            out[i] = (self->playPos_ < (int)self->leftData_.size()) ? self->leftData_[self->playPos_++] : 0.0f;
//        }
//        return AAUDIO_CALLBACK_RESULT_CONTINUE;
//    }
    static aaudio_data_callback_result_t
    leftCallback(AAudioStream* /*stream*/, void* ud, void* data, int32_t frames) {
        auto* self = reinterpret_cast<Engine*>(ud);
        float* out = reinterpret_cast<float*>(data);
        int samplesPlayed = 0;

        for (int i = 0; i < frames; ++i) {
            if (self->playPos_ < (int)self->leftData_.size()) {
                out[i] = self->leftData_[self->playPos_++];
                ++samplesPlayed;
            } else {
                out[i] = 0.0f;
            }
        }
        if (samplesPlayed == 0) {
            // No more data left: tell AAudio to stop calling us
            return AAUDIO_CALLBACK_RESULT_STOP;
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
};

static Engine* gEngine = nullptr;

extern "C" {
//JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_startTranscription(JNIEnv* env, jclass clazz, jstring model) {
//    const char* m = env->GetStringUTFChars(model, nullptr);
//    if (!gEngine) gEngine = new Engine();
//    gEngine->startTranscription(m);
//    env->ReleaseStringUTFChars(model, m);
//}
//
//JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_stopTranscription(JNIEnv* env, jclass clazz) {
//    if (gEngine) gEngine->stopTranscription();
//}

JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_startFullDuplex(JNIEnv* env, jclass clazz, jfloat gain) { if (!gEngine) gEngine = new Engine(); gEngine->startFullDuplex(gain); }
JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_stopFullDuplex(JNIEnv* env, jclass clazz) { if (gEngine) gEngine->stopFullDuplex(); }
JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_startPlayRecord(JNIEnv* env, jclass clazz, jstring path, jfloat gain) { const char* p = env->GetStringUTFChars(path, nullptr); if (!gEngine) gEngine = new Engine(); gEngine->startPlayRecord(p, gain); env->ReleaseStringUTFChars(path, p); }
JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_stopPlayRecord(JNIEnv* env, jclass clazz) { if (gEngine) gEngine->stopPlayRecord(); }
JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_playLeftChannel(JNIEnv* env, jclass clazz, jstring path) { const char* p = env->GetStringUTFChars(path, nullptr); if (!gEngine) gEngine = new Engine(); gEngine->playLeftChannel(p); env->ReleaseStringUTFChars(path, p); }
JNIEXPORT void JNICALL Java_com_example_whizwordz_AudioEngine_stopPlayback(JNIEnv* env, jclass clazz) { if (gEngine) gEngine->stopPlayback(); }
JNIEXPORT jstring JNICALL Java_com_example_whizwordz_AudioEngine_getLastRecordedFilePath(JNIEnv* env, jclass clazz) { if (!gEngine) return nullptr; std::string s = gEngine->getLastPath(); return env->NewStringUTF(s.c_str()); }
JNIEXPORT jint JNICALL Java_com_example_whizwordz_AudioEngine_getAudioSessionId(JNIEnv* env, jclass clazz) { return gEngine ? gEngine->getInputSessionId() : 0; }
}
