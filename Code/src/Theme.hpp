// Note Lab — Tema visual y controles comunes (DESIGN_PLUGIN_NOTE_LAB §26).
//
// Un solo sitio para la paleta, las fuentes y los controles que dan a la app
// su aspecto: botones con icono, pildoras, controles segmentados, cabeceras de
// seccion y tarjetas. Los iconos son los de Segoe Fluent Icons (Windows 11) o,
// si no esta, Segoe MDL2 Assets (Windows 10), que comparten los puntos de
// codigo; sin ninguna de las dos, los botones ensenan su texto.
#pragma once

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace nlui {

namespace icon {
inline constexpr const char* Play = "\xEE\x9D\xA8";  // U+E768
inline constexpr const char* Pause = "\xEE\x9D\xA9";  // U+E769
inline constexpr const char* Restart = "\xEE\x9C\xAC";  // U+E72C
inline constexpr const char* Undo = "\xEE\x9E\xA7";  // U+E7A7
inline constexpr const char* Redo = "\xEE\x9E\xA6";  // U+E7A6
inline constexpr const char* Close = "\xEE\x9C\x91";  // U+E711
inline constexpr const char* Add = "\xEE\x9C\x90";  // U+E710
inline constexpr const char* FolderOpen = "\xEE\xA0\xB8";  // U+E838
inline constexpr const char* Zip = "\xEF\x80\x92";  // U+F012
inline constexpr const char* Game = "\xEE\x9F\xBC";  // U+E7FC
inline constexpr const char* Warning = "\xEE\x9E\xBA";  // U+E7BA
inline constexpr const char* Error = "\xEE\x9E\x83";  // U+E783
inline constexpr const char* Info = "\xEE\xA5\x86";  // U+E946
inline constexpr const char* Check = "\xEE\x9C\xBE";  // U+E73E
inline constexpr const char* Settings = "\xEE\x9C\x93";  // U+E713
inline constexpr const char* Keyboard = "\xEE\x9D\xA5";  // U+E765
inline constexpr const char* Code = "\xEE\xA5\x83";  // U+E943
inline constexpr const char* Music = "\xEE\xA3\x96";  // U+E8D6
inline constexpr const char* Search = "\xEE\x9C\xA1";  // U+E721
inline constexpr const char* Globe = "\xEE\x9D\xB4";  // U+E774
inline constexpr const char* ZoomIn = "\xEE\xA2\xA3";  // U+E8A3
inline constexpr const char* ZoomOut = "\xEE\x9C\x9F";  // U+E71F
inline constexpr const char* Fit = "\xEE\xA6\xA6";  // U+E9A6
inline constexpr const char* Filter = "\xEE\x9C\x9C";  // U+E71C
inline constexpr const char* List = "\xEE\xA3\xBD";  // U+E8FD
inline constexpr const char* Eye = "\xEE\x9E\xB3";  // U+E7B3
inline constexpr const char* EyeOff = "\xEE\xB4\x9A";  // U+ED1A
inline constexpr const char* Document = "\xEE\xA2\xA5";  // U+E8A5
inline constexpr const char* Photo = "\xEE\xA4\x9B";  // U+E91B
inline constexpr const char* Bot = "\xEE\xA6\x9A";  // U+E99A
inline constexpr const char* Speed = "\xEE\xB1\x8A";  // U+EC4A
inline constexpr const char* Layers = "\xEE\xA0\x9E";  // U+E81E
inline constexpr const char* Puzzle = "\xEE\xAA\x86";  // U+EA86
inline constexpr const char* Brush = "\xEE\x9D\xB1";  // U+E771
inline constexpr const char* Palette = "\xEE\x9E\x90";  // U+E790
inline constexpr const char* Edit = "\xEE\x9C\x8F";  // U+E70F
inline constexpr const char* Grid = "\xEE\xA0\x8A";  // U+E80A
inline constexpr const char* Down = "\xEE\x9D\x8B";  // U+E74B
inline constexpr const char* More = "\xEE\x9C\x92";  // U+E712
inline constexpr const char* Help = "\xEE\xA2\x97";  // U+E897
inline constexpr const char* Volume = "\xEE\x9D\xA7";  // U+E767
inline constexpr const char* Circle = "\xEE\xAA\x81";  // U+EA81
inline constexpr const char* Copy = "\xEE\xA3\x88";  // U+E8C8
inline constexpr const char* Home = "\xEE\xA0\x8F";  // U+E80F
inline constexpr const char* Contact = "\xEE\x9D\xBB";  // U+E77B
inline constexpr const char* Heart = "\xEE\xAD\x91";  // U+EB51
inline constexpr const char* Pulse = "\xEE\xA7\x99";  // U+E9D9
inline constexpr const char* Delete = "\xEE\x9D\x8D";  // U+E74D
inline constexpr const char* Sort = "\xEE\xA3\x8B";  // U+E8CB
inline constexpr const char* Star = "\xEE\x9C\xB4";  // U+E734
inline constexpr const char* Font = "\xEE\xA3\x92";  // U+E8D2
}  // namespace icon

// ------------------------------------------------------------------ paleta --

namespace color {
inline constexpr ImU32 Window = IM_COL32(15, 17, 22, 255);
inline constexpr ImU32 Panel = IM_COL32(21, 24, 31, 255);
inline constexpr ImU32 Raised = IM_COL32(28, 32, 41, 255);
inline constexpr ImU32 Toolbar = IM_COL32(19, 22, 28, 255);
inline constexpr ImU32 Border = IM_COL32(40, 45, 56, 255);
inline constexpr ImU32 Text = IM_COL32(230, 232, 238, 255);
inline constexpr ImU32 Muted = IM_COL32(139, 146, 161, 255);
inline constexpr ImU32 Faint = IM_COL32(92, 99, 112, 255);
inline constexpr ImU32 Accent = IM_COL32(155, 123, 245, 255);
inline constexpr ImU32 AccentSoft = IM_COL32(155, 123, 245, 46);
inline constexpr ImU32 Success = IM_COL32(76, 195, 138, 255);
inline constexpr ImU32 Warning = IM_COL32(242, 179, 76, 255);
inline constexpr ImU32 Error = IM_COL32(240, 100, 90, 255);
inline constexpr ImU32 Info = IM_COL32(108, 166, 240, 255);
// Uno por motor, para reconocerlos de un vistazo.
inline constexpr ImU32 Codename = IM_COL32(245, 158, 66, 255);
inline constexpr ImU32 Psych = IM_COL32(79, 182, 240, 255);
inline constexpr ImU32 VSlice = IM_COL32(242, 92, 154, 255);
}  // namespace color

inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

inline ImU32 withAlpha(ImU32 c, int alpha) {
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
}

// ------------------------------------------------------------------ fuentes --

struct Fonts {
    ImFont* ui = nullptr;         // Segoe UI con los iconos mezclados
    ImFont* semibold = nullptr;   // titulos y cabeceras, tambien con iconos
    ImFont* mono = nullptr;       // codigo y rutas
    bool icons = false;
};

inline Fonts& fonts() {
    static Fonts instance;
    return instance;
}

inline constexpr float kFontSize = 16.0f;

inline ImFont* addWithIcons(ImGuiIO& io, const char* path, float size, bool& icons) {
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, size);
    if (!font) return nullptr;
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.GlyphOffset = ImVec2(0.0f, 3.0f);
    merge.GlyphMinAdvanceX = size;
    static const ImWchar ranges[] = {0xE700, 0xF8FF, 0};
    for (const char* iconFont : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"}) {
        if (io.Fonts->AddFontFromFileTTF(iconFont, size - 2.0f, &merge, ranges)) {
            icons = true;
            break;
        }
    }
    return font;
}

