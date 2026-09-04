// TextToSpeechManager.h
#ifndef TEXTTOSPEECHMANAGER_H
#define TEXTTOSPEECHMANAGER_H

#include <QObject>
#if REBULID_HAS_TEXT_TO_SPEECH
#include <QTextToSpeech>
#include <QVoice>
#endif

class TextToSpeechManager : public QObject
{
    Q_OBJECT
public:
    explicit TextToSpeechManager(QObject *parent = nullptr);
    ~TextToSpeechManager();

    Q_INVOKABLE void speak(const QString &text);

private:
#if REBULID_HAS_TEXT_TO_SPEECH
    QTextToSpeech *textToSpeech;
#endif
};

#endif // TEXTTOSPEECHMANAGER_H
