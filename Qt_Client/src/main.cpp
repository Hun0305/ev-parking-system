#include "mainwindow.h"
#include "RtspVideoItem.h"

#include <QApplication>
#include <qqml.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qmlRegisterType<RtspVideoItem>("Rtsp", 1, 0, "RtspVideoItem");

    MainWindow window;
    window.show();

    return app.exec();
}
