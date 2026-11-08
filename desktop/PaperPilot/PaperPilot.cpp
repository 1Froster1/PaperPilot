#include "PaperPilot.h"
#include "mainfamework.h"

#include "ui_PaperPilot.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrl>

PaperPilot::PaperPilot(QWidget *parent)
    : QMainWindow(parent),
      ui_(new Ui::PaperPilotClass)
{
    // setupUi() 会创建你在 Qt Designer 中拖入和配置的所有控件。
    ui_->setupUi(this);

    // QNetworkAccessManager 负责异步发送 HTTP 请求；把 this 设为父对象，
    // 主窗口销毁时 Qt 会自动回收它。
    networkManager_ = new QNetworkAccessManager(this);

    // 输入密码时显示圆点，而不是明文。
    ui_->line_password->setEchoMode(QLineEdit::Password);

    // Designer 中 objectName 为 pushButton_ok 的“提交”按钮。
    connect(ui_->pushButton_ok, &QPushButton::clicked, this, [this] {
        login();
    });

    // Designer 中 objectName 为 pushButton_quit 的“退出”按钮。
    connect(ui_->pushButton_quit, &QPushButton::clicked, this, &PaperPilot::close);
}

PaperPilot::~PaperPilot()
{
    delete ui_;
}

void PaperPilot::login()
{
    // const QString account = ui_->line_account->text().trimmed();
    // const QString password = ui_->line_password->text();

    const QString account = "123456";
    const QString password = "123456";

    if (account.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("登录失败"),
                             QStringLiteral("账号和密码不能为空。"));
        return;
    }

    // 构造要发给 Drogon 的 JSON 请求体。
    QJsonObject requestBody;
    requestBody["account"] = account;
    requestBody["password"] = password;

    QNetworkRequest request(QUrl("http://127.0.0.1:8848/api/auth/login"));//把这个网址包装成 网络请求
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");//给请求设置 HTTP 请求头：Content-Type: application/json

    // post() 不会卡住界面；网络完成后，下面的 finished 信号才会触发。
   // Qt 发起请求
   //↓ post() 立刻返回 reply
   //↓ 界面仍能正常点击、拖动，不会卡住
   //↓ Drogon 完成登录校验并返回 JSON
   //↓ reply 发出 finished 信号
   //↓ 你写的 connect(... finished...) 代码读取响应
    QNetworkReply *reply = networkManager_->post(
        request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));


    //等 Drogon 返回登录结果后，读取 HTTP 状态和 JSON，再决定弹“成功”“密码错误”还是“服务未启动”。
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        // 即使后端返回 401，Qt 也会把 reply->error() 设为错误。
        // 但这说明 HTTP 请求已到达服务端，仍应读取它返回的 JSON 消息。
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();//读取状态字
        const QByteArray responseBytes = reply->readAll();//读取回应正文
        const QString networkError = reply->errorString();//读取错误信息

        // reply 用完后自动销毁，避免网络请求对象泄漏。
        reply->deleteLater();

        // httpStatus 为 0 才表示没有收到任何 HTTP 响应，例如服务未启动、端口不通。
        if (httpStatus == 0)
        {
            QMessageBox::warning(
                this,
                QStringLiteral("无法登录"),
                QStringLiteral("请求 Drogon 服务失败：%1\n请确认 myapp 正在运行在 127.0.0.1:8848。")
                    .arg(networkError));
            return;
        }

        const QJsonDocument responseDoc = QJsonDocument::fromJson(responseBytes);//转为文档
        const QJsonObject responseBody = responseDoc.object();//取出 JSON 根对象，并保存到 responseBody

        if (responseBody["success"].toBool())
        {
            const QString name = responseBody["user"].toObject()["name"].toString();
            QMessageBox::information(this, QStringLiteral("登录成功"),
                                     QStringLiteral("欢迎，%1！").arg(name));

            // this 是 main() 中栈上的登录主窗口。若先 close()，Qt 会认为
            // "最后一个窗口已关闭"，进而退出事件循环，主界面刚创建就会被结束。
            // 先显示主界面，再隐藏登录界面；主界面关闭时恢复登录页。
            auto *mainWindow = new mainfamework();
            mainWindow->setAttribute(Qt::WA_DeleteOnClose);
            connect(mainWindow, &QObject::destroyed, this, [this] {
                this->show();
            });
            this->hide();
            mainWindow->show();


            return;
        }

        const QString message = responseBody["message"].toString();
        QMessageBox::warning(this,
                             QStringLiteral("登录失败"),
                             message.isEmpty()
                                 ? QStringLiteral("服务器返回 HTTP %1。").arg(httpStatus)
                                 : message);
    });
}
