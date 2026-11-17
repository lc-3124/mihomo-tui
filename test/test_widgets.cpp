//- test_widgets.cpp — 组件层单测（无框架，失败即退出）

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vaterm/input.hpp"
#include "vatui.hpp"
#include "vawk/widget/canvas.hpp"
#include "vawk/widget/focus.hpp"
#include "vawk/widget/list_view.hpp"
#include "vawk/widget/log_view.hpp"
#include "vawk/widget/panel.hpp"
#include "vawk/widget/rect.hpp"
#include "vawk/widget/segmented.hpp"
#include "vawk/widget/sparkline.hpp"

using namespace vawk::widget;
using vaterm::InputData;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                         #cond);                                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (!(_a == _b)) {                                                     \
            std::fprintf(stderr, "  FAIL %s:%d: %s != %s\n", __FILE__,         \
                         __LINE__, #a, #b);                                    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

std::vector<std::string> split_plain(const std::string& text) {
    std::vector<std::string> lines;
    std::string              cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

struct Frame {
    vatui::Framebuffer fb;
    std::string        text;

    Frame(int cols, int rows) {
        fb.setSize({cols, rows});
        text = fb.compose_plain();
    }
};

std::vector<std::string> render(Widget& widget, int cols, int rows) {
    vatui::Framebuffer fb;
    fb.setSize({cols, rows});
    Canvas canvas(fb, Rect{0, 0, cols, rows});
    widget.set_bounds(Rect{0, 0, cols, rows});
    widget.render(canvas);
    return split_plain(fb.compose_plain());
}

//-- Rect -------------------------------------------------------------------

void test_rect() {
    std::puts("test_rect");
    Rect r{0, 0, 80, 24};

    CHECK_EQ(inset(r, 1).x, 1);
    CHECK_EQ(inset(r, 1).w, 78);
    CHECK_EQ(inset(r, 100).w, 0);

    auto [left, right] = split_h(r, 30);
    CHECK_EQ(left.w, 30);
    CHECK_EQ(right.x, 30);
    CHECK_EQ(right.w, 50);
    CHECK_EQ(left.h, 24);

    auto [top, bottom] = split_v(r, 10);
    CHECK_EQ(top.h, 10);
    CHECK_EQ(bottom.y, 10);
    CHECK_EQ(bottom.h, 14);

    // 越界参数被夹住，不产生负宽高。
    auto [l2, r2] = split_h(r, 999);
    CHECK_EQ(l2.w, 80);
    CHECK_EQ(r2.w, 0);

    CHECK(intersect(Rect{0, 0, 10, 10}, Rect{5, 5, 10, 10}) ==
          (Rect{5, 5, 5, 5}));
    CHECK(intersect(Rect{0, 0, 4, 4}, Rect{10, 10, 2, 2}).empty());
    CHECK((Rect{0, 0, 4, 4}.contains(3, 3)));
    CHECK((!Rect{0, 0, 4, 4}.contains(4, 3)));
}

//-- Canvas -----------------------------------------------------------------

void test_canvas_clip() {
    std::puts("test_canvas_clip");

    vatui::Framebuffer fb;
    fb.setSize({20, 4});
    Canvas canvas(fb, Rect{0, 0, 20, 4});

    canvas.put_text(-3, 0, "abcdef", {});
    auto lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0], "def");

    canvas.put_text(17, 1, "abcdef", {});
    lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[1], "                 abc");

    // 右边缘放不下的宽字符整个丢弃，不留半个字形。
    canvas.fill(Rect{0, 2, 20, 1}, ' ', {});
    canvas.put_text(19, 2, "中", {});
    lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[2], "");

    // 裁剪区左边界正好切在宽字符中间：补一个空格占住可见列。
    Canvas clipped(fb, Rect{2, 0, 10, 1});
    clipped.put_text(1, 0, "中a", {});
    lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0][2], ' ');
    CHECK_EQ(lines[0][3], 'a');

    // 右侧裁剪。
    Canvas right(fb, Rect{0, 3, 5, 1});
    right.put_text(0, 3, "abcdef", {});
    lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[3], "abcde");

    // push/pop 恢复裁剪区。
    right.push_clip(Rect{0, 3, 2, 1});
    right.put_text(0, 3, "XY", {});
    right.pop_clip();
    CHECK_EQ(right.clip_rect().w, 5);

    // 文本右侧补齐。
    vatui::Framebuffer fb2;
    fb2.setSize({10, 1});
    Canvas c2(fb2, Rect{0, 0, 10, 1});
    c2.put_text_padded(0, 0, "ab", 6, {});
    c2.put_text(6, 0, "Z", {});
    CHECK_EQ(split_plain(fb2.compose_plain())[0], "ab    Z");
}

