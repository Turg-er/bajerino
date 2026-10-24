// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Settings.hpp"

#include "Application.hpp"
#include "common/Args.hpp"
#include "common/Modes.hpp"
#include "common/QLogging.hpp"
#include "controllers/filters/FilterRecord.hpp"
#include "controllers/highlights/HighlightBlacklistUser.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "controllers/highlights/Legacy.hpp"
#include "controllers/highlights/Sounds.hpp"
#include "controllers/highlights/types/AnyHighlight.hpp"
#include "controllers/highlights/types/YourMessagesHighlight.hpp"
#include "controllers/ignores/HiddenUser.hpp"
#include "controllers/ignores/IgnorePhrase.hpp"
#include "controllers/moderationactions/ModerationAction.hpp"
#include "controllers/nicknames/Nickname.hpp"
#include "debug/Benchmark.hpp"
#include "pajlada/settings/signalargs.hpp"
#include "util/Backup.hpp"
#include "util/RapidjsonHelpers.hpp"
#include "util/WindowsHelper.hpp"

#include <pajlada/signals/scoped-connection.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <rapidjson/pointer.h>

#include <cmath>
#include <optional>
#include <utility>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

float normalizedUiScale(float value)
{
    if (!std::isfinite(value))
    {
        return 1.F;
    }
    return std::round(std::clamp(value, 0.2F, 10.F) * 10000.F) / 10000.F;
}

template <typename T>
void initializeSignalVector(pajlada::Signals::SignalHolder &signalHolder,
                            ChatterinoSetting<std::vector<T>> &setting,
                            SignalVector<T> &vec)
{
    // Fill the SignalVector up with initial values
    for (auto &&item : setting.getValue())
    {
        vec.append(item);
    }

    // Set up a signal to
    signalHolder.managedConnect(vec.delayedItemsChanged, [&] {
        setting.setValue(vec.raw());
    });

    setting.connect(
        [&](const auto &v, const auto &args) {
            if (args.source == pajlada::Settings::SignalArgs::Source::External)
            {
                // Settings::restoreSnapshot was called, revert signal vector data
                vec.clear();
                const auto &oldVec = v;
                for (auto &&item : oldVec)
                {
                    vec.append(item);
                }
            }
        },
        signalHolder, false);
}

struct OutgoingTranslationChannelSettings {
    QString channel;
    QString mode;
    QString targetLanguage;
};

QString normalizedOutgoingTranslationChannel(QString channelName)
{
    channelName = channelName.trimmed().toLower();
    if (channelName.startsWith('#'))
    {
        channelName.remove(0, 1);
    }

    return channelName;
}

std::optional<OutgoingTranslationChannelSettings>
    parseOutgoingTranslationChannelSettings(const QString &entry)
{
    const auto parts = entry.split('\t');
    if (parts.size() < 3)
    {
        return std::nullopt;
    }

    auto channel = normalizedOutgoingTranslationChannel(parts.at(0));
    if (channel.isEmpty())
    {
        return std::nullopt;
    }

    return OutgoingTranslationChannelSettings{
        .channel = channel,
        .mode = parts.at(1).trimmed(),
        .targetLanguage = parts.at(2).trimmed(),
    };
}

QString formatOutgoingTranslationChannelSettings(
    const OutgoingTranslationChannelSettings &settings)
{
    return QStringList{settings.channel, settings.mode, settings.targetLanguage}
        .join(QLatin1Char('\t'));
}

QString normalizedUsercardActionPlacement(QString placement)
{
    placement = placement.trimmed().toLower();
    if (placement == u"menu" || placement == u"hidden")
    {
        return placement;
    }
    return QStringLiteral("bar");
}

}  // namespace

namespace chatterino {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
const auto &LOG = chatterinoSettings;

constexpr int CHANNEL_MODE_MIGRATION_VERSION = 1;
constexpr int CURRENT_SETTINGS_VERSION = 2;
constexpr auto LEGACY_BAJERINO_ANONYMOUS_PATH = "/bajerino/joinIrcAsAnonymous";
constexpr auto LEGACY_TWITCH_READ_MODE_PATH =
    "/misc/x-7tv/twitchReadConnectionMode";
constexpr auto TWITCH_CHANNEL_MODE_PATH = "/bajerino/twitchDefaultChannelMode";
constexpr auto TWITCH_ANONYMOUS_READ_PARALLEL_PATH =
    "/bajerino/twitchAnonymousReadParallel";

void migrateSound(const BoolSetting &enableSoundSetting,
                  const QStringSetting &customSoundSetting,
                  highlights::Outcome &outcome)
{
    if (enableSoundSetting.hasValueBeenSet())
    {
        if (!enableSoundSetting.getValue())
        {
            outcome.setSound("");
        }
        else
        {
            const auto &customSoundURL = customSoundSetting.getValue();
            if (customSoundURL.isEmpty())
            {
                // Default ping sound
                outcome.setSound("001-ping2");
                assert(
                    highlights::resolveDefaultSound(outcome.sound).has_value());
            }
            else
            {
                outcome.setSound(customSoundURL);
            }
        }
    }
}

}  // namespace

class SettingsPrivate
{
public:
    ChatterinoSetting<std::vector<highlights::AnyHighlight>>
        sharedHighlightsSetting = {
            "/highlighting/highlights2",
    };

    /// Old version 0 message highlights
    ChatterinoSetting<std::vector<highlights::legacy::HighlightPhrase>>
        highlightedMessagesSetting = {
            "/highlighting/highlights",
    };
    /// Old version 0 user highlights
    ChatterinoSetting<std::vector<highlights::legacy::HighlightPhrase>>
        highlightedUsersSetting = {
            "/highlighting/users",
    };
    /// Old version 0 badge highlights
    ChatterinoSetting<std::vector<highlights::legacy::HighlightBadge>>
        highlightedBadgesSetting = {
            "/highlighting/badges",
    };

    BoolSetting enableSelfHighlight = {
        "/highlighting/selfHighlight/nameIsHighlightKeyword",
        true,
    };
    BoolSetting showSelfHighlightInMentions = {
        "/highlighting/selfHighlight/showSelfHighlightInMentions",
        true,
    };
    BoolSetting enableSelfHighlightSound = {
        "/highlighting/selfHighlight/enableSound",
        true,
    };
    BoolSetting enableSelfHighlightTaskbar = {
        "/highlighting/selfHighlight/enableTaskbarFlashing",
        true,
    };
    QStringSetting selfHighlightSoundUrl = {
        "/highlighting/selfHighlightSoundUrl",
        "",
    };
    QStringSetting selfHighlightColor = {
        "/highlighting/selfHighlightColor",
        "",
    };

