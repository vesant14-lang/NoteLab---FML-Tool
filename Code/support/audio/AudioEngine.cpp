#include "AudioEngine.hpp"

// miniaudio arrastra <windows.h>, cuyas macros min/max rompen std::min/std::max.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>

namespace fml {
namespace {

constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels   = 2;
constexpr size_t kTrackPcmLimit = 192u * 1024u * 1024u;
constexpr size_t kBankPcmLimit = 384u * 1024u * 1024u;
constexpr ma_uint64 kDecodeChunkFrames = 8192;

std::int64_t nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Track {
    std::string           name;
    std::vector<short>    pcm;      // i16 intercalado: la mitad de memoria que f32
    ma_uint64             frames = 0;
    std::atomic<float>    gain{1.0f};
    // -1 todo a la izquierda, 0 centro, +1 todo a la derecha. Ley lineal: el
    // lado hacia el que se va se queda entero y el otro baja, asi el centro
    // no cambia de volumen y un extremo apaga el otro lado del todo.
    std::atomic<float>    pan{0.0f};
};

inline void panGains(float pan, float& left, float& right) {
    const float p = std::max(-1.0f, std::min(1.0f, pan));
    left = p > 0.0f ? 1.0f - p : 1.0f;
    right = p < 0.0f ? 1.0f + p : 1.0f;
}

// El montaje, en frames. Se publica igual que el banco -entero e inmutable- y
// por el mismo motivo: lo lee el hilo de audio en cada bloque.
struct ClipSegment {
    std::int64_t start = 0;    // en el documento
    std::int64_t end = 0;
    std::int64_t delta = 0;    // fuente = documento + delta
    float        gain = 1.0f;
};
struct ClipMap {
    // Una lista por pista, en el mismo orden que el banco. Una lista vacia
    // significa «esta pista suena entera y en su sitio», que es el estado
    // normal del reproductor.
    std::vector<std::vector<ClipSegment>> tracks;
};

// Se publica entero y no vuelve a mutarse (salvo los gain atomicos). Asi una
// carga nueva no puede invalidar memoria que el callback todavia esta leyendo.
struct TrackBank {
    std::vector<std::unique_ptr<Track>> tracks;
    ma_uint64                           longest = 0;
};

struct TrackDecodeResult {
    std::shared_ptr<TrackBank> bank{std::make_shared<TrackBank>()};
    int loaded = 0;
    std::string error;
};

TrackDecodeResult decodeTrackBank(const std::vector<std::string>& paths) {
    TrackDecodeResult result;
    size_t bankBytes = 0;
    for (const std::string& path : paths) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, kChannels, kSampleRate);
        ma_decoder decoder{};
#ifdef _WIN32
        const ma_result opened = ma_decoder_init_file_w(
            std::filesystem::u8path(path).wstring().c_str(), &cfg, &decoder);
#else
        const ma_result opened = ma_decoder_init_file(path.c_str(), &cfg, &decoder);
#endif
        if (opened != MA_SUCCESS) {
            if (result.error.empty()) result.error = "no se pudo decodificar: " + path;
            continue;
        }
        try {
            const size_t allowed = std::min(kTrackPcmLimit, kBankPcmLimit - bankBytes);
            const ma_uint64 maxFrames = allowed / (kChannels * sizeof(short));
            ma_uint64 announced = 0;
            bool tooLarge = maxFrames == 0;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &announced) == MA_SUCCESS &&
                announced > maxFrames) tooLarge = true;
            if (!tooLarge) {
                auto track = std::make_unique<Track>();
                const size_t slash = path.find_last_of("/\\");
                track->name = slash == std::string::npos ? path : path.substr(slash + 1);
                if (announced > 0 && announced <= maxFrames)
                    track->pcm.reserve(static_cast<size_t>(announced) * kChannels);
                short chunk[kDecodeChunkFrames * kChannels];
                for (;;) {
                    ma_uint64 received = 0;
                    const ma_result decoded = ma_decoder_read_pcm_frames(
                        &decoder, chunk, kDecodeChunkFrames, &received);
                    if (decoded != MA_SUCCESS && decoded != MA_AT_END) {
                        if (result.error.empty()) result.error = "falló la lectura de audio: " + path;
                        track.reset();
                        break;
                    }
                    if (received > maxFrames - (track->pcm.size() / kChannels)) {
                        tooLarge = true;
                        break;
                    }
                    track->pcm.insert(track->pcm.end(), chunk, chunk + received * kChannels);
                    if (received == 0) break;
                }
                if (!tooLarge && track && !track->pcm.empty()) {
                    track->frames = track->pcm.size() / kChannels;
                    bankBytes += track->pcm.size() * sizeof(short);
                    result.bank->longest = std::max(result.bank->longest, track->frames);
                    result.bank->tracks.push_back(std::move(track));
                    ++result.loaded;
                }
            }
            if (tooLarge && result.error.empty())
                result.error = "audio demasiado grande para el límite de memoria (192 MB por pista, 384 MB en total): " + path;
        } catch (const std::bad_alloc&) {
            if (result.error.empty()) result.error = "memoria insuficiente al cargar audio: " + path;
        }
        ma_decoder_uninit(&decoder);
    }
    return result;
}

