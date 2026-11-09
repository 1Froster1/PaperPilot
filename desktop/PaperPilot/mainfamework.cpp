#include "mainfamework.h"
#include "backend_config.h"
#include "PaperPilot.h"
#include "ui_mainfamework.h"
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>

#include <functional>
#include <utility>

namespace
{
struct PaperInfo
{
    QString id;
    QString title;
    QString authors;
    QString metadata;
    QStringList tags;
    QString status;
    bool favorite{};
};

// QFrame 本身没有 clicked() 信号；这个小类让论文卡片可以被点击，
// 而不必把整张卡片改成按钮。
class ClickableFrame : public QFrame
{
  public:
    explicit ClickableFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClicked;
    std::function<void(const QPoint &)> onContextMenu;

  protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && onClicked)
            onClicked();
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onContextMenu)
        {
            onContextMenu(event->globalPos());
            event->accept();
            return;
        }
        QFrame::contextMenuEvent(event);
    }
};

QLabel *makeTag(const QString &text, QWidget *parent)
{
    auto *tag = new QLabel(text, parent);
    tag->setStyleSheet(
        "background: #e8f1ff; color: #2860c6; border-radius: 6px;"
        "padding: 5px 9px;");
    return tag;
}

QFrame *createPaperCard(const PaperInfo &paper,
                        QWidget *parent,
                        std::function<void()> onClicked,
                        std::function<void(const QPoint &)> onContextMenu,
                        std::function<void()> onFavoriteClicked)
{
    auto *card = new ClickableFrame(parent);
    card->onClicked = std::move(onClicked);
    card->onContextMenu = std::move(onContextMenu);
    card->setMinimumHeight(154);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(
        "QFrame { background: white; border: 1px solid #dce6f4; border-radius: 11px; }"
        "QFrame:hover { border: 1px solid #4d90ff; background: #f8fbff; }");

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(14, 14, 14, 14);
    cardLayout->setSpacing(12);

    auto *pdf = new QLabel("PDF", card);
    pdf->setAlignment(Qt::AlignCenter);
    pdf->setFixedSize(50, 62);
    pdf->setStyleSheet(
        "background: #ffeaec; color: #eb4054; border: 1px solid #ffd0d5;"
        "border-radius: 7px; font-weight: 700;");
    cardLayout->addWidget(pdf);

    auto *detailsLayout = new QVBoxLayout;
    detailsLayout->setSpacing(5);

    auto *title = new QLabel(paper.title, card);
    title->setWordWrap(true);
    title->setStyleSheet("color: #14234a; font-size: 17px; font-weight: 700; border: none;");
    detailsLayout->addWidget(title);

    auto *authors = new QLabel(paper.authors, card);
    authors->setStyleSheet("color: #667897; border: none;");
    detailsLayout->addWidget(authors);

    auto *metadata = new QLabel(paper.metadata, card);
    metadata->setStyleSheet("color: #8a99b1; border: none;");
    detailsLayout->addWidget(metadata);

    auto *tagsLayout = new QHBoxLayout;
    tagsLayout->setSpacing(7);
    for (const auto &tagText : paper.tags)
        tagsLayout->addWidget(makeTag(tagText, card));
    tagsLayout->addStretch();
    detailsLayout->addLayout(tagsLayout);

    cardLayout->addLayout(detailsLayout, 1);

    auto *rightLayout = new QVBoxLayout;
    auto *favorite = new QToolButton(card);
    favorite->setText(paper.favorite ? "★" : "☆");
    favorite->setToolTip(paper.favorite ? "取消收藏" : "收藏论文");
    favorite->setCursor(Qt::PointingHandCursor);
    favorite->setAutoRaise(true);
    favorite->setStyleSheet(
        QString("QToolButton { color: %1; font-size: 27px; border: none; }")
            .arg(paper.favorite ? "#2f7df4" : "#b9c6d9"));
    QObject::connect(favorite, &QToolButton::clicked, favorite,
                     [onFavoriteClicked = std::move(onFavoriteClicked)] {
                         if (onFavoriteClicked)
                             onFavoriteClicked();
                     });
    rightLayout->addWidget(favorite);
    rightLayout->addStretch();

    auto *status = new QLabel(paper.status, card);
    status->setStyleSheet(
        paper.status == "解析中"
            ? "background: #e8f1ff; color: #216df3; border: none; border-radius: 6px; padding: 5px 8px; font-weight: 600;"
            : "background: #dcf7e9; color: #159560; border: none; border-radius: 6px; padding: 5px 8px; font-weight: 600;");
    rightLayout->addWidget(status);
    cardLayout->addLayout(rightLayout);

    return card;
}

QWidget *createChatRow(const QString &text, bool fromUser, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *bubble = new QLabel(text, row);
    // 思考气泡需要通过 objectName 找到并由 QTimer 动态更新文字。
    bubble->setObjectName("chatBubble");
    bubble->setWordWrap(true);
    bubble->setMaximumWidth(480);
    bubble->setContentsMargins(12, 9, 12, 9);
    bubble->setStyleSheet(fromUser
                              ? "background: #deecff; color: #203861; border-radius: 10px;"
                              : "background: #f1f5fa; color: #26385e; border-radius: 10px;");

    if (fromUser)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
    }
    else
    {
        auto *bot = new QLabel("AI", row);
        bot->setAlignment(Qt::AlignCenter);
        bot->setFixedSize(30, 30);
        bot->setStyleSheet(
            "background: #216df3; color: white; border-radius: 15px; font-weight: 700;");
        rowLayout->addWidget(bot, 0, Qt::AlignTop);
        rowLayout->addWidget(bubble);
        rowLayout->addStretch();
    }
    return row;
}
} // namespace

