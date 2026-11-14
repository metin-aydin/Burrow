#pragma once

#include <QMainWindow>

class QListWidget;
class QLabel;

// Placeholder shell: device list on the left, status on the bottom.
// Real screens (vault unlock, device editor, tunnel profiles, settings)
// come in later milestones.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QListWidget *m_devices = nullptr;
    QLabel *m_status = nullptr;
};
