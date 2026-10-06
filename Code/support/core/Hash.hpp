#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace fml {

// SHA-256 en hexadecimal minusculo. Se usa para fingerprints de scripts,
// workspaces y permisos; no depende de CryptoAPI para mantener los probes
// headless reproducibles fuera de Windows.
std::string sha256Hex(const std::string& bytes);

// Variante incremental para corpus grandes (audio/video). Nunca carga el
// archivo entero en memoria.
std::optional<std::string> sha256File(const std::filesystem::path& path);

}  // namespace fml