mainfamework::mainfamework(QWidget *parent)
    : QWidget(parent), ui(new Ui::mainfamework)
{
    ui->setupUi(this);

    // 主页面也要有自己的网络管理器，用于请求 GET /api/papers。
    // this 是父对象，主窗口销毁时 Qt 会自动回收它。
    networkManager_ = new QNetworkAccessManager(this);

    // 将四个可选导航按钮放入互斥组：同一时刻只能高亮一个。
    auto *navigationGroup = new QButtonGroup(this);
    navigationGroup->setExclusive(true);
    navigationGroup->addButton(ui->navLibraryButton);
    navigationGroup->addButton(ui->navAiButton);
    navigationGroup->addButton(ui->navNotesButton);
    navigationGroup->addButton(ui->navTasksButton);

    // Designer 中原先的 layoutWidget 使用固定 geometry，窗口放大时不会跟着扩张。
    // 重新交给布局管理后，左右两个区域会根据 5:7 的比例自动拉伸。
    makeContentResponsive();
    buildChatDemo();

    ui->lineEdit->setPlaceholderText("搜索论文标题、作者或关键词...");
    setupLibraryControls();
    loadPapers();

    // 这个数字保留为计时器示例：每秒加一，显示程序已运行的秒数。
    ui->lcdNumber->display(0);

    timer = new QTimer(this);

    connect(timer, &QTimer::timeout, this, &mainfamework::handle);

    timer->start(1000);
}

void mainfamework::setupLibraryControls()
{
    // 将显示给用户的文字和真正发给后端的参数分开保存。
    ui->comboBox->clear();
    ui->comboBox->addItem("全部领域", "");
    ui->comboBox->addItem("NLP", "NLP");
    ui->comboBox->addItem("LLM", "LLM");
    ui->comboBox->addItem("Transformer", "Transformer");
    ui->comboBox->addItem("RAG", "RAG");

    ui->comboBox_2->clear();
    ui->comboBox_2->addItem("全部年份", "");
    for (int year = QDate::currentDate().year(); year >= 2017; --year)
        ui->comboBox_2->addItem(QString::number(year), QString::number(year));

    ui->comboBox_3->clear();
    ui->comboBox_3->addItem("排序：最新", "newest");
    ui->comboBox_3->addItem("排序：最早", "oldest");

    // 输入时稍等 350ms 再请求，避免每敲一个字都访问一次服务端。
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(350);
    connect(searchTimer_, &QTimer::timeout, this, &mainfamework::loadPapers);
    connect(ui->lineEdit, &QLineEdit::textChanged, this, [this] {
        searchTimer_->start();
    });

    const auto reload = [this] { loadPapers(); };
    connect(ui->comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, reload);
    connect(ui->comboBox_2, QOverload<int>::of(&QComboBox::currentIndexChanged), this, reload);
    connect(ui->comboBox_3, QOverload<int>::of(&QComboBox::currentIndexChanged), this, reload);
}

void mainfamework::updateTaskCard(const QString &title,
                                  const QString &status,
                                  int progress)
{
    ui->taskTitle->setText(title);
    ui->taskHint->setText(status);
    ui->taskProgressBar->setValue(qBound(0, progress, 100));
    ui->taskHint2->setText(QString("%1 · %2%").arg(status).arg(progress));
}

void mainfamework::makeContentResponsive()
{
    // 保留 Designer 创建的 layoutWidget、frame、frame_2 的父子关系。
    // 以前这里会把 frame 移出 layoutWidget 后再 delete layoutWidget；Qt 的布局
    // 仍可能持有旧 QWidgetItem，从而在登录后加载主界面时造成堆内存损坏。
    // 只需要给 ui->widget 加一层布局，layoutWidget 的 geometry 就会自动跟随窗口变化。
    auto *filterRow = ui->layoutWidget1;

    auto *rootContentLayout = new QVBoxLayout(ui->widget);
    rootContentLayout->setContentsMargins(10, 1, 10, 1);
    rootContentLayout->addWidget(ui->layoutWidget);

    // Designer 中本来就有横向布局，只需要设置两列的拉伸比例即可。
    ui->horizontalLayout->setSpacing(16);
    ui->horizontalLayout->setStretch(0, 5);
    ui->horizontalLayout->setStretch(1, 7);

    ui->frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->frame_2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 论文区原来的子控件也改为垂直布局管理，搜索框和滚动区会自动撑满宽度。
    auto *papersLayout = new QVBoxLayout(ui->frame);
    papersLayout->setContentsMargins(18, 16, 18, 16);
    papersLayout->setSpacing(10);
    papersLayout->addWidget(ui->brandTitle_2);
    papersLayout->addWidget(ui->lineEdit);
    papersLayout->addWidget(filterRow);
    papersLayout->addWidget(ui->scrollArea, 1);

    ui->lineEdit->setMinimumHeight(38);
}

