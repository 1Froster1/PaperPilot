#pragma once

#include <drogon/WebSocketController.h>

// 一个最小的 WebSocket 控制器示例。
// Drogon 会自动发现并注册该类，不需要在 main.cc 中手动注册。
class WebSocketEchoController
    : public drogon::WebSocketController<WebSocketEchoController>
{
  public:
    // WebSocket 握手请求地址：ws://127.0.0.1:8848/ws/echo
    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/echo");
    WS_PATH_LIST_END

    // 建立 WebSocket 连接后调用一次。
    void handleNewConnection(
        const drogon::HttpRequestPtr &req,
        const drogon::WebSocketConnectionPtr &conn) override;

    // 每当客户端发来一条 WebSocket 消息时调用。
    void handleNewMessage(
        const drogon::WebSocketConnectionPtr &conn,
        std::string &&message,
        const drogon::WebSocketMessageType &type) override;

    // WebSocket 连接关闭后调用一次。
    void handleConnectionClosed(
        const drogon::WebSocketConnectionPtr &conn) override;
};