// Publicado por el hilo de audio con un seqlock (impar = escribiendo).
struct ClockSnapshot {
    double    playheadFrames = 0.0;
    double    rate           = 1.0;
    std::int64_t hostTimeNs  = 0;
    ma_uint32 framesThisCb   = 0;
    bool      playing        = false;
    std::uint64_t appliedSeekGen = 0;
    double    displayOffsetFrames = 0.0;
    std::uint64_t loopCount = 0;
};

}  // namespace

struct AudioEngine::Impl {
    struct AsyncLoader {
        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false;
        bool hasRequest = false;
        bool decoding = false;
        bool completedReady = false;
        std::uint64_t currentGeneration = 0;
        std::uint64_t requestedGeneration = 0;
        std::uint64_t decodingGeneration = 0;
        std::uint64_t completedGeneration = 0;
        std::vector<std::string> requestedPaths;
        std::shared_ptr<TrackBank> completedBank;
        int completedLoaded = 0;
        std::string completedError;
        std::thread worker;
    } loader;

    ma_device device{};
    bool      deviceOk = false;

    // atomic_load/store para shared_ptr estan disponibles desde C++11. El
    // callback toma una referencia estable sin bloquear durante toda la mezcla.
    std::shared_ptr<TrackBank> trackBank{std::make_shared<TrackBank>()};
    // El montaje del documento. Nulo -lo normal- es «sin montaje».
    std::shared_ptr<const ClipMap> clipMap{};

    std::atomic<bool>  playing{false};
    std::atomic<float> rateAtomic{1.0f};
    std::atomic<float> master{0.8f};

    std::atomic<std::uint64_t> seekGen{0};
    std::atomic<double>        seekTarget{0.0};
    std::atomic<double>        seekLogicalMs{0.0};
    std::atomic<double>        seekDisplayOffset{0.0};

    std::atomic<double> loopA{-1.0}, loopB{-1.0};

    // exclusivos del hilo de audio
    double        playhead = 0.0;
    std::uint64_t appliedSeekGen = 0;
    double        displayOffsetFrames = 0.0;
    std::uint64_t loopCount = 0;

    std::atomic<std::uint32_t> seq{0};
    // Los campos tambien son atomicos: un seqlock sobre memoria ordinaria es
    // una carrera de datos en C++, aunque en x86 pareciera funcionar.
    std::atomic<double>        snapPlayheadFrames{0.0};
    std::atomic<double>        snapRate{1.0};
    std::atomic<std::int64_t>  snapHostTimeNs{0};
    std::atomic<ma_uint32>     snapFramesThisCb{0};
    std::atomic<bool>          snapPlaying{false};
    std::atomic<std::uint64_t> snapAppliedSeekGen{0};
    std::atomic<double>        snapDisplayOffsetFrames{0.0};
    std::atomic<std::uint64_t> snapLoopCount{0};

    double latencyFrames = 0.0;

    // estado del lector (hilo de UI)
    mutable double        lastSmoothMs = -1e9;
    mutable std::uint64_t lastSeenGen  = 0;

    Impl();
    ~Impl();
    void cancelTrackLoad();
    void queueTrackLoad(const std::vector<std::string>& paths);
    bool pollTrackLoad(int* loaded, std::string* error);
    bool trackLoadPending();

    std::shared_ptr<TrackBank> readTrackBank() const {
        return std::atomic_load_explicit(&trackBank, std::memory_order_acquire);
    }

    void publishTrackBank(std::shared_ptr<TrackBank> bank) {
        std::atomic_store_explicit(&trackBank, std::move(bank), std::memory_order_release);
    }

    std::shared_ptr<const ClipMap> readClipMap() const {
        return std::atomic_load_explicit(&clipMap, std::memory_order_acquire);
    }

    void publishClipMap(std::shared_ptr<const ClipMap> map) {
        std::atomic_store_explicit(&clipMap, std::move(map), std::memory_order_release);
    }

    // Que trozo suena en `position` y con cuanta ganancia. `false` = silencio.
    // Los clips de una misma pista no se solapan -el documento los ordena- asi
    // que la busqueda lineal sobre dos o tres tramos no necesita mas.
    static bool locate(const std::vector<ClipSegment>& clips, double position,
                       double* source, float* gain) {
        for (const ClipSegment& clip : clips) {
            if (position < static_cast<double>(clip.start) ||
                position >= static_cast<double>(clip.end)) continue;
            *source = position + static_cast<double>(clip.delta);
            *gain = clip.gain;
            return true;
        }
        return false;
    }

