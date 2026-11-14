//- canvas.cpp — 裁剪绘制面

#include "vawk/widget/canvas.hpp"

#include <algorithm>
#include <string>

namespace vawk::widget {

namespace {

// 把一个 UTF-8 字形重复 n 次（n<=0 返回空串）。
std::string repeat(std::string_view s, int n) {
    if (n <= 0 || s.empty()) return {};
    std::string out;
    out.reserve(s.size() * static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) out.append(s);
    return out;
}

// 按 BoxStyle 取线型：上左、上右、下左、下右、横、竖。
struct BoxChars {
    std::string_view tl, tr, bl, br, h, v;
};

BoxChars box_chars(BoxStyle s) {
    switch (s) {
    case BoxStyle::Double: return {"╔", "╗", "╚", "╝", "═", "║"};
    case BoxStyle::Round:  return {"╭", "╮", "╰", "╯", "─", "│"};
    case BoxStyle::Single: return {"┌", "┐", "└", "┘", "─", "│"};
    case BoxStyle::None:   break;
    }
    return {"", "", "", "", "", ""};
}

} // namespace

Canvas::Canvas(vatui::Framebuffer& fb, Rect clip_area)
    : fb_(fb), base_(intersect(clip_area, Rect{0, 0, fb.getColMax(), fb.getRowMax()})) {}

Rect Canvas::clip_rect() const {
    Rect r = base_;
    for (const auto& c : clips_) r = intersect(r, c);
    return r;
}

Rect Canvas::push_clip(Rect r) {
    clips_.push_back(r);
    return clip_rect();
}

void Canvas::pop_clip() {
    if (!clips_.empty()) clips_.pop_back();
}

void Canvas::put_text(int col, int row, std::string_view text, const vatui::Style& style) {
    Rect area = clip_rect();
    if (area.empty()) return;
    if (row < area.y || row >= area.bottom()) return;
    if (col >= area.right()) return;
    if (text.empty()) return;

    const int limit = area.right();
    int       c     = col;
    std::size_t pos = 0;

    bool        started = false;
    int         run_col = 0;
    std::size_t run_pos = 0;

    while (pos < text.size()) {
        std::size_t n = vaterm::utf::char_bytes(text.substr(pos));
        if (n == 0) { ++pos; continue; }             // 非法续字节：丢弃
        if (pos + n > text.size()) break;            // 截断的半个码点

        int w = vaterm::utf::char_width(text.substr(pos));
        if (w < 1) w = 1;                            // 控制字符按 1 列占位

        if (c + w <= area.x) { c += w; pos += n; continue; } // 完全在左裁剪切外
        if (c >= limit) break;                              // 完全在右裁剪切外

        if (!started) {
            // 宽字符跨左边界：只露出一半，补一个空格占住可见的那一列。
            if (c < area.x) {
                fb_.printText(vatui::TextArgs{.col = area.x, .row = row,
                                              .text = " ", .style = style}, true);
                c += w;
                pos += n;
                continue;
            }
            started = true;
            run_col = c;
            run_pos = pos;
        }

        if (c + w > limit) break;                    // 跨右边界：整个字形舍弃
        c += w;
        pos += n;
    }

    if (!started) return;
    // 每段都绝对定位：右对齐的延迟、面板边框不能依赖上一段的实际宽度。
    fb_.printText(vatui::TextArgs{.col = run_col, .row = row,
                                  .text = text.substr(run_pos, pos - run_pos),
                                  .style = style}, true);
}

void Canvas::put_text(int col, int row, std::string_view text) {
    put_text(col, row, text, vatui::Style{});
}

int Canvas::put_text_padded(int col, int row, std::string_view text, int width,
                            const vatui::Style& style) {
    if (width <= 0) return 0;
    put_text(col, row, text, style);
    int used = vaterm::utf::width(text);
    if (used > width) used = width;
    if (used < width) fill(Rect{col + used, row, width - used, 1}, ' ', style);
    return used;
}

void Canvas::put_text_right(int right_col, int row, std::string_view text,
                            const vatui::Style& style) {
    int w = vaterm::utf::width(text);
    put_text(right_col + 1 - w, row, text, style);
}

void Canvas::fill(Rect r, char ch, const vatui::Style& style) {
    r = intersect(r, clip_rect());
    if (r.empty()) return;
    fb_.fillRegion(vatui::FillArgs{.col = r.x, .row = r.y, .w = r.w, .h = r.h,
                                   .ch = ch, .style = style});
}

void Canvas::clear(Rect r) { fill(r, ' ', vatui::Style{}); }

void Canvas::hline(int col, int row, int len, std::string_view ch,
                   const vatui::Style& style) {
    if (len <= 0) return;
    put_text(col, row, repeat(ch, len), style);
}

void Canvas::vline(int col, int row, int len, std::string_view ch,
                   const vatui::Style& style) {
    for (int i = 0; i < len; ++i) put_text(col, row + i, ch, style);
}

void Canvas::border(Rect r, BoxStyle style, const vatui::Style& line_style,
                    std::string_view title, const vatui::Style& title_style) {
    Rect area = intersect(r, clip_rect());
    if (area.empty()) return;

    if (style == BoxStyle::None) {
        // 无边框时标题仍然要显示，否则面板看起来像没头。
        if (!title.empty()) put_text(r.x + 1, r.y, title, title_style);
        return;
    }

    const BoxChars b = box_chars(style);

    hline(r.x, r.y, r.w, b.h, line_style);
    hline(r.x, r.bottom() - 1, r.w, b.h, line_style);
    for (int y = r.y + 1; y < r.bottom() - 1; ++y) {
        put_text(r.x, y, b.v, line_style);
        put_text(r.right() - 1, y, b.v, line_style);
    }
    put_text(r.x, r.y, b.tl, line_style);
    put_text(r.right() - 1, r.y, b.tr, line_style);
    put_text(r.x, r.bottom() - 1, b.bl, line_style);
    put_text(r.right() - 1, r.bottom() - 1, b.br, line_style);

    if (!title.empty() && r.w > 6) {
        std::string t = " ";
        t += title;
        t += ' ';
        if (vaterm::utf::width(t) <= r.w - 4)
            put_text(r.x + 2, r.y, t, title_style);
    }
}

} // namespace vawk::widget
