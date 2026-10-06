// Note Lab — el editor de bloques (BlockCanvas.hpp).
#include "BlockCanvas.hpp"

#include "../core/NoteProject.hpp"
#include "Theme.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace nlblocks {
using namespace fml;
using namespace fml::notelab;
namespace ui = nlui;

// ---------------------------------------------------------------- sonidos --
//
// Un sintetizador pequeno, sin archivos: tonos con FM (campanas y golpes de
// madera), ruido filtrado (clics y barridos), glissandos y un poco de sala.
// Cada sonido se calcula una vez y se reparte entre varias voces que SDL
// mezcla (la musica va por miniaudio, aparte), asi que uno no corta al otro.

namespace {

constexpr int kRate = 48000;
constexpr float kTwoPi = 6.28318531f;

// La senal en mono mientras se compone; luego se le pone la sala en estereo.
struct Mix {
    std::vector<float> dry;
    size_t at(float seconds) const { return static_cast<size_t>(std::max(0.0f, seconds) * kRate); }
    void fit(size_t n) {
        if (dry.size() < n) dry.resize(n, 0.0f);
    }
};

float envelope(float t, float attack, float decay) {
    return t < attack ? t / attack : std::exp(-(t - attack) / decay);
}

// Un tono con glissando exponencial de `from` a `to` Hz.
void glide(Mix& m, float start, float seconds, float from, float to, float attack, float decay, float gain) {
    const size_t first = m.at(start), count = m.at(seconds);
    m.fit(first + count);
    double phase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float hz = from * std::pow(to / from, t / seconds);
        phase += kTwoPi * hz / kRate;
        m.dry[first + i] += std::sin(static_cast<float>(phase)) * envelope(t, attack, decay) * gain;
    }
}

// FM: una portadora modulada por otra `ratio` veces mas rapida; el indice cae
// con el tiempo, asi que empieza brillante (campana, madera) y se aclara.
void bell(Mix& m, float start, float seconds, float hz, float ratio, float index, float attack, float decay, float gain) {
    const size_t first = m.at(start), count = m.at(seconds);
    m.fit(first + count);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float modulation = index * std::exp(-t / (decay * 0.5f)) * std::sin(kTwoPi * hz * ratio * t);
        m.dry[first + i] += std::sin(kTwoPi * hz * t + modulation) * envelope(t, attack, decay) * gain;
    }
}

// Una cuerda punteada (Karplus-Strong ampliado), como el arpa de los .wav del
// tutorial: un pulso de ruido filtrado que da vueltas por un retardo de un
// periodo, con perdidas (`decay` s) y un paso todo de afinacion fina.
void pluck(Mix& m, float start, float seconds, float hz, float gain, float bright, float decay) {
    const size_t first = m.at(start), total = m.at(seconds);
    m.fit(first + total);
    const float period = static_cast<float>(kRate) / hz - 0.5f;
    const int n = std::max(2, static_cast<int>(period));
    const float frac = period - static_cast<float>(n);
    std::vector<float> buf(total, 0.0f);
    std::uint32_t seed = 0x2545F491u ^ static_cast<std::uint32_t>(hz * 977.0f);
    const float a = 1.0f - std::exp(-kTwoPi * (900.0f + 7000.0f * bright) / kRate);
    std::vector<float> excitation(static_cast<size_t>(n));
    float low = 0.0f, mean = 0.0f;
    for (int i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        low += a * ((static_cast<float>(seed >> 8) / 8388608.0f - 1.0f) - low);
        excitation[static_cast<size_t>(i)] = low;
    }
    const int pick = std::max(1, static_cast<int>(0.2f * n));
    std::vector<float> combed(excitation.size());
    for (int i = 0; i < n; ++i) combed[static_cast<size_t>(i)] = excitation[static_cast<size_t>(i)] - excitation[static_cast<size_t>((i - pick + n) % n)];
    for (float v : combed) mean += v / static_cast<float>(n);
    for (int i = 0; i < n && static_cast<size_t>(i) < total; ++i) buf[static_cast<size_t>(i)] = combed[static_cast<size_t>(i)] - mean;
    const float loss = std::exp(-1.0f / (decay * hz));
    const float c = (1.0f - frac) / (1.0f + frac);
    float x1 = 0.0f, y1 = 0.0f, peak = 1e-9f;
    for (size_t i = static_cast<size_t>(n); i < total; ++i) {
        const float x = loss * 0.5f * (buf[i - static_cast<size_t>(n)] + buf[i - static_cast<size_t>(n) - 1]);
        const float y = c * x + x1 - c * y1;
        x1 = x;
        y1 = y;
        buf[i] = y;
    }
    for (float v : buf) peak = std::max(peak, std::fabs(v));
    const size_t fade = std::min(total, static_cast<size_t>(0.06f * kRate));
    for (size_t i = 0; i < total; ++i) {
        float v = buf[i] / peak * gain;
        if (i + fade >= total) v *= 0.5f * (1.0f + std::cos(3.14159265f * static_cast<float>(i + fade - total) / static_cast<float>(fade)));
        m.dry[first + i] += v;
    }
}

// Ruido por un paso banda (filtro de estado variable) que puede barrer de
// `from` a `to` Hz: clics, golpes, soplidos.
void hiss(Mix& m, float start, float seconds, float from, float to, float q, float attack, float decay, float gain) {
    const size_t first = m.at(start), count = m.at(seconds);
    m.fit(first + count);
    std::uint32_t seed = 0x9E3779B9u ^ static_cast<std::uint32_t>(first * 2654435761u + count);
    float low = 0.0f, band = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kRate;
        seed = seed * 1664525u + 1013904223u;
        const float white = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
        const float hz = from * std::pow(to / from, t / seconds);
        const float f = 2.0f * std::sin(3.14159265f * std::min(hz, 18000.0f) / kRate);
        low += f * band;
        const float high = white - low - band / q;
        band += f * high;
        m.dry[first + i] += band * envelope(t, attack, decay) * gain;
    }
}

// Una sala pequena (Schroeder: cuatro peines y dos pasatodo), algo distinta
// en cada oido, y un limitador suave. Devuelve estereo entrelazado.
std::vector<float> room(const Mix& m, float wet) {
    const size_t tail = static_cast<size_t>(0.35f * kRate);
    const size_t n = m.dry.size() + tail;
    std::vector<float> out(n * 2, 0.0f);
    for (int channel = 0; channel < 2; ++channel) {
        const int spread = channel == 0 ? 0 : 23;
        const int combs[4] = {1427 + spread, 1781 + spread, 1973 + spread, 2099 + spread};
        std::vector<float> wetSignal(n, 0.0f);
        for (int length : combs) {
            std::vector<float> buffer(static_cast<size_t>(length), 0.0f);
            size_t index = 0;
            float damp = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                const float input = i < m.dry.size() ? m.dry[i] : 0.0f;
                const float delayed = buffer[index];
                damp = delayed * 0.6f + damp * 0.4f;
                buffer[index] = input + damp * 0.74f;
                index = (index + 1) % buffer.size();
                wetSignal[i] += delayed * 0.25f;
            }
        }
        for (int length : {241 + spread, 82 + spread / 2}) {
            std::vector<float> buffer(static_cast<size_t>(length), 0.0f);
            size_t index = 0;
            for (size_t i = 0; i < n; ++i) {
                const float delayed = buffer[index];
                const float input = wetSignal[i];
                buffer[index] = input + delayed * 0.5f;
                wetSignal[i] = delayed - input * 0.5f;
                index = (index + 1) % buffer.size();
            }
        }
        for (size_t i = 0; i < n; ++i) {
            const float dry = i < m.dry.size() ? m.dry[i] : 0.0f;
            out[i * 2 + static_cast<size_t>(channel)] = std::tanh((dry + wetSignal[i] * wet) * 1.3f) * 0.62f;
        }
    }
    return out;
}

std::vector<float> compose(Cue cue) {
    Mix m;
    switch (cue) {
        case Cue::Pick:  // coger: un «tup» que sube
            glide(m, 0.0f, 0.05f, 520.0f, 900.0f, 0.001f, 0.012f, 0.30f);
            hiss(m, 0.0f, 0.02f, 3200.0f, 3200.0f, 3.0f, 0.0005f, 0.004f, 0.10f);
            break;
        case Cue::Snap:  // encajar: el «clac» de dos piezas de madera y un cierre
            hiss(m, 0.0f, 0.03f, 1800.0f, 1500.0f, 4.0f, 0.0003f, 0.005f, 0.55f);
            bell(m, 0.0f, 0.08f, 1320.0f, 1.5f, 2.0f, 0.0008f, 0.022f, 0.24f);
            glide(m, 0.0f, 0.08f, 240.0f, 200.0f, 0.001f, 0.028f, 0.16f);
            hiss(m, 0.016f, 0.02f, 2600.0f, 2600.0f, 5.0f, 0.0003f, 0.003f, 0.30f);
            break;
        case Cue::Drop:  // soltar suelto: un golpe de fieltro
            glide(m, 0.0f, 0.12f, 210.0f, 130.0f, 0.002f, 0.032f, 0.38f);
            hiss(m, 0.0f, 0.04f, 700.0f, 500.0f, 2.0f, 0.001f, 0.012f, 0.10f);
            break;
        case Cue::Delete:  // borrar: un soplido que baja y un golpe en la papelera
            hiss(m, 0.0f, 0.24f, 3600.0f, 280.0f, 1.6f, 0.01f, 0.09f, 0.40f);
            glide(m, 0.0f, 0.2f, 720.0f, 170.0f, 0.004f, 0.07f, 0.16f);
            glide(m, 0.19f, 0.09f, 130.0f, 90.0f, 0.001f, 0.028f, 0.30f);
            hiss(m, 0.19f, 0.03f, 900.0f, 600.0f, 2.5f, 0.0005f, 0.008f, 0.14f);
            break;
        case Cue::Preset: {  // anadir un preset: un arpegio de campanas
            const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
            for (int i = 0; i < 4; ++i) bell(m, 0.07f * static_cast<float>(i), 0.5f, notes[i], 3.5f, 1.2f, 0.002f, 0.16f, 0.15f);
            glide(m, 0.0f, 0.45f, 261.63f, 261.63f, 0.01f, 0.18f, 0.06f);
            break;
        }
        case Cue::Undo:  // deshacer: dos notas que bajan
            bell(m, 0.0f, 0.18f, 783.99f, 2.0f, 0.9f, 0.002f, 0.06f, 0.20f);
            bell(m, 0.065f, 0.22f, 587.33f, 2.0f, 0.9f, 0.002f, 0.08f, 0.20f);
            break;
        case Cue::Redo:  // rehacer: las mismas, subiendo
            bell(m, 0.0f, 0.18f, 587.33f, 2.0f, 0.9f, 0.002f, 0.06f, 0.20f);
            bell(m, 0.065f, 0.22f, 783.99f, 2.0f, 0.9f, 0.002f, 0.08f, 0.20f);
            break;
        case Cue::Edit:  // escribir un valor: una tecla de maquina
            hiss(m, 0.0f, 0.02f, 4200.0f, 3800.0f, 3.5f, 0.0003f, 0.0028f, 0.40f);
            glide(m, 0.0f, 0.03f, 1800.0f, 1700.0f, 0.0005f, 0.006f, 0.08f);
            break;
        case Cue::Choose:  // elegir en un desplegable: un «plop» que sube
            glide(m, 0.0f, 0.06f, 480.0f, 960.0f, 0.001f, 0.014f, 0.30f);
            bell(m, 0.0f, 0.06f, 960.0f, 2.0f, 0.6f, 0.001f, 0.02f, 0.08f);
            break;
        case Cue::Duplicate:  // duplicar o pegar: dos destellos agudos
            bell(m, 0.0f, 0.12f, 1174.66f, 2.0f, 1.0f, 0.001f, 0.03f, 0.18f);
            bell(m, 0.055f, 0.14f, 1396.91f, 2.0f, 1.0f, 0.001f, 0.035f, 0.18f);
            break;
        case Cue::Tidy:  // ordenar: piezas que se colocan y un acorde
            for (int i = 0; i < 5; ++i)
                hiss(m, 0.035f * static_cast<float>(i), 0.02f, 2200.0f + 250.0f * static_cast<float>(i), 2400.0f + 250.0f * static_cast<float>(i),
                     4.0f, 0.0004f, 0.004f, 0.26f - 0.03f * static_cast<float>(i));
            bell(m, 0.19f, 0.35f, 783.99f, 2.0f, 0.7f, 0.004f, 0.12f, 0.08f);
            bell(m, 0.19f, 0.35f, 987.77f, 2.0f, 0.7f, 0.004f, 0.12f, 0.08f);
            break;
        case Cue::ZoomIn:  // acercar: un soplido que sube
            hiss(m, 0.0f, 0.14f, 700.0f, 3200.0f, 1.8f, 0.05f, 0.05f, 0.20f);
            break;
        case Cue::ZoomOut:  // alejar: el mismo, bajando
            hiss(m, 0.0f, 0.14f, 3200.0f, 700.0f, 1.8f, 0.05f, 0.05f, 0.20f);
            break;
        case Cue::Click:  // un clic de interfaz
            hiss(m, 0.0f, 0.015f, 2600.0f, 2600.0f, 4.0f, 0.0003f, 0.002f, 0.30f);
            glide(m, 0.0f, 0.02f, 1000.0f, 1000.0f, 0.0005f, 0.004f, 0.05f);
            break;
        case Cue::Success: {  // tipo nuevo o exportacion lista: un acorde mayor
            const float chord[3] = {523.25f, 659.25f, 783.99f};
            for (float hz : chord) bell(m, 0.0f, 0.6f, hz, 2.0f, 1.0f, 0.003f, 0.22f, 0.12f);
            bell(m, 0.09f, 0.6f, 1046.5f, 3.0f, 1.0f, 0.003f, 0.2f, 0.10f);
            break;
        }
        case Cue::Error:  // algo salio mal: dos notas graves
            glide(m, 0.0f, 0.14f, 330.0f, 300.0f, 0.003f, 0.06f, 0.26f);
            glide(m, 0.11f, 0.18f, 262.0f, 230.0f, 0.003f, 0.08f, 0.26f);
            break;
        case Cue::Add:  // anadir un bloque o un evento: un «plip»
            bell(m, 0.0f, 0.1f, 880.0f, 2.0f, 1.5f, 0.001f, 0.035f, 0.22f);
            bell(m, 0.03f, 0.1f, 1318.51f, 2.0f, 1.0f, 0.001f, 0.03f, 0.10f);
            break;
    }
    std::vector<float> out = room(m, cue == Cue::Preset || cue == Cue::Success || cue == Cue::Tidy ? 0.28f : 0.16f);
    // Todos a un pico parecido: los de accion un poco mas presentes que los clics.
    float target = 0.32f;
    switch (cue) {
        case Cue::Snap: target = 0.45f; break;
        case Cue::Delete: target = 0.40f; break;
        case Cue::Preset:
        case Cue::Success: target = 0.36f; break;
        case Cue::Click:
        case Cue::Edit: target = 0.24f; break;
        case Cue::ZoomIn:
        case Cue::ZoomOut: target = 0.20f; break;
        default: break;
    }
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f)
        for (float& v : out) v *= target / peak;
    return out;
}

