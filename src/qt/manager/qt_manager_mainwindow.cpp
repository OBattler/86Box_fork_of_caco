#include "qt_manager_mainwindow.h"
#include "ui_qt_manager_mainwindow.h"

#include "qt_manager_vmadddialog.h"
#include "ui_qt_manager_vmadddialog.h"
#include "qt_manager_settings.h"

#include "../qt_settings.hpp"

#ifdef Q_OS_WINDOWS
#include <windows.h>
#include <shlobj.h>
#endif

#include <QVariant>
#include <QDir>
#include <QProcessEnvironment>
#include <QLocalSocket>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMenu>
#include <QDesktopServices>
#include <QStandardPaths>
#include <set>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/version.h>
}

ManagerMainWindow::ManagerMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::ManagerMainWindow)
    , server(new QLocalServer(this))
{
    ui->setupUi(this);

    statusBar()->show();
    statusBarLabel = new QLabel;
    statusBar()->addWidget(statusBarLabel);
    config_load_global();
    server->listen(QApplication::applicationFilePath() + QString::asprintf(":%lld", (qint64)QApplication::applicationPid()));
    ui->tableWidget->setShowGrid(manager_config.enable_grid_lines);

    trayIcon = new QSystemTrayIcon(qApp->windowIcon(), this);

    connect(trayIcon, &QSystemTrayIcon::activated, this, [this] (QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Context && reason != QSystemTrayIcon::Unknown)
            showNormal();
    });

    trayIcon->show();

    auto menu = new QMenu(this);
    menu->addAction(tr("Show 86Box"), this, [this] () {
        showNormal();
    });

    menu->addAction(tr("Settings"), this, [this] () {
        showNormal();
        ui->pushButtonSettings->click();
    });

    menu->addSeparator();
    menu->addAction(tr("Exit"), this, [this] () {
        bool running = false;
        for (int i = 0; i < 256; i++) {
            running |= (processes[i].state() == QProcess::Running);
        }

        if (running) {
            showNormal();
            auto res = QMessageBox::warning(this, tr("Virtual machines are still running"), tr("Some virtual machines are still running. It's recommended you stop them first before closing 86Box Manager. Do you want to stop them now?"),
                                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            switch (res) {
                default:
                case QMessageBox::Cancel:
                    return;
                case QMessageBox::No:
                    {
                        for (int i = 0; i < 256; i++) {
                            if (sockets[i]) {
                                sockets[i]->close();
                                delete sockets[i];
                                sockets[i] = nullptr;
                            }
                            processes[i].detach();
                        }
                        QApplication::quit();
                        return;
                    }
                case QMessageBox::Yes:
                    {
                        QApplication::quit();
                        return;
                    }
            }
        } else {
            QApplication::quit();
        }
    });

    trayIcon->setContextMenu(menu);

    connect(ui->actionHard_reset, &QAction::triggered, ui->pushButtonReset, &QPushButton::clicked);
    connect(ui->actionStart, &QAction::triggered, ui->pushButtonStartStop, &QPushButton::clicked);
    connect(ui->actionSend_CTRL_ALT_DEL, &QAction::triggered, ui->pushButtonCAD, &QPushButton::clicked);
    connect(ui->actionConfigure, &QAction::triggered, ui->pushButtonConfigure, &QPushButton::clicked);
    connect(ui->actionPause, &QAction::triggered, ui->pushButtonPause, &QPushButton::clicked);

    connect(ui->actionEdit, &QAction::triggered, ui->pushButtonEdit, &QPushButton::clicked);
    connect(ui->actionRemove, &QAction::triggered, ui->pushButtonRemove, &QPushButton::clicked);

    connect(server, &QLocalServer::newConnection, this, [this] {
        auto rowCount = ui->tableWidget->rowCount();

        for (int i = 0; i < rowCount; i++) {
            if (selectedVMIndex == ui->tableWidget->item(i, 0)->data(Qt::UserRole).toInt()) {
                sockets[selectedVMIndex] = server->nextPendingConnection();
                connect(sockets[selectedVMIndex], &QLocalSocket::readyRead, this, [this, i] () {
                    auto VMIndex = ui->tableWidget->item(i, 0)->data(Qt::UserRole).toInt();
                    while (sockets[VMIndex]->canReadLine()) {
                        auto byteArray = sockets[VMIndex]->readLine();
                        switch(byteArray[0]) {
                            case '0':
                                blocked[VMIndex] = false;
                                break;
                            case '1':
                                blocked[VMIndex] = true;
                                break;
                            case '2':
                                paused[VMIndex] = false;
                                break;
                            case '3':
                                paused[VMIndex] = true;
                                break;
                        }
                        if (blocked[VMIndex])
                            ui->tableWidget->item(i, 1)->setText(tr("Waiting"));
                        else
                            ui->tableWidget->item(i, 1)->setText(paused[VMIndex] ? tr("Paused") : tr("Running"));
                        ui->tableWidget->setCurrentItem(ui->tableWidget->item(i, 0));
                        emit ui->tableWidget->itemPressed(ui->tableWidget->item(i, 0));
                    }
                });
                setDisabled(false);
                ui->tableWidget->item(i, 1)->setText(tr("Running"));
                blocked[selectedVMIndex] = false;
                ui->tableWidget->setCurrentItem(ui->tableWidget->item(i, 0));
                emit ui->tableWidget->itemPressed(ui->tableWidget->item(i, 0));
                if (manager_config.minimize_when_vm_started)
                    showMinimized();
                return;
            }
        }
    });

    ui->pushButtonStartStop->setDisabled(true);
    ui->pushButtonConfigure->setDisabled(true);
    ui->pushButtonCAD->setDisabled(true);
    ui->pushButtonEdit->setDisabled(true);
    ui->pushButtonPause->setDisabled(true);
    ui->pushButtonReset->setDisabled(true);
    ui->pushButtonRemove->setDisabled(true);
    ui->menuOptions->setDisabled(true);
    for (int i = 0; i < 256; i++) {
        if (manager_config.vm[i].name[0]) {
            ui->tableWidget->insertRow(ui->tableWidget->rowCount());
            int row = ui->tableWidget->rowCount() - 1;

            // Name.
            auto tableItem = new QTableWidgetItem(manager_config.vm[i].name);
            tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            tableItem->setData(Qt::UserRole, i);
            ui->tableWidget->setItem(row, 0, tableItem);
            // Status
            tableItem = new QTableWidgetItem(tr("Stopped"));
            tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            tableItem->setData(Qt::UserRole, i);
            ui->tableWidget->setItem(row, 1, tableItem);
            // Description.
            tableItem = new QTableWidgetItem(manager_config.vm[i].description);
            tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            tableItem->setData(Qt::UserRole, i);
            ui->tableWidget->setItem(row, 2, tableItem);
            // Path.
            tableItem = new QTableWidgetItem(manager_config.vm[i].path);
            tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            tableItem->setData(Qt::UserRole, i);
            ui->tableWidget->setItem(row, 3, tableItem);
        }
        connect(&processes[i], &QProcess::errorOccurred, this, [this, i] (QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart || error == QProcess::Crashed) {
                if (sockets[i]) {
                    delete sockets[i];
                    sockets[i] = nullptr;
                }
                auto rowCount = ui->tableWidget->rowCount();

                for (int c = 0; c < rowCount; c++) {
                    if (i == ui->tableWidget->item(c, 0)->data(Qt::UserRole).toInt()) {
                        ui->tableWidget->item(c, 1)->setText(tr("Stopped"));
                        ui->tableWidget->setCurrentItem(ui->tableWidget->item(c, 0));
                        emit ui->tableWidget->itemPressed(ui->tableWidget->item(i, 0));
                    }
                }

                paused[i] = false;
                blocked[i] = false;
                this->setDisabled(false);
            }
        });
        connect(&processes[i], &QProcess::finished, this, [this, i] (int exitCode, QProcess::ExitStatus status) {
            if (sockets[i]) {
                delete sockets[i];
                sockets[i] = nullptr;
            }
            auto rowCount = ui->tableWidget->rowCount();

            for (int c = 0; c < rowCount; c++) {
                if (i == ui->tableWidget->item(c, 0)->data(Qt::UserRole).toInt()) {
                    ui->tableWidget->item(c, 1)->setText(tr("Stopped"));
                    ui->tableWidget->setCurrentItem(ui->tableWidget->item(c, 0));
                    emit ui->tableWidget->itemPressed(ui->tableWidget->item(i, 0));
                }
            }

            paused[i] = false;
            blocked[i] = false;
            this->setDisabled(false);
        });
        blocked[i] = false;
        paused[i] = false;
    }
    connect(qApp, &QApplication::aboutToQuit, this, [this] () {
        for (int i = 0; i < 256; i++) {
            if (sockets[i] && processes[i].state() == QProcess::Running) {
                sockets[i]->write("shutdownnoprompt\n");
                processes[i].waitForFinished(500);
                sockets[i]->close();
                delete sockets[i];
                sockets[i] = nullptr;
            }
        }
    });