inline void loadFonts(ImGuiIO& io) {
    Fonts& f = fonts();
    f.ui = addWithIcons(io, "C:\\Windows\\Fonts\\segoeui.ttf", kFontSize, f.icons);
    if (!f.ui) {
        f.ui = io.Fonts->AddFontDefault();
        f.icons = false;
    }
    bool semiboldIcons = false;
    f.semibold = addWithIcons(io, "C:\\Windows\\Fonts\\seguisb.ttf", kFontSize, semiboldIcons);
    if (!f.semibold) f.semibold = f.ui;
    f.mono = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 14.0f);
    if (!f.mono) f.mono = f.ui;
    io.FontDefault = f.ui;
}

// Etiqueta con icono: «<icono>  texto», o solo el texto si no hay iconos.
inline std::string label(const char* glyph, const std::string& text) {
    if (!fonts().icons || !glyph) return text;
    return text.empty() ? std::string(glyph) : std::string(glyph) + "  " + text;
}

// -------------------------------------------------------------------- estilo --

inline void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(12.0f, 10.0f);
    s.FramePadding = ImVec2(9.0f, 5.0f);
    s.CellPadding = ImVec2(8.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 7.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 11.0f;
    s.GrabMinSize = 10.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.WindowRounding = 8.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(0.0f, 4.0f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = vec(color::Text);
    c[ImGuiCol_TextDisabled] = vec(color::Muted);
    c[ImGuiCol_WindowBg] = vec(color::Window);
    c[ImGuiCol_ChildBg] = vec(color::Panel);
    c[ImGuiCol_PopupBg] = ImVec4(0.105f, 0.118f, 0.150f, 0.99f);
    c[ImGuiCol_Border] = vec(color::Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.118f, 0.133f, 0.168f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.150f, 0.168f, 0.212f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.176f, 0.196f, 0.250f, 1.0f);
    c[ImGuiCol_TitleBg] = vec(color::Toolbar);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.125f, 0.160f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = vec(color::Toolbar);
    c[ImGuiCol_MenuBarBg] = vec(color::Toolbar);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.33f, 0.40f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.38f, 0.41f, 0.50f, 1.0f);
    c[ImGuiCol_CheckMark] = vec(color::Accent);
    c[ImGuiCol_SliderGrab] = ImVec4(0.61f, 0.48f, 0.96f, 0.90f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.58f, 1.00f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.140f, 0.157f, 0.200f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.196f, 0.188f, 0.290f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.290f, 0.235f, 0.450f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.61f, 0.48f, 0.96f, 0.20f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.61f, 0.48f, 0.96f, 0.12f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.61f, 0.48f, 0.96f, 0.28f);
    c[ImGuiCol_Separator] = vec(color::Border);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.61f, 0.48f, 0.96f, 0.60f);
    c[ImGuiCol_SeparatorActive] = vec(color::Accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.61f, 0.48f, 0.96f, 0.45f);
    c[ImGuiCol_ResizeGripActive] = vec(color::Accent);
    c[ImGuiCol_Tab] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.61f, 0.48f, 0.96f, 0.16f);
    c[ImGuiCol_TabSelected] = ImVec4(0.61f, 0.48f, 0.96f, 0.22f);
    c[ImGuiCol_TabSelectedOverline] = vec(color::Accent);
    c[ImGuiCol_TabDimmed] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.61f, 0.48f, 0.96f, 0.14f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.110f, 0.125f, 0.160f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = vec(color::Border);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.61f, 0.48f, 0.96f, 0.35f);
    c[ImGuiCol_NavCursor] = vec(color::Accent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.04f, 0.62f);
}