void mainfamework::buildChatDemo()
{
    auto *chatLayout = new QVBoxLayout(ui->frame_2);
    chatLayout->setContentsMargins(22, 18, 22, 18);
    chatLayout->setSpacing(10);

    detailTitle_ = new QLabel("请选择一篇论文", ui->frame_2);
    detailTitle_->setWordWrap(true);
    detailTitle_->setStyleSheet("color: #14234a; font-size: 23px; font-weight: 700;");
    chatLayout->addWidget(detailTitle_);

    detailMeta_ = new QLabel("从左侧论文库选择论文后，可在这里查看信息并提问。", ui->frame_2);
    detailMeta_->setWordWrap(true);
    detailMeta_->setStyleSheet("color: #7485a2;");
    chatLayout->addWidget(detailMeta_);

    detailStatus_ = new QLabel("未选择论文", ui->frame_2);
    detailStatus_->setStyleSheet(
        "background: #e8f1ff; color: #216df3; border-radius: 6px; padding: 5px 9px;");
    chatLayout->addWidget(detailStatus_, 0, Qt::AlignLeft);

    auto *abstractTitle = new QLabel("论文摘要", ui->frame_2);
    abstractTitle->setStyleSheet(
        "color: #26385e; font-weight: 700; margin-top: 6px;");

    detailAbstract_ = new QLabel(
        "选择论文后，这里会显示数据库中的摘要内容。",
        ui->frame_2);

    detailAbstract_->setWordWrap(true);
    detailAbstract_->setMaximumHeight(90);
    detailAbstract_->setStyleSheet(
        "color: #5f708d; background: #f5f8fc;"
        "border-radius: 8px; padding: 10px;");

    chatLayout->addWidget(abstractTitle);
    chatLayout->addWidget(detailAbstract_);


    auto *tabLine = new QFrame(ui->frame_2);
    tabLine->setStyleSheet("border-bottom: 1px solid #dfe7f2;");
    auto *tabLayout = new QHBoxLayout(tabLine);
    tabLayout->setContentsMargins(0, 8, 0, 8);
    auto *overview = new QLabel("▤  概览", tabLine);
    overview->setStyleSheet("color: #70809c; padding: 6px 18px;");
    auto *aiTab = new QLabel("●  AI 对话", tabLine);
    aiTab->setStyleSheet(
        "color: #216df3; font-weight: 700; border-bottom: 3px solid #216df3; padding: 6px 18px;");
    auto *notes = new QLabel("▤  笔记", tabLine);
    notes->setStyleSheet("color: #70809c; padding: 6px 18px;");
    tabLayout->addWidget(overview);
    tabLayout->addWidget(aiTab);
    tabLayout->addWidget(notes);
    tabLayout->addStretch();
    chatLayout->addWidget(tabLine);

    auto *chatScroll = new QScrollArea(ui->frame_2);
    chatScroll->setWidgetResizable(true);
    chatScroll->setFrameShape(QFrame::NoFrame);
    auto *messageArea = new QWidget;
    chatMessagesLayout_ = new QVBoxLayout(messageArea);
    chatMessagesLayout_->setContentsMargins(2, 8, 2, 8);
    chatMessagesLayout_->setSpacing(14);
    chatMessagesLayout_->addStretch();
    chatScroll->setWidget(messageArea);
    chatLayout->addWidget(chatScroll, 1);

    // 新消息插入布局后，等 Qt 完成一次布局刷新再滚到底部；否则 maximum()
    // 仍可能是旧值，长回答时会停在倒数第二条消息。
    const auto scrollChatToBottom = [chatScroll] {
        QTimer::singleShot(0, chatScroll, [chatScroll] {
            QScrollBar *scrollBar = chatScroll->verticalScrollBar();
            scrollBar->setValue(scrollBar->maximum());
        });
    };

    auto *inputRow = new QHBoxLayout;
    chatInput_ = new QLineEdit(ui->frame_2);
    chatInput_->setPlaceholderText("向 AI 询问这篇论文...");
    chatInput_->setMinimumHeight(42);
    chatInput_->setStyleSheet(
        "border: 1px solid #d5e1f0; border-radius: 8px; padding: 0 10px; background: white;");

    auto *modeSelector = new QComboBox(ui->frame_2);
    modeSelector->setMinimumHeight(42);
    modeSelector->setMinimumWidth(118);
    modeSelector->addItem("论文问答", "qa");
    modeSelector->addItem("论文总结", "summary");
    modeSelector->addItem("阅读笔记", "note");
    modeSelector->addItem("批判性阅读", "critique");
    modeSelector->setToolTip("选择本轮 AI 对话模式");
    modeSelector->setStyleSheet(
        "QComboBox { border: 1px solid #d5e1f0; border-radius: 8px; "
        "padding: 0 8px; background: white; color: #26385e; }"
        "QComboBox::drop-down { border: none; width: 24px; }");

    auto *sendButton = new QPushButton("➤  发送", ui->frame_2);
    sendButton->setMinimumSize(94, 42);
    sendButton->setStyleSheet(
        "QPushButton { background: #216df3; color: white; border: none; border-radius: 8px; font-weight: 700; }"
        "QPushButton:hover { background: #155bd2; }");
    inputRow->addWidget(chatInput_, 1);
    inputRow->addWidget(modeSelector);
    inputRow->addWidget(sendButton);
    chatLayout->addLayout(inputRow);

    connect(sendButton, &QPushButton::clicked, this,
            [this, messageArea, modeSelector, sendButton, scrollChatToBottom] {
        const QString question = chatInput_->text().trimmed();
        if (question.isEmpty() || currentPaperId_.isEmpty())
            return;
        chatMessagesLayout_->insertWidget(chatMessagesLayout_->count() - 1,
                                          createChatRow(question, true, messageArea));

        // 请求等待期间先显示思考气泡，让用户知道问题已经发出。
        auto *thinkingRow = createChatRow("AI 正在思考", false, messageArea);
        auto *thinkingBubble =
            thinkingRow->findChild<QLabel *>("chatBubble");
        auto *thinkingTimer = new QTimer(thinkingRow);
        thinkingTimer->setProperty("dotCount", 0);
        connect(thinkingTimer, &QTimer::timeout, thinkingRow,
                [thinkingTimer, thinkingBubble] {
                    const int dots = (thinkingTimer->property("dotCount").toInt() + 1) % 4;
                    thinkingTimer->setProperty("dotCount", dots);
                    thinkingBubble->setText(
                        QStringLiteral("AI 正在思考") + QString(dots, QLatin1Char('.')));
                });
        thinkingTimer->start(400);
        chatMessagesLayout_->insertWidget(chatMessagesLayout_->count() - 1, thinkingRow);
        scrollChatToBottom();

        // 同一时间只发一个问题，避免回答和思考气泡的顺序混乱。
        sendButton->setEnabled(false);
        chatInput_->setEnabled(false);
        modeSelector->setEnabled(false);

        QNetworkRequest request(QUrl(backendBaseUrl() + "/api/papers/" +
                                     currentPaperId_ +
                                     "/chat"));//把这个网址包装成 网络请求
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");//给请求设置 HTTP 请求头：Content-Type: application/json

        QJsonObject requestBody;
        requestBody["question"] = question;
        requestBody["mode"] = modeSelector->currentData().toString();
        if (chatSessionPaperId_ != currentPaperId_)
        {
            chatSessionPaperId_ = currentPaperId_;
            chatSessionId_.clear();
        }
        if (!chatSessionId_.isEmpty())
            requestBody["session_id"] = chatSessionId_;


        QNetworkReply *reply = networkManager_->post(
            request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));

        connect(reply, &QNetworkReply::finished, this,
                [this, reply, messageArea, modeSelector, thinkingRow, thinkingTimer, sendButton,
                 scrollChatToBottom] {
                    // 不论成功、网络失败还是 JSON 失败，都先移除思考气泡并恢复输入。
                    thinkingTimer->stop();
                    chatMessagesLayout_->removeWidget(thinkingRow);
                    thinkingRow->deleteLater();
                    sendButton->setEnabled(true);
                    chatInput_->setEnabled(true);
                    modeSelector->setEnabled(true);

                    const int httpStatus =
                        reply->attribute(
                                 QNetworkRequest::HttpStatusCodeAttribute).toInt();

                    const QByteArray responseBytes = reply->readAll();
                    const QString networkError = reply->errorString();

                            // 先删除网络回复对象，避免内存泄漏。
                    reply->deleteLater();

                    if (httpStatus == 0)
                    {
                        chatMessagesLayout_->insertWidget(
                            chatMessagesLayout_->count() - 1,
                            createChatRow(
                                "无法连接 C++ 后端：" + networkError,
                                false,
                                messageArea));
                        scrollChatToBottom();
                        return;
                    }

                    const QJsonDocument responseDoc =
                        QJsonDocument::fromJson(responseBytes);

                    if (!responseDoc.isObject())
                    {
                        chatMessagesLayout_->insertWidget(
                            chatMessagesLayout_->count() - 1,
                            createChatRow(
                                "服务端返回的数据不是合法 JSON。",
                                false,
                                messageArea));
                        scrollChatToBottom();
                        return;
                    }

                    const QJsonObject responseBody = responseDoc.object();

                            // Agent 回答就在 answer 字段。
                    const QString answer = responseBody["answer"].toString();

                    if (responseBody["success"].toBool() && !answer.isEmpty())
                    {
                        const QString returnedSessionId =
                            responseBody["session_id"].toString();
                        if (!returnedSessionId.isEmpty())
                            chatSessionId_ = returnedSessionId;

                        chatMessagesLayout_->insertWidget(
                            chatMessagesLayout_->count() - 1,
                            createChatRow(answer, false, messageArea));

                        const QJsonArray externalReferences =
                            responseBody["external_references"].toArray();
                        if (!externalReferences.isEmpty())
                        {
                            QStringList lines;
                            lines << QStringLiteral("OpenAlex 外部文献：");
                            for (int index = 0; index < externalReferences.size(); ++index)
                            {
                                const QJsonObject reference =
                                    externalReferences.at(index).toObject();
                                const QJsonArray authors =
                                    reference["authors"].toArray();
                                QStringList authorNames;
                                for (const QJsonValue &author : authors)
                                    authorNames << author.toString();
                                lines << QStringLiteral("【外部文献 %1】%2（%3，%4）")
                                             .arg(index + 1)
                                             .arg(reference["title"].toString())
                                             .arg(authorNames.join(QStringLiteral(", ")))
                                             .arg(reference["publication_year"].toVariant().toString());
                            }
                            chatMessagesLayout_->insertWidget(
                                chatMessagesLayout_->count() - 1,
                                createChatRow(lines.join('\n'), false, messageArea));
                        }

                        const QJsonArray webSources =
                            responseBody["web_sources"].toArray();
                        if (!webSources.isEmpty())
                        {
                            QStringList lines;
                            lines << QStringLiteral("网络网页来源：");
                            for (int index = 0; index < webSources.size(); ++index)
                            {
                                const QJsonObject source =
                                    webSources.at(index).toObject();
                                lines << QStringLiteral("【网页来源 %1】%2\n%3")
                                             .arg(index + 1)
                                             .arg(source["title"].toString())
                                             .arg(source["url"].toString());
                            }
                            chatMessagesLayout_->insertWidget(
                                chatMessagesLayout_->count() - 1,
                                createChatRow(lines.join('\n'), false, messageArea));
                        }
                        scrollChatToBottom();
                        return;
                    }

                            // C++ 或 Python Agent 出错时，读取 message 字段。
                    const QString errorMessage =
                        responseBody["message"].toString();

                    chatMessagesLayout_->insertWidget(
                        chatMessagesLayout_->count() - 1,
                        createChatRow(
                            errorMessage.isEmpty()
                                ? QStringLiteral("AI 暂时无法回答该问题。")
                                : errorMessage,
                            false,
                            messageArea));
                    scrollChatToBottom();
                });
        chatInput_->clear();
    });
}