#ifndef Q_OS_WINDOWS
    ui->actionCreate_a_desktop_shortcut->setDisabled(true);
#endif
}

ManagerMainWindow::~ManagerMainWindow()
{
    config_save_global();
    delete ui;
}

void ManagerMainWindow::closeEvent(QCloseEvent* event)
{
    if (manager_config.close_to_tray_icon) {
        hide();
        event->ignore();
        return;
    }
    bool running = false;
    for (int i = 0; i < 256; i++) {
        running |= (processes[i].state() == QProcess::Running);
    }

    if (running) {
        showNormal();
        auto res = QMessageBox::warning(this, tr("Virtual machines are still running"), tr("Some virtual machines are still running. It's recommended you stop them first before closing 86Box Manager. Do you want to stop them now?"),
                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        switch (res) {
            default:
            case QMessageBox::Cancel:
                event->ignore();
                return;
            case QMessageBox::No:
                {
                    for (int i = 0; i < 256; i++) {
                        if (sockets[i]) {
                            sockets[i]->close();
                            delete sockets[i];
                            sockets[i] = nullptr;
                        }
                        processes[i].detach();
                    }
                    event->accept();
                    return;
                }
            case QMessageBox::Yes:
                {
                    event->accept();
                    return;
                }
        }
    }
    event->accept();
}

bool ManagerMainWindow::event(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        bool res = QMainWindow::event(event);
        if ((windowState() & Qt::WindowMinimized) && manager_config.minimize_to_tray_icon) {
            hide();
            trayIcon->show();
        }
        return res;
    }
    return QMainWindow::event(event);
}

