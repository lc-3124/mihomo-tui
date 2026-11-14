#ifndef MIHOMO_DNS_HPP
#define MIHOMO_DNS_HPP

//- mihomo::dns_ — 走 TCP/53 的 A 记录查询
//
// 本机的网络环境很特殊：节点域名的 A 记录在 UDP/53 上会被静默丢弃，
// 只有 TCP/53 能拿到答案（见任务书第 2 节）。所以 `--use --compat`
// 不能依赖 getaddrinfo()（它默认走 UDP），得自己发 DNS 报文。
//
// 只实现最小集：单问题 A 查询、无 EDNS、无 CNAME 跟随（部分 CDN 会
// 返回 CNAME + A 的组合，答案段里的 A 仍然可用）。

#include <string>
#include <vector>

namespace mihomo {

struct DnsAnswer {
    bool        ok = false;
    std::string ip;
    std::string server;
    std::string error;
};

/** @brief 判断字符串是不是点分十进制 IPv4。 */
bool is_ipv4(std::string_view text);

/** @brief 读 /etc/resolv.conf 的 nameserver 列表（保持顺序）。 */
std::vector<std::string> system_nameservers();

/** @brief 按 server 列表逐个尝试；全部失败时 error 里带最后一次的原因。 */
DnsAnswer resolve_a_tcp(const std::string& host,
                        const std::vector<std::string>& servers,
                        int timeout_ms = 3000);

/**
 * @brief 多次查询并合并 A 记录。
 *
 * 机场的节点域名用 DNS 轮换返回多个 IP（例如 155.254.103.175 / .188 / .189），
 * 其中有些 IP 的端口是死的。单次查询只拿到一个，钉错了就是「一片节点全超时」。
 * 这个接口多查几次把候选集收齐，交给调用方按端口探测。
 */
struct DnsAnswers {
    bool                     ok = false;
    std::vector<std::string> ips;
    std::string              error;
};

DnsAnswers resolve_a_all_tcp(const std::string& host,
                             const std::vector<std::string>& servers,
                             int timeout_ms = 2000, int repeats = 3);

/** @brief 对 (ip, port) 做一次带超时的 TCP connect。 */
bool tcp_reachable(const std::string& ip, int port, int timeout_ms = 600);

} // namespace mihomo

#endif // MIHOMO_DNS_HPP
