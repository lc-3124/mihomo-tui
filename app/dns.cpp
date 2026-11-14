//- dns.cpp — 最小 DNS over TCP 客户端

#include "dns.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.hpp"

namespace mihomo {

namespace {

// 非阻塞 connect，带超时。
int connect_tcp(const std::string& server, int port, int timeout_ms) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, server.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (rc != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    if (rc != 0) {
        pollfd pfd{fd, POLLOUT, 0};
        int    pr = ::poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            ::close(fd);
            return -1;
        }
        int       err = 0;
        socklen_t len = sizeof err;
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            ::close(fd);
            return -1;
        }
    }
    return fd;
}

bool send_all(int fd, const uint8_t* data, std::size_t n, int timeout_ms) {
    std::size_t off = 0;
    while (off < n) {
        pollfd pfd{fd, POLLOUT, 0};
        if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
        ssize_t w = ::send(fd, data + off, n - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
        off += static_cast<std::size_t>(w);
    }
    return true;
}

bool recv_all(int fd, uint8_t* data, std::size_t n, int timeout_ms) {
    std::size_t off = 0;
    while (off < n) {
        pollfd pfd{fd, POLLIN, 0};
        if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
        ssize_t r = ::recv(fd, data + off, n - off, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return false;
        }
        if (r == 0) return false;
        off += static_cast<std::size_t>(r);
    }
    return true;
}

void encode_name(std::vector<uint8_t>& out, const std::string& host) {
    std::size_t start = 0;
    while (start < host.size()) {
        std::size_t dot = host.find('.', start);
        if (dot == std::string::npos) dot = host.size();
        std::size_t len = dot - start;
        if (len == 0 || len > 63) { // 非法标签：整体放弃
            out.assign(1, 0);
            return;
        }
        out.push_back(static_cast<uint8_t>(len));
        out.insert(out.end(), host.begin() + static_cast<long>(start),
                   host.begin() + static_cast<long>(dot));
        start = dot + 1;
    }
    out.push_back(0);
}

// 跳过（可能压缩的）名字，成功时 off 前进到名字之后。
bool skip_name(const uint8_t* p, std::size_t n, std::size_t& off) {
    while (off < n) {
        uint8_t len = p[off];
        if ((len & 0xC0) == 0xC0) {
            if (off + 1 >= n) return false;
            off += 2;
            return true;
        }
        if (len == 0) {
            ++off;
            return true;
        }
        if (off + 1 + len > n) return false;
        off += 1 + len;
    }
    return false;
}

// 收集应答里的全部 A 记录（不只看第一条）。
bool parse_response(const uint8_t* p, std::size_t n, uint16_t want_id,
                    std::vector<std::string>& out) {
    if (n < 12) return false;
    uint16_t id    = static_cast<uint16_t>((p[0] << 8) | p[1]);
    uint16_t flags = static_cast<uint16_t>((p[2] << 8) | p[3]);
    uint16_t qd    = static_cast<uint16_t>((p[4] << 8) | p[5]);
    uint16_t an    = static_cast<uint16_t>((p[6] << 8) | p[7]);

    if (id != want_id) return false;
    if (!(flags & 0x8000)) return false;     // 不是响应
    if ((flags & 0x000F) != 0) return false; // NXDOMAIN / SERVFAIL ...

    std::size_t off = 12;
    for (uint16_t i = 0; i < qd; ++i) {
        if (!skip_name(p, n, off)) return false;
        off += 4; // qtype + qclass
        if (off > n) return false;
    }

    for (uint16_t i = 0; i < an; ++i) {
        if (!skip_name(p, n, off)) return false;
        if (off + 10 > n) return false;
        uint16_t type  = static_cast<uint16_t>((p[off] << 8) | p[off + 1]);
        uint16_t cls   = static_cast<uint16_t>((p[off + 2] << 8) | p[off + 3]);
        uint16_t rdlen = static_cast<uint16_t>((p[off + 8] << 8) | p[off + 9]);
        off += 10;
        if (off + rdlen > n) return false;
        if (type == 1 && cls == 1 && rdlen == 4) {
            char buf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, p + off, buf, sizeof buf);
            out.emplace_back(buf);
        }
        off += rdlen;
    }
    return true;
}

