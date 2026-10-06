// fml_audio — Reloj y mezclador. El corazon de la fase 4.
//
// Es el spike de la fase 0 convertido en modulo, con las tres reglas que aquel
// destapo (ver DESIGN §1.2). Resumen de por que esta escrito asi:
//
//   * TODAS las pistas se leen del MISMO playhead en el mismo bucle del mixer.
//     El desfase entre Inst y Voices no es "pequeno": es identicamente cero,
//     porque no hay dos relojes que sincronizar.
//   * El getter de posicion y el seek viven en el MISMO sistema de coordenadas.
//     Si el getter resta la latencia y el seek no la vuelve a sumar, cada salto
//     pierde esos milisegundos (el frame-step perdia 30 ms exactos, medido).
//   * El seek lleva CONTADOR DE GENERACION, no un flag. Con un flag, quien lee
//     la posicion ve el snapshot anterior durante un callback entero y vuelve a
//     disparar el mismo seek: el loop A-B se repetia 15 veces en vez de 5.
//
// La posicion que se expone se interpola contra el reloj del host, con techo en
// el proximo callback y monotonia forzada. El snapshot crudo escalona al tamano
// del buffer: el 62 % de las lecturas de un bucle a 370 Hz eran repeticiones.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fml {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(std::string* error);
    void shutdown();
    bool ready() const;

    // Carga N pistas y las deja alineadas al mismo playhead. Sustituye lo que
    // hubiera cargado. `paths` son rutas reales del disco.
    // Devuelve cuantas cargaron; deja en `error` la primera que fallo.
    int  loadTracks(const std::vector<std::string>& paths, std::string* error);
    // Variante para la aplicacion: decodifica en un unico worker y publica el
    // banco completo de forma atomica. Una carga nueva reemplaza la solicitud
    // anterior sin abrir varios decoders ni bloquear el hilo de UI.
    void loadTracksAsync(const std::vector<std::string>& paths);
    bool pollTrackLoad(int* loaded = nullptr, std::string* error = nullptr);
    bool trackLoadPending();
    void clearTracks();

    size_t trackCount() const;
    // Devuelve una copia porque el banco completo puede cambiar mientras el
    // hilo de audio conserva el banco anterior.
    std::string trackName(size_t i) const;
    void  setTrackGain(size_t i, float gain);
    float trackGain(size_t i) const;
    // -1 izquierda, 0 centro, +1 derecha. Vive en la pista como la ganancia
    // y lo lee el hilo de audio en cada bloque.
    void  setTrackPan(size_t i, float pan);
    void  setMasterGain(float g);
    float masterGain() const;

    double durationMs() const;
    // Cuanto dura UNA pista. `durationMs` es la mas larga del banco, y un
    // clip que se estira a eso puede pasarse del final de su propio audio si
    // otra pista -el sonido de un video- es mas larga que la suya.
    double trackDurationMs(int track) const;

    // --- transporte ---
    void   play();
    void   pause();
    bool   playing() const;
    void   seekMs(double ms);
    double positionMs() const;      // interpolada, monotona
    void   setRate(float r);
    float  rate() const;
    void   stepFrames(int frames, float fps = 60.0f);   // avance en pausa

    // Loop de seccion. a<0 lo desactiva.
    void setLoop(double aMs, double bMs);
    void clearLoop();
    bool hasLoop() const;

    // Memoria del PCM decodificado (i16, la mitad que f32).
    size_t pcmBytes() const;

    // Pico absoluto normalizado por columna, calculado sobre todas las pistas
    // y canales ya decodificados. Es una copia pequena para dibujar waveform
    // en la UI; el callback de audio conserva su banco inmutable y sin locks.
    // `track` < 0 mezcla el banco entero; con un indice valido devuelve solo
    // esa pista, que es lo que permite dibujar Inst y Voices por separado.
    std::vector<float> waveformPeaks(size_t columns, int track = -1) const;

    // Analisis local para visualizadores. Devuelve bandas logaritmicas entre
    // graves y agudos alrededor de un instante del mismo PCM que se oye. No
    // abre otro decoder ni usa el callback de audio como fuente de estado.
    std::vector<float> spectrumBands(double positionMs, size_t bands,
                                     int track = -1) const;

    // --- montaje: que trozo de cada pista suena, y donde ---
    //
    // El visualizador puede cortar un clip de audio y mover el trozo a otro
    // sitio. Hasta ahora eso solo movia una VENTANA DE GANANCIA sobre la
    // cancion: la pista seguia sonando en su sitio, asi que mover un trozo
    // hacia atras hacia sonar el audio que ya vivia alli -y un corte no
    // quitaba nada, solo lo callaba-. Con el mapa, la pista deja de sonar
    // fuera de sus clips y cada clip suena EL TROZO QUE LLEVA DENTRO.
    struct TrackClip {
        double startMs = 0.0;         // donde empieza a sonar en el documento
        double endMs = 0.0;
        double sourceStartMs = 0.0;   // que instante de la pista suena ahi
        float  gain = 1.0f;           // multiplica a la ganancia de la pista
    };
    // Sustituye el mapa entero. Una pista sin clips suena entera y en su sitio,
    // que es como se ha comportado siempre: el mapa vacio es el estado normal
    // del reproductor y de la preview del chart.
    void setTrackClips(const std::vector<std::vector<TrackClip>>& perTrack);
    void clearTrackClips();

    // --- mezcla offline, para exportar ---
    // El video exportado tiene que sonar como la preview, y la preview es ESTE
    // banco con ESTAS ganancias. Se mezcla al margen del callback: no toca el
    // reloj, el dispositivo ni la posicion, asi que se puede pedir mientras
    // suena. Va a velocidad 1.0 por definicion -un fichero no se exporta a
    // camara lenta-, por eso no interpola como el mixer.
    int sampleRate() const;
    int channels() const;
    // `gains` es la ganancia de cada pista para ESTE bloque, la misma que se le
    // pasaria a setTrackGain; las pistas que falten en la lista cuentan como
    // silencio, no como sonando. Rellena `out` con `frames * channels()`
    // muestras i16 intercaladas y devuelve cuantos frames traian contenido:
    // pasado el final del banco escribe silencio y devuelve menos.
    std::size_t renderMix(double fromMs, std::size_t frames,
                          const std::vector<float>& gains,
                          std::vector<short>& out,
                          const std::vector<float>* pans = nullptr) const;

    // Ventana cruda de amplitud con signo alrededor de un instante, para las
    // formas de onda tipo osciloscopio. Es la misma memoria decodificada: no
    // hay un segundo muestreo ni una copia del PCM completo.
    std::vector<float> waveformWindow(double centerMs, double spanMs,
                                      size_t columns, int track = -1) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace fml
