#pragma once

#include "PsychSource.hpp"
#include <cctype>

namespace fml::psych {

struct SongAudio {
    std::string songPath;
    std::string instPath, voicesPath, playerVoicesPath, opponentVoicesPath;
    bool hasVoices() const {
        return !voicesPath.empty() || !playerVoicesPath.empty() || !opponentVoicesPath.empty();
    }
};

// Paths.formatToSongPath, not the chart's filename or difficulty label.
inline std::string formatSongPath(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::string(".,'\"%?!").find(c) != std::string::npos) continue;
        if (std::isspace(c) || std::string("~&;:<>#").find(c) != std::string::npos) out += '-';
        else out += static_cast<char>(std::tolower(c));
    }
    return out;
}

inline SongAudio resolveSongAudio(const std::vector<std::filesystem::path>& sources,
                                  const std::filesystem::path& chartPath,
                                  const std::string& fallbackId) {
    using json = nlohmann::json;
    SongAudio audio;
    const json root = json::parse(read(chartPath), nullptr, false, true);
    const json* song = &root;
    if (root.is_object() && root.contains("song") && root["song"].is_object()) song = &root["song"];
    auto field = [&](const char* key) -> std::string {
        return song->is_object() && song->contains(key) && (*song)[key].is_string()
            ? (*song)[key].get<std::string>() : std::string();
    };
    const std::vector<std::string> candidates{formatSongPath(field("song")), fallbackId,
        chartPath.parent_path().filename().u8string()};
    for (const auto& id : candidates) {
        if (!relativeKey(id)) continue;
        const auto inst = resolve(sources, "songs/" + id + "/Inst.ogg");
        if (!inst.empty()) { audio.songPath = id; audio.instPath = inst.u8string(); break; }
    }
    if (audio.songPath.empty()) for (const auto& id : candidates)
        if (relativeKey(id)) { audio.songPath = id; break; }
    auto voice = [&](const std::string& suffix) {
        return resolve(sources, "songs/" + audio.songPath + "/Voices" + suffix + ".ogg").u8string();
    };
    audio.voicesPath = voice("");
    auto characterVoice = [&](const char* key, const char* defaultSuffix) {
        std::string suffix = defaultSuffix;
        const auto who = field(key);
        if (relativeKey(who)) {
            const json node = json::parse(read(resolve(sources, "characters/" + who + ".json")), nullptr, false, true);
            if (node.is_object() && node.contains("vocals_file") && node["vocals_file"].is_string() &&
                !node["vocals_file"].get<std::string>().empty()) suffix = node["vocals_file"].get<std::string>();
        }
        return relativeKey(suffix) ? voice("-" + suffix) : std::string();
    };
    audio.playerVoicesPath = characterVoice("player1", "Player");
    audio.opponentVoicesPath = characterVoice("player2", "Opponent");
    // Psych prefers the character's vocals. Generic Voices is the player fallback.
    if (audio.playerVoicesPath.empty() && !audio.opponentVoicesPath.empty()) audio.playerVoicesPath = audio.voicesPath;
    if (!audio.playerVoicesPath.empty() || !audio.opponentVoicesPath.empty()) audio.voicesPath.clear();
    return audio;
}

} // namespace fml::psych
