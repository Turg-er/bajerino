#pragma once

#include <QString>

#include <functional>
#include <vector>

class QObject;

namespace chatterino {

struct TranslationLanguage {
    QString code;
    QString name;
};

struct TranslationResult {
    QString translatedText;
    QString detectedLanguage;
};

using TranslationSuccessCallback =
    std::function<void(const TranslationResult &result)>;
using TranslationErrorCallback = std::function<void(const QString &error)>;
using TranslationFinishedCallback = std::function<void()>;

QString normalizedLanguageCode(QString language);
QString translationLanguageCodeFromInput(QString language);
QString normalizedTranslationTargetLanguage(QString language);
QString translationLanguageName(const QString &language);
bool isSupportedTranslationLanguage(const QString &language);
QString trimTextForTranslation(QString text);
const std::vector<TranslationLanguage> &supportedTranslationLanguages();

void requestTextTranslation(const QString &text, const QString &targetLanguage,
                            QObject *caller,
                            const TranslationSuccessCallback &onSuccess,
                            const TranslationErrorCallback &onError,
                            const TranslationFinishedCallback &onFinished = {});

}  // namespace chatterino
