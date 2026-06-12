#include "providers/translation/Translator.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace chatterino {

namespace {

constexpr int MAX_TRANSLATION_TEXT_LENGTH = 1200;

std::optional<TranslationResult> parseGoogleTranslationResult(
    const NetworkResult &result)
{
    const auto root = result.parseJsonArray();
    const auto segments = root.isEmpty() ? QJsonArray{} : root.at(0).toArray();
    if (segments.isEmpty())
    {
        return std::nullopt;
    }

    QString translated;
    for (const auto segmentValue : segments)
    {
        const auto segment = segmentValue.toArray();
        if (!segment.isEmpty())
        {
            translated += segment.at(0).toString();
        }
    }
    translated = translated.trimmed();
    if (translated.isEmpty())
    {
        return std::nullopt;
    }

    auto detectedLanguage = root.size() > 2
                                ? normalizedLanguageCode(root.at(2).toString())
                                : QString{};

    return TranslationResult{
        .translatedText = translated,
        .detectedLanguage = detectedLanguage,
    };
}

}  // namespace

QString normalizedLanguageCode(QString language)
{
    language = language.trimmed().toLower();
    language.replace('_', '-');

    return language;
}

const std::vector<TranslationLanguage> &supportedTranslationLanguages()
{
    static const std::vector<TranslationLanguage> languages{
        {.code = u"en"_s, .name = u"English"_s},
        {.code = u"es"_s, .name = u"Spanish"_s},
        {.code = u"pt"_s, .name = u"Portuguese"_s},
        {.code = u"fr"_s, .name = u"French"_s},
        {.code = u"de"_s, .name = u"German"_s},
        {.code = u"it"_s, .name = u"Italian"_s},
        {.code = u"nl"_s, .name = u"Dutch"_s},
        {.code = u"pl"_s, .name = u"Polish"_s},
        {.code = u"tr"_s, .name = u"Turkish"_s},
        {.code = u"ru"_s, .name = u"Russian"_s},
        {.code = u"ja"_s, .name = u"Japanese"_s},
        {.code = u"ko"_s, .name = u"Korean"_s},
        {.code = u"zh-cn"_s, .name = u"Chinese (Simplified)"_s},
        {.code = u"zh-tw"_s, .name = u"Chinese (Traditional)"_s},
        {.code = u"ar"_s, .name = u"Arabic"_s},
        {.code = u"af"_s, .name = u"Afrikaans"_s},
        {.code = u"sq"_s, .name = u"Albanian"_s},
        {.code = u"am"_s, .name = u"Amharic"_s},
        {.code = u"hy"_s, .name = u"Armenian"_s},
        {.code = u"az"_s, .name = u"Azerbaijani"_s},
        {.code = u"eu"_s, .name = u"Basque"_s},
        {.code = u"be"_s, .name = u"Belarusian"_s},
        {.code = u"bn"_s, .name = u"Bengali"_s},
        {.code = u"bs"_s, .name = u"Bosnian"_s},
        {.code = u"bg"_s, .name = u"Bulgarian"_s},
        {.code = u"ca"_s, .name = u"Catalan"_s},
        {.code = u"ceb"_s, .name = u"Cebuano"_s},
        {.code = u"co"_s, .name = u"Corsican"_s},
        {.code = u"hr"_s, .name = u"Croatian"_s},
        {.code = u"cs"_s, .name = u"Czech"_s},
        {.code = u"da"_s, .name = u"Danish"_s},
        {.code = u"eo"_s, .name = u"Esperanto"_s},
        {.code = u"et"_s, .name = u"Estonian"_s},
        {.code = u"fi"_s, .name = u"Finnish"_s},
        {.code = u"fy"_s, .name = u"Frisian"_s},
        {.code = u"gl"_s, .name = u"Galician"_s},
        {.code = u"ka"_s, .name = u"Georgian"_s},
        {.code = u"el"_s, .name = u"Greek"_s},
        {.code = u"gu"_s, .name = u"Gujarati"_s},
        {.code = u"ht"_s, .name = u"Haitian Creole"_s},
        {.code = u"ha"_s, .name = u"Hausa"_s},
        {.code = u"haw"_s, .name = u"Hawaiian"_s},
        {.code = u"iw"_s, .name = u"Hebrew"_s},
        {.code = u"hi"_s, .name = u"Hindi"_s},
        {.code = u"hu"_s, .name = u"Hungarian"_s},
        {.code = u"is"_s, .name = u"Icelandic"_s},
        {.code = u"id"_s, .name = u"Indonesian"_s},
        {.code = u"ga"_s, .name = u"Irish"_s},
        {.code = u"jv"_s, .name = u"Javanese"_s},
        {.code = u"kn"_s, .name = u"Kannada"_s},
        {.code = u"kk"_s, .name = u"Kazakh"_s},
        {.code = u"km"_s, .name = u"Khmer"_s},
        {.code = u"ku"_s, .name = u"Kurdish"_s},
        {.code = u"ky"_s, .name = u"Kyrgyz"_s},
        {.code = u"lo"_s, .name = u"Lao"_s},
        {.code = u"la"_s, .name = u"Latin"_s},
        {.code = u"lv"_s, .name = u"Latvian"_s},
        {.code = u"lt"_s, .name = u"Lithuanian"_s},
        {.code = u"lb"_s, .name = u"Luxembourgish"_s},
        {.code = u"mk"_s, .name = u"Macedonian"_s},
        {.code = u"mg"_s, .name = u"Malagasy"_s},
        {.code = u"ms"_s, .name = u"Malay"_s},
        {.code = u"ml"_s, .name = u"Malayalam"_s},
        {.code = u"mt"_s, .name = u"Maltese"_s},
        {.code = u"mi"_s, .name = u"Maori"_s},
        {.code = u"mr"_s, .name = u"Marathi"_s},
        {.code = u"mn"_s, .name = u"Mongolian"_s},
        {.code = u"my"_s, .name = u"Myanmar (Burmese)"_s},
        {.code = u"ne"_s, .name = u"Nepali"_s},
        {.code = u"no"_s, .name = u"Norwegian"_s},
        {.code = u"ps"_s, .name = u"Pashto"_s},
        {.code = u"fa"_s, .name = u"Persian"_s},
        {.code = u"ro"_s, .name = u"Romanian"_s},
        {.code = u"sr"_s, .name = u"Serbian"_s},
        {.code = u"si"_s, .name = u"Sinhala"_s},
        {.code = u"sk"_s, .name = u"Slovak"_s},
        {.code = u"sl"_s, .name = u"Slovenian"_s},
        {.code = u"so"_s, .name = u"Somali"_s},
        {.code = u"su"_s, .name = u"Sundanese"_s},
        {.code = u"sw"_s, .name = u"Swahili"_s},
        {.code = u"sv"_s, .name = u"Swedish"_s},
        {.code = u"ta"_s, .name = u"Tamil"_s},
        {.code = u"te"_s, .name = u"Telugu"_s},
        {.code = u"th"_s, .name = u"Thai"_s},
        {.code = u"tl"_s, .name = u"Tagalog"_s},
        {.code = u"uk"_s, .name = u"Ukrainian"_s},
        {.code = u"ur"_s, .name = u"Urdu"_s},
        {.code = u"uz"_s, .name = u"Uzbek"_s},
        {.code = u"vi"_s, .name = u"Vietnamese"_s},
        {.code = u"cy"_s, .name = u"Welsh"_s},
        {.code = u"xh"_s, .name = u"Xhosa"_s},
        {.code = u"yi"_s, .name = u"Yiddish"_s},
        {.code = u"yo"_s, .name = u"Yoruba"_s},
        {.code = u"zu"_s, .name = u"Zulu"_s},
    };

    return languages;
}