    ClockSnapshot read() const {
        ClockSnapshot s;
        for (;;) {
            const std::uint32_t a = seq.load(std::memory_order_acquire);
            if (a & 1u) continue;
            s.playheadFrames = snapPlayheadFrames.load(std::memory_order_relaxed);
            s.rate = snapRate.load(std::memory_order_relaxed);
            s.hostTimeNs = snapHostTimeNs.load(std::memory_order_relaxed);
            s.framesThisCb = snapFramesThisCb.load(std::memory_order_relaxed);
            s.playing = snapPlaying.load(std::memory_order_relaxed);
            s.appliedSeekGen = snapAppliedSeekGen.load(std::memory_order_relaxed);
            s.displayOffsetFrames = snapDisplayOffsetFrames.load(std::memory_order_relaxed);
            s.loopCount = snapLoopCount.load(std::memory_order_relaxed);
            const std::uint32_t b = seq.load(std::memory_order_acquire);
            if (a == b) return s;
        }
    }

    double latencyNow(bool isPlaying) const { return isPlaying ? latencyFrames : 0.0; }

    static void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frameCount);
    void mix(float* out, ma_uint32 frameCount);
};

AudioEngine::Impl::Impl() {
    loader.worker = std::thread([this] {
        for (;;) {
            std::vector<std::string> paths;
            std::uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(loader.mutex);
                loader.wake.wait(lock, [this] {
                    return loader.stopping || loader.hasRequest;
                });
                if (loader.stopping) return;
                paths = std::move(loader.requestedPaths);
                generation = loader.requestedGeneration;
                loader.hasRequest = false;
                loader.decoding = true;
                loader.decodingGeneration = generation;
            }

            TrackDecodeResult result = decodeTrackBank(paths);
            {
                std::lock_guard<std::mutex> lock(loader.mutex);
                loader.decoding = false;
                if (loader.stopping) return;
                if (generation == loader.currentGeneration) {
                    loader.completedBank = std::move(result.bank);
                    loader.completedLoaded = result.loaded;
                    loader.completedError = std::move(result.error);
                    loader.completedGeneration = generation;
                    loader.completedReady = true;
                }
            }
        }
    });
}

AudioEngine::Impl::~Impl() {
    {
        std::lock_guard<std::mutex> lock(loader.mutex);
        loader.stopping = true;
        loader.hasRequest = false;
        loader.completedReady = false;
    }
    loader.wake.notify_one();
    if (loader.worker.joinable()) loader.worker.join();
}

void AudioEngine::Impl::cancelTrackLoad() {
    std::lock_guard<std::mutex> lock(loader.mutex);
    ++loader.currentGeneration;
    loader.hasRequest = false;
    loader.requestedPaths.clear();
    loader.completedReady = false;
    loader.completedBank.reset();
    loader.completedError.clear();
}

void AudioEngine::Impl::queueTrackLoad(const std::vector<std::string>& paths) {
    {
        std::lock_guard<std::mutex> lock(loader.mutex);
        ++loader.currentGeneration;
        loader.requestedGeneration = loader.currentGeneration;
        loader.requestedPaths = paths;
        loader.hasRequest = true;
        loader.completedReady = false;
        loader.completedBank.reset();
        loader.completedError.clear();
    }
    loader.wake.notify_one();
}

bool AudioEngine::Impl::pollTrackLoad(int* loaded, std::string* error) {
    std::shared_ptr<TrackBank> bank;
    int resultCount = 0;
    std::string resultError;
    {
        std::lock_guard<std::mutex> lock(loader.mutex);
        if (!loader.completedReady ||
            loader.completedGeneration != loader.currentGeneration) return false;
        bank = std::move(loader.completedBank);
        resultCount = loader.completedLoaded;
        resultError = std::move(loader.completedError);
        loader.completedReady = false;
    }
    publishTrackBank(bank ? std::move(bank) : std::make_shared<TrackBank>());
    if (loaded) *loaded = resultCount;
    if (error) *error = std::move(resultError);
    return true;
}

bool AudioEngine::Impl::trackLoadPending() {
    std::lock_guard<std::mutex> lock(loader.mutex);
    return loader.hasRequest ||
        (loader.decoding && loader.decodingGeneration == loader.currentGeneration) ||
        (loader.completedReady && loader.completedGeneration == loader.currentGeneration);
}

void AudioEngine::Impl::dataCallback(ma_device* dev, void* out, const void*, ma_uint32 n) {
    static_cast<Impl*>(dev->pUserData)->mix(static_cast<float*>(out), n);
}

