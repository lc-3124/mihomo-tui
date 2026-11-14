//- api.cpp — libcurl 封装

#include "api.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <curl/curl.h>

namespace mihomo {

namespace {

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t write_to_string(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct StreamCtx {
    const std::function<bool(std::string_view)>* on_data = nullptr;
    const std::atomic<bool>*                     stop    = nullptr;
    bool                                         aborted = false;
};

std::size_t stream_write(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<StreamCtx*>(userdata);
    std::size_t n = size * nmemb;
    if (ctx->stop && ctx->stop->load()) {
        ctx->aborted = true;
        return 0;
    }
    if (ctx->on_data && !(*ctx->on_data)(std::string_view(ptr, n))) {
        ctx->aborted = true;
        return 0;
    }
    return n;
}

int abort_progress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* flag = static_cast<const std::atomic<bool>*>(userdata);
    return (flag && flag->load()) ? 1 : 0;
}

int stream_progress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<StreamCtx*>(userdata);
    if (ctx->stop && ctx->stop->load()) {
        ctx->aborted = true;
        return 1;
    }
    return 0;
}

std::string curl_error(CURLcode code) {
    return curl_easy_strerror(code);
}

} // namespace

Api::Api(std::string base) : base_(std::move(base)) {
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    global_init();
}

HttpResult Api::request(std::string_view method, std::string_view path,
                        std::string_view body, std::string_view content_type) {
    HttpResult result;

    CURL* handle = curl_easy_init();
    if (!handle) {
        result.error = "curl_easy_init 失败";
        return result;
    }

    const std::string url = base_ + std::string(path);
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!body.empty() || method == "PUT" || method == "PATCH" || method == "POST") {
        const std::string ct = "Content-Type: " + std::string(content_type);
        headers = curl_slist_append(headers, ct.c_str());
    }

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result.body);

    if (abort_) {
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, abort_progress);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, abort_);
    }

    const std::string method_str(method);
    if (method_str != "GET") {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_str.c_str());
    }
    if (!body.empty()) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    CURLcode code = curl_easy_perform(handle);
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        result.error = "已取消";
    } else if (code != CURLE_OK) {
        result.error = curl_error(code);
    } else {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    return result;
}

HttpResult Api::get(std::string_view path) { return request("GET", path); }

HttpResult Api::put(std::string_view path, std::string_view body) {
    return request("PUT", path, body);
}

HttpResult Api::patch(std::string_view path, std::string_view body) {
    return request("PATCH", path, body);
}

bool Api::stream(std::string_view path,
                 const std::function<bool(std::string_view)>& on_data,
                 const std::atomic<bool>& stop) {
    CURL* handle = curl_easy_init();
    if (!handle) return false;

    StreamCtx ctx{&on_data, &stop, false};
    const std::string url = base_ + std::string(path);

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);         // 流式：不设总超时
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L); // 15s 无数据视为断流
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, stream_write);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, stream_progress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &ctx);

    CURLcode code = curl_easy_perform(handle);
    curl_easy_cleanup(handle);

    if (ctx.aborted) return true;
    return code == CURLE_OK;
}

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string json_quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
    return out;
}

} // namespace mihomo
