#pragma once

#include <QMainWindow>

namespace Ui
{
class PaperPilotClass;
}

class QNetworkAccessManager;

// 主窗口外壳。界面控件由 PaperPilot.ui 中的 Qt Designer 创建。
class PaperPilot : public QMainWindow
{
    Q_OBJECT

public:
    explicit PaperPilot(QWidget *parent = nullptr);
    ~PaperPilot() override;

private:
    void login();

    // 指向 uic 根据 PaperPilot.ui 自动生成的界面类。
    Ui::PaperPilotClass *ui_{};
    QNetworkAccessManager *networkManager_{};
};