// Los del tutorial, si faltan sus .wav: el mismo arpa, en pequeno.
std::vector<float> composeTutorial(TutorialCue cue) {
    Mix m;
    float target = 0.24f;
    switch (cue) {
        case TutorialCue::Mission:  // mision cumplida: dos cuerdas que suben una quinta (sol, re)
            pluck(m, 0.0f, 2.0f, 392.0f, 0.85f, 0.45f, 1.4f);
            pluck(m, 0.09f, 2.0f, 587.33f, 0.8f, 0.5f, 1.3f);
            pluck(m, 0.095f, 1.2f, 1174.66f, 0.18f, 0.6f, 0.8f);
            break;
        case TutorialCue::Finished: {  // tutorial terminado: una subida de arpa en do mayor que cae en sol y do agudos, con el bajo
            const float notes[5] = {261.63f, 329.63f, 392.0f, 523.25f, 659.25f};
            float at = 0.0f;
            for (int i = 0; i < 5; ++i) {
                const float k = static_cast<float>(i);
                pluck(m, at, 2.4f - 0.1f * k, notes[i], 0.7f, 0.38f + 0.04f * k, 1.7f - 0.1f * k);
                at += 0.07f - 0.006f * k;   // se acelera un poco
            }
            pluck(m, at, 2.2f, 783.99f, 0.6f, 0.5f, 1.3f);
            pluck(m, at + 0.014f, 2.2f, 1046.5f, 0.85f, 0.52f, 1.2f);
            pluck(m, at, 2.6f, 130.81f, 0.5f, 0.3f, 2.2f);
            target = 0.3f;
            break;
        }
        case TutorialCue::Offer:  // hay un tutorial: una cuerda suave
            pluck(m, 0.0f, 1.3f, 659.25f, 0.7f, 0.3f, 0.9f);
            target = 0.16f;
            break;
    }
    std::vector<float> out = room(m, 0.22f);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f)
        for (float& v : out) v *= target / peak;
    return out;
}

// Varias voces sobre un mismo dispositivo: SDL las mezcla.
struct Voices {
    SDL_AudioDeviceID device = 0;
    std::vector<SDL_AudioStream*> streams;
    bool failed = false;
};

Voices& voices() {
    static Voices v;
    if (v.device || v.failed) return v;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        v.failed = true;
        return v;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
    v.device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!v.device) {
        v.failed = true;
        return v;
    }
    for (int i = 0; i < 6; ++i)
        if (SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec)) {
            if (SDL_BindAudioStream(v.device, stream)) v.streams.push_back(stream);
            else SDL_DestroyAudioStream(stream);
        }
    if (v.streams.empty()) v.failed = true;
    else SDL_ResumeAudioDevice(v.device);
    return v;
}

}  // namespace

bool writeCues(const std::string& folder) {
    const char* names[] = {"pick", "snap", "drop", "delete", "preset", "undo", "redo", "edit", "choose",
                           "duplicate", "tidy", "zoomin", "zoomout", "click", "success", "error", "add"};
    bool ok = true;
    // Los del editor y, detras, los tres del tutorial.
    const char* tutorialNames[] = {"tutorial-mission", "tutorial-finished", "tutorial-offer"};
    for (int i = 0; i < 20; ++i) {
        const std::vector<float> samples = i < 17 ? compose(static_cast<Cue>(i)) : composeTutorial(static_cast<TutorialCue>(i - 17));
        const char* name = i < 17 ? names[i] : tutorialNames[i - 17];
        float peak = 0.0f;
        double energy = 0.0;
        for (float v : samples) {
            peak = std::max(peak, std::fabs(v));
            energy += static_cast<double>(v) * v;
        }
        const double rms = samples.empty() ? 0.0 : std::sqrt(energy / static_cast<double>(samples.size()));
        std::printf("%-18s %5.3f s  peak %.2f  rms %.3f\n", name, static_cast<double>(samples.size()) / 2.0 / kRate, peak, rms);
        const std::string path = folder + "/" + name + ".wav";
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) {
            ok = false;
            continue;
        }
        auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, file); };
        auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, file); };
        const std::uint32_t bytes = static_cast<std::uint32_t>(samples.size() * 2);
        std::fwrite("RIFF", 1, 4, file);
        u32(36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, file);
        u32(16);
        u16(1);
        u16(2);
        u32(kRate);
        u32(kRate * 4);
        u16(4);
        u16(16);
        std::fwrite("data", 1, 4, file);
        u32(bytes);
        for (float v : samples) {
            const std::int16_t sample = static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
            std::fwrite(&sample, 2, 1, file);
        }
        std::fclose(file);
    }
    return ok;
}

void playCue(Cue cue, bool enabled) {
    if (!nlbuild::blockAudioPlayback || !enabled) return;
    // El mismo sonido dos veces seguidas en menos de 40 ms suena a error: se omite.
    static std::map<int, double> last;
    const double now = static_cast<double>(SDL_GetTicksNS()) / 1.0e9;
    double& previous = last[static_cast<int>(cue)];
    if (now - previous < 0.04) return;
    previous = now;
    Voices& v = voices();
    if (v.failed || v.streams.empty()) return;
    static std::map<int, std::vector<float>> cache;
    std::vector<float>& samples = cache[static_cast<int>(cue)];
    if (samples.empty()) samples = compose(cue);
    // La voz libre, o la que menos le queda.
    SDL_AudioStream* chosen = v.streams.front();
    int least = SDL_GetAudioStreamQueued(chosen);
    for (SDL_AudioStream* stream : v.streams) {
        const int queued = SDL_GetAudioStreamQueued(stream);
        if (queued < least) {
            least = queued;
            chosen = stream;
        }
    }
    if (least > 0) SDL_ClearAudioStream(chosen);
    SDL_PutAudioStreamData(chosen, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

// Los del tutorial se entregan como .wav (pedido del autor, 5 oct 2026; de
// los juegos probados quedo el arpa): la carpeta `sounds` junto al .exe (o
// `assets/sounds`). Sin el archivo, el sonido sintetizado de aqui.
std::vector<float> loadTutorialCue(TutorialCue cue) {
    static const char* names[3] = {"tutorial-mission", "tutorial-finished", "tutorial-offer"};
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    for (const char* folder : {"sounds/", "assets/sounds/"}) {
        const std::string path = std::string(base) + folder + names[static_cast<int>(cue)] + ".wav";
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length)) continue;
        const SDL_AudioSpec want{SDL_AUDIO_F32, 2, kRate};
        Uint8* converted = nullptr;
        int convertedLength = 0;
        const bool ok = SDL_ConvertAudioSamples(&spec, data, static_cast<int>(length), &want, &converted, &convertedLength);
        SDL_free(data);
        if (!ok || !converted || convertedLength <= 0) continue;
        const float* first = reinterpret_cast<const float*>(converted);
        std::vector<float> samples(first, first + static_cast<size_t>(convertedLength) / sizeof(float));
        SDL_free(converted);
        // Un archivo raro (muy largo o vacio) no se usa.
        if (samples.empty() || samples.size() > static_cast<size_t>(kRate) * 2u * 8u) continue;
        return samples;
    }
    return {};
}

void playTutorialCue(TutorialCue cue, bool enabled) {
    if (!nlbuild::audioPlayback || !nlbuild::tutorialAudioPlayback || !enabled) return;
    static std::map<int, double> last;
    const double now = static_cast<double>(SDL_GetTicksNS()) / 1.0e9;
    double& previous = last[static_cast<int>(cue)];
    if (now - previous < 0.25) return;   // dos misiones a la vez: una campana basta
    previous = now;
    Voices& v = voices();
    if (v.failed || v.streams.empty()) return;
    static std::map<int, std::vector<float>> cache;
    std::vector<float>& samples = cache[static_cast<int>(cue)];
    if (samples.empty()) samples = loadTutorialCue(cue);
    if (samples.empty()) samples = composeTutorial(cue);
    SDL_AudioStream* chosen = v.streams.front();
    int least = SDL_GetAudioStreamQueued(chosen);
    for (SDL_AudioStream* stream : v.streams) {
        const int queued = SDL_GetAudioStreamQueued(stream);
        if (queued < least) {
            least = queued;
            chosen = stream;
        }
    }
    if (least > 0) SDL_ClearAudioStream(chosen);
    SDL_PutAudioStreamData(chosen, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
}

namespace {

// Medidas en unidades del lienzo (pixeles con zoom 1).
constexpr float kFont = 13.5f;
constexpr float kRow = 34.0f;        // fila de una instruccion
constexpr float kSlotH = 22.0f;      // ranura con un valor escrito
constexpr float kRepH = 24.0f;       // valor redondo o hexagonal
constexpr float kPadX = 10.0f;
constexpr float kGap = 6.0f;
constexpr float kHatCap = 16.0f;     // la joroba de un evento
constexpr float kHatMinW = 124.0f;
constexpr float kArm = 14.0f;        // el brazo de un «si»
constexpr float kBodyMin = 22.0f;
constexpr float kBottom = 18.0f;
constexpr float kElseRow = 26.0f;
constexpr float kNotchX = 14.0f, kNotchW = 16.0f, kNotchD = 4.0f, kRadius = 4.0f;
constexpr float kStmtMinW = 64.0f;
constexpr float kSnapPx = 30.0f;     // distancia para encajar, en pixeles de pantalla
constexpr float kPaletteW = 244.0f;
constexpr float kPaletteScale = 0.82f;
constexpr float kPi = 3.14159265f;

struct Colors {
    ImU32 fill, stroke, dark;
};

// Los colores de Scratch 3 por categoria.
Colors colorsOf(BlockCategory category) {
    switch (category) {
        case BlockCategory::Events: return {IM_COL32(255, 191, 0, 255), IM_COL32(204, 153, 0, 255), IM_COL32(214, 160, 0, 255)};
        case BlockCategory::Control: return {IM_COL32(255, 171, 25, 255), IM_COL32(207, 139, 23, 255), IM_COL32(217, 145, 22, 255)};
        case BlockCategory::Operators: return {IM_COL32(89, 192, 89, 255), IM_COL32(56, 148, 56, 255), IM_COL32(64, 160, 64, 255)};
        case BlockCategory::Sensing: return {IM_COL32(92, 177, 214, 255), IM_COL32(46, 142, 184, 255), IM_COL32(52, 150, 192, 255)};
        case BlockCategory::Properties: return {IM_COL32(255, 140, 26, 255), IM_COL32(219, 110, 0, 255), IM_COL32(222, 112, 6, 255)};
        case BlockCategory::Game: return {IM_COL32(255, 102, 128, 255), IM_COL32(230, 51, 85, 255), IM_COL32(232, 70, 98, 255)};
        case BlockCategory::Animation: return {IM_COL32(153, 102, 255, 255), IM_COL32(119, 77, 203, 255), IM_COL32(124, 82, 212, 255)};
        case BlockCategory::Camera: return {IM_COL32(76, 151, 255, 255), IM_COL32(51, 115, 204, 255), IM_COL32(56, 122, 214, 255)};
        case BlockCategory::Sound: return {IM_COL32(207, 99, 207, 255), IM_COL32(189, 66, 189, 255), IM_COL32(190, 72, 190, 255)};
        case BlockCategory::Variables: return {IM_COL32(235, 133, 61, 255), IM_COL32(191, 93, 32, 255), IM_COL32(205, 106, 38, 255)};
    }
    return {IM_COL32(128, 128, 128, 255), IM_COL32(90, 90, 90, 255), IM_COL32(100, 100, 100, 255)};
}

constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kSlotText = IM_COL32(87, 94, 117, 255);
constexpr ImU32 kSelected = IM_COL32(255, 230, 64, 255);

ImFont* blockFont() { return ui::fonts().semibold; }

float textW(const std::string& text) { return blockFont()->CalcTextSizeA(kFont, FLT_MAX, 0.0f, text.c_str()).x; }

// Un trozo de la frase de un bloque: texto, un argumento ({n}) o el tipo ({type}).
struct Token {
    int arg = -2;          // -2 texto, -1 el tipo, >= 0 argumento
    std::string text;
};

std::vector<Token> tokensOf(const std::string& pattern) {
    std::vector<Token> out;
    std::string run;
    auto flush = [&] {
        while (!run.empty() && run.front() == ' ') run.erase(run.begin());
        while (!run.empty() && run.back() == ' ') run.pop_back();
        if (!run.empty()) out.push_back({-2, run});
        run.clear();
    };
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                flush();
                const std::string name = pattern.substr(i + 1, close - i - 1);
                out.push_back({name == "type" ? -1 : std::atoi(name.c_str()), std::string()});
                i = close;
                continue;
            }
        }
        run += pattern[i];
    }
    flush();
    return out;
}