void mainfamework::loadPapers()
{
    // 清空原有布局
    while (QLayoutItem *item = ui->paperListLayout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->deleteLater();
        }

        delete item;
    }

    QUrl url(backendBaseUrl() + "/api/papers");
    QUrlQuery query;
    query.addQueryItem("page", "1");
    query.addQueryItem("pageSize", "50");

    const QString keyword = ui->lineEdit->text().trimmed();
    if (!keyword.isEmpty())
        query.addQueryItem("keyword", keyword);

    const QString field = ui->comboBox->currentData().toString();
    if (!field.isEmpty())
        query.addQueryItem("field", field);

    const QString year = ui->comboBox_2->currentData().toString();
    if (!year.isEmpty())
        query.addQueryItem("year", year);

    query.addQueryItem("sort", ui->comboBox_3->currentData().toString());
    url.setQuery(query);
    QNetworkRequest request(url);

    QNetworkReply *reply = networkManager_->get(request);

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
                QStringLiteral("无法连接"),
            QStringLiteral("请求 Drogon 服务失败：%1\n请确认后端服务正在运行。")
                    .arg(networkError));
            return;
        }

        const QJsonDocument responseDoc = QJsonDocument::fromJson(responseBytes);//转为文档
        const QJsonObject responseBody = responseDoc.object();//取出 JSON 根对象，并保存到 responseBody

        if (responseBody["success"].toBool())
        {
            // 后端的 items 是论文数组；数组中的每个对象对应一张论文卡片。
            const QJsonArray papers = responseBody["items"].toArray();

            for (const QJsonValue &value : papers)
            {
                const QJsonObject paperJson = value.toObject();

                PaperInfo paper;

                // JSON 字段 → C++ PaperInfo 字段
                paper.id = paperJson["id"].toVariant().toString();
                paper.title = paperJson["title"].toString();
                paper.authors = paperJson["authors"].toString();

                const int year = paperJson["publishYear"].toInt();
                const QString source = paperJson["source"].toString();
                paper.metadata = year > 0
                    ? QString("%1 · %2").arg(year).arg(source)
                    : source;

                // JSON 的 tags 数组 → QStringList
                const QJsonArray tags = paperJson["tags"].toArray();
                for (const QJsonValue &tag : tags)
                    paper.tags.append(tag.toString());

                // 后端状态 → 界面显示文字
                const QString status = paperJson["status"].toString();

                if (status == "ready")
                    paper.status = "已解析";
                else if (status == "parsing")
                    paper.status = "解析中";
                else if (status == "uploaded")
                    paper.status = "等待解析";
                else
                    paper.status = "解析失败";

                paper.favorite = paperJson["favorite"].toBool();

                // 用真实数据生成一张论文卡片。
                ui->paperListLayout->addWidget(
                    createPaperCard(paper,
                                    ui->scrollAreaWidgetContents,
                                    [this, paper] {
                                        // 点击卡片时先立即显示列表中的基础信息，
                                        // 再异步请求完整摘要与标签。
                                        currentPaperId_ = paper.id;
                                        currentPaperTitle_ = paper.title;
                                        detailTitle_->setText(paper.title);
                                        detailMeta_->setText(
                                            QString("%1\n%2")
                                                .arg(paper.authors.isEmpty()
                                                         ? "作者信息待补充"
                                                         : paper.authors)
                                                .arg(paper.metadata));
                                        detailStatus_->setText(paper.status);
                                        chatInput_->setPlaceholderText(
                                            QString("向 AI 询问《%1》...").arg(paper.title));
                                        loadPaperDetail(paper.id);
                                    },
                                    [this, paper](const QPoint &globalPos) {
                                        QMenu menu(this);
                                        QAction *deleteAction = menu.addAction("删除论文");
                                        QAction *selectedAction = menu.exec(globalPos);
                                        if (selectedAction == deleteAction)
                                            deletePaper(paper.id, paper.title);
                                    },
                                    [this, paper] {
                                        toggleFavorite(paper.id, !paper.favorite);
                                    }));
            }

            if (papers.isEmpty())
            {
                auto *empty = new QLabel("没有找到符合条件的论文\n可调整搜索词或筛选条件后重试。",
                                         ui->scrollAreaWidgetContents);
                empty->setAlignment(Qt::AlignCenter);
                empty->setStyleSheet("color: #71809d; padding: 48px 10px;");
                ui->paperListLayout->addWidget(empty);
            }

            // 让所有卡片或空状态提示贴在顶部。
            ui->paperListLayout->addStretch();

            return;
        }


        const QString message = responseBody["message"].toString();
        QMessageBox::warning(this,
                             QStringLiteral("连接失败"),
                             message.isEmpty()
                                 ? QStringLiteral("服务器返回 HTTP %1。").arg(httpStatus)
                                 : message);
    });

}

