// Note Lab — el editor de bloques: paleta y lienzo como Scratch o App
// Inventor, especializado en notas de FNF (DESIGN_PLUGIN_NOTE_LAB §29).
//
// El programa (NoteBlocks.hpp) es del nucleo; aqui solo se dibuja y se edita:
// arrastrar desde la paleta, encajar por las muescas, enchufar valores,
// escribir numeros y textos en su ranura, desplegables, papelera, zoom,
// deshacer y los avisos marcados sobre cada bloque.
#pragma once

#include "../core/NoteBlocks.hpp"
#include "../third_party/imgui/imgui.h"
#include "BuildPolicy.hpp"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nlblocks {

// Los sonidos de la interfaz, sintetizados (sin archivos): coger, encajar,
// soltar, borrar, preset, deshacer, rehacer, escribir, elegir, duplicar,
// ordenar, zoom, clic, exito, error y anadir.
enum class Cue { Pick, Snap, Drop, Delete, Preset, Undo, Redo, Edit, Choose, Duplicate, Tidy, ZoomIn, ZoomOut, Click, Success, Error, Add };
void playCue(Cue cue, bool enabled);
// Los del tutorial, aparte (los del editor siguen apagados): mision cumplida,
// tutorial terminado y el aviso de que hay uno. Campanas cortas y suaves.
enum class TutorialCue { Mission, Finished, Offer };
void playTutorialCue(TutorialCue cue, bool enabled);
// El .wav del tutorial (carpeta `sounds` junto al exe), en estereo a 48 kHz; vacio si no esta.
std::vector<float> loadTutorialCue(TutorialCue cue);
// Cada sonido en un WAV (16 bits, estereo) en `folder`, para oirlos o medirlos;
// escribe el pico y el nivel medio de cada uno por la salida.
bool writeCues(const std::string& folder);

struct CanvasState {
    ImVec2 pan{28.0f, 28.0f};        // en pixeles de pantalla
    float zoom = 1.0f;
    int selected = -1;
    // Arrastre: la pila ya esta suelta en el programa y sigue al raton.
    int dragging = -1;
    ImVec2 grab{0.0f, 0.0f};         // donde se cogio, en unidades del lienzo
    int pressed = -1;                // bloque pulsado aun sin moverse
    ImVec2 pressedAt{0.0f, 0.0f};
    bool panning = false;
    std::string before;              // el programa antes del arrastre
    // Edicion de una ranura.
    int editId = -1, editArg = -1;
    bool editFocus = false;
    std::array<char, 128> editBuffer{};
    int menuId = -1, menuArg = -1;   // desplegable abierto
    int contextId = -1;              // menu de un bloque
    ImVec2 contextAt{0.0f, 0.0f};    // donde se abrio el menu del lienzo (unidades del lienzo)
    // Paleta.
    int jumpCategory = -1;
    std::array<char, 128> search{};
    // Un bundle de bloques (el del tutorial): la paleta solo ensena esos, por
    // pasos, hasta que se pide verlos todos.
    std::vector<std::string> onlyBlocks;
    bool showAllBlocks = false;
    std::array<bool, fml::notelab::kBlockCategories> collapsed{};
    ImVec4 searchRect{};
    std::array<ImVec4, fml::notelab::kBlockCategories> categoryHeaderRects{};
    std::array<float, fml::notelab::kBlockCategories> categoryY{};
    // Deshacer (fotos del programa en JSON) y el portapapeles.
    std::vector<std::string> undo, redo;
    std::string clipboard;
    bool showWarnings = false;
    std::string owner;               // el tipo que se edita: al cambiar, se limpia
    // Lo que midio el ultimo cuadro.
    ImVec2 canvasMin{0.0f, 0.0f}, canvasMax{0.0f, 0.0f};
    ImVec2 paletteMin{0.0f, 0.0f}, paletteMax{0.0f, 0.0f};
    bool wantsKeys = false;          // el lienzo usa Supr, Ctrl+Z...
    bool fitPending = false;         // encuadrar el programa en cuanto se sepa el tamano del lienzo
    // Donde quedo cada cosa en el ultimo cuadro (pantalla: x, y, ancho, alto),
    // para la prueba de interfaz (--ui-test=blocks).
    std::array<ImVec4, fml::notelab::kBlockCategories> categoryRects{};
    std::map<std::string, ImVec4> paletteRects;
    std::map<int, ImVec4> blockRects;
    std::map<long long, ImVec4> slotRects;       // id * 16 + argumento
    std::map<long long, ImVec2> bodyPoints;      // id * 16 + argumento: donde encaja el primero del cuerpo
    ImVec4 trashRect{};
    // Animaciones: el brillo de un bloque que acaba de encajar, el «puf» al
    // borrar y el zoom y el encuadre suaves.
    int glowId = -1;
    double glowStart = -10.0;
    ImVec2 poofAt{0.0f, 0.0f};
    double poofStart = -10.0;
    ImVec2 panTarget{28.0f, 28.0f};
    float zoomTarget = 1.0f;
    bool easing = false;
    bool settingsChanged = false;    // el sonido se encendio o apago: hay que guardarlo
    int hoverKey = -1;               // lo que hay bajo el raton, para la ayuda con retardo
    double hoverSince = 0.0;
};

struct CanvasEnv {
    bool spanish = false;
    std::string typeName;
    fml::notelab::Engine engine = fml::notelab::Engine::Psych;
    std::vector<std::string> sounds;                       // los de la carpeta sounds/ del mod
    std::vector<fml::notelab::BlockConflict> conflicts;    // del motor que se ve
    bool* soundOn = nullptr;         // sonidos de la interfaz; nullptr sin sonido (capturas y pruebas)
    std::function<void(int, int, fml::notelab::ArgKind)> resourcePicker;
};

// La paleta y el lienzo en el espacio disponible. Devuelve true si el programa cambio.
bool drawCanvas(CanvasState& state, fml::notelab::BlockProgram& program, const CanvasEnv& env);
// Una foto para deshacer antes de un cambio hecho desde fuera (un preset).
void remember(CanvasState& state, const fml::notelab::BlockProgram& program);
// Las pilas que no estaban en `before`, colocadas debajo de las demas.
void placeNewStacks(fml::notelab::BlockProgram& program, const std::vector<int>& before);
// Todas las pilas en una columna: «al crear», los aciertos, los fallos y lo suelto.
void arrange(fml::notelab::BlockProgram& program);
// La altura y la anchura de una pila, en unidades del lienzo.
ImVec2 stackSize(const fml::notelab::BlockProgram& program, int first, bool spanish, const std::string& typeName);

}  // namespace nlblocks
