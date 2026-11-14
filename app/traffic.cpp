//- traffic.cpp — /traffic 流线程

#include "traffic.hpp"

#include <chrono>
#include <string_view>

#include <nlohmann/json.hpp>

namespace mihomo {

TrafficStream::~TrafficStream() { stop(); }

bool TrafficStream::start(std::string api_base, Fn on_sample) {
    if (running_.load()) return false;
    api_       = Api(std::move(api_base));
    on_sample_ = std::move(on_sample);
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread(&TrafficStream::loop, this);
    return true;
}

void TrafficStream::stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

void TrafficStream::loop() {
    std::string pending;

    while (!stop_.load()) {
        pending.clear();
        bool ok = api_.stream(
            "/traffic",
            [&](std::string_view chunk) {
                pending.append(chunk);
                std::size_t start = 0;
                while (true) {
                    std::size_t nl = pending.find('\n', start);
                    if (nl == std::string::npos) break;
                    std::string_view line(pending.data() + start, nl - start);
                    start = nl + 1;
                    if (line.empty()) continue;
                    try {
                        auto j = nlohmann::json::parse(line);
                        TrafficSample s;
                        s.up         = j.value("up", 0LL);
                        s.down       = j.value("down", 0LL);
                        s.up_total   = j.value("upTotal", 0LL);
                        s.down_total = j.value("downTotal", 0LL);
                        s.valid      = true;
                        if (on_sample_) on_sample_(s);
                    } catch (const nlohmann::json::exception&) {
                        // 半截行/异常行直接忽略，下一条照常。
                    }
                }
                // 防呆：异常服务端只吐字节不吐换行。
                if (pending.size() > 1 << 20) pending.clear();
                else if (start > 0) pending.erase(0, start);
                return !stop_.load();
            },
            stop_);

        if (!ok && !stop_.load()) {
            // API 断了就歇一秒再连，别把 CPU 打满。
            for (int i = 0; i < 10 && !stop_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    running_.store(false);
}

} // namespace mihomo
