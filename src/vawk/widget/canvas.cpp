//- canvas.cpp — 裁剪绘制面

#include "vawk/widget/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

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


namespace {

double wrap_hue(double hue) {
    hue = std::fmod(hue, 360.0);
    if (hue < 0) hue += 360.0;
    return hue;
}

struct Rgb8 {
    int r = 0, g = 0, b = 0;
};

Rgb8 hsv_to_rgb(double h, double s, double v) {
    h = wrap_hue(h) / 60.0;
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    double c = v * s;
    double x = c * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    double m = v - c;

    double r = 0, g = 0, b = 0;
    if (h < 1)      { r = c; g = x; }
    else if (h < 2) { r = x; g = c; }
    else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; }
    else if (h < 5) { r = x; b = c; }
    else            { r = c; b = x; }

    return {static_cast<int>((r + m) * 255.0 + 0.5),
            static_cast<int>((g + m) * 255.0 + 0.5),
            static_cast<int>((b + m) * 255.0 + 0.5)};
}

// 色桶：色相 5°、饱和/明度 5%，让相邻单元格复用同一个 SGR，
// 既保留渐变观感，又不至于每格一个转义序列。
constexpr int kHueBucket = 5;
constexpr int kSvBucket  = 20;

vatui::Style gradient_style(double hue, double sat, double val, bool background) {
    static std::unordered_map<int, vatui::Style> cache;

    int hb = static_cast<int>(wrap_hue(hue) / kHueBucket) % (360 / kHueBucket);
    int sb = std::clamp(static_cast<int>(sat * kSvBucket + 0.5), 0, kSvBucket);
    int vb = std::clamp(static_cast<int>(val * kSvBucket + 0.5), 0, kSvBucket);

    int key = (hb << 12) | (sb << 6) | vb | (background ? 1 << 18 : 0);
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    Rgb8 rgb = hsv_to_rgb(hb * kHueBucket, sb / double(kSvBucket),
                          vb / double(kSvBucket));
    vaterm::Rgb c{static_cast<uint8_t>(rgb.r), static_cast<uint8_t>(rgb.g),
                  static_cast<uint8_t>(rgb.b)};

    vatui::Style style;
    if (background) style.bg_sgr = vatui::bg(c);
    else            style.fg_sgr = vatui::fg(c);
    cache.emplace(key, style);
    return style;
}

int hue_bucket(double hue) {
    return static_cast<int>(wrap_hue(hue) / kHueBucket) % (360 / kHueBucket);
}

double hue_at(const Gradient& g, int index, int length) {
    if (length <= 1) return g.from + g.phase;
    double t = double(index) / double(length - 1);
    return g.from + (g.to - g.from) * t + g.phase;
}

} // namespace

vatui::Style Canvas::hsv(double hue, double sat, double val, bool background) {
    return gradient_style(hue, sat, val, background);
}

void Canvas::fill_gradient(Rect r, const Gradient& gradient) {
    Rect area = intersect(r, clip_rect());
    if (area.empty()) return;

    const int length = gradient.vertical ? area.h : area.w;
    int       i      = 0;
    while (i < length) {
        const double hue = hue_at(gradient, i, length);
        const int    b   = hue_bucket(hue);

        int j = i + 1;
        while (j < length && hue_bucket(hue_at(gradient, j, length)) == b) ++j;

        const std::string spaces(static_cast<std::size_t>(j - i), ' ');
        const vatui::Style style = gradient_style(hue, gradient.sat, gradient.val, true);

        if (gradient.vertical)
            put_text(area.x, area.y + i, spaces, style);
        else
            put_text(area.x + i, area.y, spaces, style);

        i = j;
    }
}

void Canvas::put_text_rainbow(int col, int row, std::string_view text,
                              const Gradient& gradient, const vatui::Style& base) {
    if (text.empty()) return;
    const int total = vaterm::utf::width(text);
    if (total <= 0) return;

    std::string run;
    int         run_col = col;
    int         run_bucket = -1;
    int         offset = 0;

    auto make_style = [&](int at) {
        vatui::Style style =
            gradient_style(hue_at(gradient, at, total), gradient.sat,
                           gradient.val, false);
        style.bg_sgr      = base.bg_sgr;
        style.effects_sgr = base.effects_sgr;
        return style;
    };

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t n = vaterm::utf::char_bytes(text.substr(pos));
        if (n == 0) { ++pos; continue; }
        if (pos + n > text.size()) break;

        int w = vaterm::utf::char_width(text.substr(pos));
        if (w < 1) w = 1;

        const int b = hue_bucket(hue_at(gradient, offset, total));
        if (run_bucket >= 0 && b != run_bucket) {
            put_text(run_col, row, run, make_style(run_col - col));
            run.clear();
        }
        if (run.empty()) { run_col = col + offset; run_bucket = b; }

        run.append(text.substr(pos, n));
        offset += w;
        pos += n;
    }
    if (!run.empty()) put_text(run_col, row, run, make_style(run_col - col));
}

