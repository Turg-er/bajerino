#include "providers/translation/Translator.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/translation/TranslationRequest.hpp"
#include "singletons/Settings.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace chatterino {

namespace {

constexpr int MAX_TRANSLATION_TEXT_LENGTH = 1200;
constexpr qsizetype MAX_TRANSLATION_RESPONSE_BYTES = 1024 * 1024;

const QHash<QChar, QString> &morseAlphabet()
{
    static const QHash<QChar, QString> alphabet{
        {'A', QStringLiteral(".-")},      {'B', QStringLiteral("-...")},
        {'C', QStringLiteral("-.-.")},    {'D', QStringLiteral("-..")},
        {'E', QStringLiteral(".")},       {'F', QStringLiteral("..-.")},
        {'G', QStringLiteral("--.")},     {'H', QStringLiteral("....")},
        {'I', QStringLiteral("..")},      {'J', QStringLiteral(".---")},
        {'K', QStringLiteral("-.-")},     {'L', QStringLiteral(".-..")},
        {'M', QStringLiteral("--")},      {'N', QStringLiteral("-.")},
        {'O', QStringLiteral("---")},     {'P', QStringLiteral(".--.")},
        {'Q', QStringLiteral("--.-")},    {'R', QStringLiteral(".-.")},
        {'S', QStringLiteral("...")},     {'T', QStringLiteral("-")},
        {'U', QStringLiteral("..-")},     {'V', QStringLiteral("...-")},
        {'W', QStringLiteral(".--")},     {'X', QStringLiteral("-..-")},
        {'Y', QStringLiteral("-.--")},    {'Z', QStringLiteral("--..")},
        {'0', QStringLiteral("-----")},   {'1', QStringLiteral(".----")},
        {'2', QStringLiteral("..---")},   {'3', QStringLiteral("...--")},
        {'4', QStringLiteral("....-")},   {'5', QStringLiteral(".....")},
        {'6', QStringLiteral("-....")},   {'7', QStringLiteral("--...")},
        {'8', QStringLiteral("---..")},   {'9', QStringLiteral("----.")},
        {'.', QStringLiteral(".-.-.-")},  {',', QStringLiteral("--..--")},
        {'?', QStringLiteral("..--..")},  {'\'', QStringLiteral(".----.")},
        {'!', QStringLiteral("-.-.--")},  {'/', QStringLiteral("-..-.")},
        {'(', QStringLiteral("-.--.")},   {')', QStringLiteral("-.--.-")},
        {'&', QStringLiteral(".-...")},   {':', QStringLiteral("---...")},
        {';', QStringLiteral("-.-.-.")},  {'=', QStringLiteral("-...-")},
        {'+', QStringLiteral(".-.-.")},   {'-', QStringLiteral("-....-")},
        {'_', QStringLiteral("..--.-")},  {'"', QStringLiteral(".-..-.")},
        {'$', QStringLiteral("...-..-")}, {'@', QStringLiteral(".--.-.")},
    };
    return alphabet;
}

const QHash<QString, QString> &morseDecodingAlphabet()
{
    static const auto alphabet = [] {
        QHash<QString, QString> decoded;
        for (auto it = morseAlphabet().cbegin(); it != morseAlphabet().cend();
             ++it)
        {
            decoded.insert(it.value(), QString(it.key()));
        }
        return decoded;
    }();
    return alphabet;
}

const QRegularExpression &protectedEmotePlaceholder()
{
    static const QRegularExpression placeholder(
        QStringLiteral(R"(^MOLTOEMOTE\d{4}$)"));
    return placeholder;
}

QString normalizeMorseNotation(QString text)
{
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        switch (text.at(i).unicode())
        {
            case 0x00B7:
            case 0x2022:
            case 0x22C5:
                text[i] = QLatin1Char('.');
                break;
            case 0x2212:
            case 0x2013:
            case 0x2014:
            case '_':
                text[i] = QLatin1Char('-');
                break;
            default:
                break;
        }
    }
    return text.trimmed();
}