void mainfamework::loadPaperDetail(const QString &paperId)
{
    QNetworkRequest request(
        QUrl(backendBaseUrl() + QStringLiteral("/api/papers/") + paperId));

    QNetworkReply *reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, paperId] {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QByteArray responseBytes = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        // 用户可能在这次请求尚未返回时又点击了另一篇论文。
        // 此时不能让较旧的响应覆盖右侧当前选中的论文详情。
        if (paperId != currentPaperId_)
            return;

                // httpStatus 为 0：请求根本没有到达服务端。
        if (httpStatus == 0)
        {
            detailStatus_->setText("详情请求失败");
            detailAbstract_->setText(
                QString("无法连接后端：%1").arg(networkError));
            return;
        }

        const QJsonDocument document =
            QJsonDocument::fromJson(responseBytes);

        const QJsonObject body = document.object();

        if (!body["success"].toBool())
        {
            detailStatus_->setText("获取详情失败");
            detailAbstract_->setText(
                body["message"].toString("论文详情不存在。"));
            return;
        }

                // 后端约定：完整论文数据在 item 对象中。
        const QJsonObject item = body["item"].toObject();

        const QString title = item["title"].toString();
        const QString authors = item["authors"].toString();
        const int year = item["publishYear"].toInt();
        const QString source = item["source"].toString();
        const QString abstractText = item["abstract"].toString();
        const QString status = item["status"].toString();

                // tags 是 JSON 数组，转换为适合 QLabel 显示的一行文字。
        QStringList tags;
        for (const QJsonValue &tag : item["tags"].toArray())
        {
            tags.append(tag.toString());
        }

        detailTitle_->setText(title);

        detailMeta_->setText(
            QString("%1\n%2 · %3\n标签：%4")
                .arg(authors.isEmpty() ? "作者信息待补充" : authors)
                .arg(year > 0 ? QString::number(year) : "年份未知")
                .arg(source.isEmpty() ? "来源未知" : source)
                .arg(tags.isEmpty() ? "暂无标签" : tags.join("、")));

        if (status == "ready")
            detailStatus_->setText("已解析");
        else if (status == "parsing")
            detailStatus_->setText("解析中");
        else if (status == "uploaded")
            detailStatus_->setText("等待解析");
        else
            detailStatus_->setText("解析失败");

                // parsing 阶段数据库可能还没有 abstract_text。
        detailAbstract_->setText(
            abstractText.isEmpty()
                ? (status == "ready"
                       ? "暂未从 PDF 中可靠提取到摘要，可直接向 AI 提问论文内容。"
                       : "该论文仍在解析中，完成后会在这里显示摘要。")
                : abstractText);

        chatInput_->setPlaceholderText(
            QString("向 AI 询问《%1》...").arg(title));
    });
}

