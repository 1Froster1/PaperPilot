#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

// 将“过滤器、视图、会话、协程、Redis”放在一个便于学习的控制器中。
class LearningController : public drogon::HttpController<LearningController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LearningController::filterPublic,
                  "/learn/filter/public",
                  drogon::Get);
    ADD_METHOD_TO(LearningController::filterProtected,
                  "/learn/filter/protected",
                  drogon::Get,
                  "DemoTokenFilter");//绑定了 DemoTokenFilter 的接口
    ADD_METHOD_TO(LearningController::view, "/learn/view", drogon::Get);
    ADD_METHOD_TO(LearningController::sessionVisit,
                  "/learn/session/visit",
                  drogon::Get);
    ADD_METHOD_TO(LearningController::coroutineWait,
                  "/learn/coro/wait",
                  drogon::Get);
    ADD_METHOD_TO(LearningController::redisPing,
                  "/learn/redis/ping",
                  drogon::Get);
    METHOD_LIST_END

    void filterPublic(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
    void filterProtected(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
    void view(const drogon::HttpRequestPtr &req,
              std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
    void sessionVisit(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
    drogon::AsyncTask coroutineWait(
        drogon::HttpRequestPtr req,
        std::function<void(const drogon::HttpResponsePtr &)> callback);
    void redisPing(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;
};
