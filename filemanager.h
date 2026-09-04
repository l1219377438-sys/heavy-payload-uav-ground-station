#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>
#include <QString>

class FileManager : public QObject
{
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    // 根据传入的路径保存文本
    Q_INVOKABLE bool saveText(const QString &filePath, const QString &text);
    // 根据传入的路径读取文本，读取失败返回空字符串
    Q_INVOKABLE QString readText(const QString &filePath);
};

#endif // FILEMANAGER_H