// ------------------------------------------------------------------ controles --

inline void tooltip(const char* text) {
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

// Boton cuadrado con un icono; `on` lo pinta como activo (un interruptor).
inline bool iconButton(const char* id, const char* glyph, const char* fallback, const char* tip, bool on = false,
                       float size = 0.0f) {
    const float side = size > 0.0f ? size : ImGui::GetFrameHeight();
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.61f, 0.48f, 0.96f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.61f, 0.48f, 0.96f, 0.40f));
    }
    ImGui::PushID(id);
    const bool pressed = fonts().icons ? ImGui::Button(glyph, ImVec2(side, side))
                                       : ImGui::Button(fallback, ImVec2(0.0f, side));
    ImGui::PopID();
    if (on) ImGui::PopStyleColor(2);
    tooltip(tip);
    return pressed;
}

// Boton de accion principal, en el color de acento.
inline bool primaryButton(const std::string& text, ImVec2 size = ImVec2(0.0f, 0.0f)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.38f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.46f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.44f, 0.32f, 0.82f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(text.c_str(), size);
    ImGui::PopStyleColor(4);
    return pressed;
}

// Boton sin fondo hasta que se pasa por encima (barras de herramientas).
inline bool flatButton(const std::string& text, const char* tip = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool pressed = ImGui::Button(text.c_str(), size);
    ImGui::PopStyleColor();
    tooltip(tip);
    return pressed;
}

