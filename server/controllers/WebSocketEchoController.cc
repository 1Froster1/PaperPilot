#include "WebSocketEchoController.h"
#include <trantor/utils/Logger.h>

void WebSocketEchoController::handleNewConnection(
    const drogon::HttpRequestPtr &req,
    const drogon::WebSocketConnectionPtr &conn)
{
    // req 是这次 HTTP -> WebSocket 握手时的请求；conn 是之后可长期使用的连接。
    LOG_INFO << "WebSocket connected: " << req->peerAddr().toIpPort();

    // 服务端主动发送一条欢迎消息，让客户端知道握手已成功。
    conn->send("Connected to Drogon WebSocket echo server.");
}

void WebSocketEchoController::handleNewMessage(
    const drogon::WebSocketConnectionPtr &conn,
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if (type == drogon::WebSocketMessageType::Text)
    {
        // Echo 的核心：客户端发什么文本，服务端就回什么文本。
        conn->send("echo: " + message);
        return;
    }

    if (type == drogon::WebSocketMessageType::Binary)
    {
        // 二进制消息也原样返回，但保持其 Binary 类型。
        conn->send(message, drogon::WebSocketMessageType::Binary);
        return;
    }

    // Ping/Pong 是 WebSocket 的底层保活帧，Drogon 会自动处理，无需手动回复。
    LOG_DEBUG << "Received a WebSocket control frame.";
}

void WebSocketEchoController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &conn)
{
    LOG_INFO << "WebSocket disconnected: " << conn->peerAddr().toIpPort();
}
