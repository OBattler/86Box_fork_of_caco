#include "qt_manager_vmadddialog.h"
#include "ui_qt_manager_vmadddialog.h"

#include "ui_qt_manager_mainwindow.h"

#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QDir>
#include <QFile>
#include <QFileDialog>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ui.h>
#include <86box/video.h>
}

// https://stackoverflow.com/a/36460740
// Slightly modified to be more strict.
static bool copyPath(QString sourceDir, QString destinationDir, bool overWriteDirectory)
{
    QDir originDirectory(sourceDir);

    if (! originDirectory.exists())
    {
        return false;
    }

    QDir destinationDirectory(destinationDir);

    if(destinationDirectory.exists() && !overWriteDirectory)
    {
        return false;
    }
    else if(destinationDirectory.exists() && overWriteDirectory)
    {
        destinationDirectory.removeRecursively();
    }

    originDirectory.mkpath(destinationDir);

    foreach (QString directoryName, originDirectory.entryList(QDir::Dirs | \
                                                              QDir::NoDotAndDotDot))
    {
        QString destinationPath = destinationDir + "/" + directoryName;
        originDirectory.mkpath(destinationPath);
        if (!copyPath(sourceDir + "/" + directoryName, destinationPath, overWriteDirectory)) {
            return false;
        }
    }

    foreach (QString fileName, originDirectory.entryList(QDir::Files))
    {
        if (!QFile::copy(sourceDir + "/" + fileName, destinationDir + "/" + fileName)) {
            return false;
        }
    }

    /*! Possible race-condition mitigation? */
    QDir finalDestination(destinationDir);
    finalDestination.refresh();

    if(finalDestination.exists())
    {
        return true;
    }

    return false;
}

ManagerVMAddDialog::ManagerVMAddDialog(ManagerMainWindow *parent, int i, int row, int clone)
    : QDialog(parent)
    , ui(new Ui::ManagerVMAddDialog)
{
    ui->setupUi(this);

    ui->labelPathDisplay->setText(QString(manager_config.vms_path));
    ui->lineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("[^\\\\/:*?\"<>|]*")));
    ui->lineEdit_3->setValidator(new QRegularExpressionValidator(QRegularExpression("[^\\\\/:*?\"<>|]*")));
    ui->labelClone->hide();

    managerMainWindow = parent;

    if (i != -1) {
        editingVM = true;
        VMIndex = i;
        VMRow = row;
        setWindowTitle(tr("Edit a virtual machine"));
        ui->pushButton->setText(tr("Apply"));
        ui->lineEdit->setText(manager_config.vm[VMIndex].name);
        ui->lineEdit_2->setText(manager_config.vm[VMIndex].description);
        ui->lineEdit_3->hide();
        ui->checkBoxImport->hide();
        ui->pushButtonBrowse->hide();
        ui->checkBoxStartVM->hide();
        ui->checkBoxConfigureVM->hide();
    }

    if (clone != -1) {
        cloneVM = clone;
        ui->lineEdit_3->hide();
        ui->checkBoxImport->hide();
        ui->pushButtonBrowse->hide();

        ui->checkBoxImport->setChecked(true);
        ui->lineEdit_3->setText(manager_config.vm[VMIndex].path);
        ui->labelClone->setText(ui->labelClone->text().arg(manager_config.vm[VMIndex].name));
        ui->labelClone->show();

        setWindowTitle(tr("Clone a virtual machine"));
        ui->pushButton->setText(tr("Clone"));
    }
}

ManagerVMAddDialog::~ManagerVMAddDialog()
{
    delete ui;
}

