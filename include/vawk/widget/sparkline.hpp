#ifndef VAWK_WIDGET_SPARKLINE_HPP
#define VAWK_WIDGET_SPARKLINE_HPP

//- vawk::widget::Sparkline — 方块字符迷你曲线
//
// 固定窗口（默认 60 个采样点），每次 render 取窗口内最大值做归一化，
// 因此曲线永远铺满整个高度，不会因为总量太小而看不见。
// 数据不足时靠右对齐（最右永远是刚到的采样）。

#include <cstddef>
#include <deque>

#include "vatui.hpp"
#include "vawk/widget/canvas.hpp"
#include "vawk/widget/rect.hpp"

namespace vawk::widget {

class Sparkline {
  public:
    explicit Sparkline(std::size_t window = 60);

    /** @brief 追加一个采样；超过窗口时丢弃最旧的。 */
    void push(double value);
    /** @brief 清空所有采样。 */
    void reset();

    void set_window(std::size_t window);
    std::size_t window() const { return window_; }
    const std::deque<double>& values() const { return values_; }
    /** @brief 窗口内最大值；无数据为 0。 */
    double peak() const;

    /** @brief 把曲线画进 rect（高度只用第 0 行，宽度决定采样数）。 */
    void render(Canvas& canvas, Rect rect, const vatui::Style& style) const;

  private:
    std::deque<double> values_;
    std::size_t        window_;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_SPARKLINE_HPP
