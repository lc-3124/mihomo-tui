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
}

const vatui::Style& LogView::tone_style(LogTone tone) const {
    switch (tone) {
    case LogTone::Dim:    return dim_style;
    case LogTone::Proto:  return proto_style;
    case LogTone::Host:   return host_style;
    case LogTone::Out:    return out_style;
    case LogTone::Direct: return direct_style;
    case LogTone::Reject: return reject_style;
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
    bool matched = matches(line);
    lines_.push_back(std::move(line));
    // 从头部淘汰不改变「距底部的行数」，offset 不用动。
    while (lines_.size() > capacity_) lines_.pop_front();
    // 暂停时新行把内容往下顶，offset 自增以保持屏幕内容不动。
    if (!follow_ && matched) ++offset_;
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
    offset_ = 0;
    follow_ = true;
}

std::size_t LogView::size() const {
    std::lock_guard lock(mutex_);
    return lines_.size();
}

void LogView::set_follow(bool follow) {
    std::lock_guard lock(mutex_);
    follow_ = follow;
    if (follow) offset_ = 0;
}

bool LogView::following() const {
    std::lock_guard lock(mutex_);
    return follow_;
}

void LogView::scroll(int delta) {
    std::lock_guard lock(mutex_);
    if (delta == 0) return;
    offset_ = std::max(0, offset_ + delta);
    follow_ = (offset_ == 0);
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
    offset_ = static_cast<int>(lines_.size());
    follow_ = false;
}

void LogView::to_bottom() {
    std::lock_guard lock(mutex_);
    offset_ = 0;
    follow_ = true;
}

void LogView::set_filter(std::string filter) {
    std::lock_guard lock(mutex_);
    filter_ = std::move(filter);
    offset_ = 0;
    follow_ = true;
}

void LogView::set_verbose(bool verbose) {
    std::lock_guard lock(mutex_);
    verbose_ = verbose;
}

bool LogView::verbose() const {
    std::lock_guard lock(mutex_);
    return verbose_;
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
    if (follow_) offset_ = 0;
    // 留一行余量，避免「滚到最上时最后一行刚好被裁掉」的错觉。
    int max_off = std::max(0, total - 1);
    offset_ = std::clamp(offset_, 0, max_off);

    int end   = total - offset_;
    int begin = std::max(0, end - rows);
    if (end <= begin) return;

    for (int i = begin; i < end; ++i) {
        const LogLine* line = view[static_cast<std::size_t>(i)];
        const int      row  = bounds_.y + (i - begin);

        if (verbose_ || line->spans.empty()) {
            const auto& style = line->level == LogLevel::Error ? error_style
                              : line->level == LogLevel::Warn  ? warn_style
                              : line->level == LogLevel::Debug ? debug_style
                                                              : info_style;
            canvas.put_text(bounds_.x, row,
                            verbose_ ? line->raw : line->text, style);
            continue;
        }

        int col = bounds_.x;
        for (const auto& span : line->spans) {
            if (col >= bounds_.right()) break;
            canvas.put_text(col, row, span.text, tone_style(span.tone));
            col += vaterm::utf::width(span.text);
        }
    }
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
    }
    return false;
}

} // namespace vawk::widget
