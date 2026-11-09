#pragma once

#include <string>

struct ServerRuntimeConfig
{
    std::string databaseHost;
    unsigned int databasePort{};
    std::string databaseUser;
    std::string databasePassword;
    std::string databaseName;
    std::string agentWorkerBaseUrl;
};

// Must be called once during startup before routes or Worker clients run.
void loadServerRuntimeConfig(const std::string &path);
const ServerRuntimeConfig &serverRuntimeConfig();
