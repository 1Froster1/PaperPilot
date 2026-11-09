#include <drogon/drogon.h>
#include <mysql.h>
#include <exception>
#include <stdio.h>  //mysql 文件
#include "runtime_config.h"

#ifdef _WIN32
#include <windows.h>
#endif

MYSQL mysql;     // 数据库句柄
MYSQL_RES *res;  // 查询结果集
MYSQL_ROW row;   // 记录结构体

// 注册 routes.cpp 中定义的普通函数式路由。

int initMysqlConnection()
{
    const auto &config = serverRuntimeConfig();
    // 初始化数据库
    mysql_init(&mysql);

    // Qt、Drogon 源码和 papers 表统一使用 UTF-8。
    // 必须使用 utf8mb4，不能用 gbk；否则中文 UTF-8 字节写入时会报
    // Incorrect string value。
    mysql_options(&mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // Credentials live only in ignored config.local.json.
    if (mysql_real_connect(
            &mysql, config.databaseHost.c_str(), config.databaseUser.c_str(),
            config.databasePassword.c_str(), config.databaseName.c_str(),
            config.databasePort, NULL, 0) ==
        NULL)
    {
        printf("错误原因: %s\n", mysql_error(&mysql));
        printf("连接失败！\n");
        exit(-1);
    }

    // 再明确设置一次连接字符集，并在失败时直接显示原因。
    if (mysql_set_character_set(&mysql, "utf8mb4") != 0)
    {
        printf("设置 MySQL UTF-8 字符集失败: %s\n", mysql_error(&mysql));
        mysql_close(&mysql);
        exit(-1);
    }

    return 0;
}

void initRoutes(MYSQL *mysql);


int main()
{
#ifdef _WIN32
    // Drogon 日志和源代码使用 UTF-8；让 Windows 控制台也按 UTF-8 显示中文。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try
    {
        loadServerRuntimeConfig("config.local.json");
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "Server configuration error: %s\n", error.what());
        return 1;
    }

    initMysqlConnection();
    // 注册普通路由；HttpController 控制器由 Drogon 自动注册。
    initRoutes(&mysql);

    // 从项目根目录的 config.json 读取监听地址、端口等配置后启动服务。
    drogon::app().loadConfigFile("config.json").run();
    mysql_close(&mysql);
}
