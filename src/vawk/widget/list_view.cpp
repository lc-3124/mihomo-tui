//- list_view.cpp — 条目列表

#include "vawk/widget/list_view.hpp"

#include <algorithm>
#include <cctype>

#include "vaterm/utf.hpp"

namespace vawk::widget {

namespace {

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

} // namespace

ListView::ListView() {
    normal_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                                .bg_sgr = vatui::bg(vaterm::TRANS),
                                .effects_sgr = vatui::effects({})};
    selected_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                                  .bg_sgr = vatui::bg(vaterm::Color8{25}),
                                  .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    unfocused_selected_style =
        vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                     .bg_sgr = vatui::bg(vaterm::Color8{237}),
                     .effects_sgr = vatui::effects({})};
    detail_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{243}),
                                .bg_sgr = vatui::bg(vaterm::TRANS),
                                .effects_sgr = vatui::effects({vaterm::TextEffect::DIM})};
    empty_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{243}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::DIM})};
    selected_fg = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
}

void ListView::set_items(std::vector<ListItem> items) {
    items_ = std::move(items);
    selected_ = 0;
    top_      = 0;
}

void ListView::set_selected(int index) {
    selected_ = index;
    notify_select();
}

void ListView::notify_select() {
    last_click_ = -1;
    if (on_select)
        if (auto* item = selected_item()) on_select(*item);
}

const ListItem* ListView::selected_item() const {
    auto v = view();
    if (v.empty()) return nullptr;
    int idx = std::clamp(selected_, 0, static_cast<int>(v.size()) - 1);
    return &items_[static_cast<std::size_t>(v[static_cast<std::size_t>(idx)])];
}

std::vector<int> ListView::view() const {
    std::vector<int> out;
    out.reserve(items_.size());
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& it = items_[static_cast<std::size_t>(i)];
        if (!filter_.empty() && !icontains(it.label, filter_)) continue;
        out.push_back(i);
    }
    return out;
}

bool ListView::select_id(const std::string& id) {
    auto v = view();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (items_[static_cast<std::size_t>(v[i])].id == id) {
            selected_ = static_cast<int>(i);
            notify_select();
            return true;
        }
    }
    return false;
}

bool ListView::set_detail(const std::string& id, std::string detail) {
    for (auto& item : items_) {
        if (item.id == id) {
            item.detail = std::move(detail);
            return true;
        }
    }
    return false;
}

void ListView::move(int delta) {
    auto v = view();
    if (v.empty()) return;
    selected_ = std::clamp(selected_ + delta, 0, static_cast<int>(v.size()) - 1);
    notify_select();
}

void ListView::to_top() {
    selected_ = 0;
    top_      = 0;
    notify_select();
}

void ListView::to_bottom() {
    auto v = view();
    if (v.empty()) return;
    selected_ = static_cast<int>(v.size()) - 1;
    notify_select();
}

void ListView::set_filter(std::string filter) {
    filter_   = std::move(filter);
    selected_ = 0;
    top_      = 0;
    last_click_ = -1;
}

void ListView::ensure_visible(int view_index, int rows) {
    if (rows <= 0) return;
    if (view_index < top_) top_ = view_index;
    if (view_index >= top_ + rows) top_ = view_index - rows + 1;
    if (top_ < 0) top_ = 0;
}

void ListView::render(Canvas& canvas) {
    if (bounds_.empty()) return;

    const auto v    = view();
    const int  rows = bounds_.h;

    if (v.empty()) {
        canvas.put_text(bounds_.x, bounds_.y, empty_text_, empty_style);
        return;
    }

    selected_ = std::clamp(selected_, 0, static_cast<int>(v.size()) - 1);
    top_      = std::clamp(top_, 0, std::max(0, static_cast<int>(v.size()) - 1));
    ensure_visible(selected_, rows);

    for (int r = 0; r < rows; ++r) {
        int vi = top_ + r;
        if (vi >= static_cast<int>(v.size())) break;

        const ListItem& item = items_[static_cast<std::size_t>(
            v[static_cast<std::size_t>(vi)])];
        bool sel = (vi == selected_);

        const bool  gradient_row = sel && gradient_enabled && focused;
        const auto& row_style = gradient_row ? selected_fg
                              : sel ? (focused ? selected_style : unfocused_selected_style)
                                    : normal_style;

        // 整行铺底，选中态才有整条高亮。
        if (gradient_row) {
            Gradient g = selected_gradient;
            g.phase += hue_shift;
            canvas.fill_gradient(Rect{bounds_.x, bounds_.y + r, bounds_.w, 1}, g);
        } else {
            canvas.fill(Rect{bounds_.x, bounds_.y + r, bounds_.w, 1}, ' ', row_style);
        }

        int detail_w = vaterm::utf::width(item.detail);
        int label_w  = std::max(0, bounds_.w - 2 - (detail_w ? detail_w + 1 : 0));

        std::string line = sel ? "▸ " : "  ";
        line += item.label;
        canvas.put_text_padded(bounds_.x, bounds_.y + r, line, std::min(label_w, bounds_.w),
                               row_style);

        if (!item.detail.empty() && detail_w + 1 < bounds_.w) {
            const auto& style = sel ? row_style : detail_style;
            canvas.put_text_right(bounds_.right() - 1, bounds_.y + r, item.detail, style);
        }
    }
}

bool ListView::on_event(const vaterm::InputData& event) {
    if (event.inputType == vaterm::InputType::KEYBD) {
        const auto& key = event.value.keybd;
        switch (key.code) {
        case vaterm::KeyCode::UP:   move(-1); return true;
        case vaterm::KeyCode::DOWN: move(1); return true;
        case vaterm::KeyCode::PGUP: move(-std::max(1, bounds_.h)); return true;
        case vaterm::KeyCode::PGDN: move(std::max(1, bounds_.h)); return true;
        case vaterm::KeyCode::HOME: to_top(); return true;
        case vaterm::KeyCode::END:  to_bottom(); return true;
        case vaterm::KeyCode::ENTER:
            if (on_confirm)
                if (auto* item = selected_item()) on_confirm(*item);
            return true;
        default: break;
        }
        return false;
    }

    if (event.inputType == vaterm::InputType::MOUSE) {
        const auto& m = event.value.mouse;
        if (!bounds_.contains(m.col, m.row)) return false;

        if (m.action == vaterm::MouseAction::SCROLL_UP) { move(-3); return true; }
        if (m.action == vaterm::MouseAction::SCROLL_DOWN) { move(3); return true; }

        if (m.action == vaterm::MouseAction::PRESS
            && m.button == vaterm::MouseButton::LEFT) {
            int vi = top_ + (m.row - bounds_.y);
            auto v = view();
            if (vi < 0 || vi >= static_cast<int>(v.size())) return true;
            if (vi == selected_ && last_click_ == vi) {
                if (on_confirm)
                    if (auto* item = selected_item()) on_confirm(*item);
            } else {
                selected_ = vi;
                notify_select();
            }
            last_click_ = vi;
            return true;
        }
    }
    return false;
}

} // namespace vawk::widget
