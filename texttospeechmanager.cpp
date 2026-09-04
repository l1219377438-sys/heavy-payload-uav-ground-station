// TextToSpeechManager.cpp
#include "TextToSpeechManager.h"

TextToSpeechManager::TextToSpeechManager(QObject *parent)
    : QObject(parent), textToSpeech(new QTextToSpeech(this))
{
    // 可以选择语音
    QList<QVoice> voices = textToSpeech->availableVoices();
    if (!voices.isEmpty()) {
        textToSpeech->setVoice(voices.first());
    }
}

TextToSpeechManager::~TextToSpeechManager()
{
    delete textToSpeech;
}

void TextToSpeechManager::speak(const QString &text)
{
    if (textToSpeech) {
        textToSpeech->say(text);
    }
}