QString translationLanguageCodeFromInput(QString language)
{
    const auto normalized = normalizedLanguageCode(language);
    if (normalized.isEmpty())
    {
        return {};
    }

    const auto &languages = supportedTranslationLanguages();
    const auto codeIt =
        std::ranges::find_if(languages, [&](const TranslationLanguage &item) {
            return item.code == normalized;
        });
    if (codeIt != languages.end())
    {
        return codeIt->code;
    }

    const auto nameIt =
        std::ranges::find_if(languages, [&](const TranslationLanguage &item) {
            return item.name.compare(language.trimmed(), Qt::CaseInsensitive) ==
                   0;
        });
    if (nameIt != languages.end())
    {
        return nameIt->code;
    }

    static const QHash<QString, QString> aliases{
        {"zh", u"zh-cn"_s},
        {"zh-hans", u"zh-cn"_s},
        {"cn", u"zh-cn"_s},
        {"chinese", u"zh-cn"_s},
        {"simplified-chinese", u"zh-cn"_s},
        {"zh-hant", u"zh-tw"_s},
        {"tw", u"zh-tw"_s},
        {"traditional-chinese", u"zh-tw"_s},
        {"he", u"iw"_s},
        {"fil", u"tl"_s},
        {"filipino", u"tl"_s},
        {"burmese", u"my"_s},
        {"br", u"pt"_s},
        {"pt-br", u"pt"_s},
        {"jp", u"ja"_s},
        {"kr", u"ko"_s},
        {"ua", u"uk"_s},
    };

    return aliases.value(normalized);
}