void AudioEngine::Impl::mix(float* out, ma_uint32 frameCount) {
    std::memset(out, 0, sizeof(float) * frameCount * kChannels);

    const std::uint64_t reqGen = seekGen.load(std::memory_order_acquire);
    if (reqGen != appliedSeekGen) {
        playhead = seekTarget.load(std::memory_order_relaxed);
        displayOffsetFrames = seekDisplayOffset.load(std::memory_order_relaxed);
        loopCount = 0;
        appliedSeekGen = reqGen;
    }

    bool         isPlayingLocal = playing.load(std::memory_order_relaxed);
    const bool&  isPlaying = isPlayingLocal;
    const double r         = rateAtomic.load(std::memory_order_relaxed);
    const float  mg        = master.load(std::memory_order_relaxed);
    const auto   bank      = readTrackBank();

    // Al reproducir, el cursor interno representa audio ya enviado al
    // dispositivo. Se conserva este offset al pausar para que la posicion no
    // salte hacia delante una latencia completa.
    if (isPlaying) displayOffsetFrames = latencyFrames;

    // El playhead NO puede pasar del final. Antes seguia contando y la UI
    // llegaba a mostrar 170 s de una cancion de 140.
    const double totalFrames = static_cast<double>(bank->longest);
    const double requestedA = loopA.load(std::memory_order_relaxed) * kSampleRate / 1000.0;
    const double requestedB = loopB.load(std::memory_order_relaxed) * kSampleRate / 1000.0;
    const double loopStart = std::clamp(requestedA, 0.0, totalFrames);
    const double loopEnd = std::clamp(requestedB, 0.0, totalFrames);
    const bool looping = loopA.load(std::memory_order_relaxed) >= 0.0 &&
        loopEnd > loopStart + 1.0;
    auto wrap = [&](double frame) {
        if (!looping || frame < loopEnd) return frame;
        const double length = loopEnd - loopStart;
        const double laps = std::floor((frame - loopEnd) / length) + 1.0;
        loopCount += static_cast<std::uint64_t>(laps);
        return loopStart + std::fmod(frame - loopEnd, length);
    };
    if (isPlaying && looping) playhead = wrap(playhead);
    if (isPlaying && !looping && playhead >= totalFrames && totalFrames > 0.0) {
        playhead = totalFrames;
        playing.store(false);
        isPlayingLocal = false;
        displayOffsetFrames = 0.0;
    }

    if (isPlaying && !bank->tracks.empty()) {
        // El montaje se lee UNA vez por bloque: es inmutable mientras dure.
        const auto map = readClipMap();
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            double pos = playhead + static_cast<double>(i) * r;
            if (looping && pos >= loopEnd)
                pos = loopStart + std::fmod(pos - loopEnd, loopEnd - loopStart);
            if (pos < 0.0) continue;

            float l = 0.0f, rr = 0.0f;
            for (std::size_t index = 0; index < bank->tracks.size(); ++index) {
                const auto& t = bank->tracks[index];
                // Sin montaje, la pista suena entera y donde esta. Con el, se
                // busca que trozo cae aqui: fuera de sus clips no suena.
                double read = pos;
                float clipGain = 1.0f;
                if (map && index < map->tracks.size() &&
                    !map->tracks[index].empty() &&
                    !locate(map->tracks[index], pos, &read, &clipGain)) continue;
                if (read < 0.0) continue;
                const ma_uint64 i0 = static_cast<ma_uint64>(read);
                const float fr = static_cast<float>(read - static_cast<double>(i0));
                if (i0 + 1 >= t->frames) continue;
                const short* a = &t->pcm[static_cast<size_t>(i0 * kChannels)];
                const short* b = &t->pcm[static_cast<size_t>((i0 + 1) * kChannels)];
                const float g = t->gain.load(std::memory_order_relaxed) *
                                clipGain / 32768.0f;
                float pl = 1.0f, pr = 1.0f;
                panGains(t->pan.load(std::memory_order_relaxed), pl, pr);
                l  += (a[0] + (b[0] - a[0]) * fr) * g * pl;
                rr += (a[1] + (b[1] - a[1]) * fr) * g * pr;
            }
            out[i * kChannels + 0] = l * mg;
            out[i * kChannels + 1] = rr * mg;
        }
        playhead += static_cast<double>(frameCount) * r;
        if (looping) playhead = wrap(playhead);
        if (!looping && totalFrames > 0.0 && playhead >= totalFrames) {
            playhead = totalFrames;
            playing.store(false);
            isPlayingLocal = false;
            displayOffsetFrames = 0.0;
        }
    }

    const std::uint32_t s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_release);
    snapPlayheadFrames.store(playhead, std::memory_order_relaxed);
    snapRate.store(r, std::memory_order_relaxed);
    snapHostTimeNs.store(nowNs(), std::memory_order_relaxed);
    snapFramesThisCb.store(frameCount, std::memory_order_relaxed);
    snapPlaying.store(isPlaying, std::memory_order_relaxed);
    snapAppliedSeekGen.store(appliedSeekGen, std::memory_order_relaxed);
    snapDisplayOffsetFrames.store(displayOffsetFrames, std::memory_order_relaxed);
    snapLoopCount.store(loopCount, std::memory_order_relaxed);
    seq.store(s + 2, std::memory_order_release);
}

// -----------------------------------------------------------------------------
AudioEngine::AudioEngine() : m_impl(new Impl) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(std::string* error) {
    ma_device_config cfg  = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.sampleRate        = kSampleRate;
    cfg.dataCallback      = &Impl::dataCallback;
    cfg.pUserData         = m_impl.get();

    if (ma_device_init(nullptr, &cfg, &m_impl->device) != MA_SUCCESS) {
        if (error) *error = "no se pudo abrir el dispositivo de audio";
        return false;
    }
    m_impl->latencyFrames = static_cast<double>(m_impl->device.playback.internalPeriodSizeInFrames) *
                            m_impl->device.playback.internalPeriods;
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        ma_device_uninit(&m_impl->device);
        if (error) *error = "no se pudo arrancar el dispositivo de audio";
        return false;
    }
    m_impl->deviceOk = true;
    return true;
}