    BoolSetting enableSelfMessageHighlight = {
        "/highlighting/selfMessageHighlight/enabled",
        false,
    };
    BoolSetting showSelfMessageHighlightInMentions = {
        "/highlighting/selfMessageHighlight/showInMentions",
        false,
    };
    QStringSetting selfMessageHighlightColor = {
        "/highlighting/selfMessageHighlight/color",
        "",
    };

    BoolSetting enableWhisperHighlight = {
        "/highlighting/whisperHighlight/whispersHighlighted",
        true,
    };
    BoolSetting enableWhisperHighlightSound = {
        "/highlighting/whisperHighlight/enableSound",
        false,
    };
    BoolSetting enableWhisperHighlightTaskbar = {
        "/highlighting/whisperHighlight/enableTaskbarFlashing",
        false,
    };
    QStringSetting whisperHighlightSoundUrl = {
        "/highlighting/whisperHighlightSoundUrl",
        "",
    };
    QStringSetting whisperHighlightColor = {
        "/highlighting/whisperHighlightColor",
        "",
    };

    BoolSetting enableAnnouncementHighlight = {
        "/highlighting/announcement/enabled",
        true,
    };
    QStringSetting announcementHighlightColor = {
        "/highlighting/announcement/color",
        "",
    };

    BoolSetting enableColoredAnnouncementHighlight = {
        "/highlighting/announcement/coloredAnnouncement/enabled",
        true,
    };

    BoolSetting enableRedeemedHighlight = {
        "/highlighting/redeemedHighlight/highlighted",
        true,
    };
    QStringSetting redeemedHighlightColor = {
        "/highlighting/redeemedHighlightColor",
        "",
    };

    BoolSetting enableFirstMessageHighlight = {
        "/highlighting/firstMessageHighlight/highlighted",
        true,
    };
    QStringSetting firstMessageHighlightColor = {
        "/highlighting/firstMessageHighlightColor",
        "",
    };

    BoolSetting enableSubHighlight = {
        "/highlighting/subHighlight/subsHighlighted",
        true,
    };
    BoolSetting enableSubHighlightSound = {
        "/highlighting/subHighlight/enableSound",
        false,
    };
    BoolSetting enableSubHighlightTaskbar = {
        "/highlighting/subHighlight/enableTaskbarFlashing",
        false,
    };
    QStringSetting subHighlightSoundUrl = {
        "/highlighting/subHighlightSoundUrl",
        "",
    };

    QStringSetting subHighlightColor = {
        "/highlighting/subHighlightColor",
        "",
    };

    BoolSetting enableThreadHighlight = {
        "/highlighting/thread/nameIsHighlightKeyword",
        true,
    };
    BoolSetting showThreadHighlightInMentions = {
        "/highlighting/thread/showSelfHighlightInMentions",
        true,
    };
    BoolSetting enableThreadHighlightSound = {
        "/highlighting/thread/enableSound",
        true,
    };
    BoolSetting enableThreadHighlightTaskbar = {
        "/highlighting/thread/enableTaskbarFlashing",
        true,
    };
    QStringSetting threadHighlightSoundUrl = {
        "/highlighting/threadHighlightSoundUrl",
        "",
    };
    QStringSetting threadHighlightColor = {
        "/highlighting/threadHighlightColor",
        "",
    };

    BoolSetting enableAutomodHighlight = {
        "/highlighting/automod/enabled",
        true,
    };
    BoolSetting showAutomodInMentions = {
        "/highlighting/automod/showInMentions",
        false,
    };
    BoolSetting enableAutomodHighlightSound = {
        "/highlighting/automod/enableSound",
        false,
    };
    BoolSetting enableAutomodHighlightTaskbar = {
        "/highlighting/automod/enableTaskbarFlashing",
        false,
    };
    QStringSetting automodHighlightSoundUrl = {
        "/highlighting/automod/soundUrl",
        "",
    };
    QStringSetting automodHighlightColor = {
        "/highlighting/automod/color",
        "",
    };

    BoolSetting enableWatchStreakHighlight = {
        "/highlighting/watchStreak/enabled",
        true,
    };
    QStringSetting watchStreakHighlightColor = {
        "/highlighting/watchStreak/color",
        "",
    };
};

struct RegisteredSetting {
    std::weak_ptr<pajlada::Settings::SettingData> data;
    SettingResetter resetToDefault;
    SettingValidator validate;
};

std::vector<RegisteredSetting> _settings;

void _actuallyRegisterSetting(
    std::weak_ptr<pajlada::Settings::SettingData> setting,
    SettingResetter resetToDefault, SettingValidator validate)
{
    std::erase_if(_settings, [](const auto &entry) {
        return entry.data.expired();
    });
    if (std::ranges::any_of(_settings, [&](const auto &entry) {
            return !entry.data.owner_before(setting) &&
                   !setting.owner_before(entry.data);
        }))
    {
        return;
    }
    _settings.push_back(
        {std::move(setting), std::move(resetToDefault), validate});
}

bool Settings::isEmoteModifierEnabled(const QString &name) const
{
    return this->enableEmoteModifiers.getValue() &&
           !this->disabledEmoteModifiers.getValue().contains(name);
}

void Settings::migrateChannelModes(bool isTest)
{
    if (this->settingsVersion.getValue() >= CHANNEL_MODE_MIGRATION_VERSION)
    {
        return;
    }

    auto manager = pajlada::Settings::SettingManager::getInstance();

    bool legacyBajerinoAnonymous = false;
    if (const auto *value = manager->get(LEGACY_BAJERINO_ANONYMOUS_PATH);
        value && value->IsBool())
    {
        legacyBajerinoAnonymous = value->GetBool();
    }

    auto legacyReadMode = QStringLiteral("authenticated");
    if (const auto *value = manager->get(LEGACY_TWITCH_READ_MODE_PATH);
        value && value->IsString())
    {
        legacyReadMode =
            QString::fromUtf8(value->GetString(), value->GetStringLength());
    }

    const bool legacyParallel =
        legacyReadMode.compare(QStringLiteral("anonymousparallel"),
                               Qt::CaseInsensitive) == 0;
    const bool legacyAnonymousRead =
        legacyParallel || legacyReadMode.compare(QStringLiteral("anonymous"),
                                                 Qt::CaseInsensitive) == 0;

    if (manager->get(TWITCH_CHANNEL_MODE_PATH) == nullptr)
    {
        TwitchChannelMode migratedMode = TwitchChannelMode::Authenticated;
        if (legacyBajerinoAnonymous)
        {
            migratedMode = TwitchChannelMode::BajerinoAnonymous;
        }
        else if (legacyAnonymousRead)
        {
            migratedMode = TwitchChannelMode::AnonymousRead;
        }
        this->twitchDefaultChannelMode.setValue(
            qmagicenum::enumNameString(migratedMode).toLower());
    }

    if (manager->get(TWITCH_ANONYMOUS_READ_PARALLEL_PATH) == nullptr)
    {
        this->twitchAnonymousReadParallel.setValue(legacyParallel);
    }

    manager->removeSetting(LEGACY_BAJERINO_ANONYMOUS_PATH);
    manager->removeSetting(LEGACY_TWITCH_READ_MODE_PATH);
    this->settingsVersion.setValue(CHANNEL_MODE_MIGRATION_VERSION);

    if (!isTest)
    {
        qCInfo(LOG) << "Migrated Twitch channel mode settings";
    }
}

