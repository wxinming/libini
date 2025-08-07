#include <QtCore/QCoreApplication>
#include "libini.h"
#include <QDebug>
#include <thread>
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    //将会在当前目录下创建config.ini文件, 是否需要加密值
    Ini ini("config.ini", false);

    // 如果不存在则创建
    ini.newValue("app/version", "1.0", "应用程序版本");

    ini.beginGroup("app");
    ini.newValue("file_size", "64mb", "文件大小"); // 与app/file_size相同
    ini.newValue("file_name", "libini", "文件名称");
    ini.endGroup();

    std::thread t0([&]() {
        for (int i = 0; i < 1000; ++i) {
            ini.beginGroup("app"); // 每个线程都有独立的上下文, 所以在此处t0线程中beginGroup与t1线程中不冲突
            auto fileSize = ini.value("file_size").toString();
            ini.endGroup();
            qDebug() << fileSize;
        }
    });

    std::thread t1([&]() {
        for (int i = 0; i < 1000; ++i) {
            ini.beginGroup("app");
            ini.setValue("file_size", QString::number(i) + "mb");
            ini.endGroup();
        }
    });

    t0.join();
    t1.join();
    return app.exec();
}
