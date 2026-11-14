#ifndef VAWK_WIDGET_CANVAS_HPP
#define VAWK_WIDGET_CANVAS_HPP

//- vawk::widget::Canvas — 带裁剪的绘制面
//
// 包一层 vatui::Framebuffer，补上三件 Framebuffer 没有的事：
//   1. 矩形裁剪栈（push_clip / pop_clip），越界内容直接被丢弃；
//   2. 按显示宽度排版（CJK / emoji 占两列，宽字符跨裁剪边界时舍弃或补空格）；
//   3. 便捷图元：fill / hline / vline / border，以及带标题的边框。
//
// 坐标一律是「帧缓冲绝对坐标」，与 Widget::bounds() 同一坐标系，
// 这样嵌套控件不需要自己做坐标平移。

#include <string>
#include <string_view>
#include <vector>

#include "vaterm/utf.hpp"
#include "vatui.hpp"
#include "vawk/widget/rect.hpp"

namespace vawk::widget {

/** @brief 边框线型。 */
enum class BoxStyle { None, Single, Double, Round };

/**
 * @brief 裁剪绘制面。
 * @note 构造时的 clip 是初始裁剪区，通常就是整屏。
 *       所有绘制函数都只在当前裁剪区内落笔。
 */
class Canvas {
  public:
    Canvas(vatui::Framebuffer& fb, Rect clip);

    /** @brief 帧缓冲宽度（不受裁剪影响）。 */
    int screen_w() const { return fb_.getColMax(); }
    /** @brief 帧缓冲高度。 */
    int screen_h() const { return fb_.getRowMax(); }

    /** @brief 当前生效的裁剪区（栈顶与初始裁剪区的交）。 */
    Rect clip_rect() const;

    //-- 文本

    /** @brief 在 (col,row) 起绘制一段 UTF-8 文本，超出裁剪区的部分被裁掉。 */
    void put_text(int col, int row, std::string_view text, const vatui::Style& style);

    /** @brief 同上，用默认样式（白字、透明背景）。 */
    void put_text(int col, int row, std::string_view text);

    /**
     * @brief 绘制文本并把宽度补齐到 width 列（补空格，用于整行反显）。
     * @return 实际写入的列数。
     */
    int put_text_padded(int col, int row, std::string_view text, int width,
                        const vatui::Style& style);

    /** @brief 右对齐绘制：文本的最后一个字符落在 right_col 列。 */
    void put_text_right(int right_col, int row, std::string_view text,
                        const vatui::Style& style);

    //-- 图形

    /** @brief 用单字节字符填满矩形（与裁剪区求交）。 */
    void fill(Rect r, char ch, const vatui::Style& style);

    /** @brief 用空格 + 默认样式清空矩形。 */
    void clear(Rect r);

    /** @brief 水平线；ch 是 UTF-8 串，可传 "─" 之类。 */
    void hline(int col, int row, int len, std::string_view ch, const vatui::Style& style);

    /** @brief 垂直线。 */
    void vline(int col, int row, int len, std::string_view ch, const vatui::Style& style);

    /**
     * @brief 画边框，可选在顶边写标题。
     * @param title     顶边标题，从 r.x + 2 开始；已带两侧空格。
     * @param title_style 标题样式；为空则沿用边框样式。
     * @note BoxStyle::None 时只画标题、不画线。
     */
    void border(Rect r, BoxStyle style, const vatui::Style& line_style,
                std::string_view title = {}, const vatui::Style& title_style = {});

    //-- 裁剪栈

    /**
     * @brief 压入一个新的裁剪区（与当前裁剪区取交）。
     * @return 压入后实际生效的裁剪区，便于调用方提前判断是否还要绘制。
     */
    Rect push_clip(Rect r);
    /** @brief 弹出裁剪区；栈空时无操作。 */
    void pop_clip();
    /** @brief 当前裁剪栈深度。 */
    std::size_t clip_depth() const { return clips_.size(); }

    //-- 底层

    /** @brief 取回底层帧缓冲（需要直接操作时用）。 */
    vatui::Framebuffer& framebuffer() { return fb_; }

  private:
    vatui::Framebuffer& fb_;
    Rect                base_;
    std::vector<Rect>   clips_;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_CANVAS_HPP