// Etiqueta redondeada: el motor, el uso de un estilo, «de serie»...
inline void pill(const char* text, ImU32 tint, bool solid = false) {
    ImFont* font = fonts().semibold;
    const float size = 12.5f;
    ImGui::PushFont(font, size);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    const ImVec2 pad(7.0f, 2.0f);
    const float height = std::max(textSize.y + pad.y * 2.0f, 18.0f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float lift = (ImGui::GetTextLineHeight() - height) * 0.5f;
    const ImVec2 min(at.x, at.y + std::max(0.0f, lift) + 1.0f);
    const ImVec2 max(min.x + textSize.x + pad.x * 2.0f, min.y + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, solid ? tint : withAlpha(tint, 40), height * 0.5f);
    if (!solid) draw->AddRect(min, max, withAlpha(tint, 110), height * 0.5f);
    draw->AddText(font, size, ImVec2(min.x + pad.x, min.y + (height - textSize.y) * 0.5f),
                  solid ? IM_COL32(18, 18, 24, 255) : tint, text);
    ImGui::Dummy(ImVec2(max.x - min.x, std::max(ImGui::GetTextLineHeight(), height)));
}

// Cabecera de seccion: texto pequeno en mayusculas y una linea fina.
inline void sectionHeader(const char* text, const char* glyph = nullptr) {
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold, 12.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, vec(color::Muted));
    if (glyph && fonts().icons) {
        ImGui::TextUnformatted(glyph);
        ImGui::SameLine(0.0f, 6.0f);
    }
    // Mayusculas tambien para las letras con tilde y la ñ: en UTF-8 son 0xC3
    // seguido de 0xA0-0xBE, y su mayuscula esta 0x20 antes (salvo ÷).
    std::string upper = text;
    for (size_t i = 0; i < upper.size(); ++i) {
        char& ch = upper[i];
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        const auto byte = static_cast<unsigned char>(ch);
        if (byte == 0xC3 && i + 1 < upper.size()) {
            const auto next = static_cast<unsigned char>(upper[i + 1]);
            if (next >= 0xA0 && next <= 0xBE && next != 0xB7) upper[i + 1] = static_cast<char>(next - 0x20);
            ++i;
        }
    }
    ImGui::TextUnformatted(upper.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float y = at.y + ImGui::GetTextLineHeight() * 0.5f;
    const float right = at.x + ImGui::GetContentRegionAvail().x;
    if (right > at.x + 4.0f) ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, y), ImVec2(right, y), color::Border);
    ImGui::NewLine();
}

