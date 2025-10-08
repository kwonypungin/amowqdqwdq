#include "LoginDialog.hpp"
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QLoggingCategory>
#include <QtKeychain/keychain.h>

Q_LOGGING_CATEGORY(lcLogin, "ui.login")

namespace {
constexpr auto kServiceName = "UpbitTrader";
constexpr auto kAccessKey = "upbit_access";
constexpr auto kSecretKey = "upbit_secret";
}

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("API 로그인");
    auto* layout = new QFormLayout(this);
    access_ = new QLineEdit(this);
    secret_ = new QLineEdit(this);
    secret_->setEchoMode(QLineEdit::Password);
    remember_ = new QCheckBox("API 키 저장(시스템 키체인)", this);

    layout->addRow("Access Key", access_);
    layout->addRow("Secret Key", secret_);
    layout->addRow("", remember_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    loadStoredKeys();
}

QString LoginDialog::accessKey() const { return access_->text(); }
QString LoginDialog::secretKey() const { return secret_->text(); }
bool LoginDialog::remember() const { return remember_->isChecked(); }

void LoginDialog::accept() {
    persistKeys();
    QDialog::accept();
}

void LoginDialog::loadStoredKeys() {
    using namespace QKeychain;
    auto onResult = [this](Job* job, bool& restoredFlag, QLineEdit* target) {
        if (job->error() == NoError) {
            if (target->text().isEmpty()) target->setText(job->textData());
            restoredFlag = true;
            qCInfo(lcLogin) << "Loaded" << job->key() << "from keychain";
        } else if (job->error() != EntryNotFound) {
            qCWarning(lcLogin) << "Keychain read failed" << job->key() << job->errorString();
        }
        if (--pendingKeychainLoads_ == 0 && restoredAccess_ && restoredSecret_) {
            remember_->setChecked(true);
        }
        job->deleteLater();
    };

    pendingKeychainLoads_ = 0;
    restoredAccess_ = false;
    restoredSecret_ = false;

    auto* accessJob = new ReadPasswordJob(kServiceName, this);
    accessJob->setKey(kAccessKey);
    ++pendingKeychainLoads_;
    connect(accessJob, &Job::finished, this, [this, onResult, accessJob]() mutable {
        bool& flag = restoredAccess_;
        onResult(accessJob, flag, access_);
    });
    accessJob->start();

    auto* secretJob = new ReadPasswordJob(kServiceName, this);
    secretJob->setKey(kSecretKey);
    ++pendingKeychainLoads_;
    connect(secretJob, &Job::finished, this, [this, onResult, secretJob]() mutable {
        bool& flag = restoredSecret_;
        onResult(secretJob, flag, secret_);
    });
    secretJob->start();
}

void LoginDialog::persistKeys() {
    using namespace QKeychain;
    if (!remember_->isChecked()) {
        deleteStoredKeys();
        return;
    }

    auto* writeAccess = new WritePasswordJob(kServiceName, this);
    writeAccess->setKey(kAccessKey);
    writeAccess->setTextData(access_->text());
    connect(writeAccess, &Job::finished, this, [](Job* job) {
        if (job->error() != NoError) {
            qCWarning(lcLogin) << "Keychain write failed" << job->key() << job->errorString();
        } else {
            qCInfo(lcLogin) << "Stored" << job->key() << "in keychain";
        }
        job->deleteLater();
    });
    writeAccess->start();

    auto* writeSecret = new WritePasswordJob(kServiceName, this);
    writeSecret->setKey(kSecretKey);
    writeSecret->setTextData(secret_->text());
    connect(writeSecret, &Job::finished, this, [](Job* job) {
        if (job->error() != NoError) {
            qCWarning(lcLogin) << "Keychain write failed" << job->key() << job->errorString();
        } else {
            qCInfo(lcLogin) << "Stored" << job->key() << "in keychain";
        }
        job->deleteLater();
    });
    writeSecret->start();
}

void LoginDialog::deleteStoredKeys() {
    using namespace QKeychain;
    auto cleanup = [](Job* job) {
        if (job->error() != NoError && job->error() != EntryNotFound) {
            qCWarning(lcLogin) << "Keychain delete failed" << job->key() << job->errorString();
        } else {
            qCInfo(lcLogin) << "Deleted keychain entry" << job->key();
        }
        job->deleteLater();
    };

    auto* delAccess = new DeletePasswordJob(kServiceName, this);
    delAccess->setKey(kAccessKey);
    connect(delAccess, &Job::finished, this, cleanup);
    delAccess->start();

    auto* delSecret = new DeletePasswordJob(kServiceName, this);
    delSecret->setKey(kSecretKey);
    connect(delSecret, &Job::finished, this, cleanup);
    delSecret->start();
}
