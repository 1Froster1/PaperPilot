#pragma once

#include <drogon/drogon.h>
#include <functional>
#include <mutex>
#include <string>
#include <mysql.h>

// Shared by route handlers and asynchronous Worker callbacks because the
// application currently owns one MySQL C API connection.
extern std::mutex mysqlMutex;

std::string escapeSql(MYSQL *mysql, const std::string &value);
void markPdfParseFailed(MYSQL *mysql,
                        const std::string &taskId,
                        const std::string &paperId,
                        std::string errorMessage);
void startPaperVectorIndex(MYSQL *mysql,
                           const std::string &taskId,
                           const std::string &paperId,
                           const Json::Value &indexRequest);
void startPdfParseTask(MYSQL *mysql,
                       std::string taskId,
                       std::string paperId,
                       std::string absolutePdfPath);
void sendMessageToAgent(const Json::Value &agentMessages,
                        std::function<void(const Json::Value &)> onSuccess,
                        std::function<void(const std::string &)> onError);
void sendDatabaseError(
    const std::function<void(const drogon::HttpResponsePtr &)> &callback,
    MYSQL *mysql);
bool isPositiveInteger(const std::string &value);
int readPositiveParameter(const drogon::HttpRequestPtr &req,
                          const std::string &name,
                          int defaultValue,
                          int maximum);
std::string columnOrEmpty(MYSQL_ROW row, int index);
void advanceRunningTasks(MYSQL *mysql);

bool savePaperChatTurn(MYSQL *mysql,
                       const std::string &paperId,
                       const std::string &userId,
                       const std::string &question,
                       const Json::Value &workerResult);
