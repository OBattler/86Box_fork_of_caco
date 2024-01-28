#ifndef QT_MANAGER_SETTINGS_H
#define QT_MANAGER_SETTINGS_H

#include <QDialog>
#include <QAbstractButton>
#include <QPushButton>

namespace Ui {
class ManagerSettings;
}

class ManagerSettings : public QDialog {
    Q_OBJECT

public:
    explicit ManagerSettings(QWidget *parent = nullptr);
    ~ManagerSettings();

protected:
    void reject() override;

private slots:
    void on_checkBoxLoggingEnabled_clicked(bool checked);
    void save();

    void on_buttonBox_clicked(QAbstractButton *button);

    void on_checkBoxMinimizeTrayIcon_stateChanged(int arg1);

    void on_checkBoxCloseTrayIcon_stateChanged(int arg1);

    void on_checkBoxMinimizeVMStarted_stateChanged(int arg1);

    void on_checkBoxLoggingEnabled_stateChanged(int arg1);

    void on_checkBoxEnableGridLines_stateChanged(int arg1);

    void on_pushButtonDefaults_clicked();

    void on_lineEditVMPath_textChanged(const QString &arg1);

    void on_lineEditLoggingPath_textChanged(const QString &arg1);

    void on_pushButtonBrowseVMPath_clicked();

    void on_pushButton_clicked();

private:
    Ui::ManagerSettings *ui;
    QPushButton* applyButton;

    bool settingsChanged();
};

#endif // QT_MANAGER_SETTINGS_H
