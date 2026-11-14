#ifndef VAWK_WIDGET_RECT_HPP
#define VAWK_WIDGET_RECT_HPP

//- vawk::widget::Rect — 屏幕矩形与布局原语
//
// 坐标系：列 x / 行 y，左上角为 (0,0)，宽高可为 0（空矩形）。
// 所有布局函数都容忍空矩形与负数宽度：结果会被夹成空矩形，
// 保证调用方可以无脑链式切分。

#include <algorithm>
#include <utility>

namespace vawk::widget {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
    constexpr bool empty() const { return w <= 0 || h <= 0; }

    constexpr bool contains(int col, int row) const {
        return col >= x && col < x + w && row >= y && row < y + h;
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

/** @brief 四边各向内收 n 格；结果为空时返回 {x,y,0,0}。 */
inline Rect inset(Rect r, int n) {
    r.x += n;
    r.y += n;
    r.w -= 2 * n;
    r.h -= 2 * n;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

/** @brief 两个矩形的交；不相交时返回空矩形（位于交叠原点）。 */
inline Rect intersect(Rect a, Rect b) {
    int x0 = std::max(a.x, b.x);
    int y0 = std::max(a.y, b.y);
    int x1 = std::min(a.right(), b.right());
    int y1 = std::min(a.bottom(), b.bottom());
    if (x1 <= x0 || y1 <= y0) return Rect{x0, y0, 0, 0};
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

/** @brief clip 的别名，读起来更符合「把 r 裁到 by 里」。 */
inline Rect clip(Rect r, Rect by) { return intersect(r, by); }

/** @brief 是否共享至少一个单元格。 */
inline bool overlaps(Rect a, Rect b) { return !intersect(a, b).empty(); }

/**
 * @brief 左右切分：左侧固定 left_w，剩余给右侧。
 * @note left_w 会被夹到 [0, r.w]；两侧共享边界，不重叠。
 */
inline std::pair<Rect, Rect> split_h(Rect r, int left_w) {
    left_w = std::clamp(left_w, 0, r.w);
    return {Rect{r.x, r.y, left_w, r.h},
            Rect{r.x + left_w, r.y, r.w - left_w, r.h}};
}

/**
 * @brief 上下切分：上方固定 top_h，剩余给下方。
 * @note top_h 会被夹到 [0, r.h]。
 */
inline std::pair<Rect, Rect> split_v(Rect r, int top_h) {
    top_h = std::clamp(top_h, 0, r.h);
    return {Rect{r.x, r.y, r.w, top_h},
            Rect{r.x, r.y + top_h, r.w, r.h - top_h}};
}

} // namespace vawk::widget

#endif // VAWK_WIDGET_RECT_HPP
