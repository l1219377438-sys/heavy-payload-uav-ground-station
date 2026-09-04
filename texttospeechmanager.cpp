// TextToSpeechManager.cpp
#include "TextToSpeechManager.h"

TextToSpeechManager::TextToSpeechManager(QObject *parent)
#if REBULID_HAS_TEXT_TO_SPEECH
    : QObject(parent), textToSpeech(new QTextToSpeech(this))
#else
    : QObject(parent)
#endif
{
#if REBULID_HAS_TEXT_TO_SPEECH
    // 可以选择语音
    QList<QVoice> voices = textToSpeech->availableVoices();
    if (!voices.isEmpty()) {
        textToSpeech->setVoice(voices.first());
    }
#endif
}

TextToSpeechManager::~TextToSpeechManager()
{
#if REBULID_HAS_TEXT_TO_SPEECH
    delete textToSpeech;
#endif
}

void TextToSpeechManager::speak(const QString &text)
{
#if REBULID_HAS_TEXT_TO_SPEECH
    if (textToSpeech) {
        textToSpeech->say(text);
    }
#else
    Q_UNUSED(text)
#endif
}
