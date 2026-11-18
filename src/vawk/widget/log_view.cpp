//- log_view.cpp — 环形日志视图

#include "vawk/widget/log_view.hpp"

#include <algorithm>
#include <cctype>

namespace vawk::widget {

namespace {

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

} // namespace

LogView::LogView(std::size_t capacity) : capacity_(capacity ? capacity : 1) {
    debug_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{244}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::DIM})};
    info_style  = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({})};
    warn_style  = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{220}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({})};
    error_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{203}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    empty_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{243}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::DIM})};

    dim_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{243}),
                             .bg_sgr = vatui::bg(vaterm::TRANS),
                             .effects_sgr = vatui::effects({})};
    proto_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{81}),
                               .bg_sgr = vatui::bg(vaterm::TRANS),
                               .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    host_style = info_style;
    out_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{150}),
                             .bg_sgr = vatui::bg(vaterm::TRANS),
                             .effects_sgr = vatui::effects({})};
    direct_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{114}),
                                .bg_sgr = vatui::bg(vaterm::TRANS),
                                .effects_sgr = vatui::effects({})};
    reject_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{203}),
                                .bg_sgr = vatui::bg(vaterm::TRANS),
                                .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    accent_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{213}),
                                .bg_sgr = vatui::bg(vaterm::TRANS),
                                .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    selected_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                                  .bg_sgr = vatui::bg(vaterm::Color8{25}),
                                  .effects_sgr = vatui::effects({})};
    selected_time_style = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                                       .bg_sgr = vatui::bg(vaterm::Color8{31}),
                                       .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
}

const vatui::Style& LogView::tone_style(LogTone tone) const {
    switch (tone) {
    case LogTone::Dim:    return dim_style;
    case LogTone::Proto:  return proto_style;
    case LogTone::Host:   return host_style;
    case LogTone::Out:    return out_style;
    case LogTone::Direct: return direct_style;
    case LogTone::Reject: return reject_style;
    case LogTone::Accent: return accent_style;
    case LogTone::Time:   return dim_style;
    case LogTone::Warn:   return warn_style;
    case LogTone::Error:  return error_style;
    case LogTone::Default: break;
    }
    return info_style;
}

bool LogView::matches(const LogLine& line) const {
    if (filter_.empty()) return true;
    return icontains(line.text, filter_) || icontains(line.raw, filter_);
}

void LogView::push(LogLine line) {
    std::lock_guard lock(mutex_);
    lines_.push_back(std::move(line));
    while (lines_.size() > capacity_) lines_.pop_front();
    // follow 时选中行在 render 里贴到最新；暂停时 selected_ 是视图下标，
    // 新行追加在末尾，不会改变已选中的那一行。
}

void LogView::push(LogLevel level, std::string text) {
    LogLine line;
    line.level = level;
    line.raw   = text;
    line.text  = std::move(text);
    push(std::move(line));
}

void LogView::clear() {
    std::lock_guard lock(mutex_);
    lines_.clear();
    top_      = 0;
    selected_ = -1;
    follow_   = true;
}

std::size_t LogView::size() const {
    std::lock_guard lock(mutex_);
    return lines_.size();
}

void LogView::set_follow(bool follow) {
    std::lock_guard lock(mutex_);
    follow_ = follow;
    if (follow) selected_ = -1; // render 时贴到最后一行
    else if (selected_ < 0) selected_ = static_cast<int>(filtered_locked().size()) - 1;
}

bool LogView::following() const {
    std::lock_guard lock(mutex_);
    return follow_;
}

void LogView::scroll(int delta) {
    std::lock_guard lock(mutex_);
    if (delta == 0) return;

    const int total = static_cast<int>(filtered_locked().size());
    if (total == 0) return;

    if (selected_ < 0) selected_ = total - 1;
    follow_   = false;
    // delta > 0 表示往旧的方向（视图下标更小）。
    selected_ = std::clamp(selected_ - delta, 0, total - 1);
}

void LogView::page_up(int page) {
    if (page < 1) page = 1;
    scroll(page);
}

void LogView::page_down(int page) {
    if (page < 1) page = 1;
    scroll(-page);
}

void LogView::to_top() {
    std::lock_guard lock(mutex_);
    follow_   = false;
    selected_ = 0;
    top_      = 0;
}

void LogView::to_bottom() {
    std::lock_guard lock(mutex_);
    follow_   = true;
    selected_ = -1;
}

void LogView::set_filter(std::string filter) {
    std::lock_guard lock(mutex_);
    filter_   = std::move(filter);
    top_      = 0;
    selected_ = -1;
    follow_   = true;
}

void LogView::set_verbose(bool verbose) {
    std::lock_guard lock(mutex_);
    verbose_ = verbose;
}

bool LogView::verbose() const {
    std::lock_guard lock(mutex_);
    return verbose_;
}

