// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageFlag.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "util/Expected.hpp"
#include "util/Helpers.hpp"
#include "util/Twitch.hpp"

#include <controllers/commands/builtin/twitch/Pin.hpp>
#include <pajlada/signals/scoped-connection.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QCommandLineParser>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include <memory>
#include <optional>

namespace {

using namespace Qt::Literals;
using namespace chatterino;

using chatterino::commands::PinDurationParseResult;

constexpr int MAX_TIMED_PIN_DURATION = 30 * 60;
constexpr int SENT_MESSAGE_PIN_TIMEOUT_MS = 10000;

struct PendingPin {
    bool active = true;
    pajlada::Signals::SignalHolder connections;

    explicit PendingPin(TwitchChannel *channel)
    {
        auto invalidate = [this] {
            this->active = false;
        };
        this->connections.managedConnect(
            getApp()->getAccounts()->twitch.currentUserChanged, invalidate);

        auto checkAuth = [this, channelId = channel->roomId(),
                          channelLogin = channel->getName(),
                          token = MoltorinoAuth::resolveModerationToken(
                                      channel->roomId(), channel->getName())
                                      .token] {
            if (MoltorinoAuth::resolveModerationToken(channelId, channelLogin)
                    .token != token)
            {
                this->active = false;
            }
        };
        getSettings()->customPinAuthToken.connect(checkAuth, this->connections,
                                                  false);
        getSettings()->moltorinoAuthAccounts.connect(checkAuth,
                                                     this->connections, false);
    }
};

QString usage()
{
    QStringList forms{QStringLiteral("/pin <message-id> [duration]")};

    if (getSettings()->enablePinUserCommand)
    {
        forms.push_back(getSettings()->requireAtForPinUserCommand
                            ? QStringLiteral("/pin @<username> [duration]")
                            : QStringLiteral("/pin <username> [duration]"));
    }

    if (getSettings()->enablePinCommandMessages)
    {
        forms.push_back(QStringLiteral("/pin <message text>"));
    }

    return QStringLiteral("Usage: %1.").arg(forms.join(", "));
}

QString pinTextDisabledMessage()
{
    QStringList alternatives{QStringLiteral("/pin <message-id> [duration]")};
    if (getSettings()->enablePinUserCommand)
    {
        alternatives.push_back(
            getSettings()->requireAtForPinUserCommand
                ? QStringLiteral("/pin @<username> [duration]")
                : QStringLiteral("/pin <username> [duration]"));
    }

    return QStringLiteral("/pin <message text> is disabled. Use %1.")
        .arg(alternatives.join(QStringLiteral(" or ")));
}

bool isMessageId(const QString &text)
{
    return !QUuid(text).isNull();
}

bool isUsernameLike(const QString &text)
{
    static const QRegularExpression usernameRegex(
        QStringLiteral(R"(^[A-Za-z0-9_]{1,25}$)"));

    return usernameRegex.match(text).hasMatch();
}

bool isCurrentUserMessage(const MessagePtr &message, const QString &login,
                          const QString &text)
{
    return message != nullptr && !message->id.isEmpty() &&
           message->loginName.compare(login, Qt::CaseInsensitive) == 0 &&
           message->messageText == text;
}

QString durationHelp()
{
    return QStringLiteral("Use seconds, 5m, 30m, 1h, or indefinite. Durations "
                          "over 30m are pinned indefinitely.");
}

int normalizeParsedPinDuration(qint64 amount, qint64 secondsPerUnit)
{
    if (amount <= 0 || secondsPerUnit <= 0)
    {
        return 0;
    }

    if (amount > MAX_TIMED_PIN_DURATION / secondsPerUnit)
    {
        return 0;
    }

    return static_cast<int>(amount * secondsPerUnit);
}

struct PinUserArgs {
    bool matched = false;
    QString error;
    QString login;
    QString fallbackMessage;
    bool explicitUser = false;
    int durationSeconds = 0;
    int fallbackDurationSeconds = 0;
};

PinUserArgs parsePinUserArgs(const QStringList &words)
{
    if (words.size() < 2)
    {
        return {};
    }

    auto target = words.at(1).trimmed();
    const auto explicitUser = target.startsWith('@');
    stripChannelName(target);

    if (!getSettings()->enablePinUserCommand)
    {
        return {};
    }

    if (getSettings()->requireAtForPinUserCommand && !explicitUser)
    {
        return {};
    }

    if (target.isEmpty() || isMessageId(target))
    {
        return {};
    }

    const auto defaultDuration =
        commands::normalizePinDuration(getSettings()->defaultPinDuration);
    const auto fallbackMessage = words.mid(1).join(' ').trimmed();

    if (words.size() == 2)
    {
        if (!isUsernameLike(target))
        {
            return {};
        }

        return {
            .matched = true,
            .login = target,
            .fallbackMessage = fallbackMessage,
            .explicitUser = explicitUser,
            .durationSeconds = defaultDuration,
            .fallbackDurationSeconds = defaultDuration,
        };
    }

    const auto parsed = commands::parsePinDuration(words.mid(2).join(' '));
    if (parsed.matched)
    {
        if (!parsed.error.isEmpty())
        {
            return {
                .matched = true,
                .error = parsed.error,
            };
        }

        if (!isUsernameLike(target))
        {
            if (explicitUser)
            {
                return {
                    .matched = true,
                    .error = QStringLiteral("Invalid username: %1").arg(target),
                };
            }

            return {};
        }

        return {
            .matched = true,
            .login = target,
            .fallbackMessage = fallbackMessage,
            .explicitUser = explicitUser,
            .durationSeconds = parsed.durationSeconds,
            .fallbackDurationSeconds = defaultDuration,
        };
    }

    if (explicitUser)
    {
        return {
            .matched = true,
            .error = durationHelp(),
        };
    }

    return {};
}

MessagePtr findLatestBufferedMessageFromUser(TwitchChannel *channel,
                                             const QString &login)
{
    if (channel == nullptr)
    {
        return nullptr;
    }

    const auto snapshot = channel->getMessageSnapshot();
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
    {
        const auto &message = *it;
        if (message == nullptr || message->id.isEmpty())
        {
            continue;
        }
        if (message->flags.hasAny({MessageFlag::System, MessageFlag::Timeout,
                                   MessageFlag::Disabled,
                                   MessageFlag::Whisper}))
        {
            continue;
        }
        if (message->loginName.compare(login, Qt::CaseInsensitive) == 0)
        {
            return message;
        }
    }

    return nullptr;
}

void sendAndPinMessage(TwitchChannel *channel, const QString &messageText,
                       int durationSeconds);

void handleUnknownPinUser(TwitchChannel *channel, const QString &login,
                          const QString &fallbackMessage,
                          int fallbackDurationSeconds, bool explicitUser)
{
    if (!explicitUser && !fallbackMessage.isEmpty())
    {
        if (!getSettings()->enablePinCommandMessages)
        {
            channel->addSystemMessage(
                QStringLiteral("Could not find a user named %1. %2")
                    .arg(login, pinTextDisabledMessage()));
            return;
        }

        sendAndPinMessage(channel, fallbackMessage, fallbackDurationSeconds);
        return;
    }

    channel->addSystemMessage(
        QStringLiteral("Could not find a user named %1.").arg(login));
}

void pinLatestMessageFromUser(TwitchChannel *channel, const QString &login,
                              int durationSeconds,
                              const QString &fallbackMessage,
                              int fallbackDurationSeconds, bool explicitUser)
{
    if (auto message = findLatestBufferedMessageFromUser(channel, login))
    {
        channel->pinMessage(message->id, durationSeconds);
        return;
    }

    const auto weak = channel->weak_from_this();
    const auto pending = std::make_shared<PendingPin>(channel);
    if (!MoltorinoAuth::hasConfiguredAuth())
    {
        auto account = getApp()->getAccounts()->twitch.getCurrent();
        if (!account || account->isAnon())
        {
            channel->addSystemMessage(
                "Log in to Twitch to look up that username.");
            return;
        }

        getHelix()->fetchUsers(
            {}, {login},
            [weak, pending, login, fallbackMessage, fallbackDurationSeconds,
             explicitUser](const std::vector<HelixUser> &users) {
                auto shared =
                    std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
                if (!shared || !pending->active)
                {
                    return;
                }
                if (users.empty())
                {
                    handleUnknownPinUser(shared.get(), login, fallbackMessage,
                                         fallbackDurationSeconds, explicitUser);
                    return;
                }

                const auto &user = users.front();
                const auto displayName =
                    user.displayName.isEmpty() ? user.login : user.displayName;
                shared->addSystemMessage(
                    QStringLiteral(
                        "No recent message from %1 is in this chat buffer. "
                        "Older message lookup requires Moltorino "
                        "Authentication.")
                        .arg(displayName));
            },
            [weak, pending, login] {
                if (auto shared =
                        std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
                    shared && pending->active)
                {
                    shared->addSystemMessage(
                        QStringLiteral("Could not look up user %1 through "
                                       "Twitch. Try again.")
                            .arg(login));
                }
            });
        return;
    }

    TwitchGql::getUserByLogin(
        login, QString{},
        [weak, pending, login, fallbackMessage, fallbackDurationSeconds,
         explicitUser, durationSeconds](std::optional<GqlUser> user) {
            auto shared = std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
            if (!shared || !pending->active)
            {
                return;
            }

            if (!user.has_value())
            {
                handleUnknownPinUser(shared.get(), login, fallbackMessage,
                                     fallbackDurationSeconds, explicitUser);
                return;
            }

            const auto displayName =
                user->displayName.isEmpty() ? user->login : user->displayName;

            QString authError;
            auto auth = MoltorinoAuth::resolveModerationToken(
                shared->roomId(), shared->getName(), &authError);
            if (!auth.hasToken())
            {
                shared->addSystemMessage(
                    QStringLiteral("Could not search older messages for %1: %2")
                        .arg(displayName, authError));
                return;
            }

            TwitchGql::getLatestModLogMessageBySender(
                shared->roomId(), user->id, auth.token,
                [weak, pending, displayName,
                 durationSeconds](std::optional<GqlModLogMessage> message) {
                    auto shared =
                        std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
                    if (!shared || !pending->active)
                    {
                        return;
                    }

                    if (!message.has_value())
                    {
                        shared->addSystemMessage(
                            QStringLiteral("Could not find a recent message "
                                           "from %1 to pin.")
                                .arg(displayName));
                        return;
                    }

                    shared->pinMessage(message->id, durationSeconds);
                },
                [weak, pending, displayName](const QString &error) {
                    auto shared =
                        std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
                    if (!shared || !pending->active)
                    {
                        return;
                    }

                    shared->addSystemMessage(
                        QStringLiteral(
                            "Could not search older messages for %1: %2")
                            .arg(displayName,
                                 MoltorinoAuth::normalizeAuthError(
                                     "searching older messages", error)));
                });
        },
        [weak, pending, login](const QString &error) {
            auto shared = std::dynamic_pointer_cast<TwitchChannel>(weak.lock());
            if (!shared || !pending->active)
            {
                return;
            }

            shared->addSystemMessage(
                QStringLiteral("Could not look up user %1: %2")
                    .arg(login, MoltorinoAuth::normalizeAuthError(
                                    "looking up users", error)));
        });
}

void sendAndPinMessage(TwitchChannel *channel, const QString &messageText,
                       int durationSeconds)
{
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser->isAnon())
    {
        channel->addSystemMessage(
            "You must be logged in to send and pin a message.");
        return;
    }

