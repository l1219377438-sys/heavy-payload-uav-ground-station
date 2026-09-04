#include "filemanager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

FileManager::FileManager(QObject *parent) : QObject(parent)
{
}

bool FileManager::saveText(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法打开文件进行写入:" << file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << text;
    file.close();
    qDebug() << "成功保存文件:" << filePath;
    return true;
}

QString FileManager::readText(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开文件进行读取:" << file.errorString();
        return QString();
    }
    QTextStream in(&file);
    QString content = in.readAll();
    file.close();
    qDebug() << "成功读取文件:" << filePath;
    return content;
}