void AudioEngine::shutdown() {
    if (!m_impl) return;
    m_impl->cancelTrackLoad();
    if (m_impl->deviceOk) {
        m_impl->playing.store(false);
        ma_device_uninit(&m_impl->device);
        m_impl->deviceOk = false;
    }
    m_impl->publishTrackBank(std::make_shared<TrackBank>());
}

bool AudioEngine::ready() const { return m_impl && m_impl->deviceOk; }

void AudioEngine::clearTracks() {
    m_impl->playing.store(false);
    m_impl->cancelTrackLoad();
    m_impl->publishTrackBank(std::make_shared<TrackBank>());
    m_impl->seekTarget.store(0.0);
    m_impl->seekLogicalMs.store(0.0);
    m_impl->seekDisplayOffset.store(0.0);
    m_impl->seekGen.fetch_add(1);
    m_impl->lastSmoothMs = -1e9;
}

int AudioEngine::loadTracks(const std::vector<std::string>& paths, std::string* error) {
    clearTracks();
    TrackDecodeResult result = decodeTrackBank(paths);
    if (error && error->empty()) *error = result.error;
    m_impl->publishTrackBank(std::move(result.bank));
    return result.loaded;
}

void AudioEngine::loadTracksAsync(const std::vector<std::string>& paths) {
    clearTracks();
    if (!paths.empty()) m_impl->queueTrackLoad(paths);
}

bool AudioEngine::pollTrackLoad(int* loaded, std::string* error) {
    return m_impl->pollTrackLoad(loaded, error);
}

bool AudioEngine::trackLoadPending() { return m_impl->trackLoadPending(); }

size_t AudioEngine::trackCount() const { return m_impl->readTrackBank()->tracks.size(); }

std::string AudioEngine::trackName(size_t i) const {
    const auto bank = m_impl->readTrackBank();
    return i < bank->tracks.size() ? bank->tracks[i]->name : std::string();
}

void AudioEngine::setTrackGain(size_t i, float g) {
    const auto bank = m_impl->readTrackBank();
    if (i < bank->tracks.size()) bank->tracks[i]->gain.store(g);
}

float AudioEngine::trackGain(size_t i) const {
    const auto bank = m_impl->readTrackBank();
    return i < bank->tracks.size() ? bank->tracks[i]->gain.load() : 0.0f;
}

void AudioEngine::setTrackPan(size_t i, float pan) {
    const auto bank = m_impl->readTrackBank();
    if (i < bank->tracks.size()) bank->tracks[i]->pan.store(pan);
}

void AudioEngine::setMasterGain(float g) {
    m_impl->master.store(std::max(0.0f, std::min(g, 1.0f)));
}

float AudioEngine::masterGain() const { return m_impl->master.load(); }

double AudioEngine::durationMs() const {
    return static_cast<double>(m_impl->readTrackBank()->longest) / kSampleRate * 1000.0;
}

double AudioEngine::trackDurationMs(int track) const {
    const auto bank = m_impl->readTrackBank();
    if (!bank || track < 0 || track >= static_cast<int>(bank->tracks.size()))
        return 0.0;
    const auto& entry = bank->tracks[static_cast<std::size_t>(track)];
    if (!entry) return 0.0;
    return static_cast<double>(entry->frames) / kSampleRate * 1000.0;
}

size_t AudioEngine::pcmBytes() const {
    const auto bank = m_impl->readTrackBank();
    size_t n = 0;
    for (const auto& t : bank->tracks) n += t->pcm.size() * sizeof(short);
    return n;
}

std::vector<float> AudioEngine::waveformPeaks(size_t columns, int only) const {
    if (columns == 0) return {};
    const auto bank = m_impl->readTrackBank();
    if (!bank || bank->tracks.empty() || bank->longest == 0) return {};
    if (only >= static_cast<int>(bank->tracks.size())) return {};
    std::vector<float> peaks(columns, 0.0f);
    for (size_t index = 0; index < bank->tracks.size(); ++index) {
        if (only >= 0 && static_cast<int>(index) != only) continue;
        const auto& track = bank->tracks[index];
        if (!track || track->frames == 0 || track->pcm.empty()) continue;
        for (size_t column = 0; column < columns; ++column) {
            // El eje es el del banco, no el de la pista: si Voices dura menos
            // que Inst, su onda termina donde termina el audio y no se estira
            // para rellenar el ancho, que es lo que desalineaba las dos capas.
            const ma_uint64 begin = static_cast<ma_uint64>(
                (static_cast<long double>(column) * bank->longest) / columns);
            const ma_uint64 end = std::max<ma_uint64>(begin + 1,
                static_cast<ma_uint64>((static_cast<long double>(column + 1) *
                                        bank->longest) / columns));
            if (begin >= track->frames) break;
            // Muestrear un maximo fijo evita que un audio largo vuelva pesada
            // la ventana avanzada sin perder la silueta general de la onda.
            const ma_uint64 span = std::max<ma_uint64>(1, end - begin);
            const ma_uint64 step = std::max<ma_uint64>(1, span / 256);
            short peak = 0;
            for (ma_uint64 frame = begin; frame < end && frame < track->frames;
                 frame += step) {
                for (int channel = 0; channel < kChannels; ++channel) {
                    const short sample = track->pcm[
                        static_cast<size_t>(frame) * kChannels + channel];
                    const int magnitude = sample == std::numeric_limits<short>::min()
                        ? 32768 : std::abs(static_cast<int>(sample));
                    peak = static_cast<short>(std::max<int>(peak, magnitude > 32767
                        ? 32767 : magnitude));
                }
            }
            peaks[column] = std::max(peaks[column], peak / 32767.0f);
        }
    }
    return peaks;
}

