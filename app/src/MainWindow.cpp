#include "MainWindow.h"

#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include "net/NetCore.h"
#include "rdp/FreeRdpEngine.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("gremote"));
    resize(960, 600);

    m_devices = new QListWidget(this);
    m_devices->addItem(tr("Henüz kayıtlı cihaz yok"));
    m_devices->setEnabled(false);

    auto *placeholder = new QLabel(tr("Bağlanmak için soldan bir cihaz seç."), this);
    placeholder->setAlignment(Qt::AlignCenter);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_devices);
    splitter->addWidget(placeholder);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_status = new QLabel(this);
    m_status->setText(tr("gremote %1 · netcore %2 · %3")
                          .arg(QStringLiteral(GREMOTE_VERSION),
                               NetCore::version(),
                               FreeRdpEngine::engineVersion()));
    statusBar()->addPermanentWidget(m_status);
}