bool isSupportedTranslationLanguage(const QString &language)
{
    return !translationLanguageCodeFromInput(language).isEmpty();
}

QString normalizedTranslationTargetLanguage(QString language)
{
    auto code = translationLanguageCodeFromInput(std::move(language));
    if (code.isEmpty())
    {
        return u"en"_s;
    }

    return code;
}

QString translationLanguageName(const QString &language)
{
    const auto normalized = translationLanguageCodeFromInput(language);
    if (normalized.isEmpty())
    {
        return {};
    }

    const auto &languages = supportedTranslationLanguages();
    const auto it =
        std::ranges::find_if(languages, [&](const TranslationLanguage &item) {
            return item.code == normalized;
        });
    if (it != languages.end())
    {
        return it->name;
    }

    return normalized.toUpper();
}

QString trimTextForTranslation(QString text)
{
    text = text.trimmed();
    if (text.size() > MAX_TRANSLATION_TEXT_LENGTH)
    {
        text = text.left(MAX_TRANSLATION_TEXT_LENGTH);
    }

    return text;
}

void requestTextTranslation(const QString &text, const QString &targetLanguage,
                            QObject *caller,
                            const TranslationSuccessCallback &onSuccess,
                            const TranslationErrorCallback &onError,
                            const TranslationFinishedCallback &onFinished)
{
    const auto requestText = trimTextForTranslation(text);
    if (requestText.isEmpty())
    {
        if (onError)
        {
            onError(u"There is no text to translate."_s);
        }
        if (onFinished)
        {
            onFinished();
        }
        return;
    }

    const auto target = normalizedTranslationTargetLanguage(targetLanguage);

    QUrl url(u"https://translate.googleapis.com/translate_a/single"_s);
    QUrlQuery query;
    query.addQueryItem(u"client"_s, u"gtx"_s);
    query.addQueryItem(u"sl"_s, u"auto"_s);
    query.addQueryItem(u"tl"_s, target);
    query.addQueryItem(u"dt"_s, u"t"_s);
    query.addQueryItem(u"q"_s, requestText);
    url.setQuery(query);

    auto request =
        NetworkRequest(url)
            .timeout(8000)
            .onSuccess([onSuccess, onError](const NetworkResult &result) {
                const auto parsed = parseGoogleTranslationResult(result);
                if (!parsed.has_value())
                {
                    if (onError)
                    {
                        onError(u"Translation failed: invalid response."_s);
                    }
                    return;
                }

                if (onSuccess)
                {
                    onSuccess(*parsed);
                }
            })
            .onError([onError](const NetworkResult &result) {
                (void)result;
                if (onError)
                {
                    onError(u"Translation failed: network error."_s);
                }
            })
            .finally([onFinished] {
                if (onFinished)
                {
                    onFinished();
                }
            });

    if (caller != nullptr)
    {
        std::move(request).caller(caller).execute();
    }
    else
    {
        std::move(request).execute();
    }
}

}  // namespace chatterino