// 向单个 server 查询 A 记录；失败返回空。
std::vector<std::string> query_a(const std::string& host,
                                 const std::string& server, int timeout_ms) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    uint16_t id = static_cast<uint16_t>(rng() & 0xFFFF);

    std::vector<uint8_t> query;
    query.push_back(static_cast<uint8_t>(id >> 8));
    query.push_back(static_cast<uint8_t>(id & 0xFF));
    query.push_back(0x01); // RD
    query.push_back(0x00);
    query.push_back(0x00); query.push_back(0x01); // QDCOUNT
    query.push_back(0x00); query.push_back(0x00); // ANCOUNT
    query.push_back(0x00); query.push_back(0x00);
    query.push_back(0x00); query.push_back(0x00);
    encode_name(query, host);
    if (query.size() < 12) return {};
    query.push_back(0x00); query.push_back(0x01); // QTYPE A
    query.push_back(0x00); query.push_back(0x01); // QCLASS IN

    std::vector<uint8_t> framed;
    framed.reserve(query.size() + 2);
    framed.push_back(static_cast<uint8_t>(query.size() >> 8));
    framed.push_back(static_cast<uint8_t>(query.size() & 0xFF));
    framed.insert(framed.end(), query.begin(), query.end());

    int fd = connect_tcp(server, 53, timeout_ms);
    if (fd < 0) return {};

    bool ok = send_all(fd, framed.data(), framed.size(), timeout_ms);
    uint8_t lenbuf[2]{};
    if (ok) ok = recv_all(fd, lenbuf, 2, timeout_ms);
    std::size_t len = 0;
    if (ok) {
        len = (static_cast<std::size_t>(lenbuf[0]) << 8) | lenbuf[1];
        if (len < 12 || len > 65535) ok = false;
    }

    std::vector<uint8_t> resp;
    if (ok) {
        resp.resize(len);
        ok = recv_all(fd, resp.data(), resp.size(), timeout_ms);
    }
    ::close(fd);
    if (!ok) return {};

    std::vector<std::string> ips;
    if (!parse_response(resp.data(), resp.size(), id, ips)) return {};
    return ips;
}

} // namespace

bool is_ipv4(std::string_view text) {
    in_addr addr{};
    std::string s(text);
    return ::inet_pton(AF_INET, s.c_str(), &addr) == 1;
}

std::vector<std::string> system_nameservers() {
    std::vector<std::string> servers;

    auto push_unique = [&](const std::string& ip) {
        if (!is_ipv4(ip)) return;
        if (std::find(servers.begin(), servers.end(), ip) == servers.end())
            servers.push_back(ip);
    };

    // 显式覆盖：MIHOMO_TUI_DNS="223.6.6.6,119.28.28.28"
    if (const char* env = std::getenv("MIHOMO_TUI_DNS"); env && *env) {
        std::string_view all(env);
        std::size_t      start = 0;
        while (start <= all.size()) {
            std::size_t comma = all.find(',', start);
            if (comma == std::string_view::npos) comma = all.size();
            auto part = trim(all.substr(start, comma - start));
            push_unique(std::string(part));
            start = comma + 1;
        }
        if (!servers.empty()) return servers;
    }

    // AliDNS 优先：同一个节点域名在不同解析器上会拿到不同的 Anycast IP，
    // 选错时会出现「域名钉了 IP，但该 IP 的 relay 端口不通」→ 一片节点全超时。
    push_unique("223.6.6.6");
    push_unique("223.5.5.5");

    if (auto content = read_file("/etc/resolv.conf")) {
        std::string_view all(*content);
        std::size_t      start = 0;
        while (start < all.size()) {
            std::size_t nl = all.find('\n', start);
            if (nl == std::string_view::npos) nl = all.size();
            std::string_view line = all.substr(start, nl - start);
            start = nl + 1;
            line = trim(line);
            if (!starts_with(line, "nameserver")) continue;
            push_unique(std::string(trim(line.substr(10))));
        }
    }

    push_unique("119.28.28.28");
    push_unique("114.114.114.114");
    return servers;
}

DnsAnswer resolve_a_tcp(const std::string& host,
                        const std::vector<std::string>& servers,
                        int timeout_ms) {
    DnsAnswer answer;

    if (is_ipv4(host)) {
        answer.ok     = true;
        answer.ip     = host;
        answer.server = "(literal)";
        return answer;
    }

    std::string last_error = "没有可用的 DNS 服务器";
    for (const auto& server : servers) {
        auto ips = query_a(host, server, timeout_ms);
        if (!ips.empty()) {
            answer.ok     = true;
            answer.ip     = ips.front();
            answer.server = server;
            return answer;
        }
        last_error = server + ": 无 A 记录或超时";
    }

    answer.error = last_error;
    return answer;
}

DnsAnswers resolve_a_all_tcp(const std::string& host,
                             const std::vector<std::string>& servers,
                             int timeout_ms, int repeats) {
    DnsAnswers out;
    std::set<std::string> seen;

    if (is_ipv4(host)) {
        out.ok  = true;
        out.ips = {host};
        return out;
    }

    // 每个解析器都问一遍，再重复几轮：节点域名会轮换返回多个 Anycast IP。
    // 某台解析器本轮没响应就拉黑，避免每轮都白等一个超时。
    std::set<std::string> dead_servers;
    for (int round = 0; round < std::max(1, repeats); ++round) {
        bool any = false;
        for (const auto& server : servers) {
            if (dead_servers.count(server)) continue;
            auto ips = query_a(host, server, timeout_ms);
            if (ips.empty()) {
                dead_servers.insert(server);
                continue;
            }
            any = true;
            for (const auto& ip : ips) {
                if (seen.insert(ip).second) out.ips.push_back(ip);
            }
        }
        if (!any) out.error = "所有解析器都没有返回 A 记录";
    }
    out.ok = !out.ips.empty();
    return out;
}

bool tcp_reachable(const std::string& ip, int port, int timeout_ms) {
    if (port <= 0 || port > 65535) return false;
    int fd = connect_tcp(ip, port, timeout_ms);
    if (fd < 0) return false;
    ::close(fd);
    return true;
}

} // namespace mihomo