void AudioEngine::setTrackClips(
        const std::vector<std::vector<TrackClip>>& perTrack) {
    bool any = false;
    for (const std::vector<TrackClip>& clips : perTrack)
        if (!clips.empty()) any = true;
    if (!any) { clearTrackClips(); return; }
    auto map = std::make_shared<ClipMap>();
    map->tracks.resize(perTrack.size());
    const double toFrames = static_cast<double>(kSampleRate) / 1000.0;
    for (std::size_t track = 0; track < perTrack.size(); ++track) {
        for (const TrackClip& clip : perTrack[track]) {
            if (clip.endMs <= clip.startMs) continue;
            ClipSegment segment;
            segment.start = static_cast<std::int64_t>(
                std::llround(clip.startMs * toFrames));
            segment.end = static_cast<std::int64_t>(
                std::llround(clip.endMs * toFrames));
            // El desfase es lo unico que hace falta guardar: la fuente se
            // calcula sumandolo a la posicion del documento, asi el bucle del
            // callback no tiene que restar nada por frame.
            segment.delta = static_cast<std::int64_t>(
                std::llround((clip.sourceStartMs - clip.startMs) * toFrames));
            segment.gain = clip.gain;
            map->tracks[track].push_back(segment);
        }
        // Ordenados por inicio: el callback recorre y para en el primero que
        // contiene la posicion.
        std::sort(map->tracks[track].begin(), map->tracks[track].end(),
                  [](const ClipSegment& a, const ClipSegment& b) {
                      return a.start < b.start;
                  });
    }
    m_impl->publishClipMap(std::move(map));
}

void AudioEngine::clearTrackClips() {
    m_impl->publishClipMap(nullptr);
}

int AudioEngine::sampleRate() const { return static_cast<int>(kSampleRate); }
int AudioEngine::channels() const { return static_cast<int>(kChannels); }

std::size_t AudioEngine::renderMix(double fromMs, std::size_t frames,
                                   const std::vector<float>& gains,
                                   std::vector<short>& out,
                                   const std::vector<float>* pans) const {
    out.assign(frames * kChannels, 0);
    if (frames == 0) return 0;
    const auto bank = m_impl->readTrackBank();
    if (!bank || bank->tracks.empty()) return 0;
    // Se acumula en float y se convierte al final. Sumar en i16 recortaria en
    // cada pista en vez de en la mezcla, y dos pistas fuertes sonarian
    // distorsionadas aqui pero limpias en la preview.
    std::vector<float> mixed(frames * kChannels, 0.0f);
    // El mismo montaje que oye la preview: si el video no lo usara, exportar
    // devolveria el audio que el corte quito.
    const auto map = m_impl->readClipMap();
    const std::int64_t first = static_cast<std::int64_t>(
        std::llround(fromMs * static_cast<double>(kSampleRate) / 1000.0));
    std::size_t withContent = 0;
    for (std::size_t index = 0; index < bank->tracks.size(); ++index) {
        const Track* track = bank->tracks[index].get();
        if (!track || track->frames == 0) continue;
        const float gain = index < gains.size() ? gains[index] : 0.0f;
        if (gain <= 0.0f) continue;
        float panLeft = 1.0f, panRight = 1.0f;
        if (pans && index < pans->size()) panGains((*pans)[index], panLeft, panRight);
        const std::vector<ClipSegment>* clips =
            map && index < map->tracks.size() && !map->tracks[index].empty()
                ? &map->tracks[index] : nullptr;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const std::int64_t at = first + static_cast<std::int64_t>(frame);
            std::int64_t source = at;
            float clipGain = 1.0f;
            if (clips) {
                double read = 0.0;
                if (!Impl::locate(*clips, static_cast<double>(at), &read, &clipGain))
                    continue;
                source = static_cast<std::int64_t>(read);
            }
            if (source < 0 || source >= static_cast<std::int64_t>(track->frames))
                continue;
            const short* sample =
                &track->pcm[static_cast<std::size_t>(source) * kChannels];
            for (ma_uint32 channel = 0; channel < kChannels; ++channel)
                mixed[frame * kChannels + channel] +=
                    static_cast<float>(sample[channel]) * gain * clipGain *
                    (channel == 0 ? panLeft : panRight);
            withContent = std::max(withContent, frame + 1);
        }
    }
    const float master = m_impl->master.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < mixed.size(); ++i)
        out[i] = static_cast<short>(std::clamp(mixed[i] * master,
                                               -32768.0f, 32767.0f));
    return withContent;
}

