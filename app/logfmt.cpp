//- logfmt.cpp — 日志精简与分色

#include "logfmt.hpp"

#include <algorithm>
#include <cctype>

#include "common.hpp"
#include "vaterm/utf.hpp"

namespace mihomo {

namespace {

using vawk::widget::LogLine;
using vawk::widget::LogTone;

struct Parsed {
    std::string time;  ///< HH:MM:SS
    std::string msg;   ///< 去掉 time/level/msg 包装后的正文
    bool        structured = false;
};

// 找 key="..." 的值；msg 里的 \" 要跳过。
bool quoted_field(std::string_view line, std::string_view key, std::string& out) {
    auto pos = line.find(key);
    if (pos == std::string_view::npos) return false;
    pos += key.size();
    std::string value;
    bool        escaped = false;
    for (std::size_t i = pos; i < line.size(); ++i) {
        char c = line[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            out = std::move(value);
            return true;
        }
        value.push_back(c);
    }
    return false;
}

Parsed parse(std::string_view raw) {
    Parsed out;

    std::string time_value;
    if (quoted_field(raw, "time=\"", time_value)) {
        // RFC3339：2026-11-14T17:20:01.123+08:00 → 17:20:01
        if (time_value.size() >= 19 && time_value[10] == 'T')
            out.time = time_value.substr(11, 8);
        else
            out.time = time_value;
    }

    if (quoted_field(raw, "msg=\"", out.msg)) {
        out.structured = true;
    } else {
        out.msg = std::string(trim(raw));
    }

    if (out.time.empty()) out.time = now_hms();
    return out;
}

// 截断到指定显示宽度；mode = 保留尾部（域名）/ 保留头部（节点名）。
std::string shorten(std::string_view text, int width, bool keep_tail) {
    if (vaterm::utf::width(text) <= width) return std::string(text);

    std::string_view view = text;
    if (keep_tail) {
        // 从头部逐码点删，直到剩下 width-1 列。
        while (!view.empty() && vaterm::utf::width(view) > width - 1) {
            std::size_t n = vaterm::utf::char_bytes(view);
            if (n == 0) { view.remove_prefix(1); continue; }
            view.remove_prefix(n);
        }
        return "…" + std::string(view);
    }

    while (!view.empty() && vaterm::utf::width(view) > width - 1) {
        std::size_t n = 0;
        while (n < view.size()) {
            std::size_t cb = vaterm::utf::char_bytes(view.substr(n));
            if (n + cb >= view.size()) break;
            n += cb;
        }
        view = view.substr(0, n);
    }
    return std::string(view) + "…";
}

// 去掉常见默认端口，减小噪声。
std::string strip_default_port(std::string host) {
    static const char* ports[] = {":443", ":80", ":53"};
    for (const char* p : ports) {
        if (ends_with(host, p)) {
            host.resize(host.size() - std::string(p).size());
            break;
        }
    }
    return host;
}

LogTone outbound_tone(std::string_view out) {
    if (out.empty()) return LogTone::Dim;
    if (starts_with(out, "DIRECT")) return LogTone::Direct;
    if (out.find("REJECT") != std::string_view::npos
        || out.find("REJECT-DROP") != std::string_view::npos
        || starts_with(out, "DROP"))
        return LogTone::Reject;
    return LogTone::Out;
}

void add(std::vector<vawk::widget::LogSpan>& spans, std::string text, LogTone tone) {
    if (text.empty()) return;
    spans.push_back(vawk::widget::LogSpan{std::move(text), tone});
}

} // namespace

std::string strip_ansi(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\x1b') { out.push_back(text[i]); continue; }
        if (i + 1 >= text.size()) break;
        if (text[i + 1] == '[') { // CSI ... final byte 0x40-0x7E
            i += 2;
            while (i < text.size()
                   && !(static_cast<unsigned char>(text[i]) >= 0x40
                        && static_cast<unsigned char>(text[i]) <= 0x7E))
                ++i;
        } else if (text[i + 1] == ']') { // OSC ... BEL / ST
            i += 2;
            while (i < text.size() && text[i] != '\x07') ++i;
        } else {
            ++i;
        }
    }
    return out;
}

