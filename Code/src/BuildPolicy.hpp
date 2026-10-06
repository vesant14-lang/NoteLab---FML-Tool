#pragma once

namespace nlbuild {
inline constexpr bool audioPlayback = true;
inline constexpr bool blockAudioPlayback = false;
// Los avisos del tutorial suenan aparte: los del editor de bloques siguen apagados.
// 1.0.3, primera build publica de la v1: silenciados por pedido del autor (6 oct
// 2026) y sin sus .wav en los paquetes; su interruptor no se muestra.
inline constexpr bool tutorialAudioPlayback = false;
inline constexpr const char* version = "1.0.3";
}
