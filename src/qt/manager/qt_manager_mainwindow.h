#ifndef QT_MANAGER_MAINWINDOW_H
#define QT_MANAGER_MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QLocalServer>
#include <QApplication>
#include <QMap>
#include <QTableWidgetItem>
#include <QLabel>
#include <QSystemTrayIcon>

namespace Ui {
class ManagerMainWindow;
}

class QDetachableProcess : public QProcess
{
public:
    QDetachableProcess(QObject *parent = 0) : QProcess(parent){}
    void detach()
    {
        this->waitForStarted();
        setProcessState(QProcess::NotRunning);
    }
};

class ManagerMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ManagerMainWindow(QWidget *parent = nullptr);
    ~ManagerMainWindow();

    void refreshVM(int i);

private:
    void updateMenu();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;

private slots:
    void on_tableWidget_currentItemChanged(QTableWidgetItem *item, QTableWidgetItem *oldItem);
    void on_pushButtonConfigure_clicked();
    void on_pushButtonStartStop_clicked();
    void on_pushButtonCAD_clicked();
    void on_pushButtonPause_clicked();
    void on_tableWidget_cellChanged(int row, int column);
    void on_pushButtonAdd_clicked();
    void on_pushButtonRemove_clicked();
    void on_pushButtonEdit_clicked();
    void on_tableWidget_itemPressed(QTableWidgetItem *item);
    void on_pushButtonSettings_clicked();
    void on_pushButtonAbout_clicked();
    void on_actionClone_triggered();

    void on_actionOpen_folder_of_VM_triggered();

    void on_actionOpen_config_file_triggered();

    void on_actionKill_triggered();

    void on_actionWipe_triggered();

    void on_actionCreate_a_desktop_shortcut_triggered();

    void on_tableWidget_itemActivated(QTableWidgetItem *item);

private:
    Ui::ManagerMainWindow *ui;
    QLocalServer* server;
    // Mapped by index;
    std::array<QDetachableProcess, 256> processes;
    std::array<bool, 256> paused, blocked;
    QMap<unsigned int, QLocalSocket*> sockets;
    int selectedVMIndex;
    int multipleSelected;
    QLabel* statusBarLabel;
    QSystemTrayIcon* trayIcon;
    QMenu* trayIconMenu;

    friend class ManagerVMAddDialog;
};

#endif // QT_MANAGER_MAINWINDOW_H
