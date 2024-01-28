#include "qt_manager_settings.h"
#include "ui_qt_manager_settings.h"

#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QMessageBox>
#include <QFileDialog>

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/ui.h>
#include <86box/video.h>
}

ManagerSettings::ManagerSettings(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ManagerSettings)
{
    ui->setupUi(this);

    ui->checkBoxCloseTrayIcon->setChecked(!!manager_config.close_to_tray_icon);
    ui->checkBoxMinimizeTrayIcon->setChecked(!!manager_config.minimize_to_tray_icon);
    ui->checkBoxMinimizeVMStarted->setChecked(!!manager_config.minimize_when_vm_started);
    ui->checkBoxEnableGridLines->setChecked(!!manager_config.enable_grid_lines);
    ui->checkBoxLoggingEnabled->setChecked(!!manager_config.enable_logging);
    ui->lineEditLoggingPath->setEnabled(!!manager_config.enable_logging);

    ui->lineEditLoggingPath->setText(manager_config.logging_path);
    ui->lineEditVMPath->setText(manager_config.vms_path);

    ui->lineEditLoggingPath->setValidator(new QRegularExpressionValidator(QRegularExpression("[^\\\\/:*?\"<>|]*")));
    ui->lineEditVMPath->setValidator(new QRegularExpressionValidator(QRegularExpression("[^\\\\/:*?\"<>|]*")));

    connect(this, &ManagerSettings::accepted, this, &ManagerSettings::save);

    applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    applyButton->setDisabled(true);

    this->setEnabled(true);
}

ManagerSettings::~ManagerSettings()
{
    delete ui;
}

void ManagerSettings::reject()
{
    bool changed = settingsChanged();

    if (changed) {
        auto res = QMessageBox::question(this, tr("Unsaved changes"), tr("Would you like to save the changes you've made to the settings?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        switch (res) {
            default:
            case QMessageBox::Cancel:
                return;
            case QMessageBox::No:
                QDialog::reject();
                return;
            case QMessageBox::Yes:
                save();
                QDialog::accept();
                return;
        }
    } else
        QDialog::reject();
}

void ManagerSettings::save()
{
    manager_config.close_to_tray_icon = ui->checkBoxCloseTrayIcon->isChecked();
    manager_config.minimize_to_tray_icon = ui->checkBoxMinimizeTrayIcon->isChecked();
    manager_config.minimize_when_vm_started = ui->checkBoxMinimizeVMStarted->isChecked();
    manager_config.enable_grid_lines = ui->checkBoxEnableGridLines->isChecked();
    manager_config.enable_logging = ui->checkBoxLoggingEnabled->isChecked();

    strncpy(manager_config.vms_path, ui->lineEditVMPath->text().toUtf8(), sizeof(manager_config.vms_path));
    strncpy(manager_config.logging_path, ui->lineEditLoggingPath->text().toUtf8(), sizeof(manager_config.logging_path));
    config_save_global();

    applyButton->setDisabled(true);
}

bool ManagerSettings::settingsChanged()
{
    bool changed = false;
    auto vmPath = ui->lineEditVMPath->text().toUtf8();
    auto loggingPath = ui->lineEditLoggingPath->text().toUtf8();

    changed |= (manager_config.close_to_tray_icon != ui->checkBoxCloseTrayIcon->isChecked());
    changed |= (manager_config.minimize_to_tray_icon != ui->checkBoxMinimizeTrayIcon->isChecked());
    changed |= (manager_config.minimize_when_vm_started != ui->checkBoxMinimizeVMStarted->isChecked());
    changed |= (manager_config.enable_grid_lines != ui->checkBoxEnableGridLines->isChecked());
    changed |= (manager_config.enable_logging != ui->checkBoxLoggingEnabled->isChecked());

    changed |= !!strncmp(manager_config.vms_path, ui->lineEditVMPath->text().toUtf8(), sizeof(manager_config.vms_path));
    changed |= !!strncmp(manager_config.logging_path, ui->lineEditLoggingPath->text().toUtf8(), sizeof(manager_config.logging_path));

    applyButton->setDisabled(!changed);

    return changed;
}

void ManagerSettings::on_checkBoxLoggingEnabled_clicked(bool checked)
{
    ui->lineEditLoggingPath->setEnabled(checked);
}

void ManagerSettings::on_buttonBox_clicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole)
        save();
}

void ManagerSettings::on_checkBoxMinimizeTrayIcon_stateChanged(int arg1)
{
    settingsChanged();
}

void ManagerSettings::on_checkBoxCloseTrayIcon_stateChanged(int arg1)
{
    settingsChanged();
}

void ManagerSettings::on_checkBoxMinimizeVMStarted_stateChanged(int arg1)
{
    settingsChanged();
}

void ManagerSettings::on_checkBoxLoggingEnabled_stateChanged(int arg1)
{
    settingsChanged();
}

void ManagerSettings::on_checkBoxEnableGridLines_stateChanged(int arg1)
{
    settingsChanged();
}

void ManagerSettings::on_pushButtonDefaults_clicked()
{
    if (QMessageBox::warning(this, tr("Settings will be reset"), tr("All settings will be reset to their default values. Do you wish to continue?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        char vms_path_default[1024] = { 0 };

        ui->checkBoxCloseTrayIcon->setChecked(0);
        ui->checkBoxMinimizeTrayIcon->setChecked(0);
        ui->checkBoxMinimizeVMStarted->setChecked(0);
        ui->checkBoxEnableGridLines->setChecked(0);
        ui->checkBoxLoggingEnabled->setChecked(0);
        ui->lineEditLoggingPath->setEnabled(0);

        plat_get_global_config_dir(vms_path_default);
        path_slash(vms_path_default);
        strncat(vms_path_default, "86Box VMs", sizeof(vms_path_default) - 1);
        strncat(vms_path_default, path_get_slash(vms_path_default), sizeof(vms_path_default) - 1);

        ui->lineEditVMPath->setText(vms_path_default);
        ui->lineEditLoggingPath->setText("");

        settingsChanged();
    }
}

void ManagerSettings::on_lineEditVMPath_textChanged(const QString &arg1)
{
    settingsChanged();
}

void ManagerSettings::on_lineEditLoggingPath_textChanged(const QString &arg1)
{
    settingsChanged();
}


void ManagerSettings::on_pushButtonBrowseVMPath_clicked()
{
    auto str = QFileDialog::getExistingDirectory(this, tr("Select a folder where your virtual machines (configs, nvr folders, etc.) will be located"), qApp->applicationDirPath());
    if (str.size())
        ui->lineEditVMPath->setText(str);
}


void ManagerSettings::on_pushButton_clicked()
{
    auto str = QFileDialog::getSaveFileName(this, tr("Select a file where 86Box logs will be saved"), qApp->applicationDirPath(), "Log files (*.log)|*.log");
    if (str.size())
        ui->lineEditLoggingPath->setText(str);
}