std::vector<float> AudioEngine::spectrumBands(double positionMs, size_t bands,
                                              int only) const {
    if (bands == 0) return {};
    bands = std::min<std::size_t>(bands, 256);
    const auto bank = m_impl->readTrackBank();
    if (!bank || bank->tracks.empty() || bank->longest == 0 ||
        only >= static_cast<int>(bank->tracks.size()))
        return std::vector<float>(bands, 0.0f);

    constexpr std::size_t window = 1024;
    constexpr double pi = 3.14159265358979323846;
    const std::int64_t center = static_cast<std::int64_t>(
        std::max(0.0, positionMs) * static_cast<double>(kSampleRate) / 1000.0);
    const std::int64_t begin = center - static_cast<std::int64_t>(window / 2);
    std::vector<float> result(bands, 0.0f);
    for (std::size_t band = 0; band < bands; ++band) {
        const double t = bands <= 1 ? 0.0
            : static_cast<double>(band) / static_cast<double>(bands - 1);
        // El reparto logaritmico se parece mas a como se leen graves/medios/
        // agudos que N columnas lineales llenas de frecuencias inaudibles.
        const double frequency = 45.0 * std::pow(16000.0 / 45.0, t);
        const double coefficient = 2.0 * std::cos(2.0 * pi * frequency /
                                                  static_cast<double>(kSampleRate));
        double strongest = 0.0;
        for (std::size_t index = 0; index < bank->tracks.size(); ++index) {
            if (only >= 0 && static_cast<int>(index) != only) continue;
            const auto& track = bank->tracks[index];
            if (!track || track->pcm.empty()) continue;
            const double trackGain = std::max(
                0.0, static_cast<double>(track->gain.load(std::memory_order_relaxed)));
            if (trackGain <= 0.0) continue;
            double previous = 0.0, previous2 = 0.0;
            for (std::size_t index = 0; index < window; ++index) {
                const std::int64_t frame = begin + static_cast<std::int64_t>(index);
                double sample = 0.0;
                if (frame >= 0 && static_cast<ma_uint64>(frame) < track->frames) {
                    const std::size_t at = static_cast<std::size_t>(frame) * kChannels;
                    sample = (static_cast<double>(track->pcm[at]) +
                              static_cast<double>(track->pcm[at + 1])) * 0.5;
                }
                const double hann = 0.5 - 0.5 * std::cos(
                    2.0 * pi * static_cast<double>(index) /
                    static_cast<double>(window - 1));
                const double current = sample * hann + coefficient * previous - previous2;
                previous2 = previous;
                previous = current;
            }
            const double power = std::max(0.0,
                previous2 * previous2 + previous * previous -
                coefficient * previous * previous2);
            strongest = std::max(strongest, trackGain * std::sqrt(power) /
                (static_cast<double>(window) * 32768.0));
        }
        // Compresion logaritmica: un visualizador necesita movimiento visible
        // en pasajes suaves sin que un golpe fuerte sature todas las barras.
        result[band] = static_cast<float>(std::clamp(
            std::log1p(strongest * 40.0) / std::log(41.0), 0.0, 1.0));
    }
    return result;
}

std::vector<float> AudioEngine::waveformWindow(double centerMs, double spanMs,
                                               size_t columns, int only) const {
    if (columns == 0) return {};
    columns = std::min<std::size_t>(columns, 4096);
    const auto bank = m_impl->readTrackBank();
    if (!bank || bank->tracks.empty() || bank->longest == 0 ||
        only >= static_cast<int>(bank->tracks.size()))
        return std::vector<float>(columns, 0.0f);

    const double span = std::max(1.0, spanMs);
    const double firstMs = centerMs - span * 0.5;
    std::vector<float> result(columns, 0.0f);
    for (std::size_t column = 0; column < columns; ++column) {
        const double fromMs = firstMs + span * static_cast<double>(column) /
                                        static_cast<double>(columns);
        const double toMs = firstMs + span * static_cast<double>(column + 1) /
                                      static_cast<double>(columns);
        const std::int64_t begin = static_cast<std::int64_t>(
            fromMs * static_cast<double>(kSampleRate) / 1000.0);
        const std::int64_t end = std::max<std::int64_t>(begin + 1,
            static_cast<std::int64_t>(toMs * static_cast<double>(kSampleRate) / 1000.0));
        // Se conserva el signo del pico dominante: una onda simetrica alrededor
        // de cero es lo que distingue un osciloscopio de un grafico de barras.
        int dominant = 0;
        for (std::size_t index = 0; index < bank->tracks.size(); ++index) {
            if (only >= 0 && static_cast<int>(index) != only) continue;
            const auto& track = bank->tracks[index];
            if (!track || track->pcm.empty()) continue;
            const float gain = std::max(
                0.0f, track->gain.load(std::memory_order_relaxed));
            if (gain <= 0.0f) continue;
            const std::int64_t step = std::max<std::int64_t>(1, (end - begin) / 32);
            for (std::int64_t frame = begin; frame < end; frame += step) {
                if (frame < 0 || static_cast<ma_uint64>(frame) >= track->frames) continue;
                const std::size_t at = static_cast<std::size_t>(frame) * kChannels;
                const int sample = static_cast<int>(((
                    static_cast<int>(track->pcm[at]) +
                    static_cast<int>(track->pcm[at + 1])) * 0.5f) * gain);
                if (std::abs(sample) > std::abs(dominant)) dominant = sample;
            }
        }
        result[column] = std::clamp(dominant / 32767.0f, -1.0f, 1.0f);
    }
    return result;
}

