#include "LearningController.h"

#include <algorithm>
#include <chrono>
#include <trantor/net/EventLoop.h>

using namespace std::chrono_literals;

namespace//这是匿名命名空间。里面的函数只能在 [LearningController.cc] 中使用，其他 .cpp 文件看不到它。
{
drogon::HttpResponsePtr jsonResponse(const Json::Value &body)
{
    return drogon::HttpResponse::newHttpJsonResponse(body);
}
}  // namespace

void LearningController::filterPublic(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    Json::Value body;
    body["message"] = "This endpoint has no filter.";
    callback(jsonResponse(body));
}

void LearningController::filterProtected(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // 只有 DemoTokenFilter 调用 next() 后，才会执行到这里。
    Json::Value body;
    body["message"] = "Filter passed. You reached the protected handler.";
    callback(jsonResponse(body));
}

void LearningController::view(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // HttpViewData 中的数据会被 views/LearningView.csp 渲染进 HTML。
    drogon::HttpViewData data;
    auto name = req->getParameter("name");
    data["name"] = name.empty() ? "Drogon learner" : name;
    data["path"] = req->path();
    callback(drogon::HttpResponse::newHttpViewResponse("LearningView", data));
}

void LearningController::sessionVisit(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
    // session 数据保存在服务端；浏览器只保存 MYAPPSESSION 这个 Cookie 中的会话 ID。
    const auto session = req->session();
    const int visits = session->getOptional<int>("visits").value_or(0) + 1;
    session->insert("visits", visits);

    Json::Value body;
    body["visits"] = visits;
    body["sessionId"] = session->sessionId();
    body["message"] = "Refresh this URL; visits increases while the Cookie remains.";
    callback(jsonResponse(body));
}

drogon::AsyncTask LearningController::coroutineWait(
    drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback)
{
    int seconds = 1;
    try
    {
        const auto input = req->getParameter("seconds");
        if (!input.empty())
            seconds = std::stoi(input);
    }
    catch (const std::exception &)
    {
        seconds = 1;
    }
    seconds = std::clamp(seconds, 0, 3);  // 示例最多等待 3 秒。

    // co_await 不会阻塞 Drogon 的 IO 线程；等待期间服务器仍可处理其他请求。
    co_await drogon::sleepCoro(trantor::EventLoop::getEventLoopOfCurrentThread(),
                               std::chrono::seconds(seconds));

    Json::Value body;
    body["message"] = "Coroutine resumed after a non-blocking wait.";
    body["waitedSeconds"] = seconds;
    callback(jsonResponse(body));
    co_return;
}

void LearningController::redisPing(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
#ifdef MYAPP_REDIS_READY//只有 CMake 中定义了 MYAPP_REDIS_READY，编译器才会编译这一段真实 Redis 代码。
    // 真实 Redis 示例：不会阻塞当前 IO 线程，结果通过两个回调返回。
    auto redis = drogon::app().getRedisClient();
    auto callbackPtr = std::make_shared<
        std::function<void(const drogon::HttpResponsePtr &)>>(std::move(callback));
    redis->execCommandAsync(//向 Redis 异步执行命令
        [callbackPtr](const drogon::nosql::RedisResult &result) {
            Json::Value body;
            body["redisReply"] = result.asString();  // PING 成功时是 PONG
            (*callbackPtr)(jsonResponse(body));
        },
        [callbackPtr](const std::exception &error) {
            Json::Value body;
            body["error"] = error.what();
            auto response = jsonResponse(body);
            response->setStatusCode(drogon::k503ServiceUnavailable);
            (*callbackPtr)(response);
        },
        "PING");
#else
#endif
}
