#include <QCoreApplication>
#include <QtDebug>
#include <core/Version.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qDebug() << "NetworkLib version:" << NetCore::versionString();
    return 0;
}
