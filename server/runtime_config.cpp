#include "runtime_config.h"

#include <drogon/drogon.h>

#include <fstream>
#include <stdexcept>

namespace
{
ServerRuntimeConfig config;
bool loaded = false;

std::string requiredString(const Json::Value &object, const char *key)
{
    const auto value = object.get(key, "").asString();
    if (value.empty())
        throw std::runtime_error(std::string("Missing configuration value: ") + key);
    return value;
}
}  // namespace

void loadServerRuntimeConfig(const std::string &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open local server configuration: " + path);

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors))
        throw std::runtime_error("Invalid local server configuration: " + errors);

    const Json::Value database = root["paperpilot_runtime"]["database"];
    const Json::Value worker = root["paperpilot_runtime"]["agent_worker"];

    config.databaseHost = requiredString(database, "host");
    config.databasePort = database.get("port", 3306).asUInt();
    config.databaseUser = requiredString(database, "user");
    // Empty passwords are valid for some local MySQL development setups.
    config.databasePassword = database.get("password", "").asString();
    config.databaseName = requiredString(database, "database");
    config.agentWorkerBaseUrl = requiredString(worker, "base_url");
    while (!config.agentWorkerBaseUrl.empty() &&
           config.agentWorkerBaseUrl.back() == '/')
        config.agentWorkerBaseUrl.pop_back();

    loaded = true;
}

const ServerRuntimeConfig &serverRuntimeConfig()
{
    if (!loaded)
        throw std::runtime_error("Server runtime configuration has not been loaded.");
    return config;
}