void Settings::migrate(bool isTest)
{
    bool ranMigration = false;

    auto currentVersion = this->settingsVersion.getValue();

    qCDebug(LOG) << "Migrating settings from" << currentVersion;

    if (currentVersion < CHANNEL_MODE_MIGRATION_VERSION)
    {
        this->migrateChannelModes(isTest);
    }

    if (currentVersion < CURRENT_SETTINGS_VERSION)
    {
        if (!this->p->sharedHighlightsSetting.hasValueBeenSet())
        {
            this->migrateHighlights(isTest);
        }
        currentVersion = CURRENT_SETTINGS_VERSION;
        ranMigration = true;
    }

    this->settingsVersion.setValue(currentVersion);

    if (ranMigration)
    {
        qCInfo(LOG) << "Saving settings after migrations";
        auto res = this->requestSave();

        switch (res)
        {
            case pajlada::Settings::SettingManager::SaveResult::Failed:
                qCWarning(LOG) << "Failed saving settings after migration";
                break;
            case pajlada::Settings::SettingManager::SaveResult::Success:
                qCDebug(LOG) << "Succcessfully saved settings after migration";
                break;
            case pajlada::Settings::SettingManager::SaveResult::Skipped:
                qCInfo(LOG) << "Skipped saving settings after migration";
                break;
        }
    }
}

