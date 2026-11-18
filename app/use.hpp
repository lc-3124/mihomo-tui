#ifndef MIHOMO_USE_HPP
#define MIHOMO_USE_HPP

//- mihomo::use — `--use` 子命令
//
// 流程（对应任务书第 8 节）：
//   1 识别来源 → 2 下载/读取 → 3 格式识别 → 4 兼容修正（--compat）
//   → 5 备份 → 6 临时文件热加载验证 → 7 落盘 + 热重载（失败回滚）
//
// 配置改写是「行级」的：只动 server / client-fingerprint 两类值，
// 注释、缩进、节点顺序、自定义规则全部原样保留（不用 yaml 库往返，
// 避免把用户手改过的配置洗成另一种风格）。
//
// 兼容修正是显式开关：默认不动配置，只提示有多少个 server 还是域名。

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace mihomo::cfg {

//-- 格式识别 ---------------------------------------------------------------

enum class Format { Unknown, ClashYaml, Base64Nodes };

struct DetectResult {
    Format      format      = Format::Unknown;
    int         proxy_count = 0;
    std::string hint; ///< 给用户看的补充说明（错误原因等）
};

DetectResult detect_format(std::string_view content);

/** @brief base64 解码（忽略空白，兼容 URL-safe 与缺省 padding）；失败返回空串。 */
std::string base64_decode(std::string_view text);

//-- 节点扫描 ---------------------------------------------------------------

inline constexpr std::size_t kNoLine = static_cast<std::size_t>(-1);

struct ProxyEntry {
    bool        flow        = false;   ///< 流式写法 `- {name: x, server: y}`（单行）
    std::size_t begin       = kNoLine; ///< 条目首行（"- " 那一行）
    std::size_t end         = kNoLine; ///< 条目之后的第一行
    std::size_t dash_indent = 0;
    std::size_t key_indent  = 0;
    std::size_t name_line   = kNoLine;
    std::size_t server_line = kNoLine;
    std::size_t fp_line     = kNoLine; ///< client-fingerprint 行
    int         port        = -1;      ///< 节点端口（探测 IP 用）
    std::string name;
    std::string server;                ///< 已去引号
    std::string fingerprint;
};

/** @brief 找出顶层 `proxies:` 里的每个节点；没有则返回空。 */
std::vector<ProxyEntry> scan_proxies(const std::vector<std::string>& lines);

//-- 改写 -------------------------------------------------------------------

/** @brief 把 `key: 旧值  # 注释` 换成 `key: 新值  # 注释`，保留缩进/引号/注释。 */
std::string replace_value(const std::string& line, std::string_view value);

struct CompatReport {
    int         pinned       = 0; ///< server 域名改 IP 的节点数
    int         dns_fixed    = 0; ///< 是否把不可用的 DNS 换成了本机可用的
    int         fingerprints = 0; ///< 写入 / 改成 safari 的节点数
    int         unresolved   = 0; ///< 解析失败、保持原样的节点数
    std::vector<std::string> unresolved_hosts;
};

/** @brief 解析回调：域名 + 端口 → 可用的 IPv4；返回空串表示失败。 */
using Resolver = std::function<std::string(const std::string& host, int port)>;

/** @brief 原地改写 server 与 client-fingerprint。 */
CompatReport apply_compat(std::vector<std::string>& lines, const Resolver& resolver);

/**
 * @brief 判断这份配置在本机是否「需要」兼容修正。
 *
 * 触发条件（任一）：有节点的 server 是域名；有节点的 client-fingerprint
 * 不是 safari（含完全没写）。
 */
bool config_needs_compat(const std::vector<std::string>& lines);

/**
 * @brief `dns.nameserver` 是否只有 DoH/域名、没有可用 IP 字面量。
 *
 * 订阅常带一组 DoH（有时在当前网络完全不可达），而 DNS 一旦全挂，
 * 所有走 DIRECT 的域名都会 `dns resolve failed`。这种配置需要换 DNS。
 */
bool dns_needs_fix(const std::vector<std::string>& lines);

/**
 * @brief 把 `dns.nameserver` 换成本机可用的 UDP DNS 列表。
 * @return true 表示确实改写了。
 */
bool fix_dns_servers(std::vector<std::string>& lines);

//-- 文本工具 ---------------------------------------------------------------

std::vector<std::string> split_lines(std::string_view content);
std::string              join_lines(const std::vector<std::string>& lines);

} // namespace mihomo::cfg

namespace mihomo {

struct UseOptions {
    std::string source;              ///< 本地路径或 http(s) URL
    std::string config_path;         ///< 目标配置，默认 default_config_path()
    std::string api_base = "http://127.0.0.1:9090";
    bool        compat   = false;    ///< --compat：强制钉 IP + 改 safari
    bool        no_compat = false;   ///< --no-compat：关闭自动兼容修正
    bool        restart  = true;     ///< --no-restart 时只落盘
    bool        offline  = false;    ///< --offline：不连内核，跳过在线验证
    std::vector<std::string> dns_servers; ///< --dns：覆盖节点域名解析服务器
    std::string proxy;               ///< --proxy：下载时使用的代理；空 = 直连
    bool        insecure = false;    ///< --insecure：跳过 TLS 证书校验
};

/**
 * @brief 执行 --use。
 * @return 0 成功；2 格式/内容错误；3 下载或读取失败；
 *         4 在线验证失败；5 落盘后热重载失败（已回滚）。
 */
int run_use(const UseOptions& options, std::ostream& out);

} // namespace mihomo

#endif // MIHOMO_USE_HPP