void ManagerMainWindow::refreshVM(int i)
{
    if (manager_config.vm[i].name[0]) {
        ui->tableWidget->insertRow(ui->tableWidget->rowCount());
        int row = ui->tableWidget->rowCount() - 1;

        // Name.
        auto tableItem = new QTableWidgetItem(manager_config.vm[i].name);
        tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableItem->setData(Qt::UserRole, i);
        ui->tableWidget->setItem(row, 0, tableItem);
        // Status
        tableItem = new QTableWidgetItem(tr("Stopped"));
        tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableItem->setData(Qt::UserRole, i);
        ui->tableWidget->setItem(row, 1, tableItem);
        // Description.
        tableItem = new QTableWidgetItem(manager_config.vm[i].description);
        tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableItem->setData(Qt::UserRole, i);
        ui->tableWidget->setItem(row, 2, tableItem);
        // Path.
        tableItem = new QTableWidgetItem(manager_config.vm[i].path);
        tableItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        tableItem->setData(Qt::UserRole, i);
        ui->tableWidget->setItem(row, 3, tableItem);
        ui->menuOptions->setDisabled(false);
    } else {
        auto rowCount = ui->tableWidget->rowCount();

        for (int c = 0; c < rowCount; c++) {
            if (i == ui->tableWidget->item(c, 0)->data(Qt::UserRole).toInt()) {
                if (c > 0) {
                    ui->tableWidget->setCurrentItem(ui->tableWidget->item(c - 1, 0));
                    emit ui->tableWidget->itemPressed(ui->tableWidget->item(c - 1, 0));
                }
                ui->tableWidget->removeRow(c);
                if (ui->tableWidget->rowCount() == 0) {
                    ui->pushButtonStartStop->setDisabled(true);
                    ui->pushButtonConfigure->setDisabled(true);
                    ui->pushButtonCAD->setDisabled(true);
                    ui->pushButtonEdit->setDisabled(true);
                    ui->pushButtonPause->setDisabled(true);
                    ui->pushButtonReset->setDisabled(true);
                    ui->pushButtonRemove->setDisabled(true);
                    ui->menuOptions->setDisabled(true);
                }
                break;
            }
        }
    }
    config_save_global();
}

