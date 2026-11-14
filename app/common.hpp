#ifndef MIHOMO_COMMON_HPP
#define MIHOMO_COMMON_HPP

//- mihomo-tui 公共小工具
//
// 格式化（字节/速率/延迟/时间）、路径展开、字符串处理，以及全局常量。
// 刻意不依赖任何 UI 类型，方便单测直接引用。

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mihomo {

inline constexpr const char* kDefaultApiBase = "http://127.0.0.1:9090";
inline constexpr const char* kDefaultGroup   = "🔰 节点选择";
inline constexpr const char* kDelayTestUrl =
    "http://www.gstatic.com/generate_204";

/** @brief 展开 ~ 与 $HOME，返回绝对路径。 */
std::string expand_home(std::string_view path);

/** @brief 默认内核配置路径：$MIHOMO_CONFIG，否则 ~/.config/mihomo/config.yaml。 */
std::string default_config_path();

/** @brief 目录部分（不含结尾 '/'）；没有 '/' 时返回 "."。 */
std::string dir_name(std::string_view path);

/** @brief 文件名部分。 */
std::string base_name(std::string_view path);

/** @brief 文件是否存在（普通文件或符号链接目标）。 */
bool file_exists(const std::string& path);

/** @brief 读整个文件；失败返回 nullopt。 */
std::optional<std::string> read_file(const std::string& path);

/** @brief 原子写：同目录临时文件 + fsync + rename，权限 0600。 */
bool write_file_atomic(const std::string& path, std::string_view content,
                       std::string& error);

/**
 * @brief 人类可读的字节数："0 B" "512 B" "1.2 KB" "84 KB" "1.2 MB"。
 * @note 1024 进制；小于 10 时保留一位小数，否则取整。
 */
std::string format_bytes(double bytes);

/** @brief 紧凑字节数："88M" "1.2G"，用于累计流量。 */
std::string format_bytes_short(double bytes);

/** @brief 速率："1.2 MB/s"。 */
std::string format_rate(double bytes_per_sec);

/** @brief 延迟："921ms" / "—" / "超时"。 */
std::string format_delay(int ms);

/** @brief 本地时间 "HH:MM:SS"。 */
std::string now_hms();

/** @brief 本地时间戳 "YYYYMMDD-HHMMSS"，用于备份文件名。 */
std::string timestamp_compact();

/** @brief 去掉首尾空白（空格、\t、\r、\n）。 */
std::string_view trim(std::string_view s);

bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);

/** @brief ASCII 小写化（UTF-8 非 ASCII 字节原样保留）。 */
std::string to_lower_ascii(std::string_view s);

/** @brief 把 ms 格式化成 "9m" / "1h2m" / "12s"。 */
std::string format_duration(long long seconds);

} // namespace mihomo

#endif // MIHOMO_COMMON_HPP
