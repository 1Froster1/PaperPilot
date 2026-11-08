#include "DemoTokenFilter.h"

void DemoTokenFilter::doFilter(const drogon::HttpRequestPtr &req,
                               drogon::FilterCallback &&reject,
                               drogon::FilterChainCallback &&next)
{
    // 过滤器可以在调用 next() 前检查权限、日志、限流等。
    if (req->getHeader("x-demo-token") != "drogon")//从请求头读取 x-demo-token；X - Demo - Token : drogon
    {
        Json::Value body;
        body["error"] = "missing or invalid X-Demo-Token";
        body["hint"] = "send request header: X-Demo-Token: drogon";

        auto response = drogon::HttpResponse::newHttpJsonResponse(body);
        response->setStatusCode(drogon::k401Unauthorized);
        reject(response);  // 到这里为止，不再进入控制器。
        return;
    }

    next();  // 校验成功，交给下一个过滤器或最终控制器。
}