void mainfamework::showNotesDialog()
{
    if (currentPaperId_.isEmpty())
    {
        QMessageBox::information(this,
                                 QStringLiteral("阅读笔记"),
                                 QStringLiteral("请先在论文库中点击选择一篇论文。"));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("阅读笔记 - %1").arg(currentPaperTitle_));
    dialog->resize(820, 520);

    auto *layout = new QVBoxLayout(dialog);
    auto *title = new QLabel(QStringLiteral("《%1》的阅读笔记").arg(currentPaperTitle_), dialog);
    title->setStyleSheet("font-size: 18px; font-weight: 700; color: #14234a;");
    layout->addWidget(title);

    auto *contentRow = new QHBoxLayout;
    auto *noteList = new QListWidget(dialog);
    noteList->setMinimumWidth(240);
    noteList->setToolTip(QStringLiteral("选择已有笔记即可编辑；点击新建可创建一条笔记。"));
    auto *editor = new QTextEdit(dialog);
    editor->setPlaceholderText(QStringLiteral("记录你的阅读思考、关键结论或待解决问题…"));
    contentRow->addWidget(noteList, 1);
    contentRow->addWidget(editor, 3);
    layout->addLayout(contentRow, 1);

    auto *buttons = new QDialogButtonBox(dialog);
    auto *newButton = buttons->addButton(QStringLiteral("新建笔记"), QDialogButtonBox::ActionRole);
    auto *saveButton = buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    auto *deleteButton = buttons->addButton(QStringLiteral("删除笔记"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    deleteButton->setEnabled(false);
    layout->addWidget(buttons);

    // 这个可复用函数会重新读取列表；保存、删除成功后都调用它刷新界面。
    const QString paperId = currentPaperId_;
    auto refreshNotes = std::make_shared<std::function<void()>>();
    *refreshNotes = [this, dialog, paperId, noteList, editor, deleteButton] {
        QNetworkRequest request(
            QUrl(backendBaseUrl() + QStringLiteral("/api/papers/") +
                 paperId + QStringLiteral("/notes")));
        QNetworkReply *reply = networkManager_->get(request);
        connect(reply, &QNetworkReply::finished, dialog, [noteList, editor, deleteButton, reply] {
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (httpStatus != 200 || !body["success"].toBool())
                return;

            noteList->clear();
            editor->clear();
            deleteButton->setEnabled(false);
            for (const QJsonValue &value : body["items"].toArray())
            {
                const QJsonObject note = value.toObject();
                const QString content = note["content"].toString();
                const QString preview = content.left(24).replace('\n', ' ');
                auto *item = new QListWidgetItem(
                    QStringLiteral("%1\n%2")
                        .arg(preview.isEmpty() ? QStringLiteral("未命名笔记") : preview)
                        .arg(note["updatedAt"].toString()),
                    noteList);
                item->setData(Qt::UserRole, note["id"].toString());
                item->setData(Qt::UserRole + 1, content);
            }
        });
    };

    connect(noteList, &QListWidget::currentItemChanged,
            dialog,
            [editor, deleteButton](QListWidgetItem *current, QListWidgetItem *) {
                if (current == nullptr)
                {
                    editor->clear();
                    deleteButton->setEnabled(false);
                    return;
                }
                editor->setPlainText(current->data(Qt::UserRole + 1).toString());
                deleteButton->setEnabled(true);
            });

    connect(newButton, &QPushButton::clicked, dialog, [noteList, editor, deleteButton] {
        noteList->clearSelection();
        editor->clear();
        editor->setFocus();
        deleteButton->setEnabled(false);
    });

    connect(saveButton, &QPushButton::clicked, dialog,
            [this, dialog, paperId, noteList, editor, refreshNotes] {
                const QString content = editor->toPlainText().trimmed();
                if (content.isEmpty())
                {
                    QMessageBox::information(this,
                                             QStringLiteral("阅读笔记"),
                                             QStringLiteral("笔记内容不能为空。"));
                    return;
                }

                QJsonObject requestBody;
                requestBody["content"] = content;
                QNetworkRequest request;
                const auto *selectedItem = noteList->currentItem();
                const bool isUpdate = selectedItem != nullptr;
                if (isUpdate)
                    request.setUrl(QUrl(backendBaseUrl() + QStringLiteral("/api/notes/") +
                                        selectedItem->data(Qt::UserRole).toString()));
                else
                    request.setUrl(QUrl(backendBaseUrl() + QStringLiteral("/api/papers/") +
                                        paperId + QStringLiteral("/notes")));
                request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

                QNetworkReply *reply = isUpdate
                    ? networkManager_->sendCustomRequest(
                          request, "PUT", QJsonDocument(requestBody).toJson(QJsonDocument::Compact))
                    : networkManager_->post(
                          request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
                connect(reply, &QNetworkReply::finished, dialog, [this, reply, refreshNotes] {
                    const int httpStatus =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
                    reply->deleteLater();
                    if (httpStatus < 200 || httpStatus >= 300 || !body["success"].toBool())
                    {
                        QMessageBox::warning(this,
                                             QStringLiteral("保存失败"),
                                             body["message"].toString(QStringLiteral("无法保存笔记。")));
                        return;
                    }
                    (*refreshNotes)();
                });
            });

    connect(deleteButton, &QPushButton::clicked, dialog,
            [this, dialog, noteList, refreshNotes] {
                auto *selectedItem = noteList->currentItem();
                if (selectedItem == nullptr)
                    return;
                if (QMessageBox::question(this,
                                          QStringLiteral("删除笔记"),
                                          QStringLiteral("确定删除当前笔记吗？"),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No) != QMessageBox::Yes)
                    return;

                QNetworkRequest request(
                    QUrl(backendBaseUrl() + QStringLiteral("/api/notes/") +
                         selectedItem->data(Qt::UserRole).toString()));
                QNetworkReply *reply = networkManager_->deleteResource(request);
                connect(reply, &QNetworkReply::finished, dialog, [this, reply, refreshNotes] {
                    const int httpStatus =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
                    reply->deleteLater();
                    if (httpStatus < 200 || httpStatus >= 300 || !body["success"].toBool())
                    {
                        QMessageBox::warning(this,
                                             QStringLiteral("删除失败"),
                                             body["message"].toString(QStringLiteral("无法删除笔记。")));
                        return;
                    }
                    (*refreshNotes)();
                });
            });

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    (*refreshNotes)();
    dialog->show();
}

void mainfamework::deletePaper(const QString &paperId, const QString &title)
{
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("删除论文"),
        QStringLiteral("确定删除《%1》吗？\n论文的标签、笔记和解析任务也会一并删除。")
            .arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    QNetworkRequest request(
        QUrl(backendBaseUrl() + QStringLiteral("/api/papers/") + paperId));
    QNetworkReply *reply = networkManager_->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseBytes = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        const QJsonObject body = QJsonDocument::fromJson(responseBytes).object();
        if (httpStatus < 200 || httpStatus >= 300 || !body["success"].toBool())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("删除失败"),
                body["message"].toString(
                    QStringLiteral("无法删除论文：%1").arg(networkError)));
            return;
        }

        // 当前选中的论文可能刚被删除，右侧恢复为默认提示。
        detailTitle_->setText(QStringLiteral("请选择一篇论文"));
        detailMeta_->setText(QStringLiteral("从左侧论文库选择论文后，可在这里查看信息并提问。"));
        detailStatus_->setText(QStringLiteral("未选择论文"));
        detailAbstract_->setText(QStringLiteral("选择论文后，这里会显示数据库中的摘要内容。"));
        chatInput_->setPlaceholderText(QStringLiteral("向 AI 询问这篇论文..."));

        loadPapers();
    });
}

