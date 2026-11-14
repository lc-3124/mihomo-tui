#ifndef VAWK_WIDGET_FOCUS_HPP
#define VAWK_WIDGET_FOCUS_HPP

//- vawk::widget::FocusChain — 焦点链与事件路由
//
// 按注册顺序轮转：Tab 下一个，Shift-Tab 上一个。焦点变化时同步
// 设置 Widget::focused（Panel 会把这个标记转给内容控件）。
//
// 鼠标不在这里做命中测试以外的策略：widget_at() 只回答「坐标落在谁身上」，
// 点击后是否切换焦点、是否触发动作由 App 决定。

#include <cstddef>
#include <vector>

#include "vawk/widget/widget.hpp"

namespace vawk::widget {

class FocusChain {
  public:
    /** @brief 只登记可聚焦控件；重复登记会被忽略。 */
    void add(Widget* widget);
    void clear();

    /** @brief 按指针聚焦；不在链上则无操作。 */
    void focus(Widget* widget);
    /** @brief 按索引聚焦（越界忽略）。 */
    void focus_index(int index);

    /** @brief 当前焦点索引；空链为 -1。 */
    int index() const { return index_; }
    /** @brief 当前焦点控件；空链为 nullptr。 */
    Widget* current() const;

    /** @brief 轮转到下一个；只有一项时返回 false。 */
    bool next();
    /** @brief 轮转到上一个。 */
    bool prev();

    /** @brief Tab / Shift-Tab 在内部处理，其余交给当前控件。 */
    bool route(const vaterm::InputData& event);

    /** @brief 命中测试：返回覆盖该坐标的最上层控件（链上靠后的优先）。 */
    Widget* widget_at(int col, int row) const;

    const std::vector<Widget*>& widgets() const { return widgets_; }

  private:
    void apply_focus();

    std::vector<Widget*> widgets_;
    int                  index_ = -1;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_FOCUS_HPP