    if (channel->trySendMessageViaHelixForPin(messageText, durationSeconds))
    {
        return;
    }

    const auto login = currentUser->getUserName();
    const auto key = channel->roomId() + '\n' + messageText;

    static QSet<QString> pendingMessages;
    if (pendingMessages.contains(key))
    {
        channel->addSystemMessage(
            "Already waiting for that sent message to appear in chat.");
        return;
    }
    if (pendingMessages.size() >= 32)
    {
        channel->addSystemMessage(
            "Too many messages are waiting to be pinned. Try again shortly.");
        return;
    }
    pendingMessages.insert(key);

    auto done = std::make_shared<bool>(false);
    auto connection = std::make_shared<pajlada::Signals::ScopedConnection>();
    auto weakChannel = channel->weak_from_this();
    const auto pending = std::make_shared<PendingPin>(channel);

    auto cleanup = [done, connection, key] {
        if (*done)
        {
            return false;
        }

        *done = true;
        *connection = pajlada::Signals::ScopedConnection();
        pendingMessages.remove(key);
        return true;
    };

    *connection = channel->messageAppended.connect(
        [cleanup, weakChannel, pending, login, messageText, durationSeconds](
            MessagePtr &message, auto) mutable {
            if (!isCurrentUserMessage(message, login, messageText))
            {
                return;
            }

            if (!cleanup())
            {
                return;
            }

            auto shared =
                std::dynamic_pointer_cast<TwitchChannel>(weakChannel.lock());
            if (!shared || !pending->active)
            {
                return;
            }

            shared->pinMessage(message->id, durationSeconds);
        });

