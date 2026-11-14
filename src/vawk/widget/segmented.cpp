//- segmented.cpp — 横向分段控件

#include "vawk/widget/segmented.hpp"

#include <algorithm>

#include "vaterm/utf.hpp"

namespace vawk::widget {

Segmented::Segmented(std::vector<std::string> labels, int selected)
    : labels_(std::move(labels)), selected_(selected) {
    normal_style   = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{250}),
                                  .bg_sgr = vatui::bg(vaterm::TRANS),
                                  .effects_sgr = vatui::effects({})};
    selected_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                                  .bg_sgr = vatui::bg(vaterm::Color8{33}),
                                  .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    focus_style    = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                                  .bg_sgr = vatui::bg(vaterm::Color8{24}),
                                  .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    set_selected(selected, false);
}

void Segmented::set_labels(std::vector<std::string> labels) {
    labels_ = std::move(labels);
    set_selected(selected_, false);
}

void Segmented::set_selected(int index, bool notify) {
    if (labels_.empty()) { selected_ = 0; return; }
    index = std::clamp(index, 0, static_cast<int>(labels_.size()) - 1);
    bool changed = (index != selected_);
    selected_ = index;
    if (changed && notify && on_change) on_change(selected_);
}

std::string_view Segmented::selected_label() const {
    if (labels_.empty() || selected_ < 0 || selected_ >= static_cast<int>(labels_.size()))
        return {};
    return labels_[static_cast<std::size_t>(selected_)];
}

int Segmented::preferred_width() const {
    int w = 0;
    for (const auto& l : labels_) w += vaterm::utf::width(l) + 2;
    return w;
}

int Segmented::segment_x(int index) const {
    if (index < 0 || index >= static_cast<int>(labels_.size())) return -1;
    int x = bounds_.x;
    for (int i = 0; i < index; ++i) x += vaterm::utf::width(labels_[static_cast<std::size_t>(i)]) + 2;
    return x;
}

int Segmented::segment_at(int col, int row) const {
    if (!bounds_.contains(col, row)) return -1;
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i) {
        int  x0 = segment_x(i);
        int  w  = vaterm::utf::width(labels_[static_cast<std::size_t>(i)]) + 2;
        if (col >= x0 && col < x0 + w) return i;
    }
    return -1;
}

void Segmented::render(Canvas& canvas) {
    if (bounds_.empty()) return;
    int x = bounds_.x;
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i) {
        const auto& label = labels_[static_cast<std::size_t>(i)];
        bool        sel   = (i == selected_);
        const auto& style = sel ? (focused ? focus_style : selected_style) : normal_style;
        std::string text  = " " + label + " ";
        canvas.put_text(x, bounds_.y, text, style);
        x += vaterm::utf::width(text);
    }
}

bool Segmented::on_event(const vaterm::InputData& event) {
    if (event.inputType == vaterm::InputType::KEYBD) {
        const auto& key = event.value.keybd;
        if (key.code == vaterm::KeyCode::LEFT || key.code == vaterm::KeyCode::RIGHT) {
            if (labels_.size() < 2) return true;
            int step = (key.code == vaterm::KeyCode::RIGHT) ? 1 : -1;
            int next = (selected_ + step + static_cast<int>(labels_.size()))
                       % static_cast<int>(labels_.size());
            set_selected(next, true);
            return true;
        }
        if (key.code == vaterm::KeyCode::HOME) { set_selected(0, true); return true; }
        if (key.code == vaterm::KeyCode::END) {
            set_selected(static_cast<int>(labels_.size()) - 1, true);
            return true;
        }
        return false;
    }

    if (event.inputType == vaterm::InputType::MOUSE) {
        const auto& m = event.value.mouse;
        if (m.action == vaterm::MouseAction::PRESS && m.button == vaterm::MouseButton::LEFT) {
            int idx = segment_at(m.col, m.row);
            if (idx >= 0) { set_selected(idx, true); return true; }
        }
    }
    return false;
}

} // namespace vawk::widget
