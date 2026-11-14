#ifndef VAWK_WIDGET_WIDGET_HPP
#define VAWK_WIDGET_WIDGET_HPP

//- vawk::widget::Widget — 控件基类
//
// 最小契约：
//   render(Canvas&)     往画布上画自己；
//   on_event(InputData) 返回 true 表示已消费该事件；
//   set_bounds(Rect)    由布局方调用，控件据此计算内容区。
//
// 不做「父控件持有子控件」的所有权模型：Panel 只持有裸指针，
// 生命周期由 App 统一管理（栈对象 + 固定顺序，简单且不会泄漏）。

#include "vaterm/input.hpp"
#include "vawk/widget/canvas.hpp"
#include "vawk/widget/rect.hpp"

namespace vawk::widget {

class Widget {
  public:
    virtual ~Widget() = default;

    /** @brief 绘制自身。 */
    virtual void render(Canvas& canvas) = 0;

    /**
     * @brief 处理一个输入事件。
     * @return true 表示事件已消费，不再向上/向外传递。
     */
    virtual bool on_event(const vaterm::InputData& event) {
        (void)event;
        return false;
    }

    /** @brief 设置布局矩形。 */
    virtual void set_bounds(Rect r) { bounds_ = r; }
    /** @brief 当前布局矩形。 */
    Rect bounds() const { return bounds_; }

    /** @brief 能否被 Tab 聚焦。 */
    virtual bool focusable() const { return true; }

    /** @brief 是否持有焦点。 */
    bool focused = false;

    /** @brief 焦点变化钩子；Panel 用它把焦点同步给内容控件。 */
    virtual void set_focused(bool value) { focused = value; }

  protected:
    Rect bounds_{};
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_WIDGET_HPP
