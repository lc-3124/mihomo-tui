//- panel.cpp — 边框 + 标题面板

#include "vawk/widget/panel.hpp"

#include "vaterm/color.hpp"
#include "vaterm/enums.hpp"

namespace vawk::widget {

namespace {

vatui::Style make_line_style() {
    return vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{244}),
                        .bg_sgr = vatui::bg(vaterm::TRANS),
                        .effects_sgr = vatui::effects({})};
}

vatui::Style make_title_style() {
    return vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                        .bg_sgr = vatui::bg(vaterm::TRANS),
                        .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
}

} // namespace

Panel::Panel(std::string title, BoxStyle style)
    : title_(std::move(title)), style_(style) {
    line_style  = make_line_style();
    title_style = make_title_style();

    focus_line_style  = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{39}),
                                     .bg_sgr = vatui::bg(vaterm::TRANS),
                                     .effects_sgr = vatui::effects({})};
    focus_title_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{81}),
                                     .bg_sgr = vatui::bg(vaterm::TRANS),
                                     .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    badge_style       = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{245}),
                                     .bg_sgr = vatui::bg(vaterm::TRANS),
                                     .effects_sgr = vatui::effects({vaterm::TextEffect::DIM})};
}

void Panel::set_content(Widget* content) {
    content_ = content;
    if (content_) {
        content_->set_bounds(content_rect());
        content_->set_focused(focused);
    }
}

Rect Panel::content_rect() const {
    return inset(bounds_, 1);
}

void Panel::set_bounds(Rect r) {
    Widget::set_bounds(r);
    if (content_) content_->set_bounds(content_rect());
}

void Panel::set_focused(bool value) {
    focused = value;
    if (content_) content_->focused = value;
}

void Panel::render(Canvas& canvas) {
    if (bounds_.empty()) return;
    canvas.clear(bounds_);

    const auto& line  = focused ? focus_line_style : line_style;
    const auto& title = focused ? focus_title_style : title_style;

    canvas.border(bounds_, style_, line, title_, title);

    if (!badge_.empty() && bounds_.w > 8)
        canvas.put_text_right(bounds_.right() - 3, bounds_.y, " " + badge_ + " ",
                              badge_style);

    if (content_) {
        Rect cr = content_rect();
        canvas.push_clip(cr);
        content_->render(canvas);
        canvas.pop_clip();
    }
}

bool Panel::on_event(const vaterm::InputData& event) {
    return content_ ? content_->on_event(event) : false;
}

} // namespace vawk::widget
