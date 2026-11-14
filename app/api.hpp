#ifndef MIHOMO_API_HPP
#define MIHOMO_API_HPP

//- mihomo::Api — 内核 RESTful API 的 libcurl 封装
//
// 只覆盖本程序用到的四件事：
//   GET / PATCH / PUT          同步请求，短超时（默认 2s）
//   GET /traffic 等 NDJSON 流   长连接 + 停止标志
//
// 所有函数都不抛异常；失败信息放在 HttpResult::error 里。
// 同步接口只允许在 UI 线程「短打」用（localhost，2s 超时足够）；
// 周期轮询放在后台线程，见 app.cpp。

#include <atomic>
#include <functional>
#include <string>
#include <string_view>

namespace mihomo {

struct HttpResult {
    long        status = 0;  ///< HTTP 状态码；0 表示没连上
    std::string body;
    std::string error;       ///< 传输层错误（连不上、超时等）

    bool ok() const { return status >= 200 && status < 300; }
    bool reachable() const { return status != 0; }
};

class Api {
  public:
    explicit Api(std::string base = "http://127.0.0.1:9090");

    const std::string& base() const { return base_; }
    void set_timeout_ms(long ms) { timeout_ms_ = ms; }

    /**
     * @brief 绑定一个中止标志；置 true 后正在进行的请求会尽快返回。
     * @param flag 生命周期必须覆盖所有请求（调用方保证）。
     */
    void set_abort(const std::atomic<bool>* flag) { abort_ = flag; }

    HttpResult get(std::string_view path);
    HttpResult put(std::string_view path, std::string_view body = {});
    HttpResult patch(std::string_view path, std::string_view body = {});

    /** @brief 通用同步请求；method 为 "GET"/"PUT"/"PATCH"/"POST"/"DELETE"。 */
    HttpResult request(std::string_view method, std::string_view path,
                       std::string_view body = {},
                       std::string_view content_type = "application/json");

    /**
     * @brief 流式 GET：每次收到字节就回调，回调返回 false 立即断开。
     * @param stop 外部停止标志；置 true 后最多一个 poll 周期内返回。
     * @return 正常完成或主动停止返回 true，传输错误返回 false。
     */
    bool stream(std::string_view path,
                const std::function<bool(std::string_view)>& on_data,
                const std::atomic<bool>& stop);

  private:
    std::string base_;
    long        timeout_ms_ = 2000;
    const std::atomic<bool>* abort_ = nullptr;
};

/** @brief 百分号编码（RFC 3986 unreserved 之外的字节全编码），用于路径段。 */
std::string url_encode(std::string_view text);

/** @brief 简易 JSON 字符串转义（生成请求体用）。 */
std::string json_quote(std::string_view text);

} // namespace mihomo

#endif // MIHOMO_API_HPP
