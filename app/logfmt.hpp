#ifndef MIHOMO_LOGFMT_HPP
#define MIHOMO_LOGFMT_HPP

//- mihomo::logfmt — 把 mihomo/journal 原始日志压成一行「关键信息」
//
// 原始行（journalctl -o cat）典型长这样：
//   time="2026-11-14T17:20:01.123+08:00" level=info msg="[TCP] 1.2.3.4:52344
//   --> www.google.com:443 match DomainSuffix(google) using 🇸🇬 新加坡01[直连]"
//
// 压缩后：
//   17:20:01  TCP  www.google.com  →  新加坡01[直连] · DomainSuffix
//
// 同时给出分色片段（LogSpan）：时间暗、协议青、出口按 DIRECT/REJECT/普通区分。
// 解析失败时退化为「时间 + 原文」，保证任何日志都不会丢。

#include <string>
#include <string_view>

#include "vawk/widget/log_view.hpp"

namespace mihomo {

/** @brief 解析并精简一行日志；同时保留 raw 供 verbose 模式。 */
vawk::widget::LogLine format_log_line(vawk::widget::LogLevel level,
                                      std::string               raw);

/** @brief 去掉 ANSI 转义（journalctl 有时会带颜色）。 */
std::string strip_ansi(std::string_view text);

} // namespace mihomo

#endif // MIHOMO_LOGFMT_HPP