void ManagerMainWindow::on_tableWidget_currentItemChanged(QTableWidgetItem *item, QTableWidgetItem *oldItem)
{
    if (!item) {
        ui->pushButtonStartStop->setDisabled(true);
        ui->pushButtonConfigure->setDisabled(true);
        ui->pushButtonCAD->setDisabled(true);
        ui->pushButtonEdit->setDisabled(true);
        ui->pushButtonPause->setDisabled(true);
        ui->pushButtonReset->setDisabled(true);
        ui->pushButtonRemove->setDisabled(true);
        ui->actionKill->setDisabled(true);
        ui->actionWipe->setDisabled(true);
        ui->menuOptions->setDisabled(true);
        selectedVMIndex = 0;
        updateMenu();
        return;
    }
    ui->menuOptions->setDisabled(false);
    this->selectedVMIndex = item->data(Qt::UserRole).toInt();
    std::set<int> indexes;
    for (auto &curItem : item->tableWidget()->selectedItems()) {
        indexes.emplace(curItem->data(Qt::UserRole).toInt());
    }
    multipleSelected = indexes.size() > 1;
    if (multipleSelected) {
        ui->pushButtonStartStop->setDisabled(true);
        ui->pushButtonConfigure->setDisabled(true);
        ui->pushButtonCAD->setDisabled(true);
        ui->pushButtonEdit->setDisabled(true);
        ui->pushButtonPause->setDisabled(true);
        ui->pushButtonReset->setDisabled(true);
    } else {
        ui->pushButtonStartStop->setEnabled(true);
        ui->pushButtonConfigure->setEnabled(true);
        if (blocked[selectedVMIndex]) {
            ui->pushButtonStartStop->setDisabled(true);
            ui->pushButtonConfigure->setDisabled(true);
            ui->pushButtonCAD->setDisabled(true);
            ui->pushButtonEdit->setDisabled(true);
            ui->pushButtonPause->setDisabled(true);
            ui->pushButtonReset->setDisabled(true);
            ui->pushButtonRemove->setDisabled(true);
        } else if (processes[selectedVMIndex].state() == QProcess::ProcessState::Running || processes[selectedVMIndex].state() == QProcess::ProcessState::Starting) {
            ui->pushButtonEdit->setDisabled(true);
            ui->pushButtonRemove->setDisabled(true);
            ui->pushButtonCAD->setEnabled(true);
            ui->pushButtonPause->setEnabled(true);
            ui->pushButtonReset->setEnabled(true);
            ui->pushButtonStartStop->setText(tr("Stop"));
            ui->pushButtonPause->setText(paused[selectedVMIndex] ? tr("Resume") : tr("Pause"));
        } else {
            ui->pushButtonCAD->setDisabled(true);
            ui->pushButtonPause->setDisabled(true);
            ui->pushButtonReset->setDisabled(true);
            ui->pushButtonEdit->setDisabled(false);
            ui->pushButtonRemove->setDisabled(false);
            ui->pushButtonStartStop->setText(tr("Start"));
        }
    }
    updateMenu();
}


void ManagerMainWindow::on_pushButtonConfigure_clicked()
{
    if (sockets[selectedVMIndex]) {
        sockets[selectedVMIndex]->write("showsettings\n");
        return;
    }
    cfg_path[0] = 0;
    snprintf(cfg_path, sizeof(cfg_path) - 1, "%s", manager_config.vm[this->selectedVMIndex].path);
    memcpy(usr_path, cfg_path, sizeof(usr_path));
    QDir(usr_path).mkpath(".");
    strncat(cfg_path, CONFIG_FILE, sizeof(CONFIG_FILE) - 1);
    // Load configuration.
    config_load();
    Settings settings(this);
    if (settings.exec() == QDialog::Accepted) {
        settings.save();
        config_save();
    }
}


void ManagerMainWindow::on_pushButtonStartStop_clicked()
{
    if (processes[selectedVMIndex].state() == QProcess::ProcessState::NotRunning) {
        auto envList = QProcessEnvironment::systemEnvironment();
        QStringList argList = {
            "-P",
            QString(manager_config.vm[selectedVMIndex].path),
            "-V",
            QString(manager_config.vm[selectedVMIndex].name)
        };
        if (manager_config.enable_logging) {
            argList.push_back("-L");
            if (manager_config.logging_path[0] == 0) {
                argList.push_back(QString(manager_config.vm[selectedVMIndex].path) + QString("/86Box.log"));
            } else {
                argList.push_back(manager_config.logging_path);
            }
        }
        envList.insert(QStringLiteral("86BOX_MANAGER_SOCKET"), server->serverName());
        this->setDisabled(true);
        paused[selectedVMIndex] = false;
        processes[selectedVMIndex].setProcessEnvironment(envList);
        processes[selectedVMIndex].start(QApplication::applicationFilePath(),
                                         {
                                           "-P",
                                           QString(manager_config.vm[selectedVMIndex].path),
                                           "-V",
                                           QString(manager_config.vm[selectedVMIndex].name)
                                         });
    } else if (processes[selectedVMIndex].state() == QProcess::ProcessState::Running && sockets[selectedVMIndex]) {
        sockets[selectedVMIndex]->write("shutdown\n");
    }
}