// Titulo grande en semibold.
inline void title(const char* text, float size = 19.0f) {
    ImGui::PushFont(fonts().semibold, size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// Control segmentado: una fila de opciones excluyentes.
inline bool segmented(const char* id, int* value, const char* const* labels, int count, float itemWidth = 0.0f) {
    bool changed = false;
    ImGui::PushID(id);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    float total = 0.0f;
    float widths[8] = {};
    for (int i = 0; i < count && i < 8; ++i) {
        widths[i] = itemWidth > 0.0f ? itemWidth : ImGui::CalcTextSize(labels[i]).x + 22.0f;
        total += widths[i];
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, ImVec2(start.x + total + 4.0f, start.y + height),
                        ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
    float x = start.x + 2.0f;
    for (int i = 0; i < count && i < 8; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("seg", ImVec2(widths[i], height))) {
            if (*value != i) changed = true;
            *value = i;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const ImVec2 a(x, start.y + 2.0f), b(x + widths[i], start.y + height - 2.0f);
        if (*value == i) draw->AddRectFilled(a, b, IM_COL32(155, 123, 245, 70), ImGui::GetStyle().FrameRounding - 1.0f);
        else if (hovered) draw->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14), ImGui::GetStyle().FrameRounding - 1.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        draw->AddText(ImVec2(x + (widths[i] - textSize.x) * 0.5f, start.y + (height - textSize.y) * 0.5f),
                      *value == i ? color::Text : color::Muted, labels[i]);
        x += widths[i];
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(total + 4.0f, height));
    ImGui::PopID();
    return changed;
}

// Interruptor tipo casilla con icono: se ve como un boton que queda encendido.
inline bool toggle(const char* id, const std::string& text, bool* value, const char* tip = nullptr) {
    ImGui::PushID(id);
    if (*value) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.61f, 0.48f, 0.96f, 0.26f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.61f, 0.48f, 0.96f, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(color::Text));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_Text, vec(color::Muted));
    }
    const bool pressed = ImGui::Button(text.c_str());
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    if (pressed) *value = !*value;
    tooltip(tip);
    return pressed;
}

// Contador de hallazgos con su color; pulsarlo abre la lista.
inline bool countBadge(const char* id, const char* glyph, int count, ImU32 tint, const char* tip) {
    const bool lit = count > 0;
    char text[64];
    std::snprintf(text, sizeof(text), "%s%s%d", fonts().icons ? glyph : "", fonts().icons ? "  " : "", count);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, lit ? vec(withAlpha(tint, 46)) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(withAlpha(tint, lit ? 80 : 30)));
    ImGui::PushStyleColor(ImGuiCol_Text, lit ? vec(tint) : vec(color::Muted));
    const bool pressed = ImGui::Button(text);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    tooltip(tip);
    return pressed;
}

// Tarjeta de cifra: «Piezas 32/32».
inline void statCard(const char* caption, const std::string& value, ImU32 tint, float width) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = 50.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), color::Raised, 7.0f);
    draw->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + height), tint, 7.0f, ImDrawFlags_RoundCornersLeft);
    draw->AddText(fonts().ui, 12.5f, ImVec2(at.x + 12.0f, at.y + 7.0f), color::Muted, caption);
    draw->AddText(fonts().semibold, 18.0f, ImVec2(at.x + 12.0f, at.y + 23.0f), color::Text, value.c_str());
    ImGui::Dummy(ImVec2(width, height));
}

// Texto apagado y pequeno.
inline void caption(const char* text) {
    ImGui::PushFont(nullptr, 13.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, vec(color::Muted));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Ruta o valor tecnico, en monoespaciada y apagado.
inline void monoText(const std::string& text, ImU32 tint = color::Muted) {
    ImGui::PushFont(fonts().mono, 13.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, vec(tint));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Una fila «etiqueta: valor» con la etiqueta alineada a una columna.
inline void field(const char* name, const std::string& value, float column = 96.0f, bool mono = false) {
    ImGui::PushStyleColor(ImGuiCol_Text, vec(color::Muted));
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();
    ImGui::SameLine(column);
    if (mono) monoText(value, color::Text);
    else ImGui::TextWrapped("%s", value.c_str());
}

// Linea vertical fina entre grupos de una barra.
inline void vrule() {
    ImGui::SameLine(0.0f, 10.0f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + 4.0f), ImVec2(at.x, at.y + h - 4.0f), color::Border);
    ImGui::Dummy(ImVec2(1.0f, h));
    ImGui::SameLine(0.0f, 10.0f);
}

}  // namespace nlui