    QTimer::singleShot(
        SENT_MESSAGE_PIN_TIMEOUT_MS, [cleanup, weakChannel, pending] mutable {
            if (!cleanup())
            {
                return;
            }

            auto shared =
                std::dynamic_pointer_cast<TwitchChannel>(weakChannel.lock());
            if (!shared || !pending->active)
            {
                return;
            }

            shared->addSystemMessage(
                "Could not find the sent message to pin. Try again.");
        });

    channel->sendMessage(messageText);
}

struct Action {
    QString text;
    QString messageID;
    std::optional<std::chrono::seconds> duration;
};

// Duration of pin when sending message with "Send Chat Message".
// See https://dev.twitch.tv/docs/api/reference#send-chat-message.
constexpr std::chrono::minutes SEND_CHAT_MESSAGE_PIN_DURATION(20);

void sendPinnedMessage(const std::shared_ptr<TwitchChannel> &chan,
                       const std::shared_ptr<TwitchAccount> &moderator,
                       const QString &text,
                       std::optional<std::chrono::seconds> duration)
{
    getHelix()->sendChatMessage(
        {
            .broadcasterID = chan->roomId(),
            .senderID = moderator->getUserId(),
            .message = text,
            .pin = true,
        },
        [weak = std::weak_ptr(chan), origText = text, duration,
         moderator](const HelixSentMessage &res) {
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }

            if (!res.isSent)
            {
                if (res.dropReason)
                {
                    chan->addSystemMessage(res.dropReason->message);
                }
                else
                {
                    chan->addSystemMessage("Your message was not pinned.");
                }
                return;
            }

            // Default duration, no need to update it once more.
            if (duration == SEND_CHAT_MESSAGE_PIN_DURATION)
            {
                TwitchChannel::PinnedMessage pin;
                pin.messageId = res.id;
                pin.text = origText;
                pin.authorId = moderator->getUserId();
                pin.authorName = moderator->getUserName();
                pin.authorLogin = moderator->getUserName();
                pin.pinnerName = moderator->getUserName();
                pin.pinnerLogin = moderator->getUserName();
                pin.pinnedAt = QDateTime::currentDateTimeUtc();
                pin.endsAt = pin.pinnedAt->addSecs(duration->count());
                chan->setPinnedMessage(std::move(pin));
                chan->addMessage(
                    MessageBuilder::makePinSuccessMessage(origText, res.id),
                    MessageContext::Original);
                return;
            }

            chan->updatePinnedMessageAs(res.id, duration, *moderator, origText);
        },
        [weak = std::weak_ptr(chan)](auto error, auto message) {
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }

            if (message.isEmpty())
            {
                message = "(empty message)";
            }

            using Error = HelixSendMessageError;

            auto errorMessage = [&]() -> QString {
                switch (error)
                {
                    case Error::MissingText:
                        return "You can't pin an empty message.";
                    case Error::BadRequest:
                        return "Failed to pin message: " + message;
                    case Error::Forbidden:
                        return "You are not allowed to pin messages in this "
                               "channel.";
                    case Error::MessageTooLarge:
                        return "Your message was too long.";
                    case Error::UserMissingScope:
                        return "Missing required scope. Re-login with your "
                               "account and try again.";
                    case Error::Forwarded:
                        return message;
                    case Error::Unknown:
                    default:
                        return "Unknown error: " + message;
                }
            }();
            chan->addSystemMessage(errorMessage);
        });
}