void ManagerMainWindow::on_pushButtonCAD_clicked()
{
    if (sockets[selectedVMIndex]) {
        sockets[selectedVMIndex]->write("cad\n");
    }
}


void ManagerMainWindow::on_pushButtonPause_clicked()
{
    if (sockets[selectedVMIndex]) {
        sockets[selectedVMIndex]->write("pause\n");
    }
}


void ManagerMainWindow::on_tableWidget_cellChanged(int row, int column)
{
    if (column == 1) {
        unsigned int stopped = 0, running = 0, paused = 0, waiting = 0;

        for (int i = 0; i < 256; i++) {
            if (ui->tableWidget->item(i, 0) && ui->tableWidget->item(i, 0)->data(Qt::UserRole) == i) {
                if (processes[i].state() == QProcess::NotRunning)
                    stopped++;
                else if (this->blocked[i])
                    waiting++;
                else if (this->paused[i]) {
                    paused++;
                } else {
                    running++;
                }
            }
        }

        statusBarLabel->setText(QString(tr("All VMs: %1 | Running: %2 | Paused: %3 | Waiting: %4 | Stopped: %5").arg(ui->tableWidget->rowCount()).arg(running).arg(paused).arg(waiting).arg(stopped)));
    }
}


void ManagerMainWindow::on_pushButtonAdd_clicked()
{
    auto adddialog = ManagerVMAddDialog(this, -1, -1);
    auto res = adddialog.exec();
    if (res == QDialog::Accepted) {
        if (adddialog.ui->checkBoxConfigureVM->isChecked()) {
            on_pushButtonConfigure_clicked();
        }
        if (adddialog.ui->checkBoxStartVM->isChecked()) {
            on_pushButtonStartStop_clicked();
        }
    }
}