void mainfamework::toggleFavorite(const QString &paperId, bool favorite)
{
    QJsonObject requestBody;
    requestBody["favorite"] = favorite;
    QNetworkRequest request(
        QUrl(backendBaseUrl() + QStringLiteral("/api/papers/") +
             paperId + QStringLiteral("/favorite")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager_->sendCustomRequest(
        request, "PATCH", QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (httpStatus < 200 || httpStatus >= 300 || !body["success"].toBool())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("收藏操作失败"),
                body["message"].toString(
                    QStringLiteral("无法更新收藏状态：%1").arg(errorText)));
            return;
        }
        loadPapers();
    });
}

void mainfamework::showTasksDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("任务中心"));
    dialog->resize(900, 460);

    auto *layout = new QVBoxLayout(dialog);
    auto *hint = new QLabel(
        QStringLiteral("显示论文导入和模拟解析任务；列表会每两秒自动刷新。"), dialog);
    hint->setStyleSheet("color: #71809d;");
    layout->addWidget(hint);

    auto *table = new QTableWidget(dialog);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("任务 ID"), QStringLiteral("论文"), QStringLiteral("状态"),
         QStringLiteral("进度"), QStringLiteral("解析结果"), QStringLiteral("更新时间")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    // shared_ptr 本身是一个智能指针对象，不能写成 auto *。
    // 它让下面的 lambda 可以安全地引用同一个“刷新任务列表”的函数。
    auto refreshTasks = std::make_shared<std::function<void()>>();
    *refreshTasks = [this, table] {
        QNetworkRequest request(QUrl(backendBaseUrl() + QStringLiteral("/api/tasks")));
        QNetworkReply *reply = networkManager_->get(request);
        connect(reply, &QNetworkReply::finished, this, [table, reply] {
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (httpStatus != 200 || !body["success"].toBool())
                return;

            const QJsonArray tasks = body["items"].toArray();
            table->setRowCount(tasks.size());
            for (int row = 0; row < tasks.size(); ++row)
            {
                const QJsonObject task = tasks[row].toObject();
                const QString status = task["status"].toString();
                const QString result = task["errorMessage"].toString().isEmpty()
                    ? task["resultText"].toString()
                    : task["errorMessage"].toString();
                const QString paperTitle = task["paperTitle"].toString().isEmpty()
                    ? QStringLiteral("已删除论文")
                    : task["paperTitle"].toString();
                const QList<QString> values{
                    task["id"].toString(),
                    paperTitle,
                    status,
                    QString::number(task["progress"].toInt()) + "%",
                    result,
                    task["updatedAt"].toString()};
                for (int column = 0; column < values.size(); ++column)
                    table->setItem(row, column, new QTableWidgetItem(values[column]));
            }
        });
    };

    auto *refreshButton = new QPushButton(QStringLiteral("立即刷新"), dialog);
    layout->addWidget(refreshButton, 0, Qt::AlignRight);
    connect(refreshButton, &QPushButton::clicked, dialog, [refreshTasks] { (*refreshTasks)(); });

    auto *refreshTimer = new QTimer(dialog);
    connect(refreshTimer, &QTimer::timeout, dialog, [refreshTasks] { (*refreshTasks)(); });
    refreshTimer->start(2000);
    (*refreshTasks)();
    dialog->show();
}