ExpectedStr<Action> parseAction(const CommandContext &ctx)
{
    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.setOptionsAfterPositionalArgumentsMode(
        QCommandLineParser::ParseAsOptions);
    QCommandLineOption idOption(u"id"_s, {}, u"ID"_s);
    QCommandLineOption durationOption(QStringList{u"d"_s, u"duration"_s}, {},
                                      u"duration"_s);
    parser.addOptions({idOption, durationOption});
    parser.parse(ctx.words);

    std::optional<std::chrono::seconds> duration;
    auto durationText = parser.value(durationOption);
    bool isExplicitUntilEnd =
        durationText == u"until-end" || durationText == u"none";
    if (!durationText.isEmpty() && !isExplicitUntilEnd)
    {
        const auto dur = parseDurationToSeconds(durationText);
        if (dur <= 0)
        {
            return makeUnexpected(u"Duration must be positive."_s);
        }
        duration.emplace(dur);
    }

    std::optional<QString> id;
    if (parser.isSet(idOption))
    {
        id.emplace(parser.value(idOption));
    }

    auto positionals = parser.positionalArguments().join(' ');

    if (id)
    {
        if (!positionals.isEmpty())
        {
            return makeUnexpected(
                u"No positional arguments can be specified when using --id."_s);
        }

        return Action{
            .messageID = *std::move(id),
            .duration = duration,
        };
    }

    if (!duration && !isExplicitUntilEnd)
    {
        duration = SEND_CHAT_MESSAGE_PIN_DURATION;
    }

    return Action{
        .text = positionals,
        .duration = duration,
    };
}

