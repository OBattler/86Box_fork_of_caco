#ifndef QT_MANAGER_VMADDDIALOG_H
#define QT_MANAGER_VMADDDIALOG_H

#include "qt_manager_mainwindow.h"

#include <QDialog>

namespace Ui {
class ManagerVMAddDialog;
}

class ManagerVMAddDialog : public QDialog {
    Q_OBJECT

public:
    ManagerVMAddDialog(ManagerMainWindow *parent = nullptr, int i = -1, int row = -1, int clone = -1);
    ~ManagerVMAddDialog();

private slots:
    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_lineEdit_textChanged(const QString &arg1);

    void on_checkBoxImport_clicked(bool checked);

    void on_pushButtonBrowse_clicked();

private:
    Ui::ManagerVMAddDialog *ui;
    ManagerMainWindow* managerMainWindow;

    bool editingVM = false;
    int cloneVM = -1;
    int VMIndex = 0;
    int VMRow = -1;

    friend class ManagerMainWindow;
};

#endif // QT_MANAGER_VMADDDIALOG_H