// Lo necesario para medir: el programa, el idioma y el nombre del tipo.
struct Look {
    const BlockProgram* program = nullptr;
    bool spanish = false;
    std::string typeName;
};

const BlockDef* defOf(const Look& look, int id) {
    const BlockNode* node = look.program->node(id);
    return node ? blockDef(node->key) : nullptr;
}

std::string patternOf(const BlockDef& def, bool spanish) { return spanish ? def.textEs : def.textEn; }

std::string literalText(const Look& look, const BlockDef& def, const BlockNode& node, size_t i) {
    const ArgDef& arg = def.args[i];
    const std::string& value = node.args[i].value;
    if (arg.kind == ArgKind::Choice) return choiceLabel(arg, value, look.spanish);
    if (arg.kind == ArgKind::Sound || arg.kind == ArgKind::Image || arg.kind == ArgKind::Video) {
        if (value.empty()) return look.spanish ? "elegir recurso…" : "pick resource…";
        const size_t slash = value.find_last_of("/\\");
        return slash == std::string::npos ? value : value.substr(slash + 1);
    }
    if (arg.kind == ArgKind::Variable) {
        const auto* variable = referencedVariable(*look.program, value);
        return variable ? variable->args[0].value : std::string(look.spanish ? "elegir variable" : "pick variable");
    }
    return value;
}

bool isValue(const BlockDef* def) { return def && (def->shape == BlockShape::Number || def->shape == BlockShape::Boolean); }
bool isStatement(const BlockDef* def) {
    return def && (def->shape == BlockShape::Statement || def->shape == BlockShape::If || def->shape == BlockShape::IfElse);
}
bool isC(const BlockDef* def) { return def && (def->shape == BlockShape::If || def->shape == BlockShape::IfElse); }

ImVec2 valueSize(const Look& look, int id);

ImVec2 argSize(const Look& look, const BlockNode& node, const BlockDef& def, size_t i) {
    const ArgDef& arg = def.args[i];
    const int plugged = node.args[i].block;
    if (arg.kind != ArgKind::Body && arg.plug && plugged >= 0 && look.program->node(plugged)) return valueSize(look, plugged);
    switch (arg.kind) {
        case ArgKind::Number: return {std::max(28.0f, textW(node.args[i].value) + 16.0f), kSlotH};
        case ArgKind::Text: return {std::max(28.0f, textW(node.args[i].value) + 14.0f), kSlotH};
        case ArgKind::Choice:
        case ArgKind::Variable:
        case ArgKind::Image:
        case ArgKind::Video:
        case ArgKind::Sound: return {textW(literalText(look, def, node, i)) + 30.0f, kSlotH};
        case ArgKind::Color: return {34.0f, kSlotH};
        case ArgKind::Boolean: return {40.0f, kSlotH};
        default: return {0.0f, 0.0f};
    }
}

ImVec2 rowSize(const Look& look, const BlockNode& node, const BlockDef& def, const std::vector<Token>& tokens) {
    float width = 0.0f, height = kFont;
    bool first = true;
    for (const Token& token : tokens) {
        float w = 0.0f, h = kFont;
        if (token.arg == -2) w = textW(token.text);
        else if (token.arg == -1) {
            w = textW(look.typeName) + 16.0f;
            h = kSlotH;
        } else if (token.arg < static_cast<int>(def.args.size()) && def.args[static_cast<size_t>(token.arg)].kind != ArgKind::Body) {
            const ImVec2 size = argSize(look, node, def, static_cast<size_t>(token.arg));
            w = size.x;
            h = size.y;
        } else {
            continue;
        }
        width += (first ? 0.0f : kGap) + w;
        height = std::max(height, h);
        first = false;
    }
    return {width, height};
}

ImVec2 valueSize(const Look& look, int id) {
    const BlockNode* node = look.program->node(id);
    const BlockDef* def = node ? blockDef(node->key) : nullptr;
    if (!def) return {40.0f, kRepH};
    const ImVec2 row = rowSize(look, *node, *def, tokensOf(patternOf(*def, look.spanish)));
    const float h = std::max(kRepH, row.y + 4.0f);
    if (def->shape == BlockShape::Boolean) return {std::max(48.0f, row.x + h + 8.0f), h};
    return {std::max(40.0f, row.x + h), h};
}

// El tamano de un bloque de pila: ancho de su fila, alto total (con cuerpos),
// alto de la fila y alto de cada cuerpo.
struct Size {
    float w = 0.0f, h = 0.0f, row = kRow, b1 = 0.0f, b2 = 0.0f;
};

float chainHeight(const Look& look, int first);

Size blockSize(const Look& look, int id) {
    Size size;
    const BlockNode* node = look.program->node(id);
    const BlockDef* def = node ? blockDef(node->key) : nullptr;
    if (!def) return size;
    if (isValue(def)) {
        const ImVec2 v = valueSize(look, id);
        size.w = v.x;
        size.h = v.y;
        size.row = v.y;
        return size;
    }
    const ImVec2 row = rowSize(look, *node, *def, tokensOf(patternOf(*def, look.spanish)));
    size.row = std::max(kRow, row.y + 12.0f);
    size.w = row.x + 2.0f * kPadX;
    if (def->shape == BlockShape::Hat) {
        size.w = std::max(kHatMinW, size.w);
        size.h = kHatCap + size.row;
    } else if (isC(def)) {
        size.w = std::max(size.w, kArm + 96.0f);
        size.b1 = std::max(kBodyMin, chainHeight(look, node->args.size() > 1 ? node->args[1].block : -1));
        size.h = size.row + size.b1 + kBottom;
        if (def->shape == BlockShape::IfElse) {
            size.b2 = std::max(kBodyMin, chainHeight(look, node->args.size() > 2 ? node->args[2].block : -1));
            size.h += kElseRow + size.b2;
            size.w = std::max(size.w, textW(look.spanish ? def->elseEs : def->elseEn) + 2.0f * kPadX + 20.0f);
        }
    } else {
        size.w = std::max(kStmtMinW, size.w);
        size.h = size.row;
    }
    return size;
}

float chainHeight(const Look& look, int first) {
    float h = 0.0f;
    for (int id : chainOf(*look.program, first)) h += blockSize(look, id).h;
    return h;
}

float chainWidth(const Look& look, int first) {
    float w = 0.0f;
    for (int id : chainOf(*look.program, first)) {
        const Size size = blockSize(look, id);
        w = std::max(w, size.w);
        const BlockNode* node = look.program->node(id);
        if (isC(defOf(look, id)))
            for (size_t arg = 1; arg < node->args.size(); ++arg) w = std::max(w, kArm + chainWidth(look, node->args[arg].block));
    }
    return w;
}

// --------------------------------------------------------------- dibujo --

enum HitKind { kHitBlock = 0, kHitLiteral, kHitChoice, kHitColor, kHitSound, kHitSlot, kHitBadge };
struct Hit {
    ImVec2 min, max;       // pantalla
    int id = -1, arg = -1, kind = kHitBlock;
};

enum ConnKind { kConnNext = 0, kConnBody, kConnAbove, kConnNumber, kConnBoolean };
struct Conn {
    ImVec2 at;             // lienzo
    ImVec2 min, max;       // la ranura, en el lienzo (valores)
    int target = -1, arg = -1, kind = kConnNext;
};

struct Painter {
    ImDrawList* draw = nullptr;
    ImVec2 origin{0.0f, 0.0f};   // pantalla del (0, 0) del lienzo
    float z = 1.0f;
    Look look;
    std::vector<Hit>* hits = nullptr;
    std::vector<Conn>* conns = nullptr;
    std::map<int, ImVec2>* positions = nullptr;
    const std::map<int, Severity>* flags = nullptr;
    CanvasState* record = nullptr;            // donde quedo cada cosa, para la prueba de interfaz
    bool shadow = false;                      // solo la silueta, en negro translucido (lo que se arrastra)
    int glowId = -1;                          // el bloque que acaba de encajar
    int glowAlpha = 0;
    int selected = -1;
    ImVec2 S(ImVec2 w) const { return {origin.x + w.x * z, origin.y + w.y * z}; }
};

void addArc(std::vector<ImVec2>& points, ImVec2 center, float radius, float from, float to) {
    constexpr int segments = 4;
    for (int i = 0; i <= segments; ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / segments;
        points.push_back({center.x + std::cos(a) * radius, center.y + std::sin(a) * radius});
    }
}

void notchTop(std::vector<ImVec2>& p, float x, float y) {
    p.push_back({x + kNotchX, y});
    p.push_back({x + kNotchX + kNotchD, y + kNotchD});
    p.push_back({x + kNotchX + kNotchW - kNotchD, y + kNotchD});
    p.push_back({x + kNotchX + kNotchW, y});
}

// La lengueta de abajo, recorrida de derecha a izquierda.
void tabBottom(std::vector<ImVec2>& p, float x, float y) {
    p.push_back({x + kNotchX + kNotchW, y});
    p.push_back({x + kNotchX + kNotchW - kNotchD, y + kNotchD});
    p.push_back({x + kNotchX + kNotchD, y + kNotchD});
    p.push_back({x + kNotchX, y});
}

std::vector<ImVec2> statementPath(float x, float y, float w, float h) {
    std::vector<ImVec2> p;
    const float r = kRadius;
    addArc(p, {x + r, y + r}, r, kPi, 1.5f * kPi);
    notchTop(p, x, y);
    addArc(p, {x + w - r, y + r}, r, 1.5f * kPi, 2.0f * kPi);
    addArc(p, {x + w - r, y + h - r}, r, 0.0f, 0.5f * kPi);
    tabBottom(p, x, y + h);
    addArc(p, {x + r, y + h - r}, r, 0.5f * kPi, kPi);
    return p;
}

std::vector<ImVec2> hatPath(float x, float y, float w, float h) {
    std::vector<ImVec2> p;
    const float r = kRadius;
    const float bump = std::min(100.0f, w - 16.0f);
    const ImVec2 p0{x, y + kHatCap}, p1{x + bump * 0.28f, y - 4.0f}, p2{x + bump * 0.72f, y - 4.0f}, p3{x + bump, y + kHatCap};
    for (int i = 0; i <= 14; ++i) {
        const float t = static_cast<float>(i) / 14.0f, u = 1.0f - t;
        p.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                     u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
    }
    addArc(p, {x + w - r, y + kHatCap + r}, r, 1.5f * kPi, 2.0f * kPi);
    addArc(p, {x + w - r, y + h - r}, r, 0.0f, 0.5f * kPi);
    tabBottom(p, x, y + h);
    addArc(p, {x + r, y + h - r}, r, 0.5f * kPi, kPi);
    return p;
}

std::vector<ImVec2> cPath(float x, float y, const Size& s, bool hasElse) {
    std::vector<ImVec2> p;
    const float r = kRadius, w = s.w;
    addArc(p, {x + r, y + r}, r, kPi, 1.5f * kPi);
    notchTop(p, x, y);
    addArc(p, {x + w - r, y + r}, r, 1.5f * kPi, 2.0f * kPi);
    addArc(p, {x + w - r, y + s.row - r}, r, 0.0f, 0.5f * kPi);
    tabBottom(p, x + kArm, y + s.row);
    p.push_back({x + kArm, y + s.row});
    float yy = y + s.row + s.b1;
    p.push_back({x + kArm, yy});
    addArc(p, {x + w - r, yy + r}, r, 1.5f * kPi, 2.0f * kPi);
    if (hasElse) {
        addArc(p, {x + w - r, yy + kElseRow - r}, r, 0.0f, 0.5f * kPi);
        tabBottom(p, x + kArm, yy + kElseRow);
        p.push_back({x + kArm, yy + kElseRow});
        yy += kElseRow + s.b2;
        p.push_back({x + kArm, yy});
        addArc(p, {x + w - r, yy + r}, r, 1.5f * kPi, 2.0f * kPi);
    }
    addArc(p, {x + w - r, yy + kBottom - r}, r, 0.0f, 0.5f * kPi);
    tabBottom(p, x, yy + kBottom);
    addArc(p, {x + r, yy + kBottom - r}, r, 0.5f * kPi, kPi);
    return p;
}

void fillPath(Painter& P, const std::vector<ImVec2>& path, const Colors& colors, bool selected, int id = -1) {
    std::vector<ImVec2> screen;
    screen.reserve(path.size());
    for (const ImVec2& point : path) screen.push_back(P.S(point));
    if (P.shadow) {
        P.draw->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), IM_COL32(0, 0, 0, 80));
        return;
    }
    P.draw->AddConcavePolyFilled(screen.data(), static_cast<int>(screen.size()), colors.fill);
    P.draw->AddPolyline(screen.data(), static_cast<int>(screen.size()), selected ? kSelected : colors.stroke, ImDrawFlags_Closed,
                        selected ? 2.5f : 1.0f);
    if (id >= 0 && id == P.glowId && P.glowAlpha > 0)
        P.draw->AddPolyline(screen.data(), static_cast<int>(screen.size()), IM_COL32(255, 255, 255, P.glowAlpha), ImDrawFlags_Closed, 3.5f);
}