void test_canvas_border() {
    std::puts("test_canvas_border");

    vatui::Framebuffer fb;
    fb.setSize({12, 4});
    Canvas canvas(fb, Rect{0, 0, 12, 4});
    canvas.border(Rect{0, 0, 12, 4}, BoxStyle::Round, {}, "hi", {});

    auto lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0], "╭─ hi ─────╮");
    CHECK_EQ(lines[1], "│          │");
    CHECK_EQ(lines[3], "╰──────────╯");
}

//-- Panel ------------------------------------------------------------------

void test_panel() {
    std::puts("test_panel");

    LogView view(10);
    view.push(LogLevel::Info, "hello");

    Panel panel("标题");
    panel.set_content(&view);
    auto lines = render(panel, 16, 4);

    CHECK_EQ(lines[0], "╭─ 标题 ───────╮");
    CHECK_EQ(lines[1], "│hello         │");
    CHECK_EQ(view.bounds(), (Rect{1, 1, 14, 2}));

    panel.set_focused(true);
    CHECK(view.focused);
    panel.set_focused(false);
    CHECK(!view.focused);

    panel.set_badge("follow");
    lines = render(panel, 24, 3);
    CHECK(lines[0].find("follow") != std::string::npos);
}

//-- LogView ----------------------------------------------------------------

void test_log_view() {
    std::puts("test_log_view");

    LogView view(100);
    for (int i = 0; i < 5; ++i) view.push(LogLevel::Info, "L" + std::to_string(i));

    auto lines = render(view, 10, 3);
    CHECK_EQ(lines[0], "L2");
    CHECK_EQ(lines[1], "L3");
    CHECK_EQ(lines[2], "L4");

    CHECK(view.following());
    view.set_follow(false);
    view.scroll(2);
    lines = render(view, 10, 3);
    // 从底部往上两行：选中 L2，视口保持 L2..L4。
    CHECK_EQ(lines[0], "L2");
    CHECK_EQ(lines[2], "L4");
    CHECK(view.selected_line());
    CHECK_EQ(view.selected_line()->text, "L2");

    // 暂停后新行到达，屏幕内容不能动、选中行也不能变。
    view.push(LogLevel::Info, "L5");
    lines = render(view, 10, 3);
    CHECK_EQ(lines[0], "L2");
    CHECK_EQ(lines[2], "L4");
    CHECK_EQ(view.selected_line()->text, "L2");

    // 恢复跟随 → 贴底。
    view.set_follow(true);
    lines = render(view, 10, 3);
    CHECK_EQ(lines[2], "L5");

    // 过滤：只留匹配的行。
    view.set_filter("L3");
    lines = render(view, 10, 3);
    CHECK_EQ(lines[0], "L3");
    CHECK(view.filter() == "L3");

    // 大小写不敏感。
    view.push(LogLevel::Warn, "LEVEL=WARN something");
    view.set_filter("level=warn");
    lines = render(view, 30, 3);
    CHECK(lines[0].find("LEVEL=WARN") != std::string::npos);
    view.set_filter("");

    // 空格键切换暂停。
    InputData space = InputData::makeKeybd({.cp = U' '});
    CHECK(view.on_event(space));
    CHECK(!view.following());
    CHECK(view.on_event(space));
    CHECK(view.following());

    // 环形缓冲截断。
    LogView small(3);
    for (int i = 0; i < 10; ++i) small.push(LogLevel::Info, std::to_string(i));
    CHECK_EQ(small.size(), std::size_t{3});
    lines = render(small, 5, 2);
    CHECK_EQ(lines[0], "8");
    CHECK_EQ(lines[1], "9");
}

void test_log_spans() {
    std::puts("test_log_spans");

    LogView view(10);
    LogLine line;
    line.level = LogLevel::Info;
    line.text  = "AB";
    line.raw   = "raw-AB";
    line.spans = {{"A", LogTone::Proto}, {"B", LogTone::Default}};
    view.push(line);

    auto lines = render(view, 10, 2);
    CHECK_EQ(lines[0], "AB");

    view.set_verbose(true);
    CHECK(view.verbose());
    lines = render(view, 10, 2);
    CHECK_EQ(lines[0], "raw-AB");
    view.set_verbose(false);

    // 过滤要能匹配到原始行（哪怕精简后看不到那个词）。
    view.set_filter("raw-");
    lines = render(view, 10, 2);
    CHECK_EQ(lines[0], "AB");
    view.set_filter("no-such-thing");
    lines = render(view, 10, 2);
    CHECK(lines[0].find("没有匹配") != std::string::npos);
}

//-- ListView ---------------------------------------------------------------

