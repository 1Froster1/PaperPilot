#include "tools.h"
#include <drogon/drogon.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <mysql.h>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace drogon;



void initRoutes(MYSQL *mysql)
{
    // 请求示例：POST /api/auth/login
    app().registerHandler(
        "/api/auth/login",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback) {
            std::lock_guard<std::mutex> lock(mysqlMutex);
            Json::Value body;
            const auto json = req->getJsonObject();

            // 请求不是合法 JSON，或缺少字段时，直接返回 400。
            if (!json || !json->isMember("account") ||
                !json->isMember("password"))
            {
                body["success"] = false;
                body["message"] = "request must contain account and password";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            const auto account = (*json)["account"].asString();
            const auto password = (*json)["password"].asString();

            // 这是临时演示账号。真实项目中应查询数据库，保存密码哈希，
            // 并在验证成功后签发 Session 或 JWT，绝不能把明文密码写在代码中。

            // 假设 inputAccount、inputPassword 是用户输入的账号和密码
            std::string sql =
                "SELECT password_hash, display_name FROM users WHERE account = "
                "'" +
                account + "';";

            mysql_query(mysql, sql.c_str());

            MYSQL_RES *res =
                mysql_store_result(mysql);  // 从数据库连接 mysql 中取出刚才
                                            // SELECT 查询的完整结果集
            MYSQL_ROW row =
                mysql_fetch_row(res);  // 从结果集 res 中，取出下一行记录

            if (row == nullptr)
            {
                LOG_WARN << "登录失败：账号不存在，account=" << account;
                printf("账号不存在\n");
                body["success"] = false;
                body["message"] = "account is not exist";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k401Unauthorized);
                mysql_free_result(res);
                callback(response);
            }
            else if (std::strcmp(row[0], password.c_str()) != 0)
            {
                // 不记录密码或
                // password_hash，日志中只保留账号，避免泄露敏感信息。
                LOG_WARN << "登录失败：密码不匹配，account=" << account;

                printf("密码错误\n");
                body["success"] = false;
                body["message"] = "invalid account or password";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k401Unauthorized);
                mysql_free_result(res);
                callback(response);
            }
            else
            {
                LOG_INFO << "登录成功，account=" << account
                         << "，display_name=" << row[1];

                printf("登录成功，欢迎 %s\n", row[1]);
                body["success"] = true;
                body["message"] = "login successful";
                body["user"]["name"] = std::string(row[1]) + " learner";
                mysql_free_result(res);
                callback(HttpResponse::newHttpJsonResponse(body));
                return;
            }
        },
        {Post});

    // 获取论文库列表：
    // GET
    // /api/papers?keyword=transformer&year=2017&field=NLP&sort=newest&page=1&pageSize=20
    app().registerHandler(
        "/api/papers",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback) {
            const auto json = req->getJsonObject();

            if (json)
            {
                LOG_INFO << "收到请求体：\n" << json->toStyledString();
            }

            const auto keyword = req->getParameter("keyword");
            const auto year = req->getParameter("year");
            const auto field = req->getParameter("field");
            const auto sort = req->getParameter("sort");
            const int page = readPositiveParameter(req, "page", 1, 1000000);
            const int pageSize = readPositiveParameter(req, "pageSize", 20, 50);

            // year 若不为空必须是四位数字，避免把错误参数带入 SQL。
            if (!year.empty() && (!isPositiveInteger(year) || year.size() != 4))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "year must be a four-digit number";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            // 只允许白名单中的排序值，绝不把客户端传入的排序文本直接拼到 SQL。
            if (!sort.empty() && sort != "newest" && sort != "oldest")
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "sort must be newest or oldest";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);

            // WHERE 1 = 1 AND(
            //     p.title LIKE '%transformer%'
            //     OR p.authors LIKE '%transformer%'
            //     OR EXISTS(
            //             SELECT 1
            //             FROM paper_tags keyword_tag
            //             WHERE keyword_tag.paper_id = p.id
            //                 AND keyword_tag.tag_name LIKE '%transformer%'
            //     )
            //)

            std::string where = " WHERE 1 = 1";
            if (!keyword.empty())
            {
                const auto escapedKeyword = escapeSql(mysql, keyword);
                const auto like = "'%" + escapedKeyword + "%'";
                where += " AND (p.title LIKE " + like + " OR p.authors LIKE " +
                         like +
                         " OR EXISTS (SELECT 1 FROM paper_tags keyword_tag"
                         " WHERE keyword_tag.paper_id = p.id"
                         " AND keyword_tag.tag_name LIKE " +
                         like + "))";
            }
            if (!year.empty())
                where += " AND p.publish_year = " + year;

            if (!field.empty())
            {
                const auto escapedField = escapeSql(mysql, field);
                where +=
                    " AND EXISTS (SELECT 1 FROM paper_tags field_tag"
                    " WHERE field_tag.paper_id = p.id"
                    " AND field_tag.tag_name = '" +
                    escapedField + "')";
            }

            // 注意：当前登录还没有创建 Session，因此学习版暂时查询所有论文。
            // 接入 Session 后应加上：AND p.user_id = 当前登录用户的 id。
            const std::string countSql =
                "SELECT COUNT(*) FROM papers p" + where + ";";
            if (mysql_query(mysql, countSql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *countResult = mysql_store_result(mysql);
            if (countResult == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_ROW countRow = mysql_fetch_row(countResult);
            const int total = countRow == nullptr || countRow[0] == nullptr
                                  ? 0
                                  : std::stoi(countRow[0]);  // 保存查询结果数量
            mysql_free_result(countResult);

            const int offset = (page - 1) * pageSize;
            const std::string orderBy =
                sort == "oldest"
                    ? " ORDER BY p.publish_year ASC, p.created_at ASC"
                    : " ORDER BY p.created_at DESC";
            const std::string papersSql =
                "SELECT p.id, p.title, p.authors, p.publish_year, p.source,"
                " p.status, p.is_favorite"
                " FROM papers p" +
                where + orderBy + " LIMIT " + std::to_string(offset) + ", " +
                std::to_string(pageSize) + ";";

            if (mysql_query(mysql, papersSql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *papersResult = mysql_store_result(mysql);
            if (papersResult == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            body["success"] = true;
            body["total"] = total;
            body["page"] = page;
            body["pageSize"] = pageSize;
            body["items"] = Json::arrayValue;

            // 同一个 MYSQL 连接在主结果集尚未释放时，不能再执行标签查询。
            // 因此先把论文行复制到 vector，释放 papersResult 后再逐篇查询标签。
            struct PaperSummary
            {
                std::string id;
                std::string title;
                std::string authors;
                std::string publishYear;
                bool hasPublishYear;
                std::string source;
                std::string status;
                bool favorite;
            };
            std::vector<PaperSummary> paperRows;
            MYSQL_ROW paperRow;
            while ((paperRow = mysql_fetch_row(papersResult)) != nullptr)
            {
                paperRows.push_back({columnOrEmpty(paperRow, 0),
                                     columnOrEmpty(paperRow, 1),
                                     columnOrEmpty(paperRow, 2),
                                     columnOrEmpty(paperRow, 3),
                                     paperRow[3] != nullptr,
                                     columnOrEmpty(paperRow, 4),
                                     columnOrEmpty(paperRow, 5),
                                     columnOrEmpty(paperRow, 6) == "1"});
            }
            mysql_free_result(papersResult);

            for (const auto &paperRow : paperRows)
            {
                Json::Value paper;
                paper["id"] = std::stoll(paperRow.id);
                paper["title"] = paperRow.title;
                paper["authors"] = paperRow.authors;
                paper["publishYear"] = paperRow.hasPublishYear
                    ? Json::Value(std::stoi(paperRow.publishYear))
                    : Json::Value();
                paper["source"] = paperRow.source;
                paper["status"] = paperRow.status;
                paper["favorite"] = paperRow.favorite;
                paper["tags"] = Json::arrayValue;  // 创建一个叫 tags 的空数组

                // tags 单独查询，逻辑直观，方便你学习表之间如何关联。
                const std::string tagsSql =
                    "SELECT tag_name FROM paper_tags WHERE paper_id = " +
                    paperRow.id + " ORDER BY tag_name;";
                if (mysql_query(mysql, tagsSql.c_str()) != 0)
                {
                    sendDatabaseError(callback, mysql);
                    return;
                }

                MYSQL_RES *tagsResult = mysql_store_result(mysql);
                if (tagsResult == nullptr)
                {
                    sendDatabaseError(callback, mysql);
                    return;
                }

                MYSQL_ROW tagRow;
                while ((tagRow = mysql_fetch_row(tagsResult)) != nullptr)
                    paper["tags"].append(columnOrEmpty(tagRow, 0));
                mysql_free_result(tagsResult);

                body["items"].append(paper);
            }

            callback(HttpResponse::newHttpJsonResponse(body));

            LOG_WARN << "返回响应体为" << body.toStyledString();
        },
        {Get});

    // POST /api/papers：上传论文
    app().registerHandler(
        "/api/papers",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback) {
            // 1. 创建 multipart 解析器
            MultiPartParser parser;

            // 2. 解析 Qt 发送过来的请求
            if (parser.parse(req) != 0)
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "multipart 解析失败";

                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            // 3. 获取普通文本字段 title
            std::string title = parser.getParameter<std::string>("title");

            if (title.empty())
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "论文标题不能为空";

                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            // 4. 获取上传文件
            auto &files = parser.getFiles();

            if (files.size() != 1)
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "请上传一个 PDF 文件";

                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            // 获取文件对象
            auto &file = files[0];

            // 获取原始文件名
            std::string fileName = file.getFileName();

            // 获取文件大小
            size_t fileSize = file.fileLength();

            // 获取文件扩展名
            std::string extension(file.getFileExtension());

            // 6. 检查文件扩展名
            if (extension != "pdf" && extension != "PDF")
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "只允许上传 PDF 文件";

                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            LOG_INFO << "论文检验成功";

            // 保存文件
            std::filesystem::create_directories("./uploads");

            LOG_INFO << "论文保存成功";

            auto timestamp = std::chrono::system_clock::now()
                                 .time_since_epoch()
                                 .count();  // 获取当前时间

            std::string savePath = "./uploads/" + fileName;

            const std::string absolutePdfPath =
                std::filesystem::absolute(savePath).string();

            // 保存上传的 PDF；后续真实解析器会从这个路径读取文件内容。
            file.saveAs(savePath);

            LOG_INFO << "论文上传成功";
            LOG_INFO << "标题：" << title;
            LOG_INFO << "原始文件名：" << fileName;
            LOG_INFO << "文件大小：" << fileSize;
            LOG_INFO << "保存路径：" << savePath;


            // 对字符串进行 SQL 转义
            std::string safeTitle = escapeSql(mysql, title);
            std::string safePath = escapeSql(mysql, savePath);

            // 构造 SQL
            std::string sql =
                "INSERT INTO papers (user_id, title, pdf_path, status) "
                "VALUES (" +
                std::to_string(1) + ", '" + safeTitle + "', '" + safePath +
                "', 'parsing');";

            int paperId = 0;
            {
                std::lock_guard<std::mutex> lock(mysqlMutex);

                if (mysql_query(mysql, sql.c_str()) != 0)
                {
                    LOG_ERROR << mysql_error(mysql);

                    Json::Value body;
                    body["success"] = false;
                    body["message"] = "数据库插入失败";

                    auto response = HttpResponse::newHttpJsonResponse(body);
                    response->setStatusCode(k500InternalServerError);
                    callback(response);
                    return;
                }

                paperId = mysql_insert_id(mysql);
            }
            
            std::string taskSql =
                "INSERT INTO ai_tasks "
                "(paper_id, task_type, status, progress) "
                "VALUES (" +
                std::to_string(paperId) + ", 'parse_pdf', 'running', 0);";

            int taskId = 0;
            {
                std::lock_guard<std::mutex> lock(mysqlMutex);

                if (mysql_query(mysql, taskSql.c_str()) != 0)
                {
                    LOG_ERROR << mysql_error(mysql);

                    Json::Value body;
                    body["success"] = false;
                    body["message"] = "数据库插入失败";

                    auto response = HttpResponse::newHttpJsonResponse(body);
                    response->setStatusCode(k500InternalServerError);
                    callback(response);
                    return;
                }

                taskId = mysql_insert_id(mysql);
            }

            LOG_ERROR << "本次taskid为 " << taskId;

             // 返回 JSON
            Json::Value body;
            body["success"] = true;
            body["message"] = "论文上传成功";
            body["title"] = title;
            body["fileName"] = fileName;
            body["fileSize"] = static_cast<Json::UInt64>(fileSize);
            body["paperId"] = paperId;
            body["taskId"] = std::to_string(taskId);
            body["status"] = "parsing";
            body["progress"] = 0;

            startPdfParseTask(mysql,
                              std::to_string(taskId),
                              std::to_string(paperId),
                              absolutePdfPath);

            auto response = HttpResponse::newHttpJsonResponse(body);
            callback(response);
        },
        {Post});

    app().registerHandler(
        "/api/tasks/{1}",
        [mysql](const HttpRequestPtr&,
            std::function<void(const HttpResponsePtr&)>&& callback,
            const std::string &taskId) {
            if (!isPositiveInteger(taskId))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "task id must be a positive integer";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const std::string sql =
                "SELECT id, paper_id, task_type, status, progress, "
                "COALESCE(error_message, '') "
                "FROM ai_tasks WHERE id = " + taskId + " LIMIT 1";

            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *result = mysql_store_result(mysql);
            if (result == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_ROW row = mysql_fetch_row(result);
            Json::Value body;
            if (row == nullptr)
            {
                mysql_free_result(result);
                body["success"] = false;
                body["message"] = "task not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }

            body["success"] = true;
            body["taskId"] = columnOrEmpty(row, 0);
            body["paperId"] = columnOrEmpty(row, 1);
            body["taskType"] = columnOrEmpty(row, 2);
            body["status"] = columnOrEmpty(row, 3);
            body["progress"] = std::atoi(columnOrEmpty(row, 4).c_str());
            body["errorMessage"] = columnOrEmpty(row, 5);
            mysql_free_result(result);

            auto response = HttpResponse::newHttpJsonResponse(body);
            callback(response);
        },
        {Get});

    // 每两秒推进一次模拟任务。runEvery 运行在 Drogon 的事件循环中，
    // 不会额外创建线程；SQL 操作仍由 mysqlMutex 保护。
    //app().getLoop()->runEvery(2.0, [mysql] { advanceRunningTasks(mysql); });//注册一个重复定时器：

    app().registerHandler(
        "/api/papers/{1}",
        [mysql](const HttpRequestPtr&,
            std::function<void(const HttpResponsePtr&)>&& callback,
            const std::string &paperId) {
            if (!isPositiveInteger(paperId))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id must be a positive integer";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);


            const std::string sql =
                "SELECT id, title, authors, publish_year, source, "
                "abstract_text, pdf_path, status, is_favorite, created_at "
                "FROM papers WHERE id = " +
                paperId + " LIMIT 1";

            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *result = mysql_store_result(mysql);
            if (result == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_ROW row = mysql_fetch_row(result);
            Json::Value body;
            if (row == nullptr)
            {
                mysql_free_result(result);
                body["success"] = false;
                body["message"] = "paper not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }

            body["success"] = true;
            body["item"]["id"] = columnOrEmpty(row, 0);
            body["item"]["title"] = columnOrEmpty(row, 1);
            body["item"]["authors"] = columnOrEmpty(row, 2);
            body["item"]["publishYear"] = columnOrEmpty(row, 3);
            body["item"]["source"] = columnOrEmpty(row, 4);
            body["item"]["abstract"] = columnOrEmpty(row, 5);
            body["item"]["pdfPath"] = columnOrEmpty(row, 6);
            body["item"]["status"] = columnOrEmpty(row, 7);
            body["item"]["favorite"] = columnOrEmpty(row, 8);
            body["item"]["tags"] = Json::arrayValue;
            
            body["item"]["createdAt"] = columnOrEmpty(row, 9);

            mysql_free_result(result);

            const std::string task_sql =
                "SELECT tag_name FROM paper_tags "
                "WHERE paper_id = " +
                paperId + " ORDER BY tag_name";

            if (mysql_query(mysql, task_sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *task_result = mysql_store_result(mysql);
            if (task_result == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            // 标签允许为空；直接循环，不能预先 fetch 一行，否则会漏掉第一个标签。
            MYSQL_ROW task_row;
            while ((task_row = mysql_fetch_row(task_result)) != nullptr)
            {
                body["item"]["tags"].append(columnOrEmpty(task_row, 0));
            }
            mysql_free_result(task_result);

            auto response = HttpResponse::newHttpJsonResponse(body);
            LOG_WARN << "返回响应体为" << body.toStyledString();
            callback(response);
         },
        {Get});

    // 删除一篇论文。先删除关联任务，随后删除 papers 主记录；paper_tags 和
    // paper_notes 都配置了外键级联删除，会由 MySQL 自动清理。
    app().registerHandler(
        "/api/papers/{1}",
        [mysql](const HttpRequestPtr &,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &paperId) {
            if (!isPositiveInteger(paperId))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id must be a positive integer";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            if (mysql_query(mysql, "START TRANSACTION") != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            const std::string deleteTasksSql =
                "DELETE FROM ai_tasks WHERE paper_id = " + paperId;
            if (mysql_query(mysql, deleteTasksSql.c_str()) != 0)
            {
                mysql_query(mysql, "ROLLBACK");
                sendDatabaseError(callback, mysql);
                return;
            }

            const std::string deletePaperSql =
                "DELETE FROM papers WHERE id = " + paperId;
            if (mysql_query(mysql, deletePaperSql.c_str()) != 0)
            {
                mysql_query(mysql, "ROLLBACK");
                sendDatabaseError(callback, mysql);
                return;
            }

            if (mysql_affected_rows(mysql) == 0)
            {
                mysql_query(mysql, "ROLLBACK");
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }

            if (mysql_query(mysql, "COMMIT") != 0)
            {
                mysql_query(mysql, "ROLLBACK");
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            body["success"] = true;
            body["message"] = "paper deleted";
            body["paperId"] = paperId;
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Delete});

    // 获取一篇论文的全部笔记。
    app().registerHandler(
        "/api/papers/{1}/notes",
        [mysql](const HttpRequestPtr &,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &paperId) {
            if (!isPositiveInteger(paperId))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id must be a positive integer";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const std::string sql =
                "SELECT id, content, created_at, updated_at FROM paper_notes "
                "WHERE paper_id = " + paperId + " ORDER BY updated_at DESC, id DESC";
            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *result = mysql_store_result(mysql);
            if (result == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            body["success"] = true;
            body["items"] = Json::arrayValue;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)) != nullptr)
            {
                Json::Value note;
                note["id"] = columnOrEmpty(row, 0);
                note["content"] = columnOrEmpty(row, 1);
                note["createdAt"] = columnOrEmpty(row, 2);
                note["updatedAt"] = columnOrEmpty(row, 3);
                body["items"].append(note);
            }
            mysql_free_result(result);
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Get});

    // 为一篇论文新增笔记，请求体：{"content":"笔记正文"}。
    app().registerHandler(
        "/api/papers/{1}/notes",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &paperId) {
            const auto json = req->getJsonObject();
            if (!isPositiveInteger(paperId) || !json ||
                !json->isMember("content") || !(*json)["content"].isString() ||
                (*json)["content"].asString().empty())
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id and note content are required";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const std::string safeContent =
                escapeSql(mysql, (*json)["content"].asString());
            // SELECT 写法可同时保证 paper_id 必须对应真实论文。
            const std::string sql =
                "INSERT INTO paper_notes (paper_id, content) "
                "SELECT id, '" + safeContent + "' FROM papers WHERE id = " + paperId;
            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }
            if (mysql_affected_rows(mysql) == 0)
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }

            Json::Value body;
            body["success"] = true;
            body["noteId"] = std::to_string(mysql_insert_id(mysql));
            auto response = HttpResponse::newHttpJsonResponse(body);
            response->setStatusCode(k201Created);
            callback(response);
        },
        {Post});

    // 修改已有笔记，请求体：{"content":"新的正文"}。
    app().registerHandler(
        "/api/notes/{1}",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &noteId) {
            const auto json = req->getJsonObject();
            if (!isPositiveInteger(noteId) || !json ||
                !json->isMember("content") || !(*json)["content"].isString() ||
                (*json)["content"].asString().empty())
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "note id and content are required";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const std::string sql =
                "UPDATE paper_notes SET content = '" +
                escapeSql(mysql, (*json)["content"].asString()) +
                "' WHERE id = " + noteId;
            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            if (mysql_affected_rows(mysql) == 0)
            {
                body["success"] = false;
                body["message"] = "note not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }
            body["success"] = true;
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Put});

    app().registerHandler(
        "/api/notes/{1}",
        [mysql](const HttpRequestPtr &,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &noteId) {
            if (!isPositiveInteger(noteId))
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "note id must be a positive integer";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const std::string sql = "DELETE FROM paper_notes WHERE id = " + noteId;
            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            if (mysql_affected_rows(mysql) == 0)
            {
                body["success"] = false;
                body["message"] = "note not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }
            body["success"] = true;
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Delete});

    // 切换论文收藏状态，请求体：{"favorite":true/false}。
    app().registerHandler(
        "/api/papers/{1}/favorite",
        [mysql](const HttpRequestPtr &req,
                std::function<void(const HttpResponsePtr &)> &&callback,
                const std::string &paperId) {
            const auto json = req->getJsonObject();
            if (!isPositiveInteger(paperId) || !json ||
                !json->isMember("favorite") || !(*json)["favorite"].isBool())
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id and boolean favorite are required";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::lock_guard<std::mutex> lock(mysqlMutex);
            const bool favorite = (*json)["favorite"].asBool();
            const std::string sql =
                "UPDATE papers SET is_favorite = " +
                std::string(favorite ? "1" : "0") + " WHERE id = " + paperId;
            if (mysql_query(mysql, sql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            if (mysql_affected_rows(mysql) == 0)
            {
                body["success"] = false;
                body["message"] = "paper not found";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }
            body["success"] = true;
            body["paperId"] = paperId;
            body["favorite"] = favorite;
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Patch});

    // 任务中心列表，使用 LEFT JOIN 保留历史上已删除论文的任务记录。
    app().registerHandler(
        "/api/tasks",
        [mysql](const HttpRequestPtr &,
                std::function<void(const HttpResponsePtr &)> &&callback) {
            std::lock_guard<std::mutex> lock(mysqlMutex);
            constexpr const char *sql =
                "SELECT t.id, t.paper_id, COALESCE(p.title, ''), t.task_type, "
                "t.status, t.progress, COALESCE(t.result_text, ''), "
                "COALESCE(t.error_message, ''), t.updated_at "
                "FROM ai_tasks t LEFT JOIN papers p ON p.id = t.paper_id "
                "ORDER BY t.updated_at DESC, t.id DESC LIMIT 100";
            if (mysql_query(mysql, sql) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *result = mysql_store_result(mysql);
            if (result == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            Json::Value body;
            body["success"] = true;
            body["items"] = Json::arrayValue;
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result)) != nullptr)
            {
                Json::Value task;
                task["id"] = columnOrEmpty(row, 0);
                task["paperId"] = columnOrEmpty(row, 1);
                task["paperTitle"] = columnOrEmpty(row, 2);
                task["taskType"] = columnOrEmpty(row, 3);
                task["status"] = columnOrEmpty(row, 4);
                task["progress"] = std::atoi(columnOrEmpty(row, 5).c_str());
                task["resultText"] = columnOrEmpty(row, 6);
                task["errorMessage"] = columnOrEmpty(row, 7);
                task["updatedAt"] = columnOrEmpty(row, 8);
                body["items"].append(task);
            }
            mysql_free_result(result);
            callback(HttpResponse::newHttpJsonResponse(body));
        },
        {Get});

    app().registerHandler(
    "/api/papers/{paperId}/chat",
    [mysql](const HttpRequestPtr &req,
            std::function<void(const HttpResponsePtr &)> &&callback,
            const std::string &paperId) {

            const auto json = req->getJsonObject();
            if (!isPositiveInteger(paperId) || !json ||
                !json->isMember("question") ||
                !(*json)["question"].isString() ||
                (*json)["question"].asString().empty())
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "paper id and question are required";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            std::string mode = "qa";
            if ((*json).isMember("mode"))
            {
                if (!(*json)["mode"].isString())
                {
                    Json::Value body;
                    body["success"] = false;
                    body["message"] = "mode must be a string";
                    auto response = HttpResponse::newHttpJsonResponse(body);
                    response->setStatusCode(k400BadRequest);
                    callback(response);
                    return;
                }
                mode = (*json)["mode"].asString();
            }

            if (mode != "qa" && mode != "summary" && mode != "note" &&
                mode != "critique")
            {
                Json::Value body;
                body["success"] = false;
                body["message"] = "unsupported agent mode";
                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k400BadRequest);
                callback(response);
                return;
            }

            LOG_INFO << "解析聊天请求头完成";
            // The shared MYSQL handle may also be used by PDF parsing tasks.
            // Serialize all queries on it and always finish/free a result set
            // before issuing the next query on the same connection.
            std::lock_guard<std::mutex> lock(mysqlMutex);

            const std::string checkPaperSql =
                "SELECT id, title, COALESCE(authors, ''), "
                "COALESCE(abstract_text, '') FROM papers "
                "WHERE id = " +
                paperId +
                " AND status = 'ready' "
                "LIMIT 1";

            if (mysql_query(mysql, checkPaperSql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *paperResult = mysql_store_result(mysql);
            if (paperResult == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_ROW paperRow = mysql_fetch_row(paperResult);

            if (paperRow == nullptr)
            {
                // 论文不存在，或者尚未解析完成。
                mysql_free_result(paperResult);

                Json::Value body;
                body["success"] = false;
                body["message"] = "paper not found or not ready";

                auto response = HttpResponse::newHttpJsonResponse(body);
                response->setStatusCode(k404NotFound);
                callback(response);
                return;
            }

            const std::string paperTitle = columnOrEmpty(paperRow, 1);
            const std::string paperAuthors = columnOrEmpty(paperRow, 2);
            const std::string paperAbstract = columnOrEmpty(paperRow, 3);

            // mysql_store_result() keeps this result active. Free it before
            // running chunksSql, otherwise MySQL reports "Commands out of sync".
            mysql_free_result(paperResult);

            LOG_INFO << "查询paper数据库完成";

            Json::Value agentRequest;

            agentRequest["request_id"] =
                paperId + "-" +
                std::to_string(std::chrono::system_clock::now()
                                   .time_since_epoch()
                                   .count());
            agentRequest["user_id"] = "1";
            agentRequest["paper_id"] = paperId;
            agentRequest["mode"] = mode;

            const std::string question = (*json)["question"].asString();
            agentRequest["question"] = question;

            Json::Value paper;
            paper["title"] = paperTitle;
            paper["authors"] = paperAuthors;
            paper["abstract"] = paperAbstract;

            // tags 必须是 JSON 数组。还没查询标签时，先发空数组。
            paper["tags"] = Json::arrayValue;
            agentRequest["paper"] = paper;

            Json::Value conversation{Json::arrayValue};

            const std::string userId = "1";
            const std::string historySql =
                "SELECT role, content FROM ("
                "SELECT id, role, content "
                "FROM paper_chat_messages "
                "WHERE paper_id = " +
                paperId + " AND user_id = " + userId +
                " ORDER BY id DESC LIMIT 10"
                ") AS recent_messages ORDER BY id ASC";
            
            // 当前先不带历史对话，后续实现多轮对话时再填充。
            agentRequest["conversation"] = Json::arrayValue;

            if (mysql_query(mysql, historySql.c_str()) != 0)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_RES *historyResult = mysql_store_result(mysql);
            if (historyResult == nullptr)
            {
                sendDatabaseError(callback, mysql);
                return;
            }

            MYSQL_ROW historyRow;
            while ((historyRow = mysql_fetch_row(historyResult)) != nullptr)
            {
                Json::Value message;
                message["role"] = columnOrEmpty(historyRow, 0);
                message["content"] = columnOrEmpty(historyRow, 1);
                conversation.append(message);
            }

            mysql_free_result(historyResult);

            agentRequest["conversation"] = conversation;


            LOG_INFO << "构造agent请求头完成";

            sendMessageToAgent(
                agentRequest,
                [mysql, callback, paperId, userId, question](
                    const Json::Value &workerResult) {
                    
                    bool success = workerResult.get("success", false).asBool();
                    if (success && !savePaperChatTurn(mysql, paperId, userId, question, workerResult))
                    {
                        LOG_ERROR << "Agent 已成功回答，但聊天记录写入失败。";
                    }

                    auto response =
                        HttpResponse::newHttpJsonResponse(workerResult);

                    callback(response);
                },
                [callback](const std::string &errorMessage) {
                    Json::Value body;
                    body["success"] = false;
                    body["message"] = errorMessage;

                    auto response = HttpResponse::newHttpJsonResponse(body);

                    response->setStatusCode(k503ServiceUnavailable);
                    callback(response);
                });
            LOG_INFO << "agent请求头发送成功";
        },
        {Post});
}



