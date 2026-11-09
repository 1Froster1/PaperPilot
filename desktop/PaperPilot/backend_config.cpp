#include "backend_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString backendBaseUrl()
{
    static const QString configuredUrl = [] {
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath("desktop.local.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QString{};

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return QString{};

        return document.object()["server_base_url"].toString().trimmed()
            .remove(QRegularExpression("/+$"));
    }();

    return configuredUrl;
}