void test_list_view() {
    std::puts("test_list_view");

    ListView view;
    view.set_items({{"a", "Alpha", "1ms"}, {"b", "Beta", "2ms"}, {"c", "Gamma", "3ms"}});
    view.set_bounds(Rect{0, 0, 20, 3});

    auto lines = render(view, 20, 3);
    CHECK(lines[0].find("Alpha") != std::string::npos);
    CHECK(lines[0].find("1ms") != std::string::npos);
    CHECK_EQ(lines[0][0], "▸"[0]);

    view.move(1);
    CHECK_EQ(view.selected(), 1);
    CHECK(view.selected_item()->id == "b");

    view.move(99);
    CHECK_EQ(view.selected(), 2);
    view.move(-99);
    CHECK_EQ(view.selected(), 0);

    std::string confirmed;
    view.on_confirm = [&](const ListItem& item) { confirmed = item.id; };

    view.move(1);
    CHECK(view.on_event(InputData::makeKeybd({.code = vaterm::KeyCode::ENTER})));
    CHECK_EQ(confirmed, "b");

    // 过滤后视图缩到 1 项，selected_item 仍然正确。
    view.set_filter("gam");
    CHECK_EQ(view.selected(), 0);
    CHECK(view.selected_item()->id == "c");

    // 点击：第一次选中，第二次应用。
    ListView clicked;
    clicked.set_items({{"x", "X", ""}, {"y", "Y", ""}});
    clicked.set_bounds(Rect{0, 5, 20, 2});
    std::string picked;
    clicked.on_confirm = [&](const ListItem& item) { picked = item.id; };

    InputData press = InputData::makeMouse({.col = 2, .row = 6,
                                            .button = vaterm::MouseButton::LEFT,
                                            .action = vaterm::MouseAction::PRESS});
    clicked.on_event(press);
    CHECK_EQ(clicked.selected(), 1);
    CHECK(picked.empty());
    clicked.on_event(press);
    CHECK_EQ(picked, "y");

    // 滚轮。
    clicked.on_event(InputData::makeMouse({.col = 2, .row = 6,
                                           .button = vaterm::MouseButton::NONE,
                                           .action = vaterm::MouseAction::SCROLL_UP}));
    CHECK_EQ(clicked.selected(), 0);
}

//-- Sparkline --------------------------------------------------------------

void test_sparkline() {
    std::puts("test_sparkline");

    Sparkline spark(4);
    for (double v : {0.0, 0.5, 1.0, 0.5, 2.0}) spark.push(v);
    CHECK_EQ(spark.values().size(), std::size_t{4});
    CHECK_EQ(spark.peak(), 2.0);

    vatui::Framebuffer fb;
    fb.setSize({4, 1});
    Canvas canvas(fb, Rect{0, 0, 4, 1});
    spark.render(canvas, Rect{0, 0, 4, 1}, {});

    std::string line = split_plain(fb.compose_plain())[0];
    CHECK_EQ(vaterm::utf::count(line), std::size_t{4});
    CHECK(line.find("█") != std::string::npos);
    CHECK(line.find("▁") == std::string::npos); // 最小非零值至少是 ▂

    Sparkline empty(4);
    CHECK_EQ(empty.peak(), 0.0);
    vatui::Framebuffer fb2;
    fb2.setSize({4, 1});
    Canvas c2(fb2, Rect{0, 0, 4, 1});
    empty.render(c2, Rect{0, 0, 4, 1}, {});
    CHECK_EQ(split_plain(fb2.compose_plain())[0], "");
}

//-- Segmented --------------------------------------------------------------

void test_segmented() {
    std::puts("test_segmented");

    Segmented seg({"直连", "规则", "全局"}, 1);
    int       changed = -1;
    seg.on_change = [&](int index) { changed = index; };

    CHECK_EQ(seg.preferred_width(), 6 + 6 + 6);
    CHECK_EQ(seg.selected(), 1);

    seg.on_event(InputData::makeKeybd({.code = vaterm::KeyCode::RIGHT}));
    CHECK_EQ(seg.selected(), 2);
    CHECK_EQ(changed, 2);

    seg.on_event(InputData::makeKeybd({.code = vaterm::KeyCode::LEFT}));
    seg.on_event(InputData::makeKeybd({.code = vaterm::KeyCode::LEFT}));
    CHECK_EQ(seg.selected(), 0);

    seg.set_bounds(Rect{0, 0, 30, 1});
    auto lines = render(seg, 30, 1);
    CHECK_EQ(lines[0], " 直连  规则  全局");

    // 点击第 3 段（列 14 落在 "全局" 上）。
    seg.on_event(InputData::makeMouse({.col = 14, .row = 0,
                                       .button = vaterm::MouseButton::LEFT,
                                       .action = vaterm::MouseAction::PRESS}));
    CHECK_EQ(seg.selected(), 2);
    CHECK_EQ(seg.selected_label(), "全局");
}