void showCurrentlyPinnedMessage(const std::shared_ptr<TwitchChannel> &chan,
                                const TwitchAccount &moderator)
{
    getHelix()->getPinnedChatMessage(
        chan->roomId(), moderator.getUserId(),
        [weak = std::weak_ptr(chan)](
            const std::optional<HelixPinnedChatMessage> &result) {
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }

            if (!result)
            {
                chan->addSystemMessage(u"There is no pinned message."_s);
                return;
            }

            chan->addMessage(
                MessageBuilder::makeCurrentPinnedMessage(*chan, *result),
                MessageContext::Original);
        },
        [weak = std::weak_ptr(chan)](const auto &result) {
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }
            chan->addSystemMessage("Failed to get pinned message: " % result);
        });
}

}  // namespace

namespace chatterino::commands {

PinDurationParseResult parsePinDuration(const QString &text)
{
    const auto duration = text.trimmed().toLower();
    if (duration.isEmpty())
    {
        return {};
    }

    if (duration == "0" || duration == "indefinite" || duration == "forever" ||
        duration == "permanent")
    {
        return {true, {}, 0};
    }

    static const QRegularExpression durationRegex(
        R"(^(\d+)\s*(s|sec|secs|second|seconds|m|min|mins|minute|minutes|h|hr|hrs|hour|hours)?$)",
        QRegularExpression::CaseInsensitiveOption);

    const auto match = durationRegex.match(duration);
    if (!match.hasMatch())
    {
        return {};
    }

    bool ok = false;
    qint64 amount = match.captured(1).toLongLong(&ok);
    if (!ok)
    {
        return {true, {}, 0};
    }

    const auto unit = match.captured(2).toLower();
    if (unit.isEmpty() || unit == "s" || unit == "sec" || unit == "secs" ||
        unit == "second" || unit == "seconds")
    {
        return {true, {}, normalizeParsedPinDuration(amount, 1)};
    }

    if (unit == "m" || unit == "min" || unit == "mins" || unit == "minute" ||
        unit == "minutes")
    {
        return {true, {}, normalizeParsedPinDuration(amount, 60)};
    }

    if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" ||
        unit == "hours")
    {
        return {true, {}, normalizeParsedPinDuration(amount, 60 * 60)};
    }

    return {true, durationHelp(), 0};
}

int normalizePinDuration(int durationSeconds)
{
    if (durationSeconds <= 0 || durationSeconds > MAX_TIMED_PIN_DURATION)
    {
        return 0;
    }

    return durationSeconds;
}

QString pinMessage(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage("/pin only works in Twitch channels.");
        return "";
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(usage());
        return "";
    }

    const auto firstArg = ctx.words.at(1).trimmed();
    if (isMessageId(firstArg))
    {
        auto duration = normalizePinDuration(getSettings()->defaultPinDuration);
        if (ctx.words.size() > 2)
        {
            const auto parsed = parsePinDuration(ctx.words.mid(2).join(' '));
            if (!parsed.matched || !parsed.error.isEmpty())
            {
                ctx.channel->addSystemMessage(
                    parsed.error.isEmpty() ? durationHelp() : parsed.error);
                return "";
            }
            duration = parsed.durationSeconds;
        }

        ctx.twitchChannel->pinMessage(firstArg, duration);
        return "";
    }

    const auto userArgs = parsePinUserArgs(ctx.words);
    if (userArgs.matched)
    {
        if (!userArgs.error.isEmpty())
        {
            ctx.channel->addSystemMessage(userArgs.error);
            return "";
        }

        pinLatestMessageFromUser(
            ctx.twitchChannel, userArgs.login, userArgs.durationSeconds,
            userArgs.fallbackMessage, userArgs.fallbackDurationSeconds,
            userArgs.explicitUser);
        return "";
    }

    const auto customMessage = ctx.words.mid(1).join(' ').trimmed();
    if (customMessage.isEmpty())
    {
        ctx.channel->addSystemMessage(usage());
        return "";
    }

    if (!getSettings()->enablePinCommandMessages)
    {
        ctx.channel->addSystemMessage(pinTextDisabledMessage());
        return "";
    }

    sendAndPinMessage(ctx.twitchChannel, customMessage,
                      normalizePinDuration(getSettings()->defaultPinDuration));

    return "";
}