void ManagerMainWindow::on_pushButtonRemove_clicked()
{
    QMessageBox questionBox(QMessageBox::Icon::Warning, tr("Remove virtual machine"), tr("Are you sure you want to remove the virtual machine \"%1\"?").arg(QString(manager_config.vm[selectedVMIndex].name)), QMessageBox::Yes | QMessageBox::No, this);
    if (questionBox.exec()) {
        if (questionBox.result() == QMessageBox::Yes) {
            QString origName = manager_config.vm[selectedVMIndex].name;
            QString origPath = manager_config.vm[selectedVMIndex].path;
            manager_config.vm[selectedVMIndex].name[0] = 0;
            refreshVM(selectedVMIndex);
            if (QMessageBox::information(this, tr("Virtual machine removed"), tr("Virtual machine \"%1\" was successfully removed. Would you like to delete its files as well?").arg(origName), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                if (!QDir(origPath).removeRecursively()) {
                    QMessageBox::critical(this, tr("Error"), tr("86Box was unable to delete the files of this virtual machine."));
                }
            }
        }
    }
}


void ManagerMainWindow::on_pushButtonEdit_clicked()
{
    ManagerVMAddDialog(this, selectedVMIndex, ui->tableWidget->currentRow()).exec();
}


void ManagerMainWindow::on_tableWidget_itemPressed(QTableWidgetItem *item)
{
    on_tableWidget_currentItemChanged(item, item);
}


void ManagerMainWindow::on_pushButtonSettings_clicked()
{
    ManagerSettings settings(this);
    settings.exec();
    if (settings.result() == QDialog::Accepted) {
        ui->tableWidget->setShowGrid(manager_config.enable_grid_lines);
    }
    this->setEnabled(true);
}

void ManagerMainWindow::updateMenu()
{
    ui->actionStart->setEnabled(ui->pushButtonStartStop->isEnabled());
    ui->actionStart->setText(ui->pushButtonStartStop->text());
    ui->actionPause->setEnabled(ui->pushButtonPause->isEnabled());
    ui->actionPause->setText(ui->pushButtonPause->text());

    ui->actionSend_CTRL_ALT_DEL->setEnabled(ui->pushButtonCAD->isEnabled());
    ui->actionConfigure->setEnabled(ui->pushButtonConfigure->isEnabled());
    ui->actionHard_reset->setEnabled(ui->pushButtonReset->isEnabled());
    ui->actionEdit->setEnabled(ui->pushButtonEdit->isEnabled());
    ui->actionRemove->setEnabled(ui->pushButtonRemove->isEnabled());
    ui->actionClone->setEnabled(ui->pushButtonAdd->isEnabled());

    ui->actionKill->setEnabled(processes[selectedVMIndex].state() != QProcess::NotRunning);
    ui->actionWipe->setEnabled(processes[selectedVMIndex].state() == QProcess::NotRunning);
}

void ManagerMainWindow::on_pushButtonAbout_clicked()
{
    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::RichText);
    QString versioninfo;
#ifdef EMU_GIT_HASH
    versioninfo = QString(" [%1]").arg(EMU_GIT_HASH);
#endif
#ifdef USE_DYNAREC
#    ifdef USE_NEW_DYNAREC
#        define DYNAREC_STR "new dynarec"
#    else
#        define DYNAREC_STR "old dynarec"
#    endif
#else
#    define DYNAREC_STR "no dynarec"
#endif
    versioninfo.append(QString(" [%1, %2]").arg(QSysInfo::buildCpuArchitecture(), tr(DYNAREC_STR)));
    msgBox.setText(QString("<b>%3%1%2</b>").arg(EMU_VERSION_FULL, versioninfo, tr("86Box v")));
    msgBox.setInformativeText(tr("An emulator of old computers\n\nAuthors: Miran Grča (OBattler), RichardG867, Jasmine Iwanek, TC1995, coldbrewed, Teemu Korhonen (Manaatti), Joakim L. Gilje, Adrien Moulin (elyosh), Daniel Balsom (gloriouscow), Cacodemon345, Fred N. van Kempen (waltje), Tiseno100, reenigne, and others.\n\nWith previous core contributions from Sarah Walker, leilei, JohnElliott, greatpsycho, and others.\n\nReleased under the GNU General Public License version 2 or later. See LICENSE for more information."));
    msgBox.setWindowTitle("About 86Box");
    msgBox.addButton("OK", QMessageBox::ButtonRole::AcceptRole);
    auto webSiteButton = msgBox.addButton(EMU_SITE, QMessageBox::ButtonRole::HelpRole);
    webSiteButton->connect(webSiteButton, &QPushButton::released, []() {
        QDesktopServices::openUrl(QUrl("https://" EMU_SITE));
    });
#ifdef RELEASE_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-green.ico").pixmap(32, 32));
#elif defined ALPHA_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-red.ico").pixmap(32, 32));
#elif defined BETA_BUILD
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-yellow.ico").pixmap(32, 32));
#else
    msgBox.setIconPixmap(QIcon(":/settings/win/icons/86Box-gray.ico").pixmap(32, 32));
#endif
    msgBox.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    msgBox.exec();
}


void ManagerMainWindow::on_actionClone_triggered()
{
    auto adddialog = ManagerVMAddDialog(this, -1, -1, selectedVMIndex);
    auto res = adddialog.exec();
    if (res == QDialog::Accepted) {
        if (adddialog.ui->checkBoxConfigureVM->isChecked()) {
            on_pushButtonConfigure_clicked();
        }
        if (adddialog.ui->checkBoxStartVM->isChecked()) {
            on_pushButtonStartStop_clicked();
        }
    }
}


void ManagerMainWindow::on_actionOpen_folder_of_VM_triggered()
{
    QDir(manager_config.vm[selectedVMIndex].path).mkpath(".");
    if (!QDesktopServices::openUrl(QString("file://") + manager_config.vm[selectedVMIndex].path)) {
        QMessageBox::critical(this, tr("Error"), tr("The folder for the virtual machine \"%1\" could not be opened. Make sure it still exists and that you have sufficient privileges to access it.").arg(manager_config.vm[selectedVMIndex].name));
    }
}


