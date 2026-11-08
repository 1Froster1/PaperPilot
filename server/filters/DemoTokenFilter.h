#pragma once

#include <drogon/HttpFilter.h>

// 演示“在控制器执行前拦截请求”的过滤器。
// 规则：请求头必须带 X-Demo-Token: drogon。
class DemoTokenFilter : public drogon::HttpFilter<DemoTokenFilter>
{
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&reject,
                  drogon::FilterChainCallback &&next) override;
};
