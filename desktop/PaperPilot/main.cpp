#include "PaperPilot.h"
#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    PaperPilot window;
    window.show();

    return app.exec();
}