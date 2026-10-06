#pragma once

#include "../core/NoteCode.hpp"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace nlcode {

struct EditorState {
    std::string text;
    fml::notelab::Engine engine = fml::notelab::Engine::Codename;
    bool config = false;
    std::vector<fml::notelab::CodeColorSpan> spans;
    std::vector<size_t> lines;
    std::array<int, 8> drawn{};
    ImVec4 rect{};
    ImGuiID id = 0;
    int firstLine = 0, visibleLines = 0;
};

inline ImU32 color(fml::notelab::CodeColorKind kind) {
    using K = fml::notelab::CodeColorKind;
    switch (kind) {
    case K::Comment: return IM_COL32(106, 153, 85, 255);
    case K::String: return IM_COL32(206, 145, 120, 255);
    case K::Number: return IM_COL32(181, 206, 168, 255);
    case K::Keyword: return IM_COL32(197, 134, 192, 255);
    case K::Type: return IM_COL32(78, 201, 176, 255);
    case K::Function: return IM_COL32(220, 220, 170, 255);
    case K::Literal: return IM_COL32(86, 156, 214, 255);
    default: return IM_COL32(212, 212, 212, 255);
    }
}

inline void update(EditorState& state, fml::notelab::Engine engine, const char* text, bool config) {
    if (state.text == text && state.engine == engine && state.config == config && !state.lines.empty()) return;
    state.text = text;
    state.engine = engine;
    state.config = config;
    state.spans = fml::notelab::colorBlockSource(engine, state.text, config);
    state.lines.assign(1, 0);
    for (size_t i = 0; i < state.text.size(); ++i) if (state.text[i] == '\n') state.lines.push_back(i + 1);
}

inline bool edit(EditorState& state, const char* label, char* buffer, size_t capacity, ImVec2 size,
    fml::notelab::Engine engine, bool config, ImGuiInputTextFlags flags) {
    update(state, engine, buffer, config);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float fontSize = ImGui::GetFontSize();
    const float gutter = ImGui::CalcTextSize(std::to_string(state.lines.size()).c_str()).x + 18.0f;
    const ImVec2 codePad(gutter + pad.x, pad.y);
    ImGuiWindow* parent = ImGui::GetCurrentWindow();
    state.id = parent->GetID(label);
    const ImU32 cursorColor = ImGui::GetColorU32(ImGuiCol_InputTextCursor);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, codePad);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(212, 212, 212, 0));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, cursorColor);
    const bool changed = ImGui::InputTextMultiline(label, buffer, capacity, size, flags);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    const ImVec2 at = ImGui::GetItemRectMin(), end = ImGui::GetItemRectMax();
    state.rect = {at.x, at.y, end.x - at.x, end.y - at.y};
    state.drawn.fill(0);
    state.visibleLines = 0;
    ImGuiWindow* child = nullptr;
    for (ImGuiWindow* candidate : parent->DC.ChildWindows)
        if (candidate->ChildId == state.id) { child = candidate; break; }
    if (!child || child->LastFrameActive != ImGui::GetFrameCount() || child->SkipItems) return changed;
    update(state, engine, buffer, config);
    ImGuiInputTextState* input = ImGui::GetInputTextState(state.id);
    const bool focused = GImGui->ActiveId == state.id || (input && GImGui->ActiveId == ImGui::GetWindowScrollbarID(child, ImGuiAxis_Y));
    const float scrollX = focused && input ? input->Scroll.x : 0.0f;
    const ImVec2 origin(child->Pos.x + child->DecoOuterSizeX1 + codePad.x - child->Scroll.x - scrollX,
        child->Pos.y + child->DecoOuterSizeY1 + codePad.y - child->Scroll.y);
    ImRect baseClip = child->InnerClipRect;
    baseClip.ClipWith(parent->ClipRect);
    ImRect clip = baseClip;
    clip.Min.x = std::max(clip.Min.x, child->Pos.x + gutter + pad.x);
    clip.Max.x = std::min(clip.Max.x, child->Pos.x + child->Size.x - child->ScrollbarSizes.x - pad.x);
    const int first = std::max(0, static_cast<int>(std::floor((clip.Min.y - origin.y) / fontSize)));
    const int last = std::min(static_cast<int>(state.lines.size()), static_cast<int>(std::ceil((clip.Max.y - origin.y) / fontSize)));
    state.firstLine = first;
    state.visibleLines = std::max(0, last - first);
    ImDrawList* draw = child->DrawList;
    draw->PushClipRect(baseClip.Min, baseClip.Max, true);
    draw->AddRectFilled(ImVec2(child->Pos.x, baseClip.Min.y), ImVec2(child->Pos.x + gutter, baseClip.Max.y), IM_COL32(20, 23, 29, 255));
    draw->AddLine(ImVec2(child->Pos.x + gutter, baseClip.Min.y), ImVec2(child->Pos.x + gutter, baseClip.Max.y), IM_COL32(45, 49, 61, 255));
    for (int row = first; row < last; ++row) {
        const std::string number = std::to_string(row + 1);
        draw->AddText(ImVec2(child->Pos.x + gutter - 8.0f - ImGui::CalcTextSize(number.c_str()).x, origin.y + row * fontSize), IM_COL32(113, 119, 132, 255), number.c_str());
    }
    draw->PopClipRect();
    if (clip.Max.x <= clip.Min.x || first >= last) return changed;
    draw->PushClipRect(clip.Min, clip.Max, true);
    size_t spanIndex = static_cast<size_t>(std::lower_bound(state.spans.begin(), state.spans.end(), state.lines[static_cast<size_t>(first)],
        [](const auto& span, size_t offset) { return span.end <= offset; }) - state.spans.begin());
    ImFont* font = ImGui::GetFont();
    const ImVec4 fineClip(clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y);
    for (int row = first; row < last; ++row) {
        const size_t lineEnd = static_cast<size_t>(row + 1) < state.lines.size() ? state.lines[static_cast<size_t>(row + 1)] - 1 : state.text.size();
        size_t offset = state.lines[static_cast<size_t>(row)];
        float x = origin.x;
        while (offset < lineEnd && spanIndex < state.spans.size()) {
            const auto& span = state.spans[spanIndex];
            const size_t to = std::min(lineEnd, span.end);
            const char* beginText = state.text.data() + offset;
            const char* endText = state.text.data() + to;
            const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0, beginText, endText).x;
            if (x + width >= clip.Min.x && x <= clip.Max.x) {
                draw->AddText(font, fontSize, ImVec2(x, origin.y + row * fontSize), color(span.kind), beginText, endText, 0, &fineClip);
                ++state.drawn[static_cast<size_t>(span.kind)];
            }
            x += width;
            offset = to;
            if (to == span.end) ++spanIndex;
        }
        while (spanIndex < state.spans.size() && state.spans[spanIndex].end <= lineEnd + 1) ++spanIndex;
    }
    draw->PopClipRect();
    return changed;
}

}
