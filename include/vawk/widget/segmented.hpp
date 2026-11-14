#ifndef VAWK_WIDGET_SEGMENTED_HPP
#define VAWK_WIDGET_SEGMENTED_HPP

//- vawk::widget::Segmented — 横向分段控件
//
// 用于模式切换（直连 / 规则 / 全局）。←→ 改选择，鼠标点击直接跳转；
// 选择变化时回调 on_change，方便 App 立刻发 API 请求。

#include <functional>
#include <string>
#include <vector>

#include "vatui.hpp"
#include "vawk/widget/widget.hpp"

namespace vawk::widget {

class Segmented : public Widget {
  public:
    Segmented(std::vector<std::string> labels, int selected = 0);

    /** @brief 标签集合（UTF-8，可含中文/emoji）。 */
    const std::vector<std::string>& labels() const { return labels_; }
    void set_labels(std::vector<std::string> labels);

    int  selected() const { return selected_; }
    void set_selected(int index, bool notify = false);

    /** @brief 当前选中标签；标签为空时返回空串。 */
    std::string_view selected_label() const;

    /** @brief 控件需要的列数。 */
    int preferred_width() const;

    /** @brief 选择变化回调，参数为新索引。 */
    std::function<void(int)> on_change;

    void render(Canvas& canvas) override;
    bool on_event(const vaterm::InputData& event) override;

    vatui::Style normal_style{};
    vatui::Style selected_style{};
    vatui::Style focus_style{};

  private:
    /** @brief 第 index 段相对 bounds_.x 的起始列；越界返回 -1。 */
    int segment_x(int index) const;
    /** @brief 命中测试：返回列所在的段索引，未命中 -1。 */
    int segment_at(int col, int row) const;

    std::vector<std::string> labels_;
    int                      selected_ = 0;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_SEGMENTED_HPP
