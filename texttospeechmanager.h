// TextToSpeechManager.h
#ifndef TEXTTOSPEECHMANAGER_H
#define TEXTTOSPEECHMANAGER_H

#include <QObject>
#include <QTextToSpeech>
#include <QVoice>

class TextToSpeechManager : public QObject
{
    Q_OBJECT
public:
    explicit TextToSpeechManager(QObject *parent = nullptr);
    ~TextToSpeechManager();

    Q_INVOKABLE void speak(const QString &text);

private:
    QTextToSpeech *textToSpeech;
};

#endif // TEXTTOSPEECHMANAGER_H
