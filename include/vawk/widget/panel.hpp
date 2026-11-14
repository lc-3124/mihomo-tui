#ifndef VAWK_WIDGET_PANEL_HPP
#define VAWK_WIDGET_PANEL_HPP

//- vawk::widget::Panel — 边框 + 标题 + 内容区
//
// 只负责画框和把事件转给内容控件；内容控件的 bounds 由 Panel 在
// set_bounds 时同步设置（内容区 = bounds 向内收 1）。

#include <string>
#include <string_view>

#include "vatui.hpp"
#include "vawk/widget/canvas.hpp"
#include "vawk/widget/widget.hpp"

namespace vawk::widget {

class Panel : public Widget {
  public:
    explicit Panel(std::string title = {}, BoxStyle style = BoxStyle::Round);

    void set_title(std::string title) { title_ = std::move(title); }
    const std::string& title() const { return title_; }

    void set_style(BoxStyle style) { style_ = style; }

    /** @brief 设置标题右侧的小标签（如 follow / paused），为空则不显示。 */
    void set_badge(std::string badge) { badge_ = std::move(badge); }

    /** @brief 挂内容控件（不取得所有权，可传 nullptr 解绑）。 */
    void set_content(Widget* content);
    Widget* content() const { return content_; }

    /** @brief 内容区（bounds 向内收 1）。 */
    Rect content_rect() const;

    void set_bounds(Rect r) override;
    void render(Canvas& canvas) override;
    bool on_event(const vaterm::InputData& event) override;

    /** @brief 焦点标记；内容控件会同步获得同样的标记。 */
    void set_focused(bool value) override;

    /** @brief 边框/标题/焦点配色，默认即可，留出定制口。 */
    vatui::Style line_style{};
    vatui::Style title_style{};
    vatui::Style focus_line_style{};
    vatui::Style focus_title_style{};
    vatui::Style badge_style{};

  private:
    std::string title_;
    std::string badge_;
    BoxStyle    style_;
    Widget*     content_ = nullptr;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_PANEL_HPP
