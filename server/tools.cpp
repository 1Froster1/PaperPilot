#include "tools.h"
#include "runtime_config.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

using namespace drogon;

std::mutex mysqlMutex;

std::string escapeSql(MYSQL *mysql, const std::string &value)
{
    std::string escaped(value.size() * 2 + 1, '\0');
    const auto length = mysql_real_escape_string(mysql, escaped.data(), value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}

void markPdfParseFailed(MYSQL *mysql, const std::string &taskId,
                        const std::string &paperId, std::string errorMessage)
{
    if (errorMessage.size() > 2000) errorMessage.resize(2000);
    std::lock_guard<std::mutex> lock(mysqlMutex);
    const std::string safeError = escapeSql(mysql, errorMessage);
    if (mysql_query(mysql, "START TRANSACTION") != 0) return;
    const std::string paperSql = "UPDATE papers SET status = 'failed', parse_error = '" + safeError + "' WHERE id = " + paperId;
    const std::string taskSql = "UPDATE ai_tasks SET status = 'failed', finished_at = NOW(), error_message = '" + safeError + "' WHERE id = " + taskId;
    if (mysql_query(mysql, paperSql.c_str()) != 0 || mysql_query(mysql, taskSql.c_str()) != 0 || mysql_query(mysql, "COMMIT") != 0)
    {
        LOG_ERROR << "更新 PDF 失败状态失败: " << mysql_error(mysql);
        mysql_query(mysql, "ROLLBACK");
    }
}

void startPaperVectorIndex(MYSQL *mysql, const std::string &taskId,
                           const std::string &paperId, const Json::Value &indexRequest)
{
    auto client = HttpClient::newHttpClient(serverRuntimeConfig().agentWorkerBaseUrl);
    auto request = HttpRequest::newHttpRequest();
    request->setMethod(Post);
    request->setPath("/worker/index-paper");
    request->setContentTypeCode(CT_APPLICATION_JSON);
    request->setBody(indexRequest.toStyledString());
    client->sendRequest(request, [client, mysql, taskId, paperId](ReqResult result, const HttpResponsePtr &response) {
        (void)client;
        if (result != ReqResult::Ok || !response)
        {
            markPdfParseFailed(mysql, taskId, paperId, "无法连接 Python 向量索引服务（ReqResult=" + std::to_string(static_cast<int>(result)) + ")");
            return;
        }
        if (response->statusCode() != k200OK)
        {
            markPdfParseFailed(mysql, taskId, paperId, "Python 向量索引服务返回 HTTP " + std::to_string(static_cast<int>(response->statusCode())) + "：" + std::string(response->body()));
            return;
        }
        std::lock_guard<std::mutex> lock(mysqlMutex);
        if (mysql_query(mysql, "START TRANSACTION") != 0) return;
        const std::string paperSql = "UPDATE papers SET status = 'ready', parse_error = NULL WHERE id = " + paperId;
        const std::string taskSql = "UPDATE ai_tasks SET status = 'completed', progress = 100, finished_at = NOW(), error_message = NULL WHERE id = " + taskId;
        if (mysql_query(mysql, paperSql.c_str()) != 0 || mysql_query(mysql, taskSql.c_str()) != 0 || mysql_query(mysql, "COMMIT") != 0)
        {
            LOG_ERROR << "更新向量索引完成状态失败: " << mysql_error(mysql);
            mysql_query(mysql, "ROLLBACK");
        }
    }, 600.0);
}

void startPdfParseTask(MYSQL *mysql, std::string taskId, std::string paperId, std::string absolutePdfPath)
{
    Json::Value body;
    body["task_id"] = taskId;
    body["paper_id"] = paperId;
    body["pdf_path"] = absolutePdfPath;
    auto client = HttpClient::newHttpClient(serverRuntimeConfig().agentWorkerBaseUrl);
    auto request = HttpRequest::newHttpRequest();
    request->setMethod(Post);
    request->setPath("/worker/parse-pdf");
    request->setContentTypeCode(CT_APPLICATION_JSON);
    request->setBody(body.toStyledString());
    client->sendRequest(request, [mysql, taskId, paperId](ReqResult result, const HttpResponsePtr &response) {
        if (result != ReqResult::Ok || !response)
        {
            markPdfParseFailed(mysql, taskId, paperId, "无法连接 Python PDF Worker（ReqResult=" + std::to_string(static_cast<int>(result)) + ")");
            return;
        }
        if (response->statusCode() != k200OK)
        {
            markPdfParseFailed(mysql, taskId, paperId, "Python PDF Worker 返回 HTTP " + std::to_string(static_cast<int>(response->statusCode())) + "：" + std::string(response->body()));
            return;
        }
        Json::Value workerResult;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream{std::string(response->body())};
        if (!Json::parseFromStream(builder, stream, &workerResult, &errors))
        {
            markPdfParseFailed(mysql, taskId, paperId, "Python PDF Worker 返回的 JSON 无法解析：" + errors);
            return;
        }
        std::lock_guard<std::mutex> lock(mysqlMutex);
        if (mysql_query(mysql, ("DELETE FROM paper_chunks WHERE paper_id = " + paperId).c_str()) != 0) return;
        Json::Value indexRequest;
        indexRequest["task_id"] = taskId;
        indexRequest["paper_id"] = paperId;
        indexRequest["replace_existing"] = true;
        indexRequest["chunks"] = Json::arrayValue;
        for (const auto &chunk : workerResult["chunks"])
        {
            if (!chunk.isObject() || !chunk.isMember("chunk_index") || !chunk.isMember("content")) return;
            const int chunkIndex = chunk["chunk_index"].asInt();
            const std::string content = chunk["content"].asString();
            const int pageStart = chunk["page_start"].asInt();
            const int pageEnd = chunk["page_end"].asInt();
            const std::string section = chunk["section"].asString();
            const int charCount = chunk.isMember("char_count") ? chunk["char_count"].asInt() : static_cast<int>(content.size());
            const std::string safeContent = escapeSql(mysql, content);
            const std::string safeSection = escapeSql(mysql, section);
            const std::string sectionSql = section.empty() ? "NULL" : "'" + safeSection + "'";
            const std::string sql = "INSERT INTO paper_chunks (paper_id, chunk_index, content, page_start, page_end, section, char_count, content_hash) VALUES (" + paperId + ", " + std::to_string(chunkIndex) + ", '" + safeContent + "', " + std::to_string(pageStart) + ", " + std::to_string(pageEnd) + ", " + sectionSql + ", " + std::to_string(charCount) + ", SHA2('" + safeContent + "', 256))";
            if (mysql_query(mysql, sql.c_str()) != 0) return;
            Json::Value indexChunk;
            indexChunk["chunk_id"] = std::to_string(chunkIndex);
            indexChunk["chunk_index"] = chunkIndex;
            indexChunk["content"] = content;
            indexChunk["page_start"] = pageStart;
            indexChunk["page_end"] = pageEnd;
            indexChunk["section"] = section;
            indexRequest["chunks"].append(indexChunk);
        }
        const std::string title = workerResult.get("extracted_title", "").asString();
        const std::string authors = workerResult.get("extracted_authors", "").asString();
        const std::string abstractText = workerResult.get("extracted_abstract", "").asString();
        std::string paperSql = "UPDATE papers SET status = 'indexing', page_count = " + std::to_string(workerResult["page_count"].asInt()) + ", ";
        if (!title.empty()) paperSql += "title = '" + escapeSql(mysql, title) + "', ";
        if (!authors.empty()) paperSql += "authors = '" + escapeSql(mysql, authors) + "', ";
        if (!abstractText.empty()) paperSql += "abstract_text = '" + escapeSql(mysql, abstractText) + "', ";
        paperSql += "parsed_at = NOW(), parse_error = NULL WHERE id = " + paperId;
        const std::string taskSql = "UPDATE ai_tasks SET status = 'running', progress = 75, finished_at = NULL, error_message = NULL WHERE id = " + taskId;
        if (mysql_query(mysql, paperSql.c_str()) != 0 || mysql_query(mysql, taskSql.c_str()) != 0) return;
        startPaperVectorIndex(mysql, taskId, paperId, indexRequest);
    }, 60.0);
}

void sendMessageToAgent(const Json::Value &agentMessages,
                        std::function<void(const Json::Value &)> onSuccess,
                        std::function<void(const std::string &)> onError)
{
    auto client = HttpClient::newHttpClient(serverRuntimeConfig().agentWorkerBaseUrl);
    auto request = HttpRequest::newHttpRequest();
    request->setMethod(Post);
    request->setPath("/agent/paper-chat");
    request->setContentTypeCode(CT_APPLICATION_JSON);
    request->setBody(agentMessages.toStyledString());
    client->sendRequest(request, [client, onSuccess = std::move(onSuccess), onError = std::move(onError)](ReqResult result, const HttpResponsePtr &response) {
        (void)client;
        if (result != ReqResult::Ok || !response) { onError("Python Agent Worker 请求失败（ReqResult=" + std::to_string(static_cast<int>(result)) + ")"); return; }
        const std::string responseBody{response->body()};
        if (response->statusCode() != k200OK) { onError("Python Agent 返回 HTTP " + std::to_string(static_cast<int>(response->statusCode())) + "：" + responseBody); return; }
        Json::Value resultJson;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream{responseBody};
        if (!Json::parseFromStream(builder, stream, &resultJson, &errors)) { onError("Agent 返回的 JSON 解析失败：" + errors); return; }
        onSuccess(resultJson);
    }, 60.0);
}

void sendDatabaseError(const std::function<void(const HttpResponsePtr &)> &callback, MYSQL *mysql)
{
    Json::Value body;
    body["success"] = false;
    body["message"] = "database query failed";
    body["detail"] = mysql_error(mysql);
    auto response = HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(k500InternalServerError);
    callback(response);
}

bool isPositiveInteger(const std::string &value)
{
    if (value.empty()) return false;
    for (char c : value) if (c < '0' || c > '9') return false;
    return true;
}

int readPositiveParameter(const HttpRequestPtr &req, const std::string &name, int defaultValue, int maximum)
{
    const auto value = req->getParameter(name);
    if (!isPositiveInteger(value)) return defaultValue;
    try { const int result = std::stoi(value); return result > 0 && result <= maximum ? result : defaultValue; }
    catch (...) { return defaultValue; }
}

std::string columnOrEmpty(MYSQL_ROW row, int index)
{
    return row[index] == nullptr ? "" : row[index];
}

void advanceRunningTasks(MYSQL *mysql)
{
    // The real Worker now owns parsing progress. Kept as a no-op compatibility
    // helper for any legacy route that still calls it.
    (void)mysql;
}

bool savePaperChatTurn(MYSQL *mysql,
                       const std::string &paperId,
                       const std::string &userId,
                       const std::string &sessionId,
                       const std::string &requestId,
                       const std::string &question,
                       const Json::Value &workerResult)
{
    const std::string answer = workerResult["answer"].asString();

    Json::Value citations{Json::objectValue};
    if (workerResult.isMember("citations") &&
        workerResult["citations"].isArray())
    {
        citations["internal"] = workerResult["citations"];
    }
    else citations["internal"] = Json::arrayValue;
    if (workerResult.isMember("external_references") &&
        workerResult["external_references"].isArray())
        citations["external"] = workerResult["external_references"];
    else citations["external"] = Json::arrayValue;
    if (workerResult.isMember("web_sources") &&
        workerResult["web_sources"].isArray())
        citations["web"] = workerResult["web_sources"];
    else citations["web"] = Json::arrayValue;

    std::lock_guard<std::mutex> lock(mysqlMutex);
    const std::string safeQuestion = escapeSql(mysql, question);
    const std::string safeAnswer = escapeSql(mysql, answer);
    const std::string safeSessionId = escapeSql(mysql, sessionId);
    const std::string safeRequestId = escapeSql(mysql, requestId);
    const std::string safeCitations =
        escapeSql(mysql, citations.toStyledString());
    const std::string safeToolTraces = escapeSql(
        mysql,
        workerResult.get("tools", Json::Value(Json::arrayValue)).toStyledString());

    if (mysql_query(mysql, "START TRANSACTION") != 0)
    {
        LOG_ERROR << "启动聊天记录事务失败: " << mysql_error(mysql);
        return false;
    }

    const std::string insertUserSql =
        "INSERT INTO paper_chat_messages "
        "(paper_id, user_id, session_id, request_id, role, content, citations_json) VALUES (" +
        paperId + ", " + userId + ", '" + safeSessionId + "', '" +
        safeRequestId + "', 'user', '" + safeQuestion + "', NULL)";

    const std::string insertAssistantSql =
        "INSERT INTO paper_chat_messages "
        "(paper_id, user_id, session_id, request_id, role, content, citations_json, tool_traces_json) VALUES (" +
        paperId + ", " + userId + ", '" + safeSessionId + "', '" +
        safeRequestId + "', 'assistant', '" + safeAnswer + "', '" +
        safeCitations + "', '" + safeToolTraces + "')";

    const std::string updateSessionSql =
        "UPDATE paper_chat_sessions SET updated_at = NOW() WHERE id = '" +
        safeSessionId + "'";

    if (mysql_query(mysql, insertUserSql.c_str()) != 0 ||
        mysql_query(mysql, insertAssistantSql.c_str()) != 0 ||
        mysql_query(mysql, updateSessionSql.c_str()) != 0)
    {
        LOG_ERROR << "写入聊天记录失败: " << mysql_error(mysql);
        mysql_query(mysql, "ROLLBACK");
        return false;
    }

    if (mysql_query(mysql, "COMMIT") != 0)
    {
        LOG_ERROR << "提交聊天记录事务失败: " << mysql_error(mysql);
        mysql_query(mysql, "ROLLBACK");
        return false;
    }

    return true;
}

bool createPaperChatSession(MYSQL *mysql,
                            const std::string &paperId,
                            const std::string &userId,
                            std::string &sessionId)
{
    if (mysql_query(mysql, "SELECT UUID()") != 0)
        return false;
    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
        return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    sessionId = row && row[0] ? row[0] : "";
    mysql_free_result(result);
    if (sessionId.empty())
        return false;

    const std::string safeSessionId = escapeSql(mysql, sessionId);
    const std::string sql =
        "INSERT INTO paper_chat_sessions (id, user_id, paper_id) VALUES ('" +
        safeSessionId + "', " + userId + ", " + paperId + ")";
    return mysql_query(mysql, sql.c_str()) == 0;
}

bool validatePaperChatSession(MYSQL *mysql,
                              const std::string &paperId,
                              const std::string &userId,
                              const std::string &sessionId)
{
    const std::string safeSessionId = escapeSql(mysql, sessionId);
    const std::string sql =
        "SELECT id FROM paper_chat_sessions WHERE id = '" + safeSessionId +
        "' AND paper_id = " + paperId + " AND user_id = " + userId + " LIMIT 1";
    if (mysql_query(mysql, sql.c_str()) != 0)
        return false;
    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
        return false;
    const bool found = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    return found;
}