void Settings::migrateHighlights(bool isTest)
{
    using namespace chatterino::highlights;

    // this migration ID is used for tests to provide a stable "uuid" replacement for created user defined highlights,
    // and also to provide some output to the user in their logs for how many highlights were migrated
    int migrationID = 0;

    {
        FirstMessageHighlight h;

        if (const auto &s = this->p->enableFirstMessageHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate
        // Did not support "flash taskbar" - no setting to migrate
        // Did not support "play sound" - no setting to migrate

        if (const auto &s = this->p->firstMessageHighlightColor;
            s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        WatchStreakHighlight h;

        if (const auto &s = this->p->enableWatchStreakHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate
        // Did not support "flash taskbar" - no setting to migrate
        // Did not support "play sound" - no setting to migrate

        if (const auto &s = this->p->watchStreakHighlightColor;
            s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        SubscriptionsHighlight h;

        if (const auto &s = this->p->enableSubHighlight; s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate

        if (const auto &s = this->p->enableSubHighlightTaskbar;
            s.hasValueBeenSet())
        {
            h.outcome.alert = s.getValue();
        }

        migrateSound(this->p->enableSubHighlightSound,
                     this->p->subHighlightSoundUrl, h.outcome);

        if (const auto &s = this->p->subHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        WhispersHighlight h;

        if (const auto &s = this->p->enableWhisperHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate

        if (const auto &s = this->p->enableWhisperHighlightTaskbar;
            s.hasValueBeenSet())
        {
            h.outcome.alert = s.getValue();
        }

        migrateSound(this->p->enableWhisperHighlightSound,
                     this->p->whisperHighlightSoundUrl, h.outcome);

        if (const auto &s = this->p->whisperHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        YourUsernameHighlight h;

        if (const auto &s = this->p->enableSelfHighlight; s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        if (const auto &s = this->p->showSelfHighlightInMentions;
            s.hasValueBeenSet())
        {
            h.outcome.showInMentions = s.getValue();
        }

        if (const auto &s = this->p->enableSelfHighlightTaskbar;
            s.hasValueBeenSet())
        {
            h.outcome.alert = s.getValue();
        }

        migrateSound(this->p->enableSelfHighlightSound,
                     this->p->selfHighlightSoundUrl, h.outcome);

        if (const auto &s = this->p->selfHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    for (const auto &from : this->p->highlightedMessagesSetting.getValue())
    {
        auto generatedId = [&] {
            migrationID += 1;

            if (isTest)
            {
                return QString("test-%1").arg(migrationID);
            }

            auto v4Uuid = QUuid::createUuid();
            return v4Uuid.toString(QUuid::StringFormat::WithoutBraces);
        }();

        MessageHighlight to{generatedId};

        to.setPattern(from.pattern);
        to.outcome.showInMentions = from.showInMentions;
        to.outcome.alert = from.hasAlert;
        to.setRegex(from.isRegex);
        to.setCaseSensitive(from.isCaseSensitive);
        if (from.hasSound)
        {
            if (!from.soundUrl.isEmpty())
            {
                to.outcome.setSound(from.soundUrl.toString());
            }
            else
            {
                to.outcome.setSound("001-ping2");
                assert(highlights::resolveDefaultSound(to.outcome.sound)
                           .has_value());
            }
        }
        else
        {
            to.outcome.setSound("");
        }
        if (auto fromColor = from.color; fromColor.isValid())
        {
            to.outcome.setBackgroundColor(fromColor);
        }

        this->p->sharedHighlightsSetting.push_back(to);
    }

    {
        YourMessagesHighlight h;

        if (const auto &s = this->p->enableSelfMessageHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        if (const auto &s = this->p->showSelfMessageHighlightInMentions;
            s.hasValueBeenSet())
        {
            h.outcome.showInMentions = s.getValue();
        }

        // Did not support "flash taskbar" - no setting to migrate
        // Did not support "play sound" - no setting to migrate

        if (const auto &s = this->p->selfMessageHighlightColor;
            s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    for (const auto &from : this->p->highlightedUsersSetting.getValue())
    {
        auto generatedId = [&] {
            migrationID += 1;

            if (isTest)
            {
                return QString("test-user-%1").arg(migrationID);
            }

            auto v4Uuid = QUuid::createUuid();
            return v4Uuid.toString(QUuid::StringFormat::WithoutBraces);
        }();

        UserHighlight to{generatedId};

        to.setUsername(from.pattern);
        to.outcome.showInMentions = from.showInMentions;
        to.outcome.alert = from.hasAlert;
        if (from.hasSound)
        {
            if (!from.soundUrl.isEmpty())
            {
                to.outcome.setSound(from.soundUrl.toString());
            }
            else
            {
                to.outcome.setSound("001-ping2");
                assert(highlights::resolveDefaultSound(to.outcome.sound)
                           .has_value());
            }
        }
        else
        {
            to.outcome.setSound("");
        }
        if (auto fromColor = from.color; fromColor.isValid())
        {
            to.outcome.setBackgroundColor(fromColor);
        }

        this->p->sharedHighlightsSetting.push_back(to);
    }

    {
        SubscribedThreadHighlight h;

        if (const auto &s = this->p->enableThreadHighlight; s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        if (const auto &s = this->p->showThreadHighlightInMentions;
            s.hasValueBeenSet())
        {
            h.outcome.showInMentions = s.getValue();
        }

        if (const auto &s = this->p->enableThreadHighlightTaskbar;
            s.hasValueBeenSet())
        {
            h.outcome.alert = s.getValue();
        }

        migrateSound(this->p->enableThreadHighlightSound,
                     this->p->threadHighlightSoundUrl, h.outcome);

        if (const auto &s = this->p->threadHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    for (const auto &from : this->p->highlightedBadgesSetting.getValue())
    {
        auto generatedId = [&] {
            migrationID += 1;

            if (isTest)
            {
                return QString("test-badge-%1").arg(migrationID);
            }

            auto v4Uuid = QUuid::createUuid();
            return v4Uuid.toString(QUuid::StringFormat::WithoutBraces);
        }();

        BadgeHighlight to{generatedId};

        to.setBadgeName(from.badgeName);
        // We discard the old displayName because it can already be calculated based on the badgeName (i.e. "subscriber" -> "Subscriber")
        to.outcome.showInMentions = from.showInMentions;
        to.outcome.alert = from.hasAlert;
        if (from.hasSound)
        {
            if (!from.soundUrl.isEmpty())
            {
                to.outcome.setSound(from.soundUrl.toString());
            }
            else
            {
                to.outcome.setSound("001-ping2");
                assert(highlights::resolveDefaultSound(to.outcome.sound)
                           .has_value());
            }
        }
        else
        {
            to.outcome.setSound("");
        }
        if (auto fromColor = from.color; fromColor.isValid())
        {
            to.outcome.setBackgroundColor(fromColor);
        }

        this->p->sharedHighlightsSetting.push_back(to);
    }

    {
        AnnouncementsHighlight h;

        if (const auto &s = this->p->enableAnnouncementHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate
        // Did not support "flash taskbar" - no setting to migrate
        // Did not support "enable regex" - no setting to migrate
        // Did not support "case-sensitive" - no setting to migrate
        // Did not support "play sound" - no setting to migrate
        // Did not support "custom sound" - no setting to migrate

        if (const auto &s = this->p->announcementHighlightColor;
            s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        if (const auto &s = this->p->enableColoredAnnouncementHighlight;
            s.hasValueBeenSet())
        {
            h.overrideColoredAnnouncements = !s.getValue();
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        ChannelPointsHighlight h;

        if (const auto &s = this->p->enableRedeemedHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Did not support "show in mentions" - no setting to migrate
        // Did not support "flash taskbar" - no setting to migrate
        // Did not support "play sound" - no setting to migrate

        if (const auto &s = this->p->redeemedHighlightColor;
            s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        AutomodCaughtHighlight h;

        if (const auto &s = this->p->enableAutomodHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        if (const auto &s = this->p->showAutomodInMentions; s.hasValueBeenSet())
        {
            h.outcome.showInMentions = s.getValue();
        }

        if (const auto &s = this->p->enableAutomodHighlightTaskbar;
            s.hasValueBeenSet())
        {
            h.outcome.alert = s.getValue();
        }

        migrateSound(this->p->enableAutomodHighlightSound,
                     this->p->automodHighlightSoundUrl, h.outcome);

        if (const auto &s = this->p->automodHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        LowTrustUserHighlight h;

        // Use enabled state from "automod highlight" as they were the same before
        if (const auto &s = this->p->enableAutomodHighlight;
            s.hasValueBeenSet())
        {
            h.enabled = s.getValue();
        }

        // Use color from "automod highlight" as they were the same before
        if (const auto &s = this->p->automodHighlightColor; s.hasValueBeenSet())
        {
            h.outcome.setBackgroundColor(s.getValue());
        }

        this->p->sharedHighlightsSetting.push_back(h);
    }

    {
        UncategorizedNotificationHighlight h;

        this->p->sharedHighlightsSetting.push_back(h);
    }
}

void Settings::cleanup()
{
    qCDebug(LOG) << "Clean up";

    bool dirty = false;

    dirty |= this->cleanupHighlights();
    dirty |= this->recreateMissingHighlights();

    if (dirty)
    {
        qCInfo(LOG) << "Saving after clean up";
        auto res = this->requestSave();

        switch (res)
        {
            case pajlada::Settings::SettingManager::SaveResult::Failed:
                qCWarning(LOG) << "Failed saving settings after cleanup";
                break;
            case pajlada::Settings::SettingManager::SaveResult::Success:
                qCDebug(LOG) << "Succcessfully saved settings after cleanup";
                break;
            case pajlada::Settings::SettingManager::SaveResult::Skipped:
                qCInfo(LOG) << "Skipped saving settings after cleanup";
                break;
        }
    }
}

bool Settings::cleanupHighlights()
{
    qCDebug(LOG) << "Clean up highlights";

    // List of highlight IDs we've already seen.
    // If a highlight ID has already been seen (i.e. if the user has any duplicate highlights), the latter ones will be removed
    std::unordered_set<QStringView> seenIDs;

    auto highlights = this->p->sharedHighlightsSetting.getValueCopy();

    auto numRemoved = std::erase_if(highlights, [&seenIDs](const auto &h) {
        const auto id = highlights::getID(h);

        if (id == u"invalid")
        {
            qCWarning(LOG) << "Found an invalid highlight, removing it.";
            return true;
        }

        const auto [_, isNew] = seenIDs.insert(id);
        if (!isNew)
        {
            qCWarning(LOG) << "A highlight with ID" << id
                           << "already exists, removing this duplicate.";
            return true;
        }

        return false;
    });

    if (numRemoved > 0)
    {
        qCInfo(LOG) << "Highlights were dirty. Built in highlights removed:"
                    << numRemoved;
        this->p->sharedHighlightsSetting.setValue(highlights);
        return true;
    }

    return false;
}

std::unordered_set<QStringView> Settings::missingBillTinHighlights()
{
    auto expectedHighlights = HighlightController::billTinHighlights();

    const auto &highlights = this->p->sharedHighlightsSetting.getValue();

    for (const auto &h : highlights)
    {
        expectedHighlights.erase(highlights::getID(h));
    }

    return expectedHighlights;
}

bool Settings::recreateMissingHighlights()
{
    using namespace chatterino::highlights;

    qCDebug(LOG) << "Check for missing built-in highlights";

    bool dirty = false;

    if (const auto missing = this->missingBillTinHighlights(); !missing.empty())
    {
        // The order here should as-closely-as-possible match the default order as set in the Settings migration.
        if (missing.contains(FirstMessageHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << FirstMessageHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(FirstMessageHighlight{});
            dirty |= true;
        }
        if (missing.contains(WatchStreakHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << WatchStreakHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(WatchStreakHighlight{});
            dirty |= true;
        }
        if (missing.contains(SubscriptionsHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << SubscriptionsHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                SubscriptionsHighlight{});
            dirty |= true;
        }
        if (missing.contains(WhispersHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << WhispersHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(WhispersHighlight{});
            dirty |= true;
        }
        if (missing.contains(YourUsernameHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << YourUsernameHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(YourUsernameHighlight{});
            dirty |= true;
        }
        if (missing.contains(YourMessagesHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << YourMessagesHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(YourMessagesHighlight{});
            dirty |= true;
        }
        if (missing.contains(SubscribedThreadHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << SubscribedThreadHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                SubscribedThreadHighlight{});
            dirty |= true;
        }
        if (missing.contains(AnnouncementsHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << AnnouncementsHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                AnnouncementsHighlight{});
            dirty |= true;
        }
        if (missing.contains(ChannelPointsHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << ChannelPointsHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                ChannelPointsHighlight{});
            dirty |= true;
        }
        if (missing.contains(AutomodCaughtHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << AutomodCaughtHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                AutomodCaughtHighlight{});
            dirty |= true;
        }
        if (missing.contains(LowTrustUserHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << LowTrustUserHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(LowTrustUserHighlight{});
            dirty |= true;
        }
        if (missing.contains(UncategorizedNotificationHighlight::ID))
        {
            qCInfo(LOG) << "Recreating missing highlight"
                        << UncategorizedNotificationHighlight::TYPE_NAME;
            this->p->sharedHighlightsSetting.push_back(
                UncategorizedNotificationHighlight{});
            dirty |= true;
        }
    }

    return dirty;
}

bool Settings::isBlacklistedUser(const QString &username)
{
    auto items = this->blacklistedUsers.readOnly();

    for (const auto &blacklistedUser : *items)
    {
        if (blacklistedUser.isMatch(username))
        {
            return true;
        }
    }

    return false;
}

bool Settings::isMutedChannel(const QString &channelName)
{
    auto items = this->mutedChannels.readOnly();

    for (const auto &channel : *items)
    {
        if (channelName.toLower() == channel.toLower())
        {
            return true;
        }
    }
    return false;
}

bool Settings::isAutoTranslateChannel(const QString &channelName)
{
    auto items = this->autoTranslateChannels.readOnly();

    return std::ranges::any_of(*items, [&](const auto &channel) {
        return channelName.compare(channel, Qt::CaseInsensitive) == 0;
    });
}

std::optional<QString> Settings::matchNickname(const QString &usernameText)
{
    auto nicknames = this->nicknames.readOnly();

    for (const auto &nickname : *nicknames)
    {
        if (auto nicknameText = nickname.match(usernameText))
        {
            return nicknameText;
        }
    }

    return std::nullopt;
}

void Settings::mute(const QString &channelName)
{
    if (!this->isMutedChannel(channelName))
    {
        this->mutedChannels.append(channelName);
    }
}

void Settings::unmute(const QString &channelName)
{
    for (std::vector<int>::size_type i = 0;
         i != this->mutedChannels.raw().size(); i++)
    {
        if (this->mutedChannels.raw()[i].toLower() == channelName.toLower())
        {
            this->mutedChannels.removeAt(i);
            i--;
        }
    }
}

void Settings::enableAutoTranslateChannel(const QString &channelName)
{
    if (!this->isAutoTranslateChannel(channelName))
    {
        this->autoTranslateChannels.append(channelName);
    }
}

void Settings::disableAutoTranslateChannel(const QString &channelName)
{
    for (std::vector<int>::size_type i = 0;
         i != this->autoTranslateChannels.raw().size(); i++)
    {
        if (this->autoTranslateChannels.raw()[i].compare(
                channelName, Qt::CaseInsensitive) == 0)
        {
            this->autoTranslateChannels.removeAt(static_cast<int>(i));
            i--;
        }
    }
}

bool Settings::toggleMutedChannel(const QString &channelName)
{
    if (this->isMutedChannel(channelName))
    {
        this->unmute(channelName);
        return false;
    }
    else
    {
        this->mutedChannels.append(channelName);
        return true;
    }
}

bool Settings::toggleAutoTranslateChannel(const QString &channelName)
{
    if (this->isAutoTranslateChannel(channelName))
    {
        this->disableAutoTranslateChannel(channelName);
        return false;
    }

    this->autoTranslateChannels.append(channelName);
    return true;
}

QString Settings::outgoingTranslationModeForChannel(const QString &channelName)
{
    const auto channel = normalizedOutgoingTranslationChannel(channelName);
    if (channel.isEmpty())
    {
        return this->outgoingTranslationMode.getValue();
    }

    for (const auto &entry :
         this->outgoingTranslationChannelSettingsSetting.getValue())
    {
        const auto parsed = parseOutgoingTranslationChannelSettings(entry);
        if (parsed.has_value() && parsed->channel == channel &&
            !parsed->mode.isEmpty())
        {
            return parsed->mode;
        }
    }

    return this->outgoingTranslationMode.getValue();
}

QString Settings::outgoingTranslationTargetLanguageForChannel(
    const QString &channelName)
{
    const auto channel = normalizedOutgoingTranslationChannel(channelName);
    if (channel.isEmpty())
    {
        return this->outgoingTranslationTargetLanguage.getValue();
    }

    for (const auto &entry :
         this->outgoingTranslationChannelSettingsSetting.getValue())
    {
        const auto parsed = parseOutgoingTranslationChannelSettings(entry);
        if (parsed.has_value() && parsed->channel == channel &&
            !parsed->targetLanguage.isEmpty())
        {
            return parsed->targetLanguage;
        }
    }

    return this->outgoingTranslationTargetLanguage.getValue();
}

void Settings::setOutgoingTranslationModeForChannel(const QString &channelName,
                                                    const QString &mode)
{
    const auto channel = normalizedOutgoingTranslationChannel(channelName);
    if (channel.isEmpty())
    {
        this->outgoingTranslationMode.setValue(mode);
        return;
    }

    auto entries = this->outgoingTranslationChannelSettingsSetting.getValue();
    std::vector<QString> cleaned;
    cleaned.reserve(entries.size() + 1);
    bool updated = false;
    for (const auto &entry : entries)
    {
        auto parsed = parseOutgoingTranslationChannelSettings(entry);
        if (!parsed.has_value())
        {
            continue;
        }

        if (parsed->channel == channel)
        {
            if (updated)
            {
                continue;
            }

            parsed->mode = mode;
            if (parsed->targetLanguage.isEmpty())
            {
                parsed->targetLanguage =
                    this->outgoingTranslationTargetLanguage.getValue();
            }
            updated = true;
        }

        cleaned.push_back(formatOutgoingTranslationChannelSettings(*parsed));
    }

    if (!updated)
    {
        cleaned.push_back(formatOutgoingTranslationChannelSettings({
            .channel = channel,
            .mode = mode,
            .targetLanguage =
                this->outgoingTranslationTargetLanguage.getValue(),
        }));
    }

    this->outgoingTranslationChannelSettingsSetting.setValue(cleaned);
}

void Settings::setOutgoingTranslationTargetLanguageForChannel(
    const QString &channelName, const QString &targetLanguage)
{
    const auto channel = normalizedOutgoingTranslationChannel(channelName);
    if (channel.isEmpty())
    {
        this->outgoingTranslationTargetLanguage.setValue(targetLanguage);
        return;
    }

    auto entries = this->outgoingTranslationChannelSettingsSetting.getValue();
    std::vector<QString> cleaned;
    cleaned.reserve(entries.size() + 1);
    bool updated = false;
    for (const auto &entry : entries)
    {
        auto parsed = parseOutgoingTranslationChannelSettings(entry);
        if (!parsed.has_value())
        {
            continue;
        }

        if (parsed->channel == channel)
        {
            if (updated)
            {
                continue;
            }

            parsed->targetLanguage = targetLanguage;
            updated = true;
        }

        cleaned.push_back(formatOutgoingTranslationChannelSettings(*parsed));
    }

    if (!updated)
    {
        cleaned.push_back(formatOutgoingTranslationChannelSettings({
            .channel = channel,
            .mode = this->outgoingTranslationMode.getValue(),
            .targetLanguage = targetLanguage,
        }));
    }

    this->outgoingTranslationChannelSettingsSetting.setValue(cleaned);
}

Settings *Settings::instance_ = nullptr;

Settings::Settings(const Modes &modes, const Args &args,
                   const QString &settingsDirectory,
                   const SettingsArgs &settingsArgs)
    : prevInstance_(Settings::instance_)
    , disableSaving(args.dontSaveSettings)
    , createShortcutForToasts(
          "/notifications/createShortcutForToasts",
          (modes.isPortable || modes.isExternallyPackaged) ? false : true)
    , p(std::make_unique<SettingsPrivate>())
{
    QString settingsPath = settingsDirectory + "/settings.json";

    // get global instance of the settings library
    auto settingsInstance = pajlada::Settings::SettingManager::getInstance();

    if (settingsArgs.isTest)
    {
        qCInfo(LOG) << "Loading settings from" << settingsPath;
        settingsInstance->load(qPrintable(settingsPath));
    }
    else
    {
        backup::loadWithBackups(
            backup::FileData{
                .fileName = u"settings.json"_s,
                .directory = settingsDirectory,
                .fileKind = u"Settings"_s,
                .fileDescription =
                    QStringLiteral("This file contains the main application "
                                   "settings such as accounts and hotkeys."),
            },
            [&]() -> ExpectedStr<void> {
                using LoadError = pajlada::Settings::SettingManager::LoadError;
                auto err = settingsInstance->load(qPrintable(settingsPath));
                switch (err)
                {
                    case LoadError::NoError:
                        return {};  // ok
                    case LoadError::CannotOpenFile:
                        return makeUnexpected(u"Failed to open '" %
                                              settingsPath % '\'');
                    case LoadError::FileHandleError:
                        return makeUnexpected("File handle error");
                    case LoadError::FileReadError:
                        return makeUnexpected("Failed to read file");
                    case LoadError::FileSeekError:
                        return makeUnexpected("Failed to seek in file");
                    case LoadError::JSONParseError:
                        return makeUnexpected("File contained malformed JSON");
                    case LoadError::SavingFromTemporaryFileFailed:
                        return makeUnexpected(
                            u"Failed to save '" % settingsPath %
                            u"' with settings from .tmp file");
                }
                assert(false);
                return makeUnexpected("Unknown error");
            });
    }

    settingsInstance->setBackupEnabled(true);
    settingsInstance->setBackupSlots(9);
    settingsInstance->saveMethod = static_cast<
        pajlada::Settings::SettingManager::SaveMethod>(
        static_cast<uint64_t>(
            pajlada::Settings::SettingManager::SaveMethod::SaveManually) |
        static_cast<uint64_t>(
            pajlada::Settings::SettingManager::SaveMethod::OnlySaveIfChanged));

    // Run setting migrations
    if (settingsArgs.runMigrations)
    {
        this->migrate(settingsArgs.isTest);
    }

    if (settingsArgs.runCleanup)
    {
        this->cleanup();
    }

    initializeSignalVector(this->signalHolder, this->p->sharedHighlightsSetting,
                           this->sharedHighlights);
    initializeSignalVector(this->signalHolder, this->blacklistedUsersSetting,
                           this->blacklistedUsers);
    initializeSignalVector(this->signalHolder, this->ignoredMessagesSetting,
                           this->ignoredMessages);
    initializeSignalVector(this->signalHolder, this->hiddenUsersSetting,
                           this->hiddenUsers);
    initializeSignalVector(this->signalHolder, this->mutedChannelsSetting,
                           this->mutedChannels);
    initializeSignalVector(this->signalHolder,
                           this->autoTranslateChannelsSetting,
                           this->autoTranslateChannels);
    initializeSignalVector(this->signalHolder, this->filterRecordsSetting,
                           this->filterRecords);
    initializeSignalVector(this->signalHolder, this->nicknamesSetting,
                           this->nicknames);
    initializeSignalVector(this->signalHolder, this->moderationActionsSetting,
                           this->moderationActions);
    initializeSignalVector(this->signalHolder, this->loggedChannelsSetting,
                           this->loggedChannels);

    instance_ = this;

    BoolSetting popupSizesReset("/moltorino/appearance/popupSizesResetV1",
                                false);
    if (!popupSizesReset)
    {
        for (auto *setting : {&this->pollPopupSize, &this->predictionPopupSize,
                              &this->editorPopupSize, &this->rewardsPopupSize,
                              &this->vanityPopupSize, &this->gifPopupSize,
                              &this->usercardPopupSize, &this->threadPopupSize})
        {
            setting->resetToDefaultValue();
        }
        popupSizesReset = true;
        this->requestSave();
    }

#ifdef USEWINSDK
    this->autorun = isRegisteredForStartup();
    this->autorun.connect(
        [](bool autorun) {
            setRegisteredForStartup(autorun);
        },
        false);
#endif

    // migration for `/emotes/showUnlistedEmotes` -> `/emotes/showUnlistedSevenTVEmotes`
    if (this->showUnlistedEmotesDontUse && !this->showUnlistedSevenTVEmotes)
    {
        this->showUnlistedSevenTVEmotes.setValue(true);
        // reset to default, so it doesn't appear in the config
        settingsInstance->removeSetting(
            this->showUnlistedEmotesDontUse.getPath());
    }

    // migration for `/appearance/badges/homies`
    // -> `/appearance/badges/homies/supporter` and
    //    `/appearance/badges/homies/custom`
    constexpr const char *OLD_HOMIES_BADGES_SETTING =
        "/appearance/badges/homies";
    if (auto *oldHomiesBadgesSetting =
            settingsInstance->get(OLD_HOMIES_BADGES_SETTING);
        oldHomiesBadgesSetting != nullptr && oldHomiesBadgesSetting->IsBool())
    {
        const auto enabled = oldHomiesBadgesSetting->GetBool();

        // removeSetting would also unregister the new split badge settings
        const auto removedOldSetting =
            rapidjson::Pointer(OLD_HOMIES_BADGES_SETTING)
                .Erase(settingsInstance->document);
        const auto wroteSupporter =
            this->showBadgesHomiesSupporter.setValue(enabled);
        const auto wroteCustom = this->showBadgesHomiesCustom.setValue(enabled);
        if (removedOldSetting || wroteSupporter || wroteCustom)
        {
            this->requestSave();
        }
    }

    auto saveHomiesBadgeSetting = [this](bool, auto) {
        this->requestSave();
    };
    this->showBadgesHomiesSupporter.connect(saveHomiesBadgeSetting,
                                            this->signalHolder, false);
    this->showBadgesHomiesCustom.connect(saveHomiesBadgeSetting,
                                         this->signalHolder, false);

    if (this->spellCheckingDefaultDictionary.getValue().isEmpty())
    {
        this->spellCheckingDefaultDictionary.setValue("included:en_US");
        this->requestSave();
    }

    bool migratedUsercardActionPlacement = false;
    auto configureUsercardActionPlacement =
        [this, settingsInstance, &migratedUsercardActionPlacement](
            QStringSetting &placement, BoolSetting *legacyVisibility,
            QString defaultVisiblePlacement) {
            const bool placementWasStored =
                settingsInstance->get(placement.getPath()) != nullptr;
            auto normalized =
                normalizedUsercardActionPlacement(placement.getValue());
            if (!placementWasStored && legacyVisibility != nullptr)
            {
                normalized = legacyVisibility->getValue()
                                 ? defaultVisiblePlacement
                                 : QStringLiteral("hidden");
            }
            else if (legacyVisibility != nullptr &&
                     legacyVisibility->getValue() != (normalized != u"hidden"))
            {
                normalized = legacyVisibility->getValue()
                                 ? defaultVisiblePlacement
                                 : QStringLiteral("hidden");
            }
            if (placement.getValue() != normalized)
            {
                migratedUsercardActionPlacement |=
                    placement.setValue(normalized);
            }

            if (legacyVisibility == nullptr)
            {
                auto *placementPtr = &placement;
                placement.connect(
                    [placementPtr](const QString &value) {
                        const auto normalizedValue =
                            normalizedUsercardActionPlacement(value);
                        if (value != normalizedValue)
                        {
                            placementPtr->setValue(normalizedValue);
                        }
                    },
                    this->signalHolder, false);
                return;
            }

            const bool visible = normalized != u"hidden";
            if (legacyVisibility->getValue() != visible)
            {
                migratedUsercardActionPlacement |=
                    legacyVisibility->setValue(visible);
            }

            auto *placementPtr = &placement;
            placement.connect(
                [placementPtr, legacyVisibility](const QString &value) {
                    const auto normalizedValue =
                        normalizedUsercardActionPlacement(value);
                    if (value != normalizedValue)
                    {
                        placementPtr->setValue(normalizedValue);
                        return;
                    }
                    legacyVisibility->setValue(normalizedValue != u"hidden");
                },
                this->signalHolder, false);
            legacyVisibility->connect(
                [placementPtr, defaultVisiblePlacement = std::move(
                                   defaultVisiblePlacement)](bool visible) {
                    const auto current = normalizedUsercardActionPlacement(
                        placementPtr->getValue());
                    if (!visible)
                    {
                        placementPtr->setValue(QStringLiteral("hidden"));
                    }
                    else if (current == u"hidden")
                    {
                        placementPtr->setValue(defaultVisiblePlacement);
                    }
                },
                this->signalHolder, false);
        };

    configureUsercardActionPlacement(this->usercardUsercardActionPlacement,
                                     nullptr, QStringLiteral("menu"));
    configureUsercardActionPlacement(this->usercardCommentsActionPlacement,
                                     &this->showModeratorCommentsButton,
                                     QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardLogsActionPlacement, nullptr,
                                     QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardRolesActionPlacement,
                                     nullptr, QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardBadgesActionPlacement,
                                     nullptr, QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardSevenTVActionPlacement,
                                     &this->showSevenTVUsercardButton,
                                     QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardNotesActionPlacement,
                                     nullptr, QStringLiteral("bar"));
    configureUsercardActionPlacement(this->usercardBlockActionPlacement,
                                     nullptr, QStringLiteral("menu"));
    configureUsercardActionPlacement(this->usercardHideActionPlacement, nullptr,
                                     QStringLiteral("menu"));
    configureUsercardActionPlacement(
        this->usercardIgnoreHighlightsActionPlacement, nullptr,
        QStringLiteral("menu"));
    configureUsercardActionPlacement(this->usercardCrossBanActionPlacement,
                                     &this->showUsercardCrossBanAction,
                                     QStringLiteral("menu"));
    configureUsercardActionPlacement(this->usercardCrossUnbanActionPlacement,
                                     &this->showUsercardCrossUnbanAction,
                                     QStringLiteral("menu"));
    if (migratedUsercardActionPlacement)
    {
        this->requestSave();
    }

    // migration for `/moltorino/pinnedMessages/showPinButtonOnModerators`
    // -> `/moltorino/pinnedMessages/showPinButtonOnModeratorsMode`
    constexpr const char *OLD_PIN_MODERATOR_BUTTON_SETTING =
        "/moltorino/pinnedMessages/showPinButtonOnModerators";
    if (auto *oldPinSetting =
            settingsInstance->get(OLD_PIN_MODERATOR_BUTTON_SETTING);
        oldPinSetting != nullptr)
    {
        if (settingsInstance->get(
                this->showPinButtonOnModeratorsMode.getPath()) == nullptr)
        {
            this->showPinButtonOnModeratorsMode.setValue(
                oldPinSetting->IsBool() && oldPinSetting->GetBool() ? 1 : 0);
        }
        settingsInstance->removeSetting(OLD_PIN_MODERATOR_BUTTON_SETTING);
    }
}

Settings::~Settings()
{
    Settings::instance_ = this->prevInstance_;
}

pajlada::Settings::SettingManager::SaveResult Settings::requestSave() const
{
    if (this->disableSaving)
    {
        return pajlada::Settings::SettingManager::SaveResult::Skipped;
    }

    return pajlada::Settings::SettingManager::gSave();
}

void Settings::saveSnapshot()
{
    BenchmarkGuard benchmark("Settings::saveSnapshot");

    rapidjson::Document *d = new rapidjson::Document(rapidjson::kObjectType);
    rapidjson::Document::AllocatorType &a = d->GetAllocator();
    this->snapshotUnsetPaths_.clear();

    for (const auto &entry : _settings)
    {
        auto setting = entry.data.lock();
        if (!setting)
        {
            continue;
        }

        rapidjson::Value key(setting->getPath().c_str(), a);
        auto *curVal = setting->unmarshalJSON();
        if (curVal == nullptr)
        {
            this->snapshotUnsetPaths_.insert(setting->getPath());
            continue;
        }

        rapidjson::Value val;
        val.CopyFrom(*curVal, a);
        d->AddMember(key.Move(), val.Move(), a);
    }

    // log("Snapshot state: {}", rj::stringify(*d));

    this->snapshot_.reset(d);
}

void Settings::restoreSnapshot()
{
    if (!this->snapshot_)
    {
        return;
    }

    BenchmarkGuard benchmark("Settings::restoreSnapshot");

    const auto &snapshot = *(this->snapshot_.get());

    if (!snapshot.IsObject())
    {
        return;
    }

    const auto registeredSettings = _settings;
    for (const auto &entry : registeredSettings)
    {
        auto setting = entry.data.lock();
        if (!setting)
        {
            continue;
        }

        const char *path = setting->getPath().c_str();

        if (!snapshot.HasMember(path))
        {
            if (this->snapshotUnsetPaths_.contains(path) &&
                setting->unmarshalJSON() != nullptr)
            {
                entry.resetToDefault(setting->getPath());
            }
            continue;
        }

        pajlada::Settings::SignalArgs args;
        // NOTE: The settings library doesn't allow for custom sources. Preferably, we'd be able to provide something like a custom enum here and be explicit that this is from a "restore snapshot" event.
        args.source = pajlada::Settings::SignalArgs::Source::External;
        args.compareBeforeSet = true;

        setting->marshalJSON(snapshot[path], std::move(args));
    }
}

QJsonObject Settings::registeredSettingsSnapshot() const
{
    rapidjson::Document document(rapidjson::kObjectType);
    auto &allocator = document.GetAllocator();

    for (const auto &entry : _settings)
    {
        const auto setting = entry.data.lock();
        if (!setting)
        {
            continue;
        }

        const auto *current = setting->unmarshalJSON();
        if (current == nullptr)
        {
            continue;
        }

        rapidjson::Value value;
        value.CopyFrom(*current, allocator);
        const auto path = setting->getPath();
        if (auto existing = document.FindMember(path.c_str());
            existing != document.MemberEnd())
        {
            existing->value = std::move(value);
        }
        else
        {
            rapidjson::Value key(path.c_str(), allocator);
            document.AddMember(key.Move(), value.Move(), allocator);
        }
    }

    const auto parsed =
        QJsonDocument::fromJson(rj::stringify(document).toUtf8());
    return parsed.isObject() ? parsed.object() : QJsonObject{};
}

bool Settings::isSettingRegistered(const QString &path) const
{
    const auto utf8Path = path.toUtf8().toStdString();
    return std::ranges::any_of(_settings, [&](const auto &entry) {
        const auto setting = entry.data.lock();
        return setting && setting->getPath() == utf8Path;
    });
}

bool Settings::isValidSettingValue(const QString &path,
                                   const QJsonValue &value) const
{
    const auto json = QJsonDocument(QJsonObject{{"value", value}})
                          .toJson(QJsonDocument::Compact);
    rapidjson::Document document;
    document.Parse(json.constData(), json.size());
    if (document.HasParseError() || !document.HasMember("value"))
    {
        return false;
    }
    const auto utf8Path = path.toUtf8().toStdString();
    for (const auto &entry : _settings)
    {
        const auto setting = entry.data.lock();
        if (setting && setting->getPath() == utf8Path)
        {
            return entry.validate(document["value"]);
        }
    }
    return false;
}

bool Settings::isSaveEnabled() const
{
    return !this->disableSaving;
}

void Settings::disableSave()
{
    this->disableSaving = true;
}

bool Settings::shouldSendHelixChat() const
{
    switch (this->chatSendProtocol.getEnum())
    {
        case ChatSendProtocol::Helix:
            return true;
        case ChatSendProtocol::Default:
        case ChatSendProtocol::IRC:
            return false;
        default:
            assert(false && "Invalid chat protocol value");
            return false;
    }
}

float Settings::getClampedUiScale() const
{
    return normalizedUiScale(this->uiScale.getValue());
}

void Settings::setClampedUiScale(float value)
{
    this->uiScale.setValue(normalizedUiScale(value));
}

float Settings::getClampedOverlayScale() const
{
    return std::clamp(this->overlayScaleFactor.getValue(), 0.2F, 10.F);
}

void Settings::setClampedOverlayScale(float value)
{
    this->overlayScaleFactor.setValue(std::clamp(value, 0.2F, 10.F));
}

Settings &Settings::instance()
{
    assert(instance_ != nullptr);

    return *instance_;
}

Settings *getSettings()
{
    return &Settings::instance();
}

}  // namespace chatterino
