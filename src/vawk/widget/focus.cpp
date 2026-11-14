//- focus.cpp — 焦点链

#include "vawk/widget/focus.hpp"

#include <algorithm>

namespace vawk::widget {

void FocusChain::add(Widget* widget) {
    if (!widget || !widget->focusable()) return;
    if (std::find(widgets_.begin(), widgets_.end(), widget) != widgets_.end()) return;
    widgets_.push_back(widget);
    if (index_ < 0) focus_index(0);
}

void FocusChain::clear() {
    for (auto* w : widgets_) w->set_focused(false);
    widgets_.clear();
    index_ = -1;
}

void FocusChain::focus(Widget* widget) {
    auto it = std::find(widgets_.begin(), widgets_.end(), widget);
    if (it == widgets_.end()) return;
    focus_index(static_cast<int>(it - widgets_.begin()));
}

void FocusChain::focus_index(int index) {
    if (widgets_.empty()) { index_ = -1; return; }
    index = std::clamp(index, 0, static_cast<int>(widgets_.size()) - 1);
    index_ = index;
    apply_focus();
}

Widget* FocusChain::current() const {
    if (index_ < 0 || index_ >= static_cast<int>(widgets_.size())) return nullptr;
    return widgets_[static_cast<std::size_t>(index_)];
}

void FocusChain::apply_focus() {
    for (std::size_t i = 0; i < widgets_.size(); ++i)
        widgets_[i]->set_focused(static_cast<int>(i) == index_);
}

bool FocusChain::next() {
    if (widgets_.size() < 2) return false;
    index_ = (index_ + 1) % static_cast<int>(widgets_.size());
    apply_focus();
    return true;
}

bool FocusChain::prev() {
    if (widgets_.size() < 2) return false;
    index_ = (index_ - 1 + static_cast<int>(widgets_.size()))
             % static_cast<int>(widgets_.size());
    apply_focus();
    return true;
}

bool FocusChain::route(const vaterm::InputData& event) {
    if (event.inputType != vaterm::InputType::KEYBD) return false;
    const auto& key = event.value.keybd;

    if (key.code == vaterm::KeyCode::TAB) {
        if (key.isShift) prev();
        else next();
        return true;
    }

    if (auto* w = current()) return w->on_event(event);
    return false;
}

Widget* FocusChain::widget_at(int col, int row) const {
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it)
        if ((*it)->bounds().contains(col, row)) return *it;
    return nullptr;
}

} // namespace vawk::widget
