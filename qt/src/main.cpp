#include <QApplication>
#include <QFile>
#include "LoginDialog.hpp"
#include "MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("UpbitTrader");
    app.setStyle("Fusion");

    QFile f(":/qss/dark.qss");
    if (f.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
    }

    LoginDialog dlg;
    if (dlg.exec() != QDialog::Accepted) return 0;
    MainWindow w(dlg.accessKey(), dlg.secretKey());
    w.show();
    return app.exec();
}