void ManagerMainWindow::on_actionOpen_config_file_triggered()
{
    QDir dir(manager_config.vm[selectedVMIndex].path);
    dir.mkpath(".");
    if (!QDesktopServices::openUrl(QString("file://") + manager_config.vm[selectedVMIndex].path + QString("/") + CONFIG_FILE)) {
        QMessageBox::critical(this, tr("Error"), tr("The config file for the virtual machine \"%1\" could not be opened. Make sure it still exists and that you have sufficient privileges to access it.").arg(manager_config.vm[selectedVMIndex].name));
    }
}


void ManagerMainWindow::on_actionKill_triggered()
{
    if (QMessageBox::warning(this, tr("Warning"), tr("Killing a virtual machine can cause data loss. Only do this if it gets stuck.\n\nDo you really wish to kill the virtual machine \"%1\"?").arg(manager_config.vm[selectedVMIndex].name), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (sockets[selectedVMIndex]) {
            sockets[selectedVMIndex]->close();
        }
        processes[selectedVMIndex].kill();
        emit processes[selectedVMIndex].finished(-1, QProcess::NormalExit);
    }
}


void ManagerMainWindow::on_actionWipe_triggered()
{
    if (QMessageBox::warning(this, tr("Warning"), tr("Wiping a virtual machine deletes its configuration and nvr files. You'll have to reconfigure the virtual machine (and the BIOS if applicable).\n\n Are you sure you wish to wipe the virtual machine \"%1\"?").arg(manager_config.vm[selectedVMIndex].name), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        bool success = false;
        QDir dir(manager_config.vm[selectedVMIndex].path);
        success = dir.remove(CONFIG_FILE) && QDir(dir.canonicalPath() + "/nvr/").removeRecursively();
        if (success)
            QMessageBox::critical(this, tr("Error"), tr("An error occurred trying to wipe the virtual machine \"%1\".").arg(manager_config.vm[selectedVMIndex].name));
        else
            QMessageBox::information(this, tr("Success"), tr("The virtual machine \"%1\" was successfully wiped").arg(manager_config.vm[selectedVMIndex].name));
    }
}


void ManagerMainWindow::on_actionCreate_a_desktop_shortcut_triggered()
{
#ifdef Q_OS_WINDOWS
    IShellLinkW* lnk = NULL;
    (void)CoInitialize(NULL);

    auto hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)&lnk);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf = NULL;

        lnk->SetPath(QApplication::applicationFilePath().toStdWString().c_str());
        lnk->SetArguments(QString("-S -P \"%1\"").arg(manager_config.vm[selectedVMIndex].path).replace("/", "\\").toStdWString().c_str());
        lnk->SetWorkingDirectory(QApplication::applicationDirPath().replace("/", "\\").toStdWString().c_str());
        lnk->SetDescription(QString(manager_config.vm[selectedVMIndex].description).toStdWString().c_str());
        lnk->SetIconLocation(QApplication::applicationFilePath().toStdWString().c_str(), 0);
        hres = lnk->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hres)) {
            hres = ppf->Save((QStandardPaths::standardLocations(QStandardPaths::DesktopLocation)[0].replace('/', '\\') + QString("\\") + QString(manager_config.vm[selectedVMIndex].name) + ".lnk").toStdWString().c_str(), TRUE);
            QMessageBox::information(this, tr("Success"), tr("A desktop shortcut for the virtual machine \"%1\" was successfully created.").arg(manager_config.vm[selectedVMIndex].name));
            ppf->Release();
        }
        else
            QMessageBox::critical(this, tr("Error"), tr("A desktop shortcut for the virtual machine \"%1\" could not be created.").arg(manager_config.vm[selectedVMIndex].name));
        lnk->Release();
    } else
        QMessageBox::critical(this, tr("Error"), tr("A desktop shortcut for the virtual machine \"%1\" could not be created.").arg(manager_config.vm[selectedVMIndex].name));
#endif
}


void ManagerMainWindow::on_tableWidget_itemActivated(QTableWidgetItem *item)
{
    selectedVMIndex = item->data(Qt::UserRole).toInt();
    if (processes[selectedVMIndex].state() == QProcess::Running && !blocked[selectedVMIndex]) {
        if (paused[selectedVMIndex])
            ui->pushButtonPause->click();
        else
            ui->pushButtonStartStop->click();
    } else if (processes[selectedVMIndex].state() == QProcess::NotRunning) {
        ui->pushButtonStartStop->click();
    }
}