LogLine format_log_line(vawk::widget::LogLevel level, std::string raw) {
    LogLine line;
    line.level = level;
    line.raw   = strip_ansi(raw);

    const Parsed     parsed = parse(line.raw);
    const std::string body  = std::string(trim(parsed.msg));

    // 时间固定 8 列 + 空格，方便纵向扫读。
    add(line.spans, parsed.time + " ", LogTone::Time);

    std::string      proto;
    std::string_view rest = body;
    if (!rest.empty() && rest.front() == '[') {
        auto close = rest.find(']');
        if (close != std::string_view::npos && close <= 8) {
            proto = std::string(rest.substr(1, close - 1));
            rest  = trim(rest.substr(close + 1));
        }
    }

    const bool is_error = (level == vawk::widget::LogLevel::Error);
    const bool is_warn  = (level == vawk::widget::LogLevel::Warn);
    const bool bad_body = body.find("error:") != std::string::npos
                       || body.find("failed") != std::string::npos
                       || body.find("panic") != std::string::npos;
    const bool has_arrow = rest.find("-->") != std::string_view::npos;
    const bool err_like  = is_error || (bad_body && !has_arrow);

    if (err_like) add(line.spans, "ERR ", LogTone::Error);
    else if (is_warn) add(line.spans, "WRN ", LogTone::Warn);

    if (!proto.empty()) add(line.spans, proto + " ", LogTone::Proto);

    // 错误 / 告警：不强行套「目标 → 出口」，保留正文里的关键原因。
    if (err_like || is_warn) {
        std::string message = shorten(rest.empty() ? body : std::string(rest), 140, false);
        add(line.spans, std::move(message),
            err_like ? LogTone::Error : LogTone::Warn);
    } else if (has_arrow) {
        // 右侧：dst [match RULE] [using OUT]；规则只在 verbose 的原文里保留，
        // 精简行优先保证「目标 + 出口」不被挤掉。
        std::string_view right = trim(rest.substr(rest.find("-->") + 3));
        std::string_view dst   = right;
        std::string_view out;

        auto match_pos = right.find(" match ");
        auto using_pos = right.find(" using ");
        if (match_pos != std::string_view::npos
            && (using_pos == std::string_view::npos || match_pos < using_pos)) {
            dst = trim(right.substr(0, match_pos));
            if (using_pos != std::string_view::npos)
                out = trim(right.substr(using_pos + 7));
        } else if (using_pos != std::string_view::npos) {
            dst = trim(right.substr(0, using_pos));
            out = trim(right.substr(using_pos + 7));
        }

        std::string host = strip_default_port(std::string(dst));
        host = shorten(host, 32, true);
        add(line.spans, std::move(host), LogTone::Host);

        if (!out.empty()) {
            std::string out_str(out);
            auto        err = out_str.find("error:");
            if (err != std::string::npos) {
                add(line.spans, " → " + shorten(out_str.substr(0, err), 20, false),
                    outbound_tone(out_str.substr(0, err)));
                add(line.spans, " " + shorten(out_str.substr(err), 70, false),
                    LogTone::Error);
            } else {
                add(line.spans, " → ", LogTone::Dim);
                std::string shown = shorten(out_str, 24, false);
                add(line.spans, std::move(shown), outbound_tone(out_str));
            }
        }
    } else {
        std::string message = shorten(rest.empty() ? body : std::string(rest), 120, false);
        add(line.spans, std::move(message),
            level == vawk::widget::LogLevel::Debug ? LogTone::Dim : LogTone::Default);
    }

    for (const auto& span : line.spans) line.text += span.text;

    if (line.text.empty()) line.text = shorten(line.raw, 140, false);
    return line;
}

} // namespace mihomo
