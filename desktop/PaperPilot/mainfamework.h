#ifndef MAINFAMEWORK_H
#define MAINFAMEWORK_H

#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QFileDialog>
#include <QDebug>


#include <QFileInfo>
#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QStringList>


class QTimer;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace Ui
{
class mainfamework;
}

class mainfamework : public QWidget
{
    Q_OBJECT

  public:
    explicit mainfamework(QWidget *parent = nullptr);
    ~mainfamework();

  private slots:
    void on_pushButton_exit_clicked();
    void handle();

    void on_navAiButton_clicked();

    void on_navNotesButton_clicked();

    void on_navTasksButton_clicked();

    void on_importButton_clicked();

  private:
    // 按当前搜索词、筛选条件向后端请求论文列表。
    void loadPapers();
    void setupLibraryControls();
    void updateTaskCard(const QString &title, const QString &status, int progress);
    void makeContentResponsive();
    void buildChatDemo();
    void loadPaperDetail(const QString &paperId);
    void deletePaper(const QString &paperId, const QString &title);
    void showNotesDialog();
    void toggleFavorite(const QString &paperId, bool favorite);
    void showTasksDialog();

    Ui::mainfamework *ui;
    QTimer *timer{};
    QTimer *searchTimer_{};
    QNetworkAccessManager *networkManager_{};

    // 右侧详情/对话区域在运行时创建，保存指针以便选中论文时刷新显示。
    QLabel *detailTitle_{};
    QLabel *detailMeta_{};
    QLabel *detailStatus_{};
    QLineEdit *chatInput_{};
    QVBoxLayout *chatMessagesLayout_{};

    QLabel *detailAbstract_{};//增加“摘要控件指针”
    QString currentPaperId_;
    QString currentPaperTitle_;
    // 服务端创建后返回；同一篇论文的连续提问会复用这个会话。
    QString chatSessionId_;
    QString chatSessionPaperId_;
};

#endif  // MAINFAMEWORK_H