void AudioEngine::play() {
    // Dar al play al final rebobina, en vez de no hacer nada.
    if (durationMs() > 0.0 && positionMs() >= durationMs() - 1.0) seekMs(0.0);
    m_impl->playing.store(true);
}
void AudioEngine::pause() { m_impl->playing.store(false); }
bool AudioEngine::playing() const { return m_impl->playing.load(); }

void AudioEngine::setRate(float r) { m_impl->rateAtomic.store(std::max(0.05f, r)); }
float AudioEngine::rate() const { return m_impl->rateAtomic.load(); }

void AudioEngine::seekMs(double ms) {
    const double dur = durationMs();
    if (ms < 0.0) ms = 0.0;
    if (dur > 0.0 && ms > dur) ms = dur;
    const bool p = m_impl->playing.load();
    const double offset = m_impl->latencyNow(p);
    const double frames = ms / 1000.0 * kSampleRate + offset;
    m_impl->seekLogicalMs.store(ms, std::memory_order_relaxed);
    m_impl->seekDisplayOffset.store(offset, std::memory_order_relaxed);
    m_impl->seekTarget.store(frames, std::memory_order_relaxed);
    m_impl->seekGen.fetch_add(1, std::memory_order_release);
    m_impl->lastSmoothMs = -1e9;
}

double AudioEngine::positionMs() const {
    const ClockSnapshot s = m_impl->read();
    const std::uint64_t req = m_impl->seekGen.load(std::memory_order_acquire);

    // Seek pendiente: el snapshot esta obsoleto. Se devuelve el destino para que
    // el editor responda al instante y para no re-disparar el mismo salto.
    if (s.appliedSeekGen != req) {
        const double ms = m_impl->seekLogicalMs.load(std::memory_order_relaxed);
        m_impl->lastSmoothMs = ms;
        return ms;
    }
    if (s.appliedSeekGen != m_impl->lastSeenGen) {
        m_impl->lastSeenGen  = s.appliedSeekGen;
        m_impl->lastSmoothMs = -1e9;
    }

    double base = s.playheadFrames - s.displayOffsetFrames;
    if (s.playing) {
        const double elapsed = static_cast<double>(nowNs() - s.hostTimeNs) / 1e9;
        base += elapsed * s.rate * kSampleRate;
        const double ceiling = s.playheadFrames - m_impl->latencyFrames +
                               static_cast<double>(s.framesThisCb) * s.rate;
        base = std::min(base, ceiling);
    }
    double ms = base / kSampleRate * 1000.0;
    const double dur = durationMs();
    const double loopStart = m_impl->loopA.load(std::memory_order_relaxed);
    const double loopEnd = std::min(m_impl->loopB.load(std::memory_order_relaxed), dur);
    if (loopStart >= 0.0 && loopEnd > loopStart && s.playing) {
        const double length = loopEnd - loopStart;
        if (ms >= loopEnd || (s.loopCount > 0 && ms < loopStart)) {
            ms = loopStart + std::fmod(std::fmod(ms - loopStart, length) + length, length);
        }
        m_impl->lastSmoothMs = ms;
        return std::clamp(ms, 0.0, dur);
    }
    if (!std::isfinite(ms) || ms < 0.0) ms = 0.0;
    if (dur > 0.0 && ms > dur) ms = dur;
    if (ms < m_impl->lastSmoothMs) ms = m_impl->lastSmoothMs;
    if (dur > 0.0 && ms > dur) ms = dur;
    m_impl->lastSmoothMs = ms;
    return ms;
}

void AudioEngine::stepFrames(int frames, float fps) {
    if (fps <= 0.0f) fps = 60.0f;
    seekMs(positionMs() + frames * 1000.0 / fps);
}

void AudioEngine::setLoop(double aMs, double bMs) {
    m_impl->loopA.store(aMs);
    m_impl->loopB.store(bMs);
    m_impl->lastSmoothMs = -1e9;
}
void AudioEngine::clearLoop() {
    m_impl->loopA.store(-1.0);
    m_impl->loopB.store(-1.0);
    m_impl->lastSmoothMs = -1e9;
}
bool AudioEngine::hasLoop() const { return m_impl->loopA.load() >= 0.0; }

}  // namespace fml