void text(Painter& P, ImVec2 at, ImU32 color, const std::string& value) {
    P.draw->AddText(blockFont(), kFont * P.z, P.S(at), color, value.c_str());
}

void hit(Painter& P, ImVec2 min, ImVec2 max, int id, int arg, int kind) {
    if (P.hits) P.hits->push_back({P.S(min), P.S(max), id, arg, kind});
}

void drawValue(Painter& P, int id, ImVec2 at);

// Una ranura: el valor escrito, un desplegable, un color o un bloque enchufado.
void drawArg(Painter& P, const BlockNode& node, int id, const BlockDef& def, size_t i, float x, float cy, const Colors& colors) {
    const ArgDef& arg = def.args[i];
    const BlockArg& slot = node.args[i];
    const ImVec2 size = argSize(P.look, node, def, i);
    const ImVec2 min{x, cy - size.y * 0.5f}, max{x + size.x, cy + size.y * 0.5f};
    if (P.record) {
        const ImVec2 a = P.S(min);
        P.record->slotRects[static_cast<long long>(id) * 16 + static_cast<long long>(i)] = ImVec4(a.x, a.y, size.x * P.z, size.y * P.z);
    }
    if (arg.plug && P.conns)
        P.conns->push_back({{x, cy}, min, max, id, static_cast<int>(i), arg.kind == ArgKind::Boolean ? kConnBoolean : kConnNumber});
    if (arg.plug && slot.block >= 0 && P.look.program->node(slot.block)) {
        drawValue(P, slot.block, min);
        return;
    }
    const float textY = cy - kFont * 0.62f;
    switch (arg.kind) {
        case ArgKind::Number: {
            P.draw->AddRectFilled(P.S(min), P.S(max), kWhite, size.y * 0.5f * P.z);
            P.draw->AddRect(P.S(min), P.S(max), colors.stroke, size.y * 0.5f * P.z);
            text(P, {x + (size.x - textW(slot.value)) * 0.5f, textY}, kSlotText, slot.value);
            hit(P, min, max, id, static_cast<int>(i), kHitLiteral);
            break;
        }
        case ArgKind::Text: {
            P.draw->AddRectFilled(P.S(min), P.S(max), kWhite, 4.0f * P.z);
            P.draw->AddRect(P.S(min), P.S(max), colors.stroke, 4.0f * P.z);
            text(P, {x + 7.0f, textY}, kSlotText, slot.value);
            hit(P, min, max, id, static_cast<int>(i), kHitLiteral);
            break;
        }
        case ArgKind::Choice:
        case ArgKind::Variable:
        case ArgKind::Image:
        case ArgKind::Video:
        case ArgKind::Sound: {
            P.draw->AddRectFilled(P.S(min), P.S(max), colors.dark, size.y * 0.5f * P.z);
            P.draw->AddRect(P.S(min), P.S(max), colors.stroke, size.y * 0.5f * P.z);
            text(P, {x + 9.0f, textY}, kWhite, literalText(P.look, def, node, i));
            const ImVec2 a = P.S({max.x - 17.0f, cy - 2.0f}), b = P.S({max.x - 9.0f, cy - 2.0f}), c = P.S({max.x - 13.0f, cy + 2.5f});
            P.draw->AddTriangleFilled(a, b, c, kWhite);
            hit(P, min, max, id, static_cast<int>(i), arg.kind == ArgKind::Sound ? kHitSound : kHitChoice);
            break;
        }
        case ArgKind::Color: {
            unsigned value = 0xFFFFFF;
            std::sscanf(slot.value.c_str(), "%x", &value);
            const ImU32 swatch = IM_COL32((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255);
            P.draw->AddRectFilled(P.S(min), P.S(max), swatch, size.y * 0.5f * P.z);
            P.draw->AddRect(P.S(min), P.S(max), kWhite, size.y * 0.5f * P.z, 0, 2.0f);
            hit(P, min, max, id, static_cast<int>(i), kHitColor);
            break;
        }
        case ArgKind::Boolean: {
            const float h = size.y, half = h * 0.5f;
            const ImVec2 hex[6] = {P.S({min.x, cy}), P.S({min.x + half, min.y}), P.S({max.x - half, min.y}),
                                   P.S({max.x, cy}), P.S({max.x - half, max.y}), P.S({min.x + half, max.y})};
            P.draw->AddConvexPolyFilled(hex, 6, colors.dark);
            hit(P, min, max, id, static_cast<int>(i), kHitSlot);
            break;
        }
        default: break;
    }
}

void drawRow(Painter& P, const BlockNode& node, int id, const BlockDef& def, const std::vector<Token>& tokens, float x, float cy,
             const Colors& colors) {
    for (const Token& token : tokens) {
        if (token.arg == -2) {
            text(P, {x, cy - kFont * 0.62f}, kWhite, token.text);
            x += textW(token.text) + kGap;
        } else if (token.arg == -1) {
            const float w = textW(P.look.typeName) + 16.0f;
            P.draw->AddRectFilled(P.S({x, cy - kSlotH * 0.5f}), P.S({x + w, cy + kSlotH * 0.5f}), colors.stroke, 5.0f * P.z);
            text(P, {x + 8.0f, cy - kFont * 0.62f}, kWhite, P.look.typeName);
            x += w + kGap;
        } else if (token.arg < static_cast<int>(def.args.size()) && def.args[static_cast<size_t>(token.arg)].kind != ArgKind::Body) {
            const size_t index = static_cast<size_t>(token.arg);
            drawArg(P, node, id, def, index, x, cy, colors);
            x += argSize(P.look, node, def, index).x + kGap;
        }
    }
}

void badge(Painter& P, int id, ImVec2 corner) {
    if (!P.flags) return;
    const auto found = P.flags->find(id);
    if (found == P.flags->end()) return;
    const ImU32 tint = found->second == Severity::Info ? ui::color::Info : found->second == Severity::Error ? ui::color::Error : ui::color::Warning;
    const ImVec2 center = P.S(corner);
    const float radius = 8.0f * P.z;
    P.draw->AddCircleFilled(center, radius, tint);
    P.draw->AddCircle(center, radius, IM_COL32(20, 20, 26, 255), 0, 1.5f);
    const char* mark = found->second == Severity::Info ? "i" : "!";
    const ImVec2 size = blockFont()->CalcTextSizeA(12.0f * P.z, FLT_MAX, 0.0f, mark);
    P.draw->AddText(blockFont(), 12.0f * P.z, {center.x - size.x * 0.5f, center.y - size.y * 0.5f}, IM_COL32(20, 20, 26, 255), mark);
    if (P.hits) P.hits->push_back({{center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}, id, -1, kHitBadge});
}

void drawValue(Painter& P, int id, ImVec2 at) {
    const BlockNode* node = P.look.program->node(id);
    const BlockDef* def = node ? blockDef(node->key) : nullptr;
    if (!def) return;
    if (P.positions) (*P.positions)[id] = at;
    const Colors colors = colorsOf(def->category);
    const ImVec2 size = valueSize(P.look, id);
    const ImVec2 max{at.x + size.x, at.y + size.y};
    if (P.record) {
        const ImVec2 a = P.S(at);
        P.record->blockRects[id] = ImVec4(a.x, a.y, size.x * P.z, size.y * P.z);
    }
    const bool selected = P.selected == id;
    const float half = size.y * 0.5f;
    hit(P, at, max, id, -1, kHitBlock);
    const bool glow = id == P.glowId && P.glowAlpha > 0;
    if (def->shape == BlockShape::Boolean) {
        const ImVec2 hex[6] = {P.S({at.x, at.y + half}), P.S({at.x + half, at.y}), P.S({max.x - half, at.y}),
                               P.S({max.x, at.y + half}), P.S({max.x - half, max.y}), P.S({at.x + half, max.y})};
        if (P.shadow) {
            P.draw->AddConvexPolyFilled(hex, 6, IM_COL32(0, 0, 0, 80));
            return;
        }
        P.draw->AddConvexPolyFilled(hex, 6, colors.fill);
        P.draw->AddPolyline(hex, 6, selected ? kSelected : colors.stroke, ImDrawFlags_Closed, selected ? 2.5f : 1.0f);
        if (glow) P.draw->AddPolyline(hex, 6, IM_COL32(255, 255, 255, P.glowAlpha), ImDrawFlags_Closed, 3.5f);
        drawRow(P, *node, id, *def, tokensOf(patternOf(*def, P.look.spanish)), at.x + half + 4.0f, at.y + half, colors);
    } else {
        if (P.shadow) {
            P.draw->AddRectFilled(P.S(at), P.S(max), IM_COL32(0, 0, 0, 80), half * P.z);
            return;
        }
        P.draw->AddRectFilled(P.S(at), P.S(max), colors.fill, half * P.z);
        P.draw->AddRect(P.S(at), P.S(max), selected ? kSelected : colors.stroke, half * P.z, 0, selected ? 2.5f : 1.0f);
        if (glow) P.draw->AddRect(P.S(at), P.S(max), IM_COL32(255, 255, 255, P.glowAlpha), half * P.z, 0, 3.5f);
        drawRow(P, *node, id, *def, tokensOf(patternOf(*def, P.look.spanish)), at.x + half, at.y + half, colors);
    }
    badge(P, id, {max.x - 2.0f, at.y + 2.0f});
}

float drawChain(Painter& P, int first, ImVec2 at);

float drawBlock(Painter& P, int id, ImVec2 at) {
    const BlockNode* node = P.look.program->node(id);
    const BlockDef* def = node ? blockDef(node->key) : nullptr;
    if (!def) return 0.0f;
    if (isValue(def)) {
        drawValue(P, id, at);
        return valueSize(P.look, id).y;
    }
    if (P.positions) (*P.positions)[id] = at;
    const Size size = blockSize(P.look, id);
    const Colors colors = colorsOf(def->category);
    const bool selected = P.selected == id;
    if (P.record) {
        const ImVec2 a = P.S(at);
        P.record->blockRects[id] = ImVec4(a.x, a.y, size.w * P.z, size.h * P.z);
    }
    const std::vector<Token> tokens = tokensOf(patternOf(*def, P.look.spanish));
    float rowTop = at.y;
    if (def->shape == BlockShape::Hat) {
        fillPath(P, hatPath(at.x, at.y, size.w, size.h), colors, selected, id);
        rowTop = at.y + kHatCap;
        hit(P, at, {at.x + size.w, at.y + size.h}, id, -1, kHitBlock);
    } else if (isC(def)) {
        fillPath(P, cPath(at.x, at.y, size, def->shape == BlockShape::IfElse), colors, selected, id);
        hit(P, at, {at.x + size.w, at.y + size.row}, id, -1, kHitBlock);
        hit(P, {at.x, at.y + size.row}, {at.x + kArm, at.y + size.h}, id, -1, kHitBlock);
        hit(P, {at.x, at.y + size.h - kBottom}, {at.x + size.w, at.y + size.h}, id, -1, kHitBlock);
        if (def->shape == BlockShape::IfElse)
            hit(P, {at.x, at.y + size.row + size.b1}, {at.x + size.w, at.y + size.row + size.b1 + kElseRow}, id, -1, kHitBlock);
    } else {
        fillPath(P, statementPath(at.x, at.y, size.w, size.h), colors, selected, id);
        hit(P, at, {at.x + size.w, at.y + size.h}, id, -1, kHitBlock);
    }
    if (P.shadow) {
        // La silueta de lo que va dentro de un «si», sin textos.
        if (isC(def)) {
            drawChain(P, node->args.size() > 1 ? node->args[1].block : -1, {at.x + kArm, at.y + size.row});
            if (def->shape == BlockShape::IfElse)
                drawChain(P, node->args.size() > 2 ? node->args[2].block : -1, {at.x + kArm, at.y + size.row + size.b1 + kElseRow});
        }
        return size.h;
    }
    drawRow(P, *node, id, *def, tokens, at.x + kPadX, rowTop + size.row * 0.5f, colors);
    if (isC(def)) {
        const ImVec2 body1{at.x + kArm, at.y + size.row};
        if (P.conns) P.conns->push_back({body1, body1, body1, id, 1, kConnBody});
        if (P.record) P.record->bodyPoints[static_cast<long long>(id) * 16 + 1] = P.S(body1);
        drawChain(P, node->args.size() > 1 ? node->args[1].block : -1, body1);
        if (def->shape == BlockShape::IfElse) {
            const float elseY = at.y + size.row + size.b1;
            text(P, {at.x + kPadX, elseY + kElseRow * 0.5f - kFont * 0.62f}, kWhite, P.look.spanish ? def->elseEs : def->elseEn);
            const ImVec2 body2{at.x + kArm, elseY + kElseRow};
            if (P.conns) P.conns->push_back({body2, body2, body2, id, 2, kConnBody});
            if (P.record) P.record->bodyPoints[static_cast<long long>(id) * 16 + 2] = P.S(body2);
            drawChain(P, node->args.size() > 2 ? node->args[2].block : -1, body2);
        }
    }
    if (P.conns) {
        const ImVec2 below{at.x, at.y + size.h};
        P.conns->push_back({below, below, below, id, -1, kConnNext});
    }
    badge(P, id, {at.x + size.w - 4.0f, (def->shape == BlockShape::Hat ? at.y + kHatCap : at.y) + 3.0f});
    return size.h;
}

float drawChain(Painter& P, int first, ImVec2 at) {
    float y = at.y;
    for (int id : chainOf(*P.look.program, first)) y += drawBlock(P, id, {at.x, y});
    return y - at.y;
}

// Una pila suelta: un evento, una pila de instrucciones o un valor solo.
void drawTop(Painter& P, int top) {
    const BlockNode* node = P.look.program->node(top);
    const BlockDef* def = node ? blockDef(node->key) : nullptr;
    if (!def) return;
    const ImVec2 at{node->x, node->y};
    if (isValue(def)) {
        drawValue(P, top, at);
        return;
    }
    if (def->shape != BlockShape::Hat && P.conns) P.conns->push_back({at, at, at, top, -1, kConnAbove});
    drawChain(P, top, at);
}

// -------------------------------------------------------------- utilidades --

void pushUndo(CanvasState& state, std::string snapshot) {
    state.undo.push_back(std::move(snapshot));
    if (state.undo.size() > 120) state.undo.erase(state.undo.begin());
    state.redo.clear();
}

bool step(std::vector<std::string>& from, std::vector<std::string>& to, BlockProgram& program) {
    if (from.empty()) return false;
    // Deshacer suena hacia abajo; rehacer, hacia arriba (el que llama elige).
    to.push_back(writeProgram(program));
    program = readProgram(from.back());
    from.pop_back();
    return true;
}

bool inside(ImVec2 point, ImVec2 min, ImVec2 max) { return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y; }

float distance(ImVec2 a, ImVec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

// La primera cabecera con esa clave (y quien, si es un acierto); o una nueva.
int hatFor(BlockProgram& program, const char* key, const char* who) {
    for (int top : program.tops) {
        const BlockNode* node = program.node(top);
        if (node && node->key == key && (!who || (!node->args.empty() && node->args[0].value == who))) return top;
    }
    const std::vector<int> before = program.tops;
    const int hat = newBlock(program, key);
    if (who) program.node(hat)->args[0].value = who;
    placeTop(program, hat, 0.0f, 0.0f);
    placeNewStacks(program, before);
    return hat;
}

std::string tooltipOf(const BlockDef& def, bool spanish) {
    std::string out = spanish ? def.helpEs : def.helpEn;
    out += "\n";
    for (Engine engine : {Engine::Codename, Engine::Psych, Engine::VSlice}) {
        const BlockSupport support = blockSupport(engine, def.key);
        const char* name = engine == Engine::Codename ? "Codename" : engine == Engine::Psych ? "Psych" : "V-Slice";
        out += std::string("\n") + name + " \xC2\xB7 " + realizationKey(support.how) + (support.cite[0] ? std::string(" \xC2\xB7 ") + support.cite : std::string());
        if (support.how == Realization::None || support.how == Realization::Approx)
            out += std::string("\n   ") + (spanish ? support.noteEs : support.noteEn);
    }
    return out;
}

// Un programa con un bloque de cada, para dibujar la paleta.
const BlockProgram& paletteProgram(std::map<std::string, int>& ids) {
    static BlockProgram program;
    static std::map<std::string, int> made;
    if (made.empty())
        for (const BlockDef& def : blockDefs()) made[def.key] = newBlock(program, def.key);
    ids = made;
    return program;
}

void drawPalette(CanvasState& state, BlockProgram& program, const CanvasEnv& env, bool& changed) {
    state.paletteMin = ImGui::GetCursorScreenPos();
    state.paletteRects.clear();
    state.categoryHeaderRects.fill(ImVec4{});
    ImGui::SetNextItemWidth(-1.0f);
    const bool searched = ImGui::InputTextWithHint("##blocksearch", env.spanish ? "Buscar bloques..." : "Search blocks...", state.search.data(), state.search.size());
    const ImVec2 searchMin = ImGui::GetItemRectMin(), searchMax = ImGui::GetItemRectMax();
    state.searchRect = {searchMin.x, searchMin.y, searchMax.x - searchMin.x, searchMax.y - searchMin.y};
    // El bundle del tutorial: solo sus bloques, con «Ver todos» para salir.
    const bool bundled = !state.onlyBlocks.empty() && !state.showAllBlocks;
    auto inBundle = [&](const BlockDef& def) {
        return !bundled || std::find(state.onlyBlocks.begin(), state.onlyBlocks.end(), def.key) != state.onlyBlocks.end();
    };
    if (!state.onlyBlocks.empty()) {
        const ImU32 accent = IM_COL32(167, 130, 255, 255);
        ImGui::PushStyleColor(ImGuiCol_Text, bundled ? accent : ui::color::Muted);
        ImGui::AlignTextToFramePadding();
        if (bundled) ImGui::Text(env.spanish ? "Bundle del tutorial · %d" : "Tutorial bundle · %d", static_cast<int>(state.onlyBlocks.size()));
        else ImGui::TextUnformatted(env.spanish ? "Todos los bloques" : "All the blocks");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton(bundled ? (env.spanish ? "Ver todos" : "Show all") : (env.spanish ? "Solo el bundle" : "Bundle only")))
            state.showAllBlocks = bundled;
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    auto lower = [](std::string text) { for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; };
    // Las categorias, como en Scratch: un circulo de color y su nombre; con el
    // bundle, las que no tienen ninguno de sus bloques, apagadas.
    std::array<bool, kBlockCategories> offered{};
    for (const BlockDef& def : blockDefs())
        if (inBundle(def)) offered[static_cast<size_t>(def.category)] = true;
    const float cellW = (kPaletteW - 12.0f) / 3.0f, cellH = 40.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Con el bundle no hacen falta las categorias: sus pocos bloques ocupan su sitio.
    if (bundled) state.categoryRects.fill(ImVec4{});
    for (int c = 0; c < kBlockCategories && !bundled; ++c) {
        const ImVec2 cell{start.x + 6.0f + cellW * static_cast<float>(c % 3), start.y + 6.0f + cellH * static_cast<float>(c / 3)};
        state.categoryRects[static_cast<size_t>(c)] = ImVec4(cell.x, cell.y, cellW, cellH - 2.0f);
        ImGui::SetCursorScreenPos(cell);
        ImGui::PushID(c);
        if (ImGui::InvisibleButton("category", ImVec2(cellW, cellH - 2.0f)) && offered[static_cast<size_t>(c)]) {
            state.jumpCategory = c;
            state.collapsed[static_cast<size_t>(c)] = false;
            state.search.fill('\0');
            state.settingsChanged = true;
            playCue(Cue::Click, env.soundOn && *env.soundOn);
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const Colors colors = colorsOf(static_cast<BlockCategory>(c));
        const bool on = offered[static_cast<size_t>(c)];
        if (hovered && on) draw->AddRectFilled(cell, ImVec2(cell.x + cellW, cell.y + cellH - 2.0f), IM_COL32(255, 255, 255, 14), 6.0f);
        const ImVec2 center{cell.x + cellW * 0.5f, cell.y + 12.0f};
        const auto dimmed = [&](ImU32 color) { return on ? color : (color & 0x00FFFFFFu) | (static_cast<ImU32>(((color >> 24) & 0xFFu) * 30u / 100u) << 24); };
        draw->AddCircleFilled(center, 8.5f, dimmed(colors.fill));
        draw->AddCircle(center, 8.5f, dimmed(colors.stroke), 0, 1.5f);
        const char* name = categoryName(static_cast<BlockCategory>(c), env.spanish);
        const ImVec2 size = ui::fonts().ui->CalcTextSizeA(11.5f, FLT_MAX, 0.0f, name);
        draw->AddText(ui::fonts().ui, 11.5f, ImVec2(cell.x + (cellW - size.x) * 0.5f, cell.y + 22.0f), dimmed(ui::color::Muted), name);
    }
    if (!bundled) ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + 6.0f + cellH * static_cast<float>((kBlockCategories + 2) / 3) + 4.0f));
    ImGui::Separator();

    ImGui::BeginChild("flyout", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove);
    if (searched) ImGui::SetScrollY(0.0f);
    const std::string query = lower(state.search.data());
    auto matches = [&](const BlockDef& def) {
        return std::string(def.key) != "code.file" && inBundle(def) && (query.empty() || lower(std::string(def.key) + " " + def.textEn + " " + def.textEs + " " + def.helpEn + " " + def.helpEs).find(query) != std::string::npos);
    };
    std::map<std::string, int> ids;
    const BlockProgram& sample = paletteProgram(ids);
    Look look{&sample, env.spanish, env.typeName};
    int totalMatches = 0;
    for (int c = 0; c < kBlockCategories; ++c) {
        const BlockCategory category = static_cast<BlockCategory>(c);
        int count = 0;
        for (const auto& def : blockDefs()) if (def.category == category && matches(def)) ++count;
        totalMatches += count;
        if (!count) continue;
        state.categoryY[static_cast<size_t>(c)] = ImGui::GetCursorPosY();
        ImGui::Spacing();
        const Colors colors = colorsOf(category);
        ImGui::PushID(c);
        ImGui::SetNextItemOpen(!query.empty() || !state.collapsed[static_cast<size_t>(c)], ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_Text, colors.fill);
        const std::string label = std::string(categoryName(category, env.spanish)) + "  " + std::to_string(count);
        const bool expanded = ImGui::CollapsingHeader(label.c_str());
        ImGui::PopStyleColor();
        const ImVec2 headerMin = ImGui::GetItemRectMin(), headerMax = ImGui::GetItemRectMax();
        state.categoryHeaderRects[static_cast<size_t>(c)] = {headerMin.x, headerMin.y, headerMax.x - headerMin.x, headerMax.y - headerMin.y};
        if (ImGui::IsItemToggledOpen() && query.empty()) { state.collapsed[static_cast<size_t>(c)] = !expanded; state.settingsChanged = true; }
        ImGui::PopID();
        if (!expanded) continue;
        ImGui::Spacing();
        for (const BlockDef& def : blockDefs()) {
            if (def.category != category || !matches(def)) continue;
            const int id = ids[def.key];
            const Size size = blockSize(look, id);
            const float extra = def.shape == BlockShape::Hat ? 4.0f : (isStatement(&def) ? kNotchD : 0.0f);
            const ImVec2 item(std::max(size.w, 20.0f) * kPaletteScale, (size.h + extra) * kPaletteScale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            ImGui::PushID(def.key);
            state.paletteRects[def.key] = ImVec4(min.x, min.y, item.x, item.y);
            ImGui::InvisibleButton("block", item);
            const bool active = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();
            const bool released = ImGui::IsItemDeactivated();
            ImGui::PopID();
            Painter P;
            P.draw = ImGui::GetWindowDrawList();
            P.origin = min;
            P.z = kPaletteScale;
            P.look = look;
            drawBlock(P, id, {0.0f, 0.0f});
            if (hovered && state.dragging < 0)
                P.draw->AddRectFilled(min, ImVec2(min.x + item.x, min.y + item.y), IM_COL32(255, 255, 255, 22), 6.0f);
            if (hovered && state.dragging < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextUnformatted(tooltipOf(def, env.spanish).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            // Arrastrar: el bloque nace en el lienzo y sigue al raton.
            if (active && state.dragging < 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const ImVec2 origin{state.canvasMin.x + state.pan.x, state.canvasMin.y + state.pan.y};
                state.before = writeProgram(program);
                const int fresh = newBlock(program, def.key);
                // Cogido donde se pulso, no donde esta el raton al pasar el umbral de arrastre.
                const ImVec2 pressedAt = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left];
                state.grab = ImVec2((pressedAt.x - min.x) / kPaletteScale, (pressedAt.y - min.y) / kPaletteScale);
                placeTop(program, fresh, (mouse.x - origin.x) / state.zoom - state.grab.x, (mouse.y - origin.y) / state.zoom - state.grab.y);
                state.dragging = fresh;
                state.selected = fresh;
                state.editId = -1;
            } else if (released && hovered && state.dragging < 0 && !isValue(&def)) {
                // Un clic lo pone en su sitio: las propiedades bajo «al crear»,
                // un evento debajo de todo y lo demas bajo «cuando el jugador toca».
                pushUndo(state, writeProgram(program));
                const std::vector<int> before = program.tops;
                const int fresh = newBlock(program, def.key);
                if (def.shape == BlockShape::Hat) {
                    placeTop(program, fresh, 0.0f, 0.0f);
                    placeNewStacks(program, before);
                } else {
                    const int hat = def.category == BlockCategory::Properties ? hatFor(program, "event.create", nullptr)
                                                                              : hatFor(program, "event.hit", "player");
                    attachAfter(program, lastOf(program, hat), fresh);
                }
                state.selected = fresh;
                state.glowId = fresh;
                state.glowStart = ImGui::GetTime();
                playCue(Cue::Add, env.soundOn && *env.soundOn);
                changed = true;
            }
            ImGui::Dummy(ImVec2(1.0f, 6.0f));
        }
        ImGui::Spacing();
    }
    if (!totalMatches) ui::caption(env.spanish ? "No hay bloques que coincidan." : "No matching blocks.");
    ImGui::Dummy(ImVec2(1.0f, 40.0f));
    if (state.jumpCategory >= 0) {
        ImGui::SetScrollY(state.categoryY[static_cast<size_t>(state.jumpCategory)]);
        state.jumpCategory = -1;
    }
    ImGui::EndChild();
    state.paletteMax = ImVec2(state.paletteMin.x + kPaletteW, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
}

// Ir a un encuadre sin salto: el lienzo se desliza hasta alli (drawCanvas).
void easeTo(CanvasState& state, ImVec2 pan, float zoom) {
    state.panTarget = pan;
    state.zoomTarget = zoom;
    state.easing = true;
}

void centerOn(CanvasState& state, ImVec2 at) {
    const ImVec2 size{state.canvasMax.x - state.canvasMin.x, state.canvasMax.y - state.canvasMin.y};
    const float zoom = state.easing ? state.zoomTarget : state.zoom;
    easeTo(state, ImVec2(size.x * 0.35f - at.x * zoom, size.y * 0.3f - at.y * zoom), zoom);
}

void zoomAround(CanvasState& state, float zoom, ImVec2 screen) {
    zoom = std::clamp(zoom, 0.4f, 2.2f);
    const ImVec2 origin{state.canvasMin.x + state.pan.x, state.canvasMin.y + state.pan.y};
    const ImVec2 world{(screen.x - origin.x) / state.zoom, (screen.y - origin.y) / state.zoom};
    easeTo(state, ImVec2(screen.x - state.canvasMin.x - world.x * zoom, screen.y - state.canvasMin.y - world.y * zoom), zoom);
}

// Todo a la vista: el zoom que cabe y centrado.
void fitAll(CanvasState& state, const BlockProgram& program, const Look& look, float minZoom = 0.4f) {
    if (program.tops.empty()) {
        easeTo(state, ImVec2(28.0f, 28.0f), 1.0f);
        return;
    }
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (int top : program.tops) {
        const BlockNode* node = program.node(top);
        if (!node) continue;
        const BlockDef* def = blockDef(node->key);
        const ImVec2 size = isValue(def) ? valueSize(look, top) : ImVec2(chainWidth(look, top), chainHeight(look, top));
        minX = std::min(minX, node->x);
        minY = std::min(minY, node->y);
        maxX = std::max(maxX, node->x + size.x);
        maxY = std::max(maxY, node->y + size.y);
    }
    const ImVec2 view{state.canvasMax.x - state.canvasMin.x, state.canvasMax.y - state.canvasMin.y};
    const float zoom = std::clamp(std::min((view.x - 80.0f) / std::max(1.0f, maxX - minX), (view.y - 80.0f) / std::max(1.0f, maxY - minY)), minZoom, 1.0f);
    easeTo(state, ImVec2((view.x - (maxX - minX) * zoom) * 0.5f - minX * zoom, 36.0f - minY * zoom), zoom);
}

// Una pila suelta que cae encima de otra se aparta a la derecha, para que
// nada quede encimado ni parezca encajado sin estarlo.
void bumpApart(BlockProgram& program, int top, const Look& look) {
    BlockNode* node = program.node(top);
    if (!node || std::find(program.tops.begin(), program.tops.end(), top) == program.tops.end()) return;
    auto sizeOf = [&](int id) {
        const BlockNode* n = program.node(id);
        const BlockDef* def = n ? blockDef(n->key) : nullptr;
        return isValue(def) ? valueSize(look, id) : ImVec2(chainWidth(look, id), chainHeight(look, id) + (def && def->shape == BlockShape::Hat ? 0.0f : kNotchD));
    };
    for (int round = 0; round < 12; ++round) {
        const ImVec2 mine = sizeOf(top);
        bool moved = false;
        for (int other : program.tops) {
            if (other == top) continue;
            const BlockNode* o = program.node(other);
            if (!o) continue;
            const ImVec2 theirs = sizeOf(other);
            const bool overlap = node->x < o->x + theirs.x + 8.0f && o->x < node->x + mine.x + 8.0f && node->y < o->y + theirs.y + 8.0f &&
                                 o->y < node->y + mine.y + 8.0f;
            if (overlap) {
                node->x = o->x + theirs.x + 32.0f;
                moved = true;
            }
        }
        if (!moved) return;
    }
}

std::string numberText(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", static_cast<double>(value));
    return text;
}

}  // namespace

// ------------------------------------------------------------------ publico --

ImVec2 stackSize(const BlockProgram& program, int first, bool spanish, const std::string& typeName) {
    const Look look{&program, spanish, typeName};
    const BlockDef* def = look.program->node(first) ? blockDef(look.program->node(first)->key) : nullptr;
    if (isValue(def)) return valueSize(look, first);
    return {chainWidth(look, first), chainHeight(look, first)};
}

void remember(CanvasState& state, const BlockProgram& program) { pushUndo(state, writeProgram(program)); }

void placeNewStacks(BlockProgram& program, const std::vector<int>& before) {
    float bottom = 28.0f;
    for (int top : program.tops) {
        if (std::find(before.begin(), before.end(), top) == before.end()) continue;
        const BlockNode* node = program.node(top);
        if (node) bottom = std::max(bottom, node->y + stackSize(program, top, true, "Tipo").y + 36.0f);
    }
    for (int top : program.tops) {
        if (std::find(before.begin(), before.end(), top) != before.end()) continue;
        BlockNode* node = program.node(top);
        if (!node) continue;
        node->x = 28.0f;
        node->y = bottom;
        bottom += stackSize(program, top, true, "Tipo").y + 36.0f;
    }
}

void arrange(BlockProgram& program) {
    auto rank = [&](int top) {
        const BlockNode* node = program.node(top);
        if (!node) return 9;
        if (node->key == "event.create") return 0;
        if (node->key == "event.hit") {
            const std::string who = node->args.empty() ? std::string() : node->args[0].value;
            return who == "player" ? 1 : who == "opponent" ? 2 : 3;
        }
        if (node->key == "event.miss") return 4;
        return 5;
    };
    std::stable_sort(program.tops.begin(), program.tops.end(), [&](int a, int b) { return rank(a) < rank(b); });
    float y = 28.0f;
    for (int top : program.tops) {
        BlockNode* node = program.node(top);
        if (!node) continue;
        node->x = 28.0f;
        node->y = y;
        y += stackSize(program, top, true, "Tipo").y + 36.0f;
    }
}

bool drawCanvas(CanvasState& state, BlockProgram& program, const CanvasEnv& env) {
    bool changed = false;
    ImGuiIO& io = ImGui::GetIO();
    const bool es = env.spanish;
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    // Paleta.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::vec(ui::color::Raised));
    ImGui::BeginChild("blockpalette", ImVec2(kPaletteW, avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    drawPalette(state, program, env, changed);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);

    // Lienzo.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.086f, 0.094f, 0.118f, 1.0f));
    ImGui::BeginChild("blockcanvas", ImVec2(0.0f, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    const ImVec2 cmin = ImGui::GetCursorScreenPos();
    const ImVec2 csize = ImGui::GetContentRegionAvail();
    const ImVec2 cmax{cmin.x + csize.x, cmin.y + csize.y};
    state.canvasMin = cmin;
    state.canvasMax = cmax;
    // Al abrir un tipo: al 100 % y desde arriba a la izquierda, como Scratch.
    if (state.fitPending && csize.x > 50.0f && csize.y > 50.0f) {
        state.fitPending = false;
        float minX = FLT_MAX, minY = FLT_MAX;
        for (int top : program.tops)
            if (const BlockNode* node = program.node(top)) {
                minX = std::min(minX, node->x);
                minY = std::min(minY, node->y);
            }
        state.zoom = 1.0f;
        state.pan = program.tops.empty() ? ImVec2(28.0f, 28.0f) : ImVec2(40.0f - minX, 34.0f - minY);
        state.easing = false;
    }
    // El deslizamiento del zoom y del encuadre.
    if (state.easing) {
        const float k = 1.0f - std::exp(-io.DeltaTime * 14.0f);
        state.zoom += (state.zoomTarget - state.zoom) * k;
        state.pan.x += (state.panTarget.x - state.pan.x) * k;
        state.pan.y += (state.panTarget.y - state.pan.y) * k;
        if (std::fabs(state.zoomTarget - state.zoom) < 0.002f && std::fabs(state.panTarget.x - state.pan.x) < 0.5f &&
            std::fabs(state.panTarget.y - state.pan.y) < 0.5f) {
            state.zoom = state.zoomTarget;
            state.pan = state.panTarget;
            state.easing = false;
        }
    }
    const double now = ImGui::GetTime();
    const bool sound = env.soundOn && *env.soundOn;
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("canvas", ImVec2(std::max(1.0f, csize.x), std::max(1.0f, csize.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    const bool mouseInCanvas = inside(mouse, cmin, cmax);

    // El arrastre se mueve antes de dibujar, para que no vaya un cuadro tarde.
    if (state.dragging >= 0 && program.node(state.dragging)) {
        BlockNode& node = *program.node(state.dragging);
        node.x = (mouse.x - cmin.x - state.pan.x) / state.zoom - state.grab.x;
        node.y = (mouse.y - cmin.y - state.pan.y) / state.zoom - state.grab.y;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(cmin, cmax, true);
    // La rejilla de puntos.
    const float gridStep = 24.0f * state.zoom;
    if (gridStep > 6.0f) {
        const float offX = std::fmod(state.pan.x, gridStep), offY = std::fmod(state.pan.y, gridStep);
        for (float y = cmin.y + offY; y < cmax.y; y += gridStep)
            for (float x = cmin.x + offX; x < cmax.x; x += gridStep) draw->AddCircleFilled(ImVec2(x, y), 1.1f, IM_COL32(255, 255, 255, 22), 4);
    }

    // El aviso mas serio de cada bloque, para su marca.
    std::map<int, Severity> flags;
    for (const BlockConflict& conflict : env.conflicts) {
        if (conflict.node < 0) continue;
        const auto rankOf = [](Severity s) { return s == Severity::Error ? 3 : s == Severity::Warning ? 2 : 1; };
        const auto found = flags.find(conflict.node);
        if (found == flags.end() || rankOf(conflict.severity) > rankOf(found->second)) flags[conflict.node] = conflict.severity;
    }

    std::vector<Hit> hits;
    std::vector<Conn> conns;
    std::map<int, ImVec2> positions;
    state.blockRects.clear();
    state.slotRects.clear();
    state.bodyPoints.clear();
    Painter P;
    P.record = &state;
    P.draw = draw;
    P.origin = ImVec2(cmin.x + state.pan.x, cmin.y + state.pan.y);
    P.z = state.zoom;
    P.look = Look{&program, es, env.typeName};
    P.hits = &hits;
    P.conns = &conns;
    P.positions = &positions;
    P.flags = &flags;
    P.selected = state.selected;
    if (now - state.glowStart < 0.45) {
        P.glowId = state.glowId;
        P.glowAlpha = static_cast<int>(230.0 * (1.0 - (now - state.glowStart) / 0.45));
    }
    const std::vector<int> tops = program.tops;
    for (int top : tops)
        if (top != state.dragging) drawTop(P, top);

    if (program.tops.empty()) {
        const char* line1 = es ? "Arrastra bloques desde la paleta" : "Drag blocks from the palette";
        const char* line2 = es ? "o elige un preset arriba. Empieza con «al crear» o «cuando el jugador toca»."
                               : "or pick a preset above. Start with «when created» or «when the player hits».";
        const ImVec2 s1 = ui::fonts().semibold->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, line1);
        const ImVec2 s2 = ui::fonts().ui->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line2);
        const ImVec2 center{(cmin.x + cmax.x) * 0.5f, (cmin.y + cmax.y) * 0.45f};
        draw->AddText(ui::fonts().semibold, 18.0f, ImVec2(center.x - s1.x * 0.5f, center.y - 20.0f), ui::color::Muted, line1);
        draw->AddText(ui::fonts().ui, 14.0f, ImVec2(center.x - s2.x * 0.5f, center.y + 6.0f), ui::color::Faint, line2);
    }

    // La papelera y el zoom, abajo a la derecha.
    const ImVec2 trashMin{cmax.x - 62.0f, cmax.y - 70.0f}, trashMax{cmax.x - 14.0f, cmax.y - 14.0f};
    state.trashRect = ImVec4(trashMin.x, trashMin.y, trashMax.x - trashMin.x, trashMax.y - trashMin.y);
    const bool overTrash = state.dragging >= 0 && inside(mouse, trashMin, trashMax);
    const bool overPalette = state.dragging >= 0 && inside(mouse, state.paletteMin, state.paletteMax);

    // El sitio donde encajaria lo que se arrastra.
    const Conn* snap = nullptr;
    if (state.dragging >= 0 && program.node(state.dragging) && !overTrash && !overPalette) {
        const BlockNode& dragged = *program.node(state.dragging);
        const BlockDef* ddef = blockDef(dragged.key);
        const ImVec2 dsize = stackSize(program, state.dragging, es, env.typeName);
        const std::vector<int> chain = isStatement(ddef) ? chainOf(program, state.dragging) : std::vector<int>{state.dragging};
        float best = kSnapPx / state.zoom;
        for (const Conn& conn : conns) {
            float d = FLT_MAX;
            if (isValue(ddef)) {
                const bool match = (conn.kind == kConnNumber && ddef->shape == BlockShape::Number) ||
                                   (conn.kind == kConnBoolean && ddef->shape == BlockShape::Boolean);
                if (!match) continue;
                const ImVec2 left{dragged.x, dragged.y + dsize.y * 0.5f};
                d = distance(left, conn.at);
                const ImVec2 mw{(mouse.x - P.origin.x) / state.zoom, (mouse.y - P.origin.y) / state.zoom};
                if (inside(mw, conn.min, conn.max)) d = 0.0f;
                const unsigned place = slotPlace(program, conn.target, conn.arg);
                if (place != 0 && !fits(*ddef, place)) continue;
            } else if (conn.kind == kConnAbove) {
                d = distance(ImVec2(dragged.x, dragged.y + dsize.y), conn.at);
            } else if (conn.kind == kConnNext || conn.kind == kConnBody) {
                if (!isStatement(ddef)) continue;
                d = distance(ImVec2(dragged.x, dragged.y), conn.at);
                const unsigned place = slotPlace(program, conn.target, conn.kind == kConnNext ? -1 : conn.arg);
                bool ok = true;
                if (place != 0)
                    for (int id : chain)
                        if (const BlockDef* def = blockDef(program.node(id)->key); def && !fits(*def, place)) ok = false;
                if (!ok) continue;
            } else {
                continue;
            }
            if (d < best) {
                best = d;
                snap = &conn;
            }
        }
        if (snap) {
            if (snap->kind == kConnNumber || snap->kind == kConnBoolean) {
                draw->AddRect(P.S(snap->min), P.S(snap->max), kWhite, 11.0f * state.zoom, 0, 2.5f);
            } else {
                const ImVec2 at = P.S(snap->at);
                const int pulse = 170 + static_cast<int>(70.0 * std::sin(now * 10.0));
                draw->AddRectFilled(ImVec2(at.x, at.y - 2.5f), ImVec2(at.x + std::max(60.0f, dsize.x * 0.6f) * state.zoom, at.y + 2.5f),
                                    IM_COL32(255, 255, 255, pulse), 2.5f);
            }
        }
    }

    // Lo que se arrastra, por encima de todo (tambien de la paleta).
    if (state.dragging >= 0 && program.node(state.dragging)) {
        Painter D = P;
        D.draw = ImGui::GetForegroundDrawList();
        D.hits = nullptr;
        D.conns = nullptr;
        D.positions = nullptr;
        D.record = nullptr;
        // Levantado: su sombra un poco mas abajo y a la derecha.
        Painter shade = D;
        shade.shadow = true;
        shade.origin = ImVec2(D.origin.x + 5.0f, D.origin.y + 6.0f);
        drawTop(shade, state.dragging);
        drawTop(D, state.dragging);
    }

    // Los botones de abajo a la derecha.
    {
        const bool icons = ui::fonts().icons;
        const float button = 32.0f;
        const float x = cmax.x - 54.0f;
        float y = trashMin.y - 5.0f * (button + 6.0f) - 6.0f;
        auto sideButton = [&](const char* id, const char* glyph, const char* fallback, const char* tip) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            y += button + 6.0f;
            return ui::iconButton(id, glyph, fallback, tip, false, button);
        };
        if (sideButton("zoomin", ui::icon::ZoomIn, "+", es ? "Acercar (Ctrl + rueda)" : "Zoom in (Ctrl + wheel)")) {
            zoomAround(state, (state.easing ? state.zoomTarget : state.zoom) * 1.2f, ImVec2((cmin.x + cmax.x) * 0.5f, (cmin.y + cmax.y) * 0.5f));
            playCue(Cue::ZoomIn, sound);
        }
        if (sideButton("zoomout", ui::icon::ZoomOut, "-", es ? "Alejar" : "Zoom out")) {
            zoomAround(state, (state.easing ? state.zoomTarget : state.zoom) / 1.2f, ImVec2((cmin.x + cmax.x) * 0.5f, (cmin.y + cmax.y) * 0.5f));
            playCue(Cue::ZoomOut, sound);
        }
        if (sideButton("fit", ui::icon::Fit, "=", es ? "Verlo todo" : "Fit everything")) {
            fitAll(state, program, P.look);
            playCue(Cue::ZoomOut, sound);
        }
        if (sideButton("tidy", ui::icon::Sort, "#", es ? "Ordenar los bloques: «al crear», los aciertos, los fallos y lo suelto"
                                                       : "Tidy up: «when created», the hits, the misses and the loose blocks") &&
            !program.tops.empty()) {
            pushUndo(state, writeProgram(program));
            arrange(program);
            fitAll(state, program, P.look, 0.7f);
            playCue(Cue::Tidy, sound);
            changed = true;
        }
        if (env.soundOn) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            y += button + 6.0f;
            if (ui::iconButton("sound", ui::icon::Volume, "S", *env.soundOn ? (es ? "Sonidos: encendidos" : "Sounds: on") : (es ? "Sonidos: apagados" : "Sounds: off"),
                               *env.soundOn, button)) {
                *env.soundOn = !*env.soundOn;
                state.settingsChanged = true;
                playCue(Cue::Success, *env.soundOn);
            }
        }
        const ImU32 trashTint = overTrash ? ui::color::Error : state.dragging >= 0 ? ui::color::Text : ui::color::Muted;
        draw->AddRectFilled(trashMin, trashMax, overTrash ? IM_COL32(240, 100, 90, 50) : IM_COL32(255, 255, 255, 10), 10.0f);
        if (icons) {
            const float glyph = overTrash ? 34.0f : 28.0f;
            const ImVec2 size = ui::fonts().ui->CalcTextSizeA(glyph, FLT_MAX, 0.0f, ui::icon::Delete);
            draw->AddText(ui::fonts().ui, glyph, ImVec2((trashMin.x + trashMax.x - size.x) * 0.5f, (trashMin.y + trashMax.y - size.y) * 0.5f - 2.0f),
                          trashTint, ui::icon::Delete);
        } else {
            draw->AddText(ImVec2(trashMin.x + 8.0f, trashMin.y + 18.0f), trashTint, es ? "papelera" : "trash");
        }
    }

    // Los avisos, abajo a la izquierda, como en App Inventor.
    int warnings = 0, errors = 0, infos = 0;
    for (const BlockConflict& conflict : env.conflicts) {
        if (conflict.severity == Severity::Error) ++errors;
        else if (conflict.severity == Severity::Warning) ++warnings;
        else ++infos;
    }
    {
        ImGui::SetCursorScreenPos(ImVec2(cmin.x + 12.0f, cmax.y - 40.0f));
        char label[160];
        std::snprintf(label, sizeof(label), "%s %d   %s %d   %s %d   %s", ui::fonts().icons ? ui::icon::Warning : "!", warnings,
                      ui::fonts().icons ? ui::icon::Error : "x", errors, ui::fonts().icons ? ui::icon::Info : "i", infos,
                      state.showWarnings ? (es ? "Ocultar avisos" : "Hide warnings") : (es ? "Mostrar avisos" : "Show warnings"));
        {
            const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
            draw->AddRectFilled(ImVec2(cmin.x + 8.0f, cmax.y - 44.0f), ImVec2(cmin.x + 8.0f + w, cmax.y - 8.0f), IM_COL32(22, 24, 31, 235), 8.0f);
            draw->AddRect(ImVec2(cmin.x + 8.0f, cmax.y - 44.0f), ImVec2(cmin.x + 8.0f + w, cmax.y - 8.0f), ui::color::Border, 8.0f);
        }
        if (ui::flatButton(label, es ? "Lo que el motor elegido no puede hacer y los bloques que no hacen nada"
                                     : "What the chosen engine can't do and the blocks that do nothing"))
        {
            state.showWarnings = !state.showWarnings;
            playCue(Cue::Click, sound);
        }
        if (state.showWarnings && !env.conflicts.empty()) {
            const float h = std::min(220.0f, 30.0f + 24.0f * static_cast<float>(env.conflicts.size()));
            ImGui::SetCursorScreenPos(ImVec2(cmin.x + 12.0f, cmax.y - 46.0f - h));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.12f, 0.155f, 0.97f));
            ImGui::BeginChild("warnings", ImVec2(std::min(560.0f, csize.x - 90.0f), h), ImGuiChildFlags_Borders);
            ImGui::PopStyleColor();
            int index = 0;
            for (const BlockConflict& conflict : env.conflicts) {
                ImGui::PushID(index++);
                const ImU32 tint = conflict.severity == Severity::Info ? ui::color::Info
                                   : conflict.severity == Severity::Error ? ui::color::Error : ui::color::Warning;
                ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(tint));
                ImGui::TextUnformatted(ui::fonts().icons ? (conflict.severity == Severity::Info ? ui::icon::Info : ui::icon::Warning) : "!");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f);
                const std::string textLine = (conflict.key.empty() ? std::string() : blockName(conflict.key, es) + ": ") + (es ? conflict.es : conflict.en);
                if (ImGui::Selectable(textLine.c_str(), false) && conflict.node >= 0) {
                    state.selected = conflict.node;
                    const auto at = positions.find(conflict.node);
                    if (at != positions.end()) centerOn(state, at->second);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }
    draw->PopClipRect();

    // ---------------------------------------------------------------- raton --
    auto hitAt = [&](ImVec2 point) -> const Hit* {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (inside(point, it->min, it->max)) return &*it;
        return nullptr;
    };
    auto startEdit = [&](int id, int arg) {
        const BlockNode* node = program.node(id);
        if (!node) return;
        state.editId = id;
        state.editArg = arg;
        state.editFocus = true;
        std::snprintf(state.editBuffer.data(), state.editBuffer.size(), "%s", node->args[static_cast<size_t>(arg)].value.c_str());
    };

    if (state.dragging >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const int id = state.dragging;
            state.dragging = -1;
            if (overTrash || overPalette) {
                removeStack(program, id);
                if (state.selected == id) state.selected = -1;
                // Lo que vuelve a la paleta sin haber estado en el lienzo no hace ruido.
                if (overTrash || writeProgram(program) != state.before) {
                    state.poofAt = mouse;
                    state.poofStart = now;
                    playCue(Cue::Delete, sound);
                }
            } else if (snap && program.node(id)) {
                state.glowId = id;
                state.glowStart = now;
                playCue(Cue::Snap, sound);
                const BlockNode& node = *program.node(id);
                switch (snap->kind) {
                    case kConnNext: attachAfter(program, snap->target, id); break;
                    case kConnBody: attachBody(program, snap->target, snap->arg, id); break;
                    case kConnAbove: {
                        const BlockNode* below = program.node(snap->target);
                        const ImVec2 size = stackSize(program, id, es, env.typeName);
                        attachAbove(program, snap->target, id, below ? below->x : node.x, below ? below->y - size.y : node.y);
                        bumpApart(program, id, P.look);
                        break;
                    }
                    case kConnNumber:
                    case kConnBoolean: attachValue(program, snap->target, snap->arg, id, snap->max.x + 24.0f, snap->min.y + 24.0f); break;
                    default: break;
                }
            } else {
                bumpApart(program, id, P.look);
                playCue(Cue::Drop, sound);
            }
            const std::string now = writeProgram(program);
            if (now != state.before) {
                pushUndo(state, state.before);
                changed = true;
            }
        }
    } else if (hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const Hit* at = hitAt(mouse);
            state.editId = -1;
            if (at) {
                state.pressed = at->id;
                state.pressedAt = mouse;
                if (at->kind == kHitLiteral) startEdit(at->id, at->arg);
                else if (at->kind == kHitChoice || at->kind == kHitSound || at->kind == kHitColor) {
                    state.menuId = at->id;
                    state.menuArg = at->arg;
                    ImGui::OpenPopup(at->kind == kHitColor ? "blockcolor" : "blockchoice");
                } else {
                    state.selected = at->id;
                }
            } else {
                state.pressed = -1;
                state.panning = true;
                state.selected = -1;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const Hit* at = hitAt(mouse);
            if (at) {
                state.contextId = at->id;
                state.selected = at->id;
                ImGui::OpenPopup("blockmenu");
            } else {
                state.contextAt = ImVec2((mouse.x - P.origin.x) / state.zoom, (mouse.y - P.origin.y) / state.zoom);
                ImGui::OpenPopup("canvasmenu");
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) state.panning = true;
        if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
            if (io.KeyCtrl) zoomAround(state, state.zoom * (io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f), mouse);
            else {
                state.easing = false;
                if (io.KeyShift) state.pan.x += io.MouseWheel * 48.0f;
                else {
                    state.pan.y += io.MouseWheel * 48.0f;
                    state.pan.x += io.MouseWheelH * 48.0f;
                }
            }
        }
    }
    // Arrastrar un bloque del lienzo: sale de donde estaba con los de debajo.
    if (state.dragging < 0 && state.pressed >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        distance(mouse, state.pressedAt) > 5.0f && program.node(state.pressed)) {
        const int id = state.pressed;
        const auto at = positions.find(id);
        const ImVec2 pos = at != positions.end() ? at->second : ImVec2(program.node(id)->x, program.node(id)->y);
        state.before = writeProgram(program);
        placeTop(program, id, pos.x, pos.y);
        // Cogido donde se pulso: el bloque no salta al pasar el umbral de arrastre.
        state.grab = ImVec2((state.pressedAt.x - P.origin.x) / state.zoom - pos.x, (state.pressedAt.y - P.origin.y) / state.zoom - pos.y);
        state.dragging = id;
        state.pressed = -1;
        state.editId = -1;
        state.selected = id;
        playCue(Cue::Pick, sound);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) state.pressed = -1;
    if (state.panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            state.easing = false;
            state.pan.x += io.MouseDelta.x;
            state.pan.y += io.MouseDelta.y;
        } else {
            state.panning = false;
        }
    }

    // La ranura que se esta escribiendo.
    if (state.editId >= 0) {
        const Hit* slot = nullptr;
        for (const Hit& h : hits)
            if (h.id == state.editId && h.arg == state.editArg && h.kind == kHitLiteral) slot = &h;
        BlockNode* node = program.node(state.editId);
        const BlockDef* def = node ? blockDef(node->key) : nullptr;
        if (!slot || !def || state.editArg < 0 || state.editArg >= static_cast<int>(def->args.size())) {
            state.editId = -1;
        } else {
            const ArgDef& arg = def->args[static_cast<size_t>(state.editArg)];
            const float width = std::max(slot->max.x - slot->min.x, 72.0f);
            ImGui::SetCursorScreenPos(ImVec2(slot->min.x, slot->min.y));
            ImGui::SetNextItemWidth(width);
            if (state.editFocus) {
                ImGui::SetKeyboardFocusHere();
                state.editFocus = false;
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, std::max(1.0f, (slot->max.y - slot->min.y - ImGui::GetFontSize()) * 0.5f)));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.17f, 0.22f, 1.0f));
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue |
                                              (arg.kind == ArgKind::Number ? ImGuiInputTextFlags_CharsScientific : 0);
            const bool enter = ImGui::InputText("##slotedit", state.editBuffer.data(), state.editBuffer.size(), flags);
            const bool done = enter || ImGui::IsItemDeactivatedAfterEdit();
            const bool left = ImGui::IsItemDeactivated();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            if (done) {
                std::string value = state.editBuffer.data();
                if (arg.kind == ArgKind::Number) {
                    char* end = nullptr;
                    const float parsed = std::strtof(value.c_str(), &end);
                    value = (end == value.c_str() || !std::isfinite(parsed)) ? node->args[static_cast<size_t>(state.editArg)].value
                                                                              : numberText(std::clamp(parsed, arg.min, arg.max));
                }
                if (value != node->args[static_cast<size_t>(state.editArg)].value) {
                    pushUndo(state, writeProgram(program));
                    node->args[static_cast<size_t>(state.editArg)].value = value;
                    playCue(Cue::Edit, sound);
                    changed = true;
                }
            }
            if (left || done) state.editId = -1;
        }
    }

    // Desplegables.
    if (ImGui::BeginPopup("blockchoice")) {
        BlockNode* node = program.node(state.menuId);
        const BlockDef* def = node ? blockDef(node->key) : nullptr;
        if (def && state.menuArg >= 0 && state.menuArg < static_cast<int>(def->args.size())) {
            const ArgDef& arg = def->args[static_cast<size_t>(state.menuArg)];
            std::string& value = node->args[static_cast<size_t>(state.menuArg)].value;
            auto choose = [&](const std::string& chosen) {
                if (chosen == value) return;
                pushUndo(state, writeProgram(program));
                value = chosen;
                playCue(Cue::Choose, sound);
                changed = true;
            };
            if (arg.kind == ArgKind::Choice) {
                for (const ArgChoice& item : arg.choices)
                    if (ImGui::Selectable(es ? item.es : item.en, value == item.value)) choose(item.value);
            } else if (arg.kind == ArgKind::Variable) {
                const auto variables = blockVariables(program);
                if (variables.empty()) ui::caption(es ? "Primero añade «definir número» en Variables." : "First add «define number» in Variables.");
                for (const auto& variable : variables) {
                    const std::string key = std::to_string(variable.id);
                    const std::string label = variable.name + "  #" + key;
                    if (ImGui::Selectable(label.c_str(), value == key)) choose(key);
                }
            } else if (arg.kind == ArgKind::Sound || arg.kind == ArgKind::Image || arg.kind == ArgKind::Video) {
                if (env.resourcePicker && ImGui::Selectable(es ? "Explorar recursos del mod…" : "Browse mod resources…")) {
                    env.resourcePicker(state.menuId, state.menuArg, arg.kind);
                    ImGui::CloseCurrentPopup();
                }
                if (!value.empty() && ImGui::Selectable(es ? "Quitar referencia" : "Clear reference")) choose("");
                if (arg.kind == ArgKind::Sound) {
                if (env.sounds.empty())
                    ui::caption(es ? "Este mod no tiene sonidos en sounds/." : "This mod has no sounds in sounds/.");
                for (const std::string& sound : env.sounds)
                    if (ImGui::Selectable(sound.c_str(), value == sound)) choose(sound);
                ImGui::Separator();
                static std::array<char, 96> other{};
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputTextWithHint("##othersound", es ? "Otro nombre (sin .ogg)" : "Another name (no .ogg)", other.data(), other.size(),
                                             ImGuiInputTextFlags_EnterReturnsTrue) &&
                    other[0]) {
                    choose(other.data());
                    other.fill('\0');
                    ImGui::CloseCurrentPopup();
                }
                }
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("blockcolor")) {
        BlockNode* node = program.node(state.menuId);
        if (node && state.menuArg >= 0 && state.menuArg < static_cast<int>(node->args.size())) {
            std::string& value = node->args[static_cast<size_t>(state.menuArg)].value;
            unsigned rgb = 0xFFFFFF;
            std::sscanf(value.c_str(), "%x", &rgb);
            float color[3] = {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                              static_cast<float>(rgb & 0xFF) / 255.0f};
            const char* swatches[] = {"FFFFFF", "000000", "F9393F", "12FA05", "00FFFF", "C24B99", "FFD700", "8B0000"};
            int index = 0;
            for (const char* swatch : swatches) {
                unsigned v = 0;
                std::sscanf(swatch, "%x", &v);
                if (index++ % 8) ImGui::SameLine(0.0f, 4.0f);
                ImGui::PushID(swatch);
                if (ImGui::ColorButton("swatch", ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, 1.0f),
                                       ImGuiColorEditFlags_NoTooltip, ImVec2(22.0f, 22.0f)) &&
                    value != swatch) {
                    pushUndo(state, writeProgram(program));
                    value = swatch;
                    playCue(Cue::Choose, sound);
                    changed = true;
                }
                ImGui::PopID();
            }
            if (ImGui::ColorPicker3("##picker", color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "%02X%02X%02X", static_cast<int>(std::lround(color[0] * 255.0f)),
                              static_cast<int>(std::lround(color[1] * 255.0f)), static_cast<int>(std::lround(color[2] * 255.0f)));
                if (value != hex) {
                    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || state.undo.empty()) pushUndo(state, writeProgram(program));
                    value = hex;
                    changed = true;
                }
            }
        }
        ImGui::EndPopup();
    }

    auto copyStack = [&](int id) {
        BlockProgram clip;
        const int copy = copyStackFrom(clip, program, id);
        if (copy >= 0) {
            placeTop(clip, copy, 0.0f, 0.0f);
            clip.tops.erase(std::remove(clip.tops.begin(), clip.tops.end(), copy), clip.tops.end());
            clip.tops.insert(clip.tops.begin(), copy);
            state.clipboard = writeProgram(clip);
            playCue(Cue::Click, sound);
        }
    };
    auto paste = [&](ImVec2 at) {
        if (state.clipboard.empty()) return;
        const BlockProgram clip = readProgram(state.clipboard);
        if (clip.tops.empty()) return;
        pushUndo(state, writeProgram(program));
        const int copy = copyStackFrom(program, clip, clip.tops.front());
        if (copy >= 0) {
            placeTop(program, copy, at.x, at.y);
            state.selected = copy;
            state.glowId = copy;
            state.glowStart = now;
            playCue(Cue::Duplicate, sound);
            changed = true;
        }
    };
    auto duplicate = [&](int id) {
        const BlockNode* node = program.node(id);
        if (!node) return;
        const auto at = positions.find(id);
        const ImVec2 pos = at != positions.end() ? at->second : ImVec2(node->x, node->y);
        pushUndo(state, writeProgram(program));
        const int copy = duplicateStack(program, id);
        if (copy >= 0) {
            placeTop(program, copy, pos.x + 28.0f, pos.y + 28.0f);
            bumpApart(program, copy, P.look);
            state.selected = copy;
            state.glowId = copy;
            state.glowStart = now;
            playCue(Cue::Duplicate, sound);
            changed = true;
        }
    };

    // Menu de un bloque.
    if (ImGui::BeginPopup("blockmenu")) {
        const BlockNode* node = program.node(state.contextId);
        const BlockDef* def = node ? blockDef(node->key) : nullptr;
        if (def) {
            ImGui::TextDisabled("%s", blockName(node->key, es).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(es ? "Duplicar" : "Duplicate", "Ctrl+D")) duplicate(state.contextId);
            if (ImGui::MenuItem(es ? "Copiar" : "Copy", "Ctrl+C")) copyStack(state.contextId);
            if (ImGui::MenuItem(es ? "Borrar el bloque" : "Delete the block", "Supr")) {
                pushUndo(state, writeProgram(program));
                removeBlock(program, state.contextId);
                state.selected = -1;
                state.poofAt = ImGui::GetMousePosOnOpeningCurrentPopup();
                state.poofStart = now;
                playCue(Cue::Delete, sound);
                changed = true;
            }
            if (isStatement(def) || def->shape == BlockShape::Hat)
                if (ImGui::MenuItem(es ? "Borrar el bloque y los de debajo" : "Delete it and the blocks below")) {
                    pushUndo(state, writeProgram(program));
                    removeStack(program, state.contextId);
                    state.selected = -1;
                    state.poofAt = ImGui::GetMousePosOnOpeningCurrentPopup();
                    state.poofStart = now;
                    playCue(Cue::Delete, sound);
                    changed = true;
                }
            ImGui::Separator();
            if (ImGui::BeginMenu(es ? "Ayuda" : "Help")) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextUnformatted(tooltipOf(*def, es).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
    // Menu del lienzo.
    if (ImGui::BeginPopup("canvasmenu")) {
        if (ImGui::MenuItem(es ? "Pegar" : "Paste", "Ctrl+V", false, !state.clipboard.empty())) paste(state.contextAt);
        if (ImGui::MenuItem(es ? "Deshacer" : "Undo", "Ctrl+Z", false, !state.undo.empty()) && step(state.undo, state.redo, program)) {
            playCue(Cue::Undo, sound);
            changed = true;
        }
        if (ImGui::MenuItem(es ? "Rehacer" : "Redo", "Ctrl+Y", false, !state.redo.empty()) && step(state.redo, state.undo, program)) {
            playCue(Cue::Redo, sound);
            changed = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu(es ? "Añadir evento" : "Add event")) {
            struct Item { const char* key; const char* who; const char* en; const char* es; };
            const Item items[] = {{"event.create", nullptr, "when created", "al crear"},
                                  {"event.hit", "player", "when the player hits", "cuando el jugador toca"},
                                  {"event.hit", "opponent", "when the opponent hits", "cuando el rival toca"},
                                  {"event.hit", "any", "when anyone hits", "cuando cualquiera toca"},
                                  {"event.miss", nullptr, "when the player misses", "cuando el jugador falla"}};
            for (const Item& item : items)
                if (ImGui::MenuItem(es ? item.es : item.en)) {
                    pushUndo(state, writeProgram(program));
                    const int hat = newBlock(program, item.key);
                    if (item.who) program.node(hat)->args[0].value = item.who;
                    placeTop(program, hat, state.contextAt.x, state.contextAt.y);
                    bumpApart(program, hat, P.look);
                    state.glowId = hat;
                    state.glowStart = now;
                    playCue(Cue::Add, sound);
                    changed = true;
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(es ? "Ordenar los bloques" : "Tidy up the blocks", nullptr, false, !program.tops.empty())) {
            pushUndo(state, writeProgram(program));
            arrange(program);
            fitAll(state, program, P.look, 0.7f);
            playCue(Cue::Tidy, sound);
            changed = true;
        }
        if (ImGui::MenuItem(es ? "Verlo todo" : "Fit everything")) {
            fitAll(state, program, P.look);
            playCue(Cue::ZoomOut, sound);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(es ? "Borrar todo" : "Delete everything", nullptr, false, !program.nodes.empty())) {
            pushUndo(state, writeProgram(program));
            program = BlockProgram{};
            state.selected = -1;
            state.poofAt = ImGui::GetMousePosOnOpeningCurrentPopup();
            state.poofStart = now;
            playCue(Cue::Delete, sound);
            changed = true;
        }
        ImGui::EndPopup();
    }

    // Teclado: con el raton en el lienzo o un bloque elegido.
    state.wantsKeys = (hovered || (state.selected >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) && !io.WantTextInput;
    if (state.wantsKeys) {
        if (state.selected >= 0 && program.node(state.selected) &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            const auto rect = state.blockRects.find(state.selected);
            if (rect != state.blockRects.end()) {
                state.poofAt = ImVec2(rect->second.x + rect->second.z * 0.5f, rect->second.y + rect->second.w * 0.5f);
                state.poofStart = now;
            }
            pushUndo(state, writeProgram(program));
            removeBlock(program, state.selected);
            state.selected = -1;
            playCue(Cue::Delete, sound);
            changed = true;
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) && step(state.undo, state.redo, program)) {
            playCue(Cue::Undo, sound);
            changed = true;
        }
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) &&
            step(state.redo, state.undo, program)) {
            playCue(Cue::Redo, sound);
            changed = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && state.selected >= 0) duplicate(state.selected);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && state.selected >= 0) copyStack(state.selected);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && mouseInCanvas)
            paste(ImVec2((mouse.x - P.origin.x) / state.zoom, (mouse.y - P.origin.y) / state.zoom));
    }

    // La ayuda de un bloque tras un momento encima, o la de su aviso enseguida.
    const Hit* under = hovered && state.dragging < 0 ? hitAt(mouse) : nullptr;
    const int hoverKey = under ? under->id * 16 + under->kind : -1;
    if (hoverKey != state.hoverKey) {
        state.hoverKey = hoverKey;
        state.hoverSince = ImGui::GetTime();
    }
    if (under && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (under->kind == kHitBadge) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            for (const BlockConflict& conflict : env.conflicts)
                if (conflict.node == under->id) ImGui::BulletText("%s", (es ? conflict.es : conflict.en).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        } else if (ImGui::GetTime() - state.hoverSince > 0.8) {
            if (const BlockNode* node = program.node(under->id))
                if (const BlockDef* def = blockDef(node->key)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                    ImGui::TextUnformatted(tooltipOf(*def, es).c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
        }
    }
    if (state.selected >= 0 && !program.node(state.selected)) state.selected = -1;
    // «Puf»: una nubecilla que se abre y se apaga donde se borro algo.
    if (now - state.poofStart < 0.45) {
        const float t = static_cast<float>((now - state.poofStart) / 0.45);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        for (int i = 0; i < 10; ++i) {
            const float a = static_cast<float>(i) * 0.6283f + t * 0.8f;
            const float r = 8.0f + 34.0f * t;
            fg->AddCircleFilled(ImVec2(state.poofAt.x + std::cos(a) * r, state.poofAt.y + std::sin(a) * r), 2.0f + 5.0f * (1.0f - t),
                                IM_COL32(220, 222, 230, static_cast<int>(200.0f * (1.0f - t))));
        }
    }
    ImGui::EndChild();
    return changed;
}

}  // namespace nlblocks