//-- Focus ------------------------------------------------------------------

namespace {

class Probe : public Widget {
  public:
    explicit Probe(std::string name) : name_(std::move(name)) {}
    void render(Canvas&) override {}
    bool on_event(const vaterm::InputData& event) override {
        last = event.value.keybd.cp;
        ++count;
        return true;
    }
    std::string name_;
    int         count = 0;
    char32_t    last  = 0;
};

} // namespace

void test_focus() {
    std::puts("test_focus");

    Probe a("a"), b("b"), c("c");
    a.set_bounds(Rect{0, 0, 10, 5});
    b.set_bounds(Rect{10, 0, 10, 5});
    c.set_bounds(Rect{0, 5, 10, 5});

    FocusChain chain;
    chain.add(&a);
    chain.add(&b);
    chain.add(&c);

    CHECK_EQ(chain.index(), 0);
    CHECK(a.focused);
    CHECK(!b.focused);

    CHECK(chain.next());
    CHECK_EQ(chain.index(), 1);
    CHECK(b.focused);
    CHECK(!a.focused);

    chain.prev();
    CHECK_EQ(chain.index(), 0);

    chain.focus(&c);
    CHECK_EQ(chain.index(), 2);

    // Tab / Shift-Tab 由链自己吃掉。
    CHECK(chain.route(InputData::makeKeybd({.code = vaterm::KeyCode::TAB})));
    CHECK_EQ(chain.index(), 0);
    CHECK(chain.route(InputData::makeKeybd({.code = vaterm::KeyCode::TAB,
                                            .isShift = true})));
    CHECK_EQ(chain.index(), 2);

    // 普通键发给当前控件。
    CHECK(chain.route(InputData::makeKeybd({.cp = U'x'})));
    CHECK_EQ(c.last, U'x');
    CHECK_EQ(c.count, 1);
    CHECK_EQ(a.count, 0);

    CHECK(chain.widget_at(12, 2) == &b);
    CHECK(chain.widget_at(2, 7) == &c);
    CHECK(chain.widget_at(50, 50) == nullptr);

    chain.clear();
    CHECK(chain.current() == nullptr);
    CHECK(!a.focused);
}

void test_absolute_anchors() {
    std::puts("test_absolute_anchors");

    // 国旗 = 两个区域指示符，终端按 2 列渲染。
    CHECK_EQ(vaterm::utf::width("🇸🇬"), 2);
    CHECK_EQ(vaterm::utf::width("🇸🇬x"), 3);
    CHECK_EQ(vaterm::utf::width("中"), 2);

    // 相邻两段文本也必须各自绝对定位，否则前一段的宽度误差会带偏后一段。
    vatui::Framebuffer fb;
    fb.setSize({10, 1});
    Canvas canvas(fb, Rect{0, 0, 10, 1});
    canvas.put_text(0, 0, "A", {});
    canvas.put_text(1, 0, "B", {});

    auto lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0], "AB");

    std::string ansi = fb.compose();
    CHECK(ansi.find("\033[1;2H") != std::string::npos);

    // 不同国旗开头的行，延迟右端必须落在同一列。
    ListView view;
    view.set_items({{"a", "🇸🇬 新加坡01", "921ms"},
                    {"b", "🇯🇵 日本01", "1004ms"}});
    auto rows = render(view, 20, 2);
    CHECK_EQ(vaterm::utf::width(rows[0]), vaterm::utf::width(rows[1]));
    CHECK(rows[0].find("921ms") != std::string::npos);
    CHECK(rows[1].find("1004ms") != std::string::npos);
}

//-- Framebuffer ------------------------------------------------------------

void test_framebuffer() {
    std::puts("test_framebuffer");

    vatui::Framebuffer fb;
    fb.setSize({6, 2});
    fb.printText({.col = 0, .row = 0, .text = "中文a", .style = {}});

    auto lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0], "中文a");

    std::string ansi = fb.compose();
    CHECK(ansi.find("\033[") != std::string::npos);
    CHECK(fb.last_composed_cells() > 0);

    // 覆盖宽字符的尾格时，头格要被清掉，不能留半个字。
    fb.printText({.col = 1, .row = 0, .text = "X", .style = {}});
    lines = split_plain(fb.compose_plain());
    CHECK_EQ(lines[0], " X文a");
}

} // namespace

int main() {
    test_rect();
    test_canvas_clip();
    test_canvas_border();
    test_panel();
    test_log_view();
    test_log_spans();
    test_list_view();
    test_sparkline();
    test_segmented();
    test_focus();
    test_absolute_anchors();
    test_framebuffer();

    if (g_failures == 0) {
        std::puts("test_widgets: OK");
        return 0;
    }
    std::fprintf(stderr, "test_widgets: %d failure(s)\n", g_failures);
    return 1;
}
