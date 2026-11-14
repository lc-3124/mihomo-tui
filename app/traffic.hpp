#ifndef MIHOMO_TRAFFIC_HPP
#define MIHOMO_TRAFFIC_HPP

//- mihomo::Traffic — /traffic NDJSON 流
//
// mihomo 每秒推一条：
//   {"up":123,"down":456,"upTotal":789,"downTotal":101112}
// 管道读取 → 按行拆 → JSON 解析 → 回调（后台线程）。
// 断流后自动重连，间隔 1s，直到 stop()。

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "api.hpp"

namespace mihomo {

struct TrafficSample {
    long long up        = 0; ///< 本采样周期上行字节
    long long down      = 0;
    long long up_total  = 0;
    long long down_total = 0;
    bool      valid     = false;
};

class TrafficStream {
  public:
    using Fn = std::function<void(const TrafficSample&)>;

    TrafficStream() = default;
    ~TrafficStream();

    TrafficStream(const TrafficStream&) = delete;
    TrafficStream& operator=(const TrafficStream&) = delete;

    /** @brief 启动；已在运行时返回 false。on_sample 在后台线程回调。 */
    bool start(std::string api_base, Fn on_sample);
    void stop();
    bool running() const { return running_.load(); }

  private:
    void loop();

    Fn                on_sample_;
    Api               api_;
    std::thread       thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
};

} // namespace mihomo

#endif // MIHOMO_TRAFFIC_HPP