mainfamework::~mainfamework()
{
    delete ui;
}


void mainfamework::on_pushButton_exit_clicked()
{
    auto *login = new PaperPilot();
    this->close();
    login->show();
}

void mainfamework::handle()
{
    int value = ui->lcdNumber->intValue();
    if(value>=1000)
    {
        timer->stop();
        return;
    }
    ui->lcdNumber->display(value+1);
}


void mainfamework::on_navAiButton_clicked()
{

}


void mainfamework::on_navNotesButton_clicked()
{
    showNotesDialog();
}


void mainfamework::on_navTasksButton_clicked()
{
    showTasksDialog();
}


void mainfamework::on_importButton_clicked()
{

    QString fileName = QFileDialog::getOpenFileName(
        this,
        "选择 PDF 论文",
        "",
        "PDF 文件 (*.pdf)"
        );

    // 用户点击“取消”时直接结束，不能继续用空路径构造上传请求。
    if (fileName.isEmpty())
        return;

    qDebug() << "选择的文件:" << fileName;


    // 创建 multipart/form-data 请求体
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 添加普通文本字段：title
    QHttpPart titlePart;
    titlePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"title\"")));

    // 先用 PDF 文件名作为论文标题
    const QString title = QFileInfo(fileName).completeBaseName();

    qDebug() << "选择的文件:" << title;

    titlePart.setBody(title.toUtf8());

    //  打开本地 PDF 文件
    auto *pdfFile = new QFile(fileName);

    if (!pdfFile->open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("导入失败"),
            QStringLiteral("无法打开 PDF 文件。"));

        delete pdfFile;
        delete multiPart;
        return;
    }

    qDebug() << "打开本地文件成功";

    // 添加文件字段：file
    QHttpPart filePart;
    filePart.setHeader(
        QNetworkRequest::ContentTypeHeader,//Content-Type：告诉服务器文件是什么类型
        QVariant(QStringLiteral("application/pdf")));

    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,//Content-Disposition：告诉服务器这部分数据是什么字段
        QVariant(
            QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                .arg(QFileInfo(fileName).fileName())));

    // 不用 readAll()，直接让 Qt 从文件流读取，适合较大的 PDF。
    /*
                setBody()	setBodyDevice()
    数据来源	QByteArray	QIODevice
    读取方式	先读取到内存	从设备读取
    内存占用	需要保存文件内容	不需要额外保存完整文件内容
    适用场景	小文件	大文件
     */
    filePart.setBodyDevice(pdfFile);

    // multiPart 销毁时，pdfFile 也会自动销毁。
    pdfFile->setParent(multiPart);//建立了一个父子关系，当multipart释放的时候会接连着子类也释放

    multiPart->append(titlePart);
    multiPart->append(filePart);

    qDebug() << "构造请求完毕";

    // 发起 POST 请求
    QNetworkRequest request(
        QUrl(backendBaseUrl() + QStringLiteral("/api/papers")));

    QNetworkReply *reply = networkManager_->post(request, multiPart);

    qDebug() << "请求发生成功";

    // 请求结束后再释放 multipart，不能在 post() 后立即 delete。
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName] {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QByteArray responseBytes = reply->readAll();
        const QString errorText = reply->errorString();

        reply->deleteLater();

        if (httpStatus != 200)
        {
            QMessageBox::warning(
                this,
                QStringLiteral("导入失败"),
                QStringLiteral("无法连接 Drogon 服务：%1").arg(errorText));
            return;
        }

        qDebug() << "状态正常 httpStatus = " << httpStatus;

        const QJsonObject body =
            QJsonDocument::fromJson(responseBytes).object();

        if (!body["success"].toBool())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("导入失败"),
                body["message"].toString());
            return;
        }

        qDebug() << "导入成功";

        QMessageBox::information(
            this,
            QStringLiteral("导入成功"),
            QStringLiteral("论文已上传，正在解析。"));

        QString taskId = body["taskId"].toString();
        qDebug() << "当前解析任务ID：" << taskId;
        updateTaskCard(QFileInfo(fileName).fileName(), "正在解析 PDF", 0);

        // 服务端尚未返回任务 ID 时，仍然刷新列表，但不能轮询一个空地址。
        if (taskId.isEmpty())
        {
            loadPapers();
            return;
        }

        QTimer *timer = new QTimer(this);

        connect(timer, &QTimer::timeout, this, [this, taskId, timer]() {

            QNetworkRequest request(
                QUrl(backendBaseUrl() + "/api/tasks/" + taskId)
                );

            QNetworkReply *reply = networkManager_->get(request);

            connect(reply, &QNetworkReply::finished, this, [reply, this, timer]() {

                const int httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QString errorText = reply->errorString();

                if (httpStatus != 200)
                {
                    reply->deleteLater();
                    QMessageBox::warning(
                        this,
                        QStringLiteral("获取进度失败"),
                        QStringLiteral("无法连接 Drogon 服务：%1").arg(errorText));
                    return;
                }

                QJsonObject body = QJsonDocument::fromJson(
                                       reply->readAll()
                                       ).object();

                QString status = body["status"].toString();
                int progress = body["progress"].toInt();

                qDebug() << "解析状态：" << status;
                qDebug() << "解析进度：" << progress << "%";
                const QString displayStatus = status == "completed"
                    ? "解析完成"
                    : status == "failed" ? "解析失败" : "正在解析 PDF";
                updateTaskCard(ui->taskTitle->text(), displayStatus, progress);

                if (status == "completed" || status == "failed") {
                    timer->stop();
                    timer->deleteLater();
                    loadPapers();
                }

                reply->deleteLater();
            });
        });

        timer->start(2000);

        loadPapers();
    });

}