QString encodeMorseWord(const QString &word)
{
    if (protectedEmotePlaceholder().match(word).hasMatch())
    {
        return word;
    }

    const auto normalized =
        word.normalized(QString::NormalizationForm_D).toUpper();
    QStringList encoded;
    encoded.reserve(normalized.size());
    const auto &alphabet = morseAlphabet();
    for (const auto codePoint : normalized.toUcs4())
    {
        if (QChar::category(static_cast<char32_t>(codePoint)) ==
            QChar::Mark_NonSpacing)
        {
            continue;
        }

        if (codePoint <= 0xFFFF)
        {
            const QChar character(static_cast<char16_t>(codePoint));
            const auto it = alphabet.constFind(character);
            encoded.push_back(it == alphabet.cend() ? QString(character) : *it);
        }
        else
        {
            const auto unicodeCharacter = static_cast<char32_t>(codePoint);
            encoded.push_back(QString::fromUcs4(&unicodeCharacter, 1));
        }
    }
    return encoded.join(QLatin1Char(' '));
}

QString encodeTextToMorse(const QString &text)
{
    QStringList encodedWords;
    for (const auto &word : text.split(
             QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
    {
        encodedWords.push_back(encodeMorseWord(word));
    }
    return encodedWords.join(QStringLiteral(" / "));
}

std::optional<QString> decodeTextFromMorse(const QString &text,
                                           int minimumTokenCount,
                                           bool requireLanguageConfidence)
{
    const auto normalized = normalizeMorseNotation(text);
    if (normalized.isEmpty())
    {
        return std::nullopt;
    }

    static const QRegularExpression separator(QStringLiteral(R"([/|\s]+)"));
    static const QRegularExpression morseToken(QStringLiteral(R"(^[.-]+$)"));

    QString currentWord;
    QStringList decodedWords;
    QSet<QString> distinctMorseTokens;
    int morseTokenCount = 0;
    bool hasExplicitWordSeparator = false;
    bool hasCompactSos = false;

    const auto finishWord = [&] {
        if (!currentWord.isEmpty())
        {
            decodedWords.push_back(std::move(currentWord));
            currentWord.clear();
        }
    };

    const auto decodeToken = [&](const QString &token) {
        if (token.isEmpty())
        {
            return true;
        }

        if (protectedEmotePlaceholder().match(token).hasMatch())
        {
            finishWord();
            decodedWords.push_back(token);
            return true;
        }

        if (morseToken.match(token).hasMatch())
        {
            if (token == QStringLiteral("...---..."))
            {
                currentWord += QStringLiteral("SOS");
                distinctMorseTokens.insert(QStringLiteral("..."));
                distinctMorseTokens.insert(QStringLiteral("---"));
                morseTokenCount += 3;
                hasCompactSos = true;
                return true;
            }

            const auto it = morseDecodingAlphabet().constFind(token);
            if (it == morseDecodingAlphabet().cend())
            {
                return false;
            }

            currentWord += *it;
            distinctMorseTokens.insert(token);
            ++morseTokenCount;
            return true;
        }

        for (const auto codePoint : token.toUcs4())
        {
            if (QChar::isLetterOrNumber(static_cast<char32_t>(codePoint)))
            {
                return false;
            }
        }

        finishWord();
        decodedWords.push_back(token);
        return true;
    };

    qsizetype tokenStart = 0;
    auto separators = separator.globalMatch(normalized);
    while (separators.hasNext())
    {
        const auto match = separators.next();
        if (!decodeToken(
                normalized.mid(tokenStart, match.capturedStart() - tokenStart)))
        {
            return std::nullopt;
        }

        if (match.captured() != QStringLiteral(" "))
        {
            hasExplicitWordSeparator = true;
            finishWord();
        }
        tokenStart = match.capturedEnd();
    }

    if (!decodeToken(normalized.mid(tokenStart)))
    {
        return std::nullopt;
    }
    finishWord();

    if (morseTokenCount < minimumTokenCount || decodedWords.isEmpty())
    {
        return std::nullopt;
    }

    const auto decoded = decodedWords.join(QLatin1Char(' '));
    if (requireLanguageConfidence && !hasCompactSos &&
        !hasExplicitWordSeparator)
    {
        static const QRegularExpression vowel(QStringLiteral("[AEIOU]"));
        if (distinctMorseTokens.size() < 2 || !decoded.contains(vowel))
        {
            return std::nullopt;
        }
    }

    return decoded;
}

QString providerErrorDetail(const NetworkResult &result)
{
    const auto root = result.parseJson();
    auto detail = root.value(QStringLiteral("message")).toString().trimmed();
    if (detail.isEmpty())
    {
        detail = root.value(QStringLiteral("error"))
                     .toObject()
                     .value(QStringLiteral("message"))
                     .toString()
                     .trimmed();
    }
    if (detail.isEmpty())
    {
        detail = root.value(QStringLiteral("error")).toString().trimmed();
    }
    detail.replace(QRegularExpression(QStringLiteral("\\s+")),
                   QStringLiteral(" "));
    return detail.left(240);
}

struct GoogleTranslationCallbacks {
    TranslationSuccessCallback onSuccess;
    TranslationErrorCallback onError;
    TranslationFinishedCallback onFinished;
    QPointer<QObject> caller;
    bool hadCaller = false;
    bool finished = false;
};

void finishGoogleRequest(
    const std::shared_ptr<GoogleTranslationCallbacks> &callbacks,
    const std::optional<TranslationResult> &result)
{
    if (callbacks->finished || (callbacks->hadCaller && !callbacks->caller))
    {
        return;
    }
    callbacks->finished = true;
    if (result && callbacks->onSuccess)
    {
        callbacks->onSuccess(*result);
    }
    else if (!result && callbacks->onError)
    {
        callbacks->onError(translation::detail::failureMessage());
    }
    if ((!callbacks->hadCaller || callbacks->caller) && callbacks->onFinished)
    {
        callbacks->onFinished();
    }
}

void executeGoogleBuiltInRequest(
    const QString &text, const QString &target, bool sourceWasMorse,
    qsizetype clientIndex,
    const std::shared_ptr<GoogleTranslationCallbacks> &callbacks)
{
    if (callbacks->finished || (callbacks->hadCaller && !callbacks->caller))
    {
        return;
    }
    const auto built = translation::detail::makeProviderRequest(
        TranslationProvider::GoogleBuiltIn, text, target, {}, {}, clientIndex);
    if (!built)
    {
        finishGoogleRequest(callbacks, std::nullopt);
        return;
    }

    auto next = [text, target, sourceWasMorse, clientIndex, callbacks] {
        executeGoogleBuiltInRequest(text, target, sourceWasMorse,
                                    clientIndex + 1, callbacks);
    };
    auto request =
        NetworkRequest(built->url, NetworkRequestType::Get)
            .timeout(built->timeoutMs)
            .maximumResponseSize(MAX_TRANSLATION_RESPONSE_BYTES)
            .followRedirects(false)
            .hideRequestBody()
            .onSuccess(
                [sourceWasMorse, callbacks, next](const NetworkResult &result) {
                    auto parsed = translation::detail::parseProviderResponse(
                        TranslationProvider::GoogleBuiltIn, result);
                    if (!parsed)
                    {
                        next();
                        return;
                    }
                    if (sourceWasMorse)
                    {
                        parsed->detectedLanguage = QStringLiteral("morse");
                    }
                    finishGoogleRequest(callbacks, parsed);
                })
            .onError([next](const NetworkResult &) {
                next();
            });
    if (callbacks->caller)
    {
        request = std::move(request).caller(callbacks->caller.data());
    }
    std::move(request).execute();
}

void executeProviderRequest(
    TranslationProvider provider, const QString &text, const QString &target,
    const QString &credential,
    const translation::detail::ConnectionSettings &connection, QObject *caller,
    bool sourceWasMorse, TranslationSuccessCallback onSuccess,
    TranslationErrorCallback onError, TranslationFinishedCallback onFinished)
{
    if (provider == TranslationProvider::GoogleBuiltIn)
    {
        auto callbacks = std::make_shared<GoogleTranslationCallbacks>(
            std::move(onSuccess), std::move(onError), std::move(onFinished),
            QPointer<QObject>(caller), caller != nullptr);
        executeGoogleBuiltInRequest(text, target, sourceWasMorse, 0, callbacks);
        return;
    }

    const auto built = translation::detail::makeProviderRequest(
        provider, text, target, credential, connection);
    if (!built)
    {
        const QPointer<QObject> guard(caller);
        const bool hadCaller = caller != nullptr;
        if (onError)
        {
            onError(built.error());
        }
        if ((!hadCaller || guard) && onFinished)
        {
            onFinished();
        }
        return;
    }
    auto request =
        NetworkRequest(built->url, NetworkRequestType::Post)
            .timeout(built->timeoutMs)
            .maximumResponseSize(MAX_TRANSLATION_RESPONSE_BYTES)
            .followRedirects(false)
            .hideRequestBody()
            .json(built->body)
            .headerList(built->headers)
            .onSuccess([provider, onSuccess, onError,
                        sourceWasMorse](const NetworkResult &result) {
                auto parsed = translation::detail::parseProviderResponse(
                    provider, result);
                if (!parsed)
                {
                    if (onError)
                    {
                        onError(translation::detail::failureMessage());
                    }
                    return;
                }
                if (sourceWasMorse)
                {
                    parsed->detectedLanguage = QStringLiteral("morse");
                }
                if (onSuccess)
                {
                    onSuccess(*parsed);
                }
            })
            .onError([provider, onError](const NetworkResult &result) {
                if (!onError)
                {
                    return;
                }
                const auto detail = providerErrorDetail(result);
                onError(detail.isEmpty()
                            ? translation::detail::failureMessage()
                            : QStringLiteral("%1: %2").arg(
                                  translationProviderName(provider), detail));
            })
            .finally([onFinished] {
                if (onFinished)
                {
                    onFinished();
                }
            });
    if (caller != nullptr)
    {
        request = std::move(request).caller(caller);
    }
    std::move(request).execute();
}

}  // namespace

QString providerTargetLanguage(TranslationProvider provider, QString target)
{
    target = normalizedTranslationTargetLanguage(std::move(target));
    switch (provider)
    {
        case TranslationProvider::DeepL:
            if (target == u"en"_s)
            {
                return QStringLiteral("EN-US");
            }
            if (target == u"pt"_s)
            {
                return QStringLiteral("PT-PT");
            }
            if (target == u"zh-cn"_s)
            {
                return QStringLiteral("ZH-HANS");
            }
            if (target == u"zh-tw"_s)
            {
                return QStringLiteral("ZH-HANT");
            }
            if (target == u"iw"_s)
            {
                return QStringLiteral("HE");
            }
            if (target == u"tl"_s)
            {
                return QStringLiteral("FIL");
            }
            return target.toUpper();
        case TranslationProvider::MicrosoftAzure:
        case TranslationProvider::MicrosoftFree:
            if (target == u"zh-cn"_s)
            {
                return QStringLiteral("zh-Hans");
            }
            if (target == u"zh-tw"_s)
            {
                return QStringLiteral("zh-Hant");
            }
            if (target == u"iw"_s)
            {
                return QStringLiteral("he");
            }
            if (target == u"tl"_s)
            {
                return QStringLiteral("fil");
            }
            if (target == u"ku"_s)
            {
                return QStringLiteral("kmr");
            }
            if (target == u"mn"_s)
            {
                return QStringLiteral("mn-Cyrl");
            }
            if (target == u"no"_s)
            {
                return QStringLiteral("nb");
            }
            if (target == u"sr"_s)
            {
                return QStringLiteral("sr-Cyrl");
            }
            return target;
        case TranslationProvider::GoogleCloud:
        case TranslationProvider::LibreTranslate:
            if (target == u"iw"_s)
            {
                return QStringLiteral("he");
            }
            if (target == u"tl"_s)
            {
                return provider == TranslationProvider::GoogleCloud
                           ? QStringLiteral("fil")
                           : u"tl"_s;
            }
            return target;
        case TranslationProvider::GoogleBuiltIn:
            return target;
    }
    return target;
}

const QStringList &googleBuiltInClientIds()
{
    static const QStringList clientIds{
        QStringLiteral("dict-chrome-ex"),
        u"it"_s,
        QStringLiteral("at"),
    };
    return clientIds;
}

const std::vector<TranslationProviderDescriptor> &translationProviders()
{
    static const std::vector<TranslationProviderDescriptor> providers{
        {TranslationProvider::GoogleBuiltIn, QStringLiteral("google"),
         QStringLiteral("Google (free)"), false, false},
        {TranslationProvider::MicrosoftFree, QStringLiteral("microsoft-free"),
         QStringLiteral("Microsoft (free)"), false, false},
        {TranslationProvider::DeepL, QStringLiteral("deepl"),
         QStringLiteral("DeepL"), true, false},
        {TranslationProvider::LibreTranslate, QStringLiteral("libretranslate"),
         QStringLiteral("LibreTranslate"), true, true},
        {TranslationProvider::GoogleCloud, QStringLiteral("google-cloud"),
         QStringLiteral("Google Cloud Translation"), true, false},
        {TranslationProvider::MicrosoftAzure, QStringLiteral("azure"),
         QStringLiteral("Microsoft Azure Translator"), true, false},
    };
    return providers;
}

TranslationProvider translationProviderFromId(const QString &id)
{
    const auto normalized = id.trimmed().toLower();
    const auto &providers = translationProviders();
    const auto it = std::ranges::find_if(
        providers, [&normalized](const TranslationProviderDescriptor &item) {
            return item.id == normalized;
        });
    return it == providers.end() ? TranslationProvider::GoogleBuiltIn
                                 : it->provider;
}

QString translationProviderId(TranslationProvider provider)
{
    const auto &providers = translationProviders();
    const auto it = std::ranges::find_if(
        providers, [provider](const TranslationProviderDescriptor &item) {
            return item.provider == provider;
        });
    return it == providers.end() ? QStringLiteral("google") : it->id;
}

QString translationProviderName(TranslationProvider provider)
{
    const auto &providers = translationProviders();
    const auto it = std::ranges::find_if(
        providers, [provider](const TranslationProviderDescriptor &item) {
            return item.provider == provider;
        });
    return it == providers.end() ? QStringLiteral("Google Translate")
                                 : it->name;
}

QString normalizedLanguageCode(QString language)
{
    language = language.trimmed().toLower();
    language.replace('_', '-');

    return language;
}

const std::vector<TranslationLanguage> &supportedTranslationLanguages()
{
    static const std::vector<TranslationLanguage> languages{
        {u"en"_s, u"English"_s},
        {u"es"_s, u"Spanish"_s},
        {u"pt"_s, u"Portuguese"_s},
        {u"fr"_s, u"French"_s},
        {u"de"_s, u"German"_s},
        {u"it"_s, u"Italian"_s},
        {u"nl"_s, u"Dutch"_s},
        {u"pl"_s, u"Polish"_s},
        {u"tr"_s, u"Turkish"_s},
        {u"ru"_s, u"Russian"_s},
        {u"ja"_s, u"Japanese"_s},
        {u"ko"_s, u"Korean"_s},
        {u"zh-cn"_s, u"Chinese (Simplified)"_s},
        {u"zh-tw"_s, u"Chinese (Traditional)"_s},
        {u"ar"_s, u"Arabic"_s},
        {u"af"_s, u"Afrikaans"_s},
        {u"sq"_s, u"Albanian"_s},
        {u"am"_s, u"Amharic"_s},
        {u"hy"_s, u"Armenian"_s},
        {u"az"_s, u"Azerbaijani"_s},
        {u"eu"_s, u"Basque"_s},
        {u"be"_s, u"Belarusian"_s},
        {u"bn"_s, u"Bengali"_s},
        {u"bs"_s, u"Bosnian"_s},
        {u"bg"_s, u"Bulgarian"_s},
        {u"ca"_s, u"Catalan"_s},
        {u"ceb"_s, u"Cebuano"_s},
        {u"co"_s, u"Corsican"_s},
        {u"hr"_s, u"Croatian"_s},
        {u"cs"_s, u"Czech"_s},
        {u"da"_s, u"Danish"_s},
        {u"eo"_s, u"Esperanto"_s},
        {u"et"_s, u"Estonian"_s},
        {u"fi"_s, u"Finnish"_s},
        {u"fy"_s, u"Frisian"_s},
        {u"gl"_s, u"Galician"_s},
        {u"ka"_s, u"Georgian"_s},
        {u"el"_s, u"Greek"_s},
        {u"gu"_s, u"Gujarati"_s},
        {u"ht"_s, u"Haitian Creole"_s},
        {u"ha"_s, u"Hausa"_s},
        {u"haw"_s, u"Hawaiian"_s},
        {u"iw"_s, u"Hebrew"_s},
        {u"hi"_s, u"Hindi"_s},
        {u"hu"_s, u"Hungarian"_s},
        {u"is"_s, u"Icelandic"_s},
        {u"id"_s, u"Indonesian"_s},
        {u"ga"_s, u"Irish"_s},
        {u"jv"_s, u"Javanese"_s},
        {u"kn"_s, u"Kannada"_s},
        {u"kk"_s, u"Kazakh"_s},
        {u"km"_s, u"Khmer"_s},
        {u"ku"_s, u"Kurdish"_s},
        {u"ky"_s, u"Kyrgyz"_s},
        {u"lo"_s, u"Lao"_s},
        {u"la"_s, u"Latin"_s},
        {u"lv"_s, u"Latvian"_s},
        {u"lt"_s, u"Lithuanian"_s},
        {u"lb"_s, u"Luxembourgish"_s},
        {u"mk"_s, u"Macedonian"_s},
        {u"mg"_s, u"Malagasy"_s},
        {u"ms"_s, u"Malay"_s},
        {u"ml"_s, u"Malayalam"_s},
        {u"mt"_s, u"Maltese"_s},
        {u"mi"_s, u"Maori"_s},
        {u"mr"_s, u"Marathi"_s},
        {QStringLiteral("morse"), QStringLiteral("Morse")},
        {u"mn"_s, u"Mongolian"_s},
        {u"my"_s, u"Myanmar (Burmese)"_s},
        {u"ne"_s, u"Nepali"_s},
        {u"no"_s, u"Norwegian"_s},
        {u"ps"_s, u"Pashto"_s},
        {u"fa"_s, u"Persian"_s},
        {u"ro"_s, u"Romanian"_s},
        {u"sr"_s, u"Serbian"_s},
        {u"si"_s, u"Sinhala"_s},
        {u"sk"_s, u"Slovak"_s},
        {u"sl"_s, u"Slovenian"_s},
        {u"so"_s, u"Somali"_s},
        {u"su"_s, u"Sundanese"_s},
        {u"sw"_s, u"Swahili"_s},
        {u"sv"_s, u"Swedish"_s},
        {u"ta"_s, u"Tamil"_s},
        {u"te"_s, u"Telugu"_s},
        {u"th"_s, u"Thai"_s},
        {u"tl"_s, u"Tagalog"_s},
        {u"uk"_s, u"Ukrainian"_s},
        {u"ur"_s, u"Urdu"_s},
        {u"uz"_s, u"Uzbek"_s},
        {u"vi"_s, u"Vietnamese"_s},
        {u"cy"_s, u"Welsh"_s},
        {u"xh"_s, u"Xhosa"_s},
        {u"yi"_s, u"Yiddish"_s},
        {u"yo"_s, u"Yoruba"_s},
        {u"zu"_s, u"Zulu"_s},
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
        {"kmr", u"ku"_s},
        {"mn-cyrl", u"mn"_s},
        {"nb", u"no"_s},
        {"sr-cyrl", u"sr"_s},
        {"burmese", u"my"_s},
        {"br", u"pt"_s},
        {"pt-br", u"pt"_s},
        {"jp", u"ja"_s},
        {"kr", u"ko"_s},
        {"ua", u"uk"_s},
        {"morse-code", QStringLiteral("morse")},
        {"morse code", QStringLiteral("morse")},
    };

    return aliases.value(normalized);
}

bool isSupportedTranslationLanguage(const QString &language)
{
    return !translationLanguageCodeFromInput(language).isEmpty();
}

QString normalizedTranslationTargetLanguage(QString language)
{
    const auto code = translationLanguageCodeFromInput(std::move(language));
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
        if (text.back().isHighSurrogate())
        {
            text.chop(1);
        }
    }

    return text;
}

