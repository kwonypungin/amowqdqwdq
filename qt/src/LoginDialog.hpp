#pragma once
#include <QDialog>

class QLineEdit;
class QCheckBox;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);
    QString accessKey() const;
    QString secretKey() const;
    bool remember() const;
    void accept() override;
private:
    QLineEdit* access_{nullptr};
    QLineEdit* secret_{nullptr};
    QCheckBox* remember_{nullptr};
    void loadStoredKeys();
    void persistKeys();
    void deleteStoredKeys();
    int pendingKeychainLoads_{0};
    bool restoredAccess_{false};
    bool restoredSecret_{false};
};
