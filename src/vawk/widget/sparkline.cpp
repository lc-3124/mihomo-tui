//- sparkline.cpp — 方块字符曲线

#include "vawk/widget/sparkline.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace vawk::widget {

namespace {

constexpr std::string_view kBlocks[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

} // namespace

Sparkline::Sparkline(std::size_t window) : window_(window ? window : 1) {}

void Sparkline::push(double value) {
    if (value < 0) value = 0;
    values_.push_back(value);
    while (values_.size() > window_) values_.pop_front();
}

void Sparkline::reset() { values_.clear(); }

void Sparkline::set_window(std::size_t window) {
    window_ = window ? window : 1;
    while (values_.size() > window_) values_.pop_front();
}

double Sparkline::peak() const {
    double p = 0;
    for (double v : values_) p = std::max(p, v);
    return p;
}

void Sparkline::render(Canvas& canvas, Rect rect, const vatui::Style& style) const {
    if (rect.empty() || values_.empty()) return;

    const double peak_v = peak();
    const std::size_t cols = static_cast<std::size_t>(std::min<int>(rect.w, 4096));
    const std::size_t take = std::min(cols, values_.size());
    const std::size_t skip = values_.size() - take;
    const int x0 = rect.x + static_cast<int>(cols - take);

    for (std::size_t i = 0; i < take; ++i) {
        double v = values_[skip + i];
        int    idx = 0;
        if (peak_v > 0) {
            double ratio = v / peak_v;
            idx = static_cast<int>(std::lround(ratio * 7.0));
            idx = std::clamp(idx, 0, 7);
            if (v > 0 && idx == 0) idx = 1; // 非零不要画成最低档的空心
        }
        if (gradient_enabled) {
            double t = static_cast<double>(idx) / 7.0;
            double hue = gradient.from + (gradient.to - gradient.from) * t
                       + gradient.phase + hue_shift;
            canvas.put_text(x0 + static_cast<int>(i), rect.y, kBlocks[idx],
                            Canvas::hsv(hue, gradient.sat, gradient.val));
        } else {
            canvas.put_text(x0 + static_cast<int>(i), rect.y, kBlocks[idx], style);
        }
    }
}

} // namespace vawk::widget
