#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gremote"));
    QApplication::setOrganizationDomain(QStringLiteral("gremote.dev"));
    QApplication::setApplicationVersion(QStringLiteral(GREMOTE_VERSION));

    MainWindow w;
    w.show();
    return app.exec();
}