void Canvas::put_text_gradient(int col, int row, std::string_view text,
                               const Gradient& gradient) {
    if (text.empty()) return;

    const int total = vaterm::utf::width(text);
    if (total <= 0) return;

    std::string run;
    int         run_col = col;
    int         run_bucket = -1;
    int         offset = 0;

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t n = vaterm::utf::char_bytes(text.substr(pos));
        if (n == 0) { ++pos; continue; }
        if (pos + n > text.size()) break;

        int w = vaterm::utf::char_width(text.substr(pos));
        if (w < 1) w = 1;

        const double hue = hue_at(gradient, offset, total);
        const int    b   = hue_bucket(hue);

        if (run_bucket >= 0 && b != run_bucket) {
            put_text(run_col, row, run,
                     gradient_style(hue_at(gradient, run_col - col, total),
                                    gradient.sat, gradient.val, false));
            run.clear();
            run_col = col + offset;
        }
        if (run.empty()) { run_col = col + offset; run_bucket = b; }

        run.append(text.substr(pos, n));
        offset += w;
        pos += n;
    }

    if (!run.empty())
        put_text(run_col, row, run,
                 gradient_style(hue_at(gradient, run_col - col, total),
                                gradient.sat, gradient.val, false));
}

void Canvas::border_gradient(Rect r, BoxStyle style, const Gradient& gradient,
                             std::string_view title, Gradient title_gradient) {
    if (r.w < 2 || r.h < 2) return;
    if (style == BoxStyle::None) {
        if (!title.empty()) put_text_gradient(r.x + 1, r.y, title, title_gradient);
        return;
    }

    const BoxChars b = box_chars(style);
    const int      w = r.w;
    const int      h = r.h;

    auto draw_edge = [&](int y, int x0, int len, std::string_view ch, double h0,
                         double h1) {
        int i = 0;
        while (i < len) {
            double hue = h0 + (h1 - h0) * (len <= 1 ? 0.0 : double(i) / (len - 1))
                       + gradient.phase;
            int bucket = hue_bucket(hue);
            int j = i + 1;
            while (j < len) {
                double hh = h0 + (h1 - h0) * (len <= 1 ? 0.0 : double(j) / (len - 1))
                          + gradient.phase;
                if (hue_bucket(hh) != bucket) break;
                ++j;
            }
            put_text(x0 + i, y, repeat(ch, j - i),
                     gradient_style(hue, gradient.sat, gradient.val, false));
            i = j;
        }
    };

    draw_edge(r.y, r.x, w, b.h, gradient.from, gradient.to);
    draw_edge(r.bottom() - 1, r.x, w, b.h, gradient.to, gradient.from);

    for (int y = r.y + 1; y < r.bottom() - 1; ++y) {
        double t = double(y - r.y) / double(h - 1);
        double hue = gradient.from + (gradient.to - gradient.from) * t + gradient.phase;
        put_text(r.x, y, b.v, gradient_style(hue, gradient.sat, gradient.val, false));
        put_text(r.right() - 1, y, b.v,
                 gradient_style(hue, gradient.sat, gradient.val, false));
    }

    put_text(r.x, r.y, b.tl, gradient_style(gradient.from + gradient.phase,
                                            gradient.sat, gradient.val, false));
    put_text(r.right() - 1, r.y, b.tr,
             gradient_style(gradient.to + gradient.phase, gradient.sat, gradient.val, false));
    put_text(r.x, r.bottom() - 1, b.bl,
             gradient_style(gradient.to + gradient.phase, gradient.sat, gradient.val, false));
    put_text(r.right() - 1, r.bottom() - 1, b.br,
             gradient_style(gradient.from + gradient.phase, gradient.sat, gradient.val, false));

    if (!title.empty() && r.w > 6) {
        std::string t = " ";
        t += title;
        t += ' ';
        if (vaterm::utf::width(t) <= r.w - 4) {
            if (title_gradient.from == 0 && title_gradient.to == 0) {
                title_gradient.from = gradient.from;
                title_gradient.to   = gradient.to;
            }
            title_gradient.phase += gradient.phase;
            put_text_gradient(r.x + 2, r.y, t, title_gradient);
        }
    }
}

} // namespace vawk::widget