QString encodeMorseText(const QString &text)
{
    return encodeTextToMorse(text);
}

std::optional<QString> decodeMorseText(const QString &text)
{
    return decodeTextFromMorse(text, 2, true);
}

bool isMorseText(const QString &text)
{
    return decodeTextFromMorse(text, 1, false).has_value();
}

void requestTextTranslation(const QString &text, const QString &targetLanguage,
                            QObject *caller,
                            TranslationSuccessCallback onSuccess,
                            TranslationErrorCallback onError,
                            TranslationFinishedCallback onFinished,
                            std::optional<TranslationProvider> providerOverride)
{
    const QPointer<QObject> finishCaller(caller);
    const bool hasFinishCaller = caller != nullptr;
    onFinished = [finishCaller, hasFinishCaller,
                  callback = std::move(onFinished)] {
        if ((!hasFinishCaller || finishCaller) && callback)
        {
            callback();
        }
    };
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
    const auto decodedMorse = decodeMorseText(requestText);
    if (target == QStringLiteral("morse"))
    {
        const auto permissiveMorse = decodeTextFromMorse(requestText, 1, false);
        const auto translated = encodeMorseText(
            permissiveMorse.has_value() ? *permissiveMorse : requestText);
        if (translated.isEmpty())
        {
            if (onError)
            {
                onError(
                    QStringLiteral("Translation failed: unsupported text."));
            }
        }
        else if (onSuccess)
        {
            onSuccess({
                .translatedText = translated,
                .detectedLanguage = permissiveMorse.has_value()
                                        ? QStringLiteral("morse")
                                        : QString{},
            });
        }
        if (onFinished)
        {
            onFinished();
        }
        return;
    }

    if (decodedMorse.has_value() && target == u"en"_s)
    {
        if (onSuccess)
        {
            onSuccess({
                .translatedText = *decodedMorse,
                .detectedLanguage = QStringLiteral("morse"),
            });
        }
        if (onFinished)
        {
            onFinished();
        }
        return;
    }

    const auto textForTranslation =
        decodedMorse.has_value() ? *decodedMorse : requestText;
    const bool sourceWasMorse = decodedMorse.has_value();
    auto *settings = getSettings();
    const auto &providers = translationProviders();
    const auto configuredId =
        settings->translationProvider.getValue().trimmed().toLower();
    const auto descriptor = std::ranges::find_if(
        providers, [&](const TranslationProviderDescriptor &item) {
            return providerOverride ? item.provider == *providerOverride
                                    : item.id == configuredId;
        });
    if (descriptor == providers.end())
    {
        if (onError)
        {
            onError(
                QStringLiteral("Choose a translation provider in Settings."));
        }
        if (onFinished)
        {
            onFinished();
        }
        return;
    }

    const auto provider = descriptor->provider;
    const translation::detail::ConnectionSettings connection{
        .libreEndpoint = settings->translationLibreEndpoint.getValue(),
        .azureRegion = settings->translationAzureRegion.getValue(),
        .azureEndpoint = settings->translationAzureEndpoint.getValue(),
    };
    if (!descriptor->usesApiKey)
    {
        executeProviderRequest(provider, textForTranslation, target, {},
                               connection, caller, sourceWasMorse,
                               std::move(onSuccess), std::move(onError),
                               std::move(onFinished));
        return;
    }

    const bool hadCaller = caller != nullptr;
    const QPointer<QObject> callerGuard(caller);
    readTranslationProviderCredential(
        provider,
        [provider, descriptor = *descriptor, textForTranslation, target,
         connection, callerGuard, hadCaller, sourceWasMorse,
         onSuccess = std::move(onSuccess), onError = std::move(onError),
         onFinished =
             std::move(onFinished)](ExpectedStr<QString> credential) mutable {
            if (hadCaller && !callerGuard)
            {
                return;
            }

            if (!credential)
            {
                if (onError)
                {
                    onError(credential.error());
                }
                if (onFinished)
                {
                    onFinished();
                }
                return;
            }
            const auto key = credential->trimmed();
            if (key.isEmpty() && !descriptor.apiKeyOptional)
            {
                if (onError)
                {
                    onError(
                        QStringLiteral("Set up %1 with an API key in Settings.")
                            .arg(descriptor.name));
                }
                if ((!hadCaller || callerGuard) && onFinished)
                {
                    onFinished();
                }
                return;
            }

            executeProviderRequest(provider, textForTranslation, target, key,
                                   connection, callerGuard.data(),
                                   sourceWasMorse, std::move(onSuccess),
                                   std::move(onError), std::move(onFinished));
        });
}

}  // namespace chatterino