void ManagerVMAddDialog::on_pushButton_clicked()
{
    if (ui->lineEdit->text().trimmed().size() == 0) {
        QMessageBox::critical(this, tr("Error"), tr("You must specify a name"));
        return;
    }
    for (int i = 0; i < 256; i++) {
        if (strncmp(manager_config.vm[i].name, QByteArray(this->ui->lineEdit->text().toUtf8()), QByteArray(this->ui->lineEdit->text().toUtf8()).size())
            && QDir(manager_config.vms_path).exists(this->ui->lineEdit->text())) {
            QMessageBox::critical(this, tr("Error"), tr("A virtual machine with this name already exists. Please pick a different name."));
            return;
        }
    }

    if (!editingVM) {
        int j = 0;
        if (ui->checkBoxImport->isChecked() && ui->lineEdit_3->text().trimmed().size() == 0) {
            QMessageBox::critical(this, tr("Error"), tr("If you wish to import VM files, you must specify a path."));
            return;
        }
        for (int j = 0; j < 256; j++) {
            if (!manager_config.vm[j].name[0]) {
                strncpy(manager_config.vm[j].name, QByteArray(this->ui->lineEdit->text().trimmed().toUtf8()), QByteArray(this->ui->lineEdit->text().toUtf8()).size());
                auto newPath = (QDir(manager_config.vms_path).canonicalPath() + QStringLiteral("/") + manager_config.vm[j].name) + QString("/");
                strncpy(manager_config.vm[j].description, QByteArray(this->ui->lineEdit_2->text().toUtf8()), QByteArray(this->ui->lineEdit_2->text().toUtf8()).size());
                strncpy(manager_config.vm[j].path, newPath.toUtf8(), newPath.toUtf8().size());
                this->managerMainWindow->refreshVM(j);
                if (ui->checkBoxImport->isChecked()) {
                    bool success = copyPath(ui->lineEdit_3->text().trimmed(), QDir(manager_config.vms_path).canonicalPath() + QStringLiteral("/") + manager_config.vm[j].name, true);

                    if (!success) {
                        QMessageBox::information(this, tr("Success"), tr("Virtual machine \"%1\" was successfully created, but files could not be imported. Make sure the path you selected was correct and valid.\n\nIf the VM is already located in your VMs folder, you don't need to select the Import option, just add a new VM with the same name.").arg(manager_config.vm[j].name));
                    } else {
                        QMessageBox::information(this, tr("Success"), tr("Virtual machine \"%1\" was successfully created, files were imported. Remember to update any paths pointing to disk images in your config!").arg(manager_config.vm[j].name));
                    }
                } else {
                    QMessageBox::information(this, tr("Success"), tr("Virtual machine \"%1\" was successfully created!").arg(manager_config.vm[j].name));
                }
                break;
            }
        }
    } else {
        if (strncmp(manager_config.vm[VMIndex].name, QByteArray(this->ui->lineEdit->text().toUtf8()), QByteArray(this->ui->lineEdit->text().toUtf8()).size())) {
            if (!(copyPath(manager_config.vm[VMIndex].path, QDir(manager_config.vms_path).canonicalPath() + QStringLiteral("/") + manager_config.vm[VMIndex].name, true)
                  && QDir(manager_config.vm[VMIndex].path).removeRecursively())) {
                QMessageBox::critical(this, tr("Error"), tr("An error has occurred while trying to move the files for this virtual machine. Please try to move them manually."));
            }
        }
        strncpy(manager_config.vm[VMIndex].name, QByteArray(this->ui->lineEdit->text().trimmed().toUtf8()), QByteArray(this->ui->lineEdit->text().toUtf8()).size());
        auto newPath = (QDir(manager_config.vms_path).canonicalPath() + QStringLiteral("/") + manager_config.vm[VMIndex].name) + QString("/");
        strncpy(manager_config.vm[VMIndex].description, QByteArray(this->ui->lineEdit_2->text().toUtf8()), QByteArray(this->ui->lineEdit_2->text().toUtf8()).size());
        strncpy(manager_config.vm[VMIndex].path, newPath.toUtf8(), newPath.toUtf8().size());
        //this->managerMainWindow->refreshVM(VMIndex);

        managerMainWindow->ui->tableWidget->item(VMRow, 0)->setText(ui->lineEdit->text());
        managerMainWindow->ui->tableWidget->item(VMRow, 2)->setText(ui->lineEdit_2->text());
        managerMainWindow->ui->tableWidget->item(VMRow, 3)->setText(newPath);
        managerMainWindow->ui->tableWidget->setCurrentItem(managerMainWindow->ui->tableWidget->item(VMRow, 0));
        emit managerMainWindow->ui->tableWidget->itemPressed(managerMainWindow->ui->tableWidget->item(VMRow, 0));
        QMessageBox::information(this, tr("Success"), tr("Virtual machine \"%1\" was successfully modified. Please update its configuration so that any absolute paths (e.g. for hard disk images) point to the new folder.").arg(ui->lineEdit->text()));
    }
    QDialog::accept();
}

void ManagerVMAddDialog::on_pushButton_2_clicked()
{
    QDialog::reject();
}


void ManagerVMAddDialog::on_lineEdit_textChanged(const QString &arg1)
{
    if (arg1.size())
        ui->labelPathDisplay->setText(QString(manager_config.vms_path) + arg1 + '/');
    else
        ui->labelPathDisplay->setText(QString(manager_config.vms_path));

    ui->pushButton->setDisabled(arg1.size() == 0);
}


void ManagerVMAddDialog::on_checkBoxImport_clicked(bool checked)
{
    ui->lineEdit_3->setEnabled(checked);
    ui->pushButtonBrowse->setEnabled(checked);
}


void ManagerVMAddDialog::on_pushButtonBrowse_clicked()
{
    auto str = QFileDialog::getExistingDirectory(this, tr("Select a folder where your virtual machines (configs, nvr folders, etc.) will be located"), qApp->applicationDirPath());
    if (str.size())
        ui->lineEdit_3->setText(str);
}