QString unpinMessage(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage("/unpin only works in Twitch channels.");
        return "";
    }

    if (ctx.words.size() > 1)
    {
        ctx.channel->addSystemMessage("Usage: /unpin");
        return "";
    }

    ctx.twitchChannel->unpinMessage();
    return "";
}

/// /pin
QString pin(const CommandContext &ctx)
{
    if (!ctx.channel)
    {
        return {};
    }
    if (!ctx.twitchChannel)
    {
        ctx.channel->addSystemMessage(
            u"The /pin command only works in Twitch channels"_s);
        return {};
    }

    if (ctx.words.empty())
    {
        return {};
    }
    const bool hasOptions = std::any_of(
        ctx.words.cbegin() + 1, ctx.words.cend(), [](const QString &word) {
            return word.startsWith('-');
        });
    if (ctx.words.size() > 1 && !hasOptions &&
        (isMessageId(ctx.words.at(1)) || parsePinUserArgs(ctx.words).matched ||
         MoltorinoAuth::hasConfiguredAuth() ||
         !getSettings()->enablePinCommandMessages))
    {
        return pinMessage(ctx);
    }

    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser->isAnon())
    {
        ctx.channel->addSystemMessage(
            u"You must be logged in to pin messages!"_s);
        return {};
    }

    if (ctx.words.empty())
    {
        assert(false && "should have /pin");
        return {};
    }

    if (ctx.words.size() == 2 &&
        (ctx.words.at(1) == u"-h" || ctx.words.at(1) == u"--help"))
    {
        ctx.channel->addSystemMessage(
            u"Pin a chat message or show the currently pinned one. Usage: /pin [--duration <seconds|until-end|none>] [message]... OR /pin --id <message-id> [--duration <seconds|until-end|none>]"_s);
        return {};
    }

    auto chan = std::static_pointer_cast<TwitchChannel>(
        ctx.twitchChannel->shared_from_this());
    if (ctx.words.size() == 1)
    {
        showCurrentlyPinnedMessage(chan, *currentUser);
        return {};
    }

    auto action = parseAction(ctx);
    if (!action)
    {
        ctx.channel->addSystemMessage(action.error());
        return {};
    }

    if (!action->messageID.isEmpty())
    {
        chan->pinMessageAs(action->messageID, action->duration, *currentUser);
        return {};
    }

    sendPinnedMessage(chan, currentUser, action->text, action->duration);
    return {};
}

/// /unpin
QString unpin(const CommandContext &ctx)
{
    if (!ctx.channel)
    {
        return {};
    }
    if (!ctx.twitchChannel)
    {
        ctx.channel->addSystemMessage(
            u"The /unpin command only works in Twitch channels"_s);
        return {};
    }

    if (ctx.words.size() == 1)
    {
        ctx.twitchChannel->unpinMessage();
        return {};
    }

    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser->isAnon())
    {
        ctx.channel->addSystemMessage(
            u"You must be logged in to unpin messages!"_s);
        return {};
    }

    if (ctx.words.empty())
    {
        assert(false);
        return {};
    }

    auto chan = std::static_pointer_cast<TwitchChannel>(
        ctx.twitchChannel->shared_from_this());
    if (ctx.words.size() == 1)
    {
        getHelix()->getPinnedChatMessage(
            chan->roomId(), currentUser->getUserId(),
            [weak = std::weak_ptr(chan), currentUser](const auto &result) {
                auto chan = weak.lock();
                if (!chan)
                {
                    return;
                }
                if (!result)
                {
                    chan->addSystemMessage(u"There is no pinned message"_s);
                    return;
                }
                chan->unpinMessageAs(result->messageID, *currentUser);
            },
            [weak = std::weak_ptr(chan)](const auto &message) {
                auto chan = weak.lock();
                if (!chan)
                {
                    return;
                }
                chan->addSystemMessage(u"Failed to get pinned message: " %
                                       message);
            });
        return {};
    }

    chan->unpinMessageAs(ctx.words.at(1), *currentUser);
    return {};
}

}  // namespace chatterino::commands