std::optional<LogLine> LogView::selected_line() const {
    std::lock_guard lock(mutex_);
    const auto view = filtered_locked();
    if (view.empty()) return std::nullopt;
    int index = selected_;
    if (follow_ || index < 0) index = static_cast<int>(view.size()) - 1;
    index = std::clamp(index, 0, static_cast<int>(view.size()) - 1);
    return *view[static_cast<std::size_t>(index)];
}

int LogView::selected_index() const {
    std::lock_guard lock(mutex_);
    const int total = static_cast<int>(filtered_locked().size());
    if (total == 0) return -1;
    if (follow_ || selected_ < 0) return total - 1;
    return std::clamp(selected_, 0, total - 1);
}

std::vector<const LogLine*> LogView::filtered_locked() const {
    std::vector<const LogLine*> out;
    out.reserve(lines_.size());
    for (const auto& line : lines_)
        if (matches(line)) out.push_back(&line);
    return out;
}

void LogView::render(Canvas& canvas) {
    if (bounds_.empty()) return;

    std::lock_guard lock(mutex_);
    const int rows = bounds_.h;
    page_lines_ = std::max(1, rows);

    std::vector<const LogLine*> view = filtered_locked();

    if (view.empty()) {
        canvas.put_text(bounds_.x, bounds_.y,
                        filter_.empty() ? "（等待日志…）" : "（没有匹配的行）",
                        empty_style);
        return;
    }

    const int total = static_cast<int>(view.size());

    if (follow_) {
        selected_ = total - 1;
        top_      = std::max(0, total - rows);
    } else {
        selected_ = std::clamp(selected_, 0, total - 1);
        if (selected_ < top_) top_ = selected_;
        if (selected_ >= top_ + rows) top_ = selected_ - rows + 1;
        top_ = std::clamp(top_, 0, std::max(0, total - 1));
    }

    for (int r = 0; r < rows; ++r) {
        const int index = top_ + r;
        if (index >= total) break;

        const LogLine* line = view[static_cast<std::size_t>(index)];
        const int      row  = bounds_.y + r;
        const bool     is_selected = (index == selected_);

        if (is_selected) canvas.fill(Rect{bounds_.x, row, bounds_.w, 1}, ' ', selected_style);

        // 选中行要让文字本身也带上选中背景，否则每个字形都会把背景「打出洞」。
        auto with_bg = [&](const vatui::Style& base) {
            if (!is_selected) return base;
            vatui::Style style = base;
            style.bg_sgr       = selected_style.bg_sgr;
            return style;
        };

        if (verbose_ || line->spans.empty()) {
            const auto& style = line->level == LogLevel::Error ? error_style
                              : line->level == LogLevel::Warn  ? warn_style
                              : line->level == LogLevel::Debug ? debug_style
                                                              : info_style;
            canvas.put_text(bounds_.x, row,
                            verbose_ ? line->raw : line->text, with_bg(style));
            continue;
        }

        int col = bounds_.x;
        int span_index = 0;
        for (const auto& span : line->spans) {
            if (col >= bounds_.right()) break;

            // 选中行：整行是下潜的深色背景，行首时间块再单独提亮，
            // 这样一眼能定位「选中的是哪一条」。
            const bool time_block =
                is_selected && focused && span_index == 0 && span.tone == LogTone::Time;

            canvas.put_text(col, row, span.text,
                            time_block ? selected_time_style
                                       : with_bg(tone_style(span.tone)));
            col += vaterm::utf::width(span.text);
            ++span_index;
        }
    }
}

void LogView::select_row(int row) {
    std::lock_guard lock(mutex_);
    const int total = static_cast<int>(filtered_locked().size());
    if (total == 0) return;
    const int index = std::clamp(top_ + row, 0, total - 1);
    selected_ = index;
    follow_   = (index == total - 1);
}

bool LogView::on_event(const vaterm::InputData& event) {
    if (event.inputType == vaterm::InputType::KEYBD) {
        const auto& key = event.value.keybd;
        if (key.cp == U' ') {
            set_follow(!following());
            return true;
        }
        switch (key.code) {
        case vaterm::KeyCode::PGUP: page_up(page_lines_); return true;
        case vaterm::KeyCode::PGDN: page_down(page_lines_); return true;
        case vaterm::KeyCode::HOME: to_top(); return true;
        case vaterm::KeyCode::END:  to_bottom(); return true;
        case vaterm::KeyCode::UP:   scroll(1); return true;
        case vaterm::KeyCode::DOWN: scroll(-1); return true;
        default: break;
        }
        return false;
    }

    if (event.inputType == vaterm::InputType::MOUSE) {
        const auto& m = event.value.mouse;
        if (!bounds_.contains(m.col, m.row)) return false;
        if (m.action == vaterm::MouseAction::SCROLL_UP) { scroll(3); return true; }
        if (m.action == vaterm::MouseAction::SCROLL_DOWN) { scroll(-3); return true; }
        if (m.action == vaterm::MouseAction::PRESS
            && m.button == vaterm::MouseButton::LEFT) {
            select_row(m.row - bounds_.y);
            return true;
        }
    }
    return false;
}

} // namespace vawk::widget
