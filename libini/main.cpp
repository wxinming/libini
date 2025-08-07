#include <QtCore/QCoreApplication>
#include "libini.h"
#include <QDebug>
#include <thread>
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    //�����ڵ�ǰĿ¼�´���config.ini�ļ�, �Ƿ���Ҫ����ֵ
    Ini ini("config.ini", false);

    // ����������򴴽�
    ini.newValue("app/version", "1.0", "Ӧ�ó���汾");

    ini.beginGroup("app");
    ini.newValue("file_size", "64mb", "�ļ���С"); // ��app/file_size��ͬ
    ini.newValue("file_name", "libini", "�ļ�����");
    ini.endGroup();

    std::thread t0([&]() {
        for (int i = 0; i < 1000; ++i) {
            ini.beginGroup("app"); // ÿ���̶߳��ж�����������, �����ڴ˴�t0�߳���beginGroup��t1�߳��в���ͻ
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
