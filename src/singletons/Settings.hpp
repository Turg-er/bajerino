// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "common/enums/MessageOverflow.hpp"
#include "common/enums/UsernameDisplayMode.hpp"
#include "common/LastMessageLineStyle.hpp"
#include "common/SignalVector.hpp"
#include "common/StreamerModeSetting.hpp"
#include "common/ThumbnailPreviewMode.hpp"
#include "common/TimeoutStackStyle.hpp"
#include "controllers/filters/FilterRecord.hpp"
#include "controllers/highlights/HighlightBlacklistUser.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "controllers/highlights/types/AnyHighlightForward.hpp"
#include "controllers/ignores/HiddenUser.hpp"
#include "controllers/ignores/IgnorePhrase.hpp"
#include "controllers/logging/ChannelLog.hpp"
#include "controllers/moderationactions/ModerationAction.hpp"
#include "controllers/nicknames/Nickname.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "providers/emoji/EmojiStyle.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Toasts.hpp"
#include "util/RapidJsonSerializeQHash.hpp"    // IWYU pragma: keep
#include "util/RapidJsonSerializeQString.hpp"  // IWYU pragma: keep
#include "util/serialize/List.hpp"             // IWYU pragma: keep
#include "widgets/NotebookEnums.hpp"

#include <pajlada/settings/setting.hpp>
#include <pajlada/settings/settinglistener.hpp>
#include <pajlada/settings/settingmanager.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QHash>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_set>

class QJsonObject;
#include <QStringView>

#include <memory>
#include <vector>

#if __has_include(<gtest/gtest_prod.h>)
#    include <gtest/gtest_prod.h>
#endif

using TimeoutButton = std::pair<QString, int>;

namespace chatterino {

class Args;
class Modes;

inline constexpr int MIN_SCROLLBACK_LIMIT = 100;
inline constexpr int MAX_SCROLLBACK_LIMIT = 100'000;

[[nodiscard]] inline size_t sanitizeScrollbackLimit(int value) noexcept
{
    return static_cast<size_t>(
        std::clamp(value, MIN_SCROLLBACK_LIMIT, MAX_SCROLLBACK_LIMIT));
}

#ifdef Q_OS_WIN32
#    define DEFAULT_FONT_FAMILY "Segoe UI"
#    define DEFAULT_FONT_SIZE 10
#else
#    ifdef Q_OS_MACOS
#        define DEFAULT_FONT_FAMILY "Helvetica Neue"
#        define DEFAULT_FONT_SIZE 16
#    else
#        define DEFAULT_FONT_FAMILY "Arial"
#        define DEFAULT_FONT_SIZE 11
#    endif
#endif

void _actuallyRegisterSetting(
    std::weak_ptr<pajlada::Settings::SettingData> setting,
    SettingResetter resetToDefault, SettingValidator validate);

enum UsernameRightClickBehavior : int {
    Reply = 0,
    Mention = 1,
    Ignore = 2,
    ContextMenu = 3,
};

enum class ChatSendProtocol : int {
    Default = 0,
    IRC = 1,
    Helix = 2,
};

enum class ShowModerationState : int {
    // Always show this moderation-related item
    Always = 0,
    // Never show this moderation-related item
    Never = 1,
};

enum class StreamLinkPreferredQuality : std::uint8_t {
    Choose,
    Source,
    High,
    Medium,
    Low,
    AudioOnly,
};

enum class TabStyle : std::uint8_t {
    Normal,
    Compact,
};

enum class EmoteTooltipScale : std::uint8_t {
    Small,
    Medium,
    Large,
    Huge,
};

enum class KickConnectionPreference : uint8_t {
    Default = 0,
    Pusher = (1 << 0),
    Centrifugo = (1 << 1),
    Any = Pusher | Centrifugo,
};

enum class BadgeAlignmentMode : int {
    Balanced = 0,
    Chatterino = 1,
};

constexpr std::optional<std::string_view> qmagicenumDisplayName(
    EmoteTooltipScale value) noexcept
{
    switch (value)
    {
        case EmoteTooltipScale::Medium:
            return "Medium (default)";

        case EmoteTooltipScale::Small:
        case EmoteTooltipScale::Large:
        case EmoteTooltipScale::Huge:
            return {};
    }
}

constexpr std::optional<std::string_view> qmagicenumDisplayName(
    TwitchChannelMode value) noexcept
{
    switch (value)
    {
        case TwitchChannelMode::Authenticated:
            return "Authenticated";

        case TwitchChannelMode::AnonymousRead:
            return "Anonymous read";

        case TwitchChannelMode::BajerinoAnonymous:
            return "Bajerino anonymous";
    }
}

struct SettingsArgs {
    bool isTest = false;
    bool runMigrations = true;
    bool runCleanup = true;
};

class SettingsPrivate;

/// Settings which are available for reading and writing on the gui thread.
// These settings are still accessed concurrently in the code but it is bad practice.
class Settings
{
    static Settings *instance_;
    Settings *prevInstance_ = nullptr;

    bool disableSaving;

public:
    Settings(const Modes &modes, const Args &args,
             const QString &settingsDirectory,
             const SettingsArgs &settingsArgs = {});
    ~Settings();

    Q_DISABLE_COPY_MOVE(Settings)

    static Settings &instance();

    /// Request the settings to be saved to file
    ///
    /// Depending on the launch options, a save might end up not happening
    ///
    /// Returns the result from the save, or Skipped if disableSave has been called
    pajlada::Settings::SettingManager::SaveResult requestSave() const;

    void saveSnapshot();
    void restoreSnapshot();

    QJsonObject registeredSettingsSnapshot() const;

    bool isSettingRegistered(const QString &path) const;
    bool isValidSettingValue(const QString &path,
                             const QJsonValue &value) const;

    void disableSave();

    bool isSaveEnabled() const;

    /// Returns true if chat messages should be sent over Helix
    bool shouldSendHelixChat() const;

    FloatSetting uiScale = {"/appearance/uiScale2", 1};
    BoolSetting windowTopMost = {"/appearance/windowAlwaysOnTop", false};

    BoolSetting useQtSystemStyle = {"/appearance/useQtSystemStyle", false};

    float getClampedUiScale() const;
    void setClampedUiScale(float value);

    /// Appearance
    BoolSetting showTimestamps = {"/appearance/messages/showTimestamps", true};
    BoolSetting showHeaderTimestamps = {
        "/appearance/messages/header/showTimestamps",
        false,
    };
    BoolSetting showAnnouncementHeader = {
        "/appearance/messages/announcements/showHeader",
        true,
    };
    BoolSetting showSubscriptionHeader = {
        "/appearance/messages/subscriptions/showHeader",
        true,
    };
    BoolSetting showWatchStreakHeader = {
        "/appearance/messages/watchstreaks/showHeader",
        true,
    };
    BoolSetting animationsWhenFocused = {
        "/appearance/enableAnimationsWhenFocused", false};
    BoolSetting hideMessageTimestampsWhenLive = {
        "/appearance/messages/hideMessageTimestampsWhenLive", false};
    QStringSetting timestampFormat = {"/appearance/messages/timestampFormat",
                                      "h:mm"};
    BoolSetting showLastMessageIndicator = {
        "/appearance/messages/showLastMessageIndicator", false};
    EnumSetting<LastMessageLineStyle> lastMessagePattern = {
        "/appearance/messages/lastMessagePattern",
        LastMessageLineStyle::Solid,
    };
    QStringSetting lastMessageColor = {"/appearance/messages/lastMessageColor",
                                       "#7f2026"};
    BoolSetting showEmptyInput = {"/appearance/showEmptyInputBox", true};
    BoolSetting showMessageLength = {"/appearance/messages/showMessageLength",
                                     false};
    BoolSetting showSendWaitTimer = {"/appearance/messages/showSendWaitTimer",
                                     false};
    EnumSetting<MessageOverflow> messageOverflow = {
        "/appearance/messages/messageOverflow", MessageOverflow::Highlight};
    BoolSetting wrapAsciiArt = {
        "/appearance/messages/wrapAsciiArt",
        false,
    };
    BoolSetting showTwitchGifs = {
        "/appearance/messages/showTwitchGifs",
        true,
    };
    BoolSetting separateMessages = {"/appearance/messages/separateMessages",
                                    false};
    BoolSetting fadeMessageHistory = {"/appearance/messages/fadeMessageHistory",
                                      true};
    BoolSetting hideModerated = {"/appearance/messages/hideModerated", false};
    BoolSetting extendedClientNonceParsing = {
        "/appearance/messages/extendedClientNonceParsing", false};
    BoolSetting hideModerationActions = {
        "/appearance/messages/hideModerationActions", false};
    BoolSetting hideDeletionActions = {
        "/appearance/messages/hideDeletionActions", false};
    BoolSetting colorizeNicknames = {"/appearance/messages/colorizeNicknames",
                                     true};
    EnumSetting<UsernameDisplayMode> usernameDisplayMode = {
        "/appearance/messages/usernameDisplayMode",
        UsernameDisplayMode::UsernameAndLocalizedName};

    EnumSetting<NotebookTabLocation> tabDirection = {"/appearance/tabDirection",
                                                     NotebookTabLocation::Top};
    EnumSetting<NotebookTabVisibility> tabVisibility = {
        "/appearance/tabVisibility",
        NotebookTabVisibility::AllTabs,
    };
    BoolSetting growWrappedNotebookLines = {
        "/appearance/growWrappedNotebookLines",
        false,
    };

    //    BoolSetting collapseLongMessages =
    //    {"/appearance/messages/collapseLongMessages", false};
    QStringSetting chatFontFamily{
        "/appearance/currentFontFamily",
        DEFAULT_FONT_FAMILY,
    };
    IntSetting chatFontSize{
        "/appearance/currentFontSize",
        DEFAULT_FONT_SIZE,
    };
    IntSetting chatFontWeight = {
        "/appearance/currentFontWeight",
        QFont::Normal,
    };
    BoolSetting hideReplyContext = {"/appearance/hideReplyContext", false};
    BoolSetting showReplyButton = {"/appearance/showReplyButton", false};
    BoolSetting stripReplyMention = {"/appearance/stripReplyMention", true};
    IntSetting collpseMessagesMinLines = {
        "/appearance/messages/collapseMessagesMinLines", 0};
    BoolSetting alternateMessages = {
        "/appearance/messages/alternateMessageBackground", false};
    FloatSetting boldScale = {"/appearance/boldScale", 63};
    BoolSetting showTabCloseButton = {"/appearance/showTabCloseButton", true};
    BoolSetting hideTabCloseButtonWhenLocked = {
        "/appearance/tabs/hideCloseButtonWhenLocked", false};
    BoolSetting keepTabWidthWhenLocked = {
        "/appearance/tabs/keepWidthWhenLocked", true};
    BoolSetting showTabLive = {"/appearance/showTabLiveButton", true};
    BoolSetting colorTabHighlightsByMessage = {
        "/appearance/tabs/colorHighlightsByMessage",
        false,
    };
    BoolSetting showTabGroupButton = {
        "/appearance/tabs/showGroupButton",
        true,
    };
    BoolSetting showTabGroupChannelNames = {
        "/appearance/tabs/showGroupChannelNames",
        true,
    };
    QStringSetting newTabGroupClickAction = {
        "/appearance/tabs/newGroupClickAction",
        "expand",
    };
    EnumStringSetting<TabStyle> tabStyle = {
        "/appearance/tabStyle",
        TabStyle::Normal,
    };
    BoolSetting hidePreferencesButton = {"/appearance/hidePreferencesButton",
                                         false};
    BoolSetting hideUserButton = {"/appearance/hideUserButton", false};
    BoolSetting enableSmoothScrolling = {"/appearance/smoothScrolling", true};
    BoolSetting enableSmoothScrollingNewMessages = {
        "/appearance/smoothScrollingNewMessages", false};
    BoolSetting displaySevenTVPaints = {"/misc/displaySevenTVPaints", false};
    BoolSetting displaySevenTVPaintShadows = {
        "/misc/displaySevenTVPaintShadows", false};
    BoolSetting largeSevenTVPaintShadows = {"/misc/largeSevenTVPaintShadows",
                                            false};
    BoolSetting boldUsernames = {"/appearance/messages/boldUsernames", true};
    BoolSetting colorUsernames = {"/appearance/messages/colorUsernames", true};
    BoolSetting linkChannelNames = {"/appearance/messages/linkChannelNames",
                                    false};
    BoolSetting findAllUsernames = {"/appearance/messages/findAllUsernames",
                                    false};
    // BoolSetting customizable splitheader
    BoolSetting headerViewerCount = {"/appearance/splitheader/showViewerCount",
                                     false};
    BoolSetting headerStreamTitle = {"/appearance/splitheader/showTitle",
                                     false};
    BoolSetting headerGame = {"/appearance/splitheader/showGame", false};
    BoolSetting headerUptime = {"/appearance/splitheader/showUptime", false};
    FloatSetting customThemeMultiplier = {"/appearance/customThemeMultiplier",
                                          -0.5F};
    // BoolSetting useCustomWindowFrame = {"/appearance/useCustomWindowFrame",
    // false};

    FloatSetting overlayScaleFactor = {"/appearance/overlay/scaleFactor", 1};
    IntSetting overlayBackgroundOpacity = {
        "/appearance/overlay/backgroundOpacity", 50};
    BoolSetting enableOverlayShadow = {"/appearance/overlay/shadow", true};
    IntSetting overlayShadowOpacity = {"/appearance/overlay/shadowOpacity",
                                       255};
    QStringSetting overlayShadowColor = {"/appearance/overlay/shadowColor",
                                         "#000"};
    // These should be floats, but there's no good input UI for them
    IntSetting overlayShadowOffsetX = {"/appearance/overlay/shadowOffsetX", 2};
    IntSetting overlayShadowOffsetY = {"/appearance/overlay/shadowOffsetY", 2};
    IntSetting overlayShadowRadius = {"/appearance/overlay/shadowRadius", 8};

    float getClampedOverlayScale() const;
    void setClampedOverlayScale(float value);

    // Badges
    FloatSetting badgeScale = {"/appearance/badges/scale", 1.f};
    IntSetting badgeAlignment = {
        "/appearance/badges/alignment",
        static_cast<int>(BadgeAlignmentMode::Balanced)};
    BoolSetting showBadgesGlobalAuthority = {
        "/appearance/badges/GlobalAuthority", true};
    BoolSetting showBadgesPredictions = {"/appearance/badges/predictions",
                                         true};
    BoolSetting showBadgesChannelAuthority = {
        "/appearance/badges/ChannelAuthority", true};
    BoolSetting showBadgesSubscription = {"/appearance/badges/subscription",
                                          true};
    BoolSetting showBadgesVanity = {"/appearance/badges/vanity", true};
    BoolSetting showVanityButton = {"/moltorino/vanity/showInputButton", true};
    BoolSetting showBadgesChatterino = {"/appearance/badges/chatterino", true};
    BoolSetting showBadgesFfz = {"/appearance/badges/ffz", true};
    BoolSetting showBadgesFfzAp = {"/appearance/badges/ffzap", true};
    BoolSetting showBadgesBluzyrino = {"/appearance/badges/bluzyrino", true};
    BoolSetting showBadgesJilChat = {"/appearance/badges/jilchat", true};
    BoolSetting showBadgesBadgeHQ = {"/appearance/badges/badgehq", true};
    BoolSetting useCustomFfzModeratorBadges = {
        "/appearance/badges/useCustomFfzModeratorBadges", true};
    BoolSetting useCustomFfzVipBadges = {
        "/appearance/badges/useCustomFfzVipBadges", true};
    BoolSetting showBadgesBttv = {"/appearance/badges/bttv", true};
    BoolSetting showBadgesSevenTV = {"/appearance/badges/seventv", true};
    BoolSetting showBadgesHomiesSupporter = {
        "/appearance/badges/homies/supporter", true};
    BoolSetting showBadgesHomiesCustom = {"/appearance/badges/homies/custom",
                                          true};
    BoolSetting showBadgesMoltorino = {"/appearance/badges/moltorino", true};
    BoolSetting useCustomBadgeLayouts = {"/appearance/badges/useCustomLayouts",
                                         true};
    QStringSetting sevenTVVanityToken = {"/moltorino/vanity/sevenTVToken", ""};
    QStringSetting sevenTVVanityTwitchUserId = {
        "/moltorino/vanity/sevenTVTwitchUserId", ""};
    QStringSetting sevenTVVanityUserId = {"/moltorino/vanity/sevenTVUserId",
                                          ""};
    QStringSetting sevenTVVanityLogin = {"/moltorino/vanity/sevenTVLogin", ""};
    QStringSetting sevenTVVanityTokenExpiry = {
        "/moltorino/vanity/sevenTVTokenExpiry", ""};
    QStringSetting localVanityLayouts = {"/moltorino/vanity/localBadgeLayouts",
                                         ""};
    BoolSetting animateSevenTVBadges = {"/appearance/badges/animateSeventv",
                                        false};
    QSizeSetting lastPopupSize = {
        "/appearance/lastPopup/size",
        {300, 500},
    };
    QSizeSetting pollPopupSize = {"/appearance/popups/poll", {}};
    QSizeSetting predictionPopupSize = {"/appearance/popups/prediction", {}};
    QSizeSetting editorPopupSize = {"/appearance/popups/editor", {}};
    QSizeSetting rewardsPopupSize = {"/appearance/popups/rewards", {}};
    QSizeSetting vanityPopupSize = {"/appearance/popups/vanity", {}};
    QSizeSetting gifPopupSize = {"/appearance/popups/gifs", {}};
    QSizeSetting usercardPopupSize = {"/appearance/popups/usercard", {}};
    QSizeSetting threadPopupSize = {"/appearance/popups/thread", {}};

    // Scrollbar
    BoolSetting hideScrollbarThumb = {
        "/appearance/scrollbar/hideThumb",
        false,
    };
    BoolSetting hideScrollbarHighlights = {
        "/appearance/scrollbar/hideHighlights",
        false,
    };

    BoolSetting pulseTextInputOnSelfMessage = {
        "/appearance/pulseTextInputOnSelfMessage",
        false,
    };

    /// Behaviour
    // Bajerino
    QStringSetting encryptionKey = {"/encryption/encryptionKey", ""};
    ChatterinoSetting<QHash<QString, bool>> encryptionChannelStates = {
        "/encryption/channel_states", {}};
    BoolSetting useLockIconForToggle = {"/encryption/useLockIconForToggle",
                                        true};
    BoolSetting big3Noticer = {"/bajerino/big3Noticer", true};
    EnumStringSetting<TwitchChannelMode> twitchDefaultChannelMode = {
        "/bajerino/twitchDefaultChannelMode", TwitchChannelMode::Authenticated};
    BoolSetting twitchAnonymousReadParallel = {
        "/bajerino/twitchAnonymousReadParallel", false};
    BoolSetting showAnonymousChannelIndicator = {
        "/bajerino/showAnonymousChannelIndicator", true};

    // General
    BoolSetting alwaysShowPinnedMessage = {"/behaviour/alwaysShowPinnedMessage",
                                           false};
    BoolSetting allowDuplicateMessages = {"/behaviour/allowDuplicateMessages",
                                          true};
    BoolSetting mentionUsersWithAt = {"/behaviour/mentionUsersWithAt", false};
    BoolSetting showJoins = {"/behaviour/showJoins", false};
    BoolSetting showParts = {"/behaviour/showParts", false};
    FloatSetting mouseScrollMultiplier = {"/behaviour/mouseScrollMultiplier",
                                          1.0};
    BoolSetting autoCloseUserPopup = {"/behaviour/autoCloseUserPopup", true};
    BoolSetting autoCloseThreadPopup = {"/behaviour/autoCloseThreadPopup",
                                        false};

    /// Specifies whether the search functionality should be enabled
    BoolSetting searchEnabled = {
        "/behaviour/search/enabled",
        false,
    };
    /// The URL of the search engine
    QStringSetting searchEngineUrl = {
        "/behaviour/search/engineUrl",
        "",
    };
    /// The name of the search engine
    QStringSetting searchEngineName = {
        "/behaviour/search/engineName",
        "",
    };
    BoolSetting searchIncognito = {
        "/behaviour/search/incognito",
        false,
    };

    EnumSetting<UsernameRightClickBehavior> usernameRightClickBehavior = {
        "/behaviour/usernameRightClickBehavior",
        UsernameRightClickBehavior::Mention,
    };
    EnumSetting<UsernameRightClickBehavior> usernameRightClickModifierBehavior =
        {
            "/behaviour/usernameRightClickBehaviorWithModifier",
            UsernameRightClickBehavior::Reply,
    };
    EnumSetting<Qt::KeyboardModifier> usernameRightClickModifier = {
        "/behaviour/usernameRightClickModifier",
        Qt::KeyboardModifier::ShiftModifier};

    BoolSetting autoSubToParticipatedThreads = {
        "/behaviour/autoSubToParticipatedThreads",
        true,
    };

    /// The maximum length the contents of a deleted message can be
    /// before we truncate it in the chat
    IntSetting deletedMessageLengthLimit = {
        "/behaviour/deletedMessageLengthLimit",
        50,
    };

    // Auto-completion
    BoolSetting prefixOnlyEmoteCompletion = {
        "/behaviour/autocompletion/prefixOnlyCompletion", true};
    BoolSetting userCompletionOnlyWithAt = {
        "/behaviour/autocompletion/userCompletionOnlyWithAt", false};
    BoolSetting emoteCompletionWithColon = {
        "/behaviour/autocompletion/emoteCompletionWithColon", true};
    BoolSetting showUsernameCompletionMenu = {
        "/behaviour/autocompletion/showUsernameCompletionMenu", true};
    BoolSetting alwaysIncludeBroadcasterInUserCompletions = {
        "/behaviour/autocompletion/alwaysIncludeBroadcasterInUserCompletions",
        true,
    };
    BoolSetting showChatterListInAllTwitchChannels = {
        "/behaviour/chatterList/showInAllTwitchChannels", true};
    BoolSetting onlyFetchChattersForSmallerStreamers = {
        "/behaviour/autocompletion/onlyFetchChattersForSmallerStreamers", true};
    IntSetting smallStreamerLimit = {
        "/behaviour/autocompletion/smallStreamerLimit", 1000};
    QStringSetting chatterListDataMode = {"/behaviour/chatterList/dataMode",
                                          "best"};
    BoolSetting useSmartEmoteCompletion = {
        "/experiments/useSmartEmoteCompletion",
        false,
    };

    BoolSetting enableSpellChecking = {
        "/behaviour/spellChecking/enabled",
        false,
    };
    QStringSetting spellCheckingDefaultDictionary = {
        "/behaviour/spellChecking/defaultDictionary",
        "included:en_US",
    };
    IntSetting nSpellCheckingSuggestions = {
        "/behaviour/spellChecking/suggestions/count",
        -1,
    };

    FloatSetting pauseOnHoverDuration = {"/behaviour/pauseOnHoverDuration", 0};
    EnumSetting<Qt::KeyboardModifier> pauseChatModifier = {
        "/behaviour/pauseChatModifier", Qt::KeyboardModifier::NoModifier};
    BoolSetting autorun = {"/behaviour/autorun", false};
    BoolSetting mentionUsersWithComma = {"/behaviour/mentionUsersWithComma",
                                         false};

    BoolSetting disableTabRenamingOnClick = {
        "/behaviour/disableTabRenamingOnClick",
        false,
    };

    IntSetting sharedChatSessionRefreshInterval = {
        "/behaviour/sharedChatSessionRefreshInterval", 60};

    BoolSetting sharedChatAlwaysShowBadge = {
        "/behaviour/sharedChatAlwaysShowBadge",
        true,
    };

    /// Emotes
    BoolSetting scaleEmotesByLineHeight = {"/emotes/scaleEmotesByLineHeight",
                                           false};
    BoolSetting enableEmoteImages = {"/emotes/enableEmoteImages", true};
    BoolSetting animateEmotes = {"/emotes/enableGifAnimations", true};
    BoolSetting enableZeroWidthEmotes = {"/emotes/enableZeroWidthEmotes", true};
    BoolSetting enableEmoteModifiers = {"/emotes/enableModifiers", true};
    FloatSetting emoteScale = {"/emotes/scale", 1.F};
    EnumStringSetting<EmoteTooltipScale> emoteTooltipScale = {
        "/emotes/tooltipScale",
        EmoteTooltipScale::Medium,
    };
    BoolSetting showUnlistedSevenTVEmotes = {
        "/emotes/showUnlistedSevenTVEmotes", true};
    /**
     * This setting is kept for backwards compatibility.
     */
    BoolSetting showUnlistedEmotesDontUse = {"/emotes/showUnlistedEmotes",
                                             false};

    EnumStringSetting<EmojiStyle> emojiSet = {
        "/emotes/emojiSet",
        EmojiStyle::Twitter,
    };

    BoolSetting stackBits = {"/emotes/stackBits", false};
    BoolSetting removeSpacesBetweenEmotes = {
        "/emotes/removeSpacesBetweenEmotes", false};

    BoolSetting enableBTTVGlobalEmotes = {"/emotes/bttv/global", true};
    BoolSetting enableBTTVChannelEmotes = {"/emotes/bttv/channel", true};
    BoolSetting enableBTTVLiveUpdates = {"/emotes/bttv/liveupdates", true};
    BoolSetting sendBTTVActivity = {"/emotes/bttv/sendActivity", false};
    BoolSetting enableFFZGlobalEmotes = {"/emotes/ffz/global", true};
    BoolSetting enableFFZChannelEmotes = {"/emotes/ffz/channel", true};
    BoolSetting enableSevenTVGlobalEmotes = {"/emotes/seventv/global", true};
    BoolSetting enableSevenTVChannelEmotes = {"/emotes/seventv/channel", true};
    BoolSetting enableSevenTVPersonalEmotes = {"/emotes/seventv/personal",
                                               false};
    BoolSetting enableSevenTVEventAPI = {"/emotes/seventv/eventapi", true};
    BoolSetting sendSevenTVActivity = {"/emotes/seventv/sendActivity", false};

    BoolSetting allowAvifImages = {"/emotes/allowAvif", true};

    ChatterinoSetting<QStringList> favouriteEmotes = {
        "/emotes/favouriteEmotes",
        {},
    };
    ChatterinoSetting<QStringList> favouriteEmojis = {
        "/emotes/favouriteEmojis",
        {},
    };
    QStringSetting favoriteTwitchGifs{"/twitch/gifs/favorites", "[]"};

    /// Links
    BoolSetting linksDoubleClickOnly = {"/links/doubleClickToOpen", false};
    BoolSetting linkInfoTooltip = {"/links/linkInfoTooltip", false};
    IntSetting thumbnailSize = {"/appearance/thumbnailSize", 0};
    IntSetting thumbnailSizeStream = {"/appearance/thumbnailSizeStream", 2};
    BoolSetting unshortLinks = {"/links/unshortLinks", false};
    BoolSetting lowercaseDomains = {"/links/linkLowercase", true};

    /// Streamer Mode
    // TODO: Should these settings be converted to booleans that live outside of
    // streamer mode?
    // Something like:
    //  - "Hide when streamer mode is enabled"
    //  - "Always hide"
    //  - "Don't hide"
    EnumSetting<StreamerModeSetting> enableStreamerMode = {
        "/streamerMode/enabled",
        StreamerModeSetting::DetectStreamingSoftware,
    };
    BoolSetting streamerModeHideUsercardAvatars = {
        "/streamerMode/hideUsercardAvatars", true};
    BoolSetting streamerModeHideLinkThumbnails = {
        "/streamerMode/hideLinkThumbnails", true};
    BoolSetting streamerModeHideViewerCountAndDuration = {
        "/streamerMode/hideViewerCountAndDuration", false};
    BoolSetting streamerModeHideModActions = {"/streamerMode/hideModActions",
                                              true};
    BoolSetting streamerModeHideRestrictedUsers = {
        "/streamerMode/hideRestrictedUsers",
        true,
    };
    BoolSetting streamerModeMuteMentions = {"/streamerMode/muteMentions", true};
    BoolSetting streamerModeSuppressLiveNotifications = {
        "/streamerMode/supressLiveNotifications", false};
    BoolSetting streamerModeSuppressInlineWhispers = {
        "/streamerMode/suppressInlineWhispers", true};
    BoolSetting streamerModeHideBlockedTermText = {
        "/streamerMode/hideBlockedTermText",
        true,
    };
    BoolSetting streamerModeHideUserNotes = {
        "/streamerMode/hideUserNotes",
        true,
    };

    /// Blocked Users
    BoolSetting enableTwitchBlockedUsers = {"/ignore/enableTwitchBlockedUsers",
                                            true};
    IntSetting showBlockedUsersMessages = {"/ignore/showBlockedUsers", 0};

    /// Moderation
    IntSetting defaultCommercialDuration = {
        "/moltorino/moderation/defaultCommercialDuration", 30};
    QStringSetting timeoutAction = {"/moderation/timeoutAction", "Disable"};
    IntSetting timeoutStackStyle = {
        "/moderation/timeoutStackStyle",
        static_cast<int>(TimeoutStackStyle::Default)};
    EnumStringSetting<ShowModerationState> showBlockedTermAutomodMessages = {
        "/moderation/showBlockedTermAutomodMessages",
        ShowModerationState::Always,
    };

    /// Highlighting
    BoolSetting highlightMatchedFragments = {
        "/highlighting/matchedFragments/enabled", false};
    QStringSetting automodMatchHighlightColor = {
        "/highlighting/automod/matchedFragment/color",
        defaultAutoModMatchColor().name(QColor::HexArgb),
    };
    IntSetting automodMatchHighlightStyle = {
        "/highlighting/automod/matchedFragment/style",
        static_cast<int>(HighlightMatchStyle::Fill),
    };
    BoolSetting advancedAutoModInChat = {
        "/moltorino/moderation/advancedAutoModInChat", false};

    BoolSetting longAlerts = {"/highlighting/alerts", false};

    BoolSetting highlightMentions = {"/highlighting/mentions", true};

    /// Filtering
    BoolSetting excludeUserMessagesFromFilter = {
        "/filtering/excludeUserMessages", false};

    /// Logging
    BoolSetting enableLogging = {"/logging/enabled", false};
    BoolSetting onlyLogListedChannels = {"/logging/onlyLogListedChannels",
                                         false};
    BoolSetting separatelyStoreStreamLogs = {
        "/logging/separatelyStoreStreamLogs",
        false,
    };
    QStringSetting logTimestampFormat = {
        "/logging/logTimestampFormat",
        "hh:mm:ss",
    };
    BoolSetting tryUseTwitchTimestamps = {
        "/logging/tryUseTwitchTimestamps",
        false,
    };
    QStringSetting logPath = {"/logging/path", ""};

    BoolSetting highlightAlwaysPlaySound = {"/highlighting/alwaysPlaySound",
                                            false};

    BoolSetting inlineWhispers = {"/whispers/enableInlineWhispers", true};

    /// Notifications
    BoolSetting notificationFlashTaskbar = {"/notifications/enableFlashTaskbar",
                                            false};
    BoolSetting notificationPlaySound = {"/notifications/enablePlaySound",
                                         false};
    BoolSetting notificationCustomSound = {"/notifications/customPlaySound",
                                           false};
    QStringSetting notificationPathSound = {"/notifications/highlightSoundPath",
                                            "qrc:/sounds/ping3.wav"};
    BoolSetting notificationOnAnyChannel = {"/notifications/onAnyChannel",
                                            false};
    BoolSetting suppressInitialLiveNotification = {
        "/notifications/suppressInitialLive", false};

    BoolSetting notificationToast = {"/notifications/enableToast", false};
    BoolSetting createShortcutForToasts;  // initialized in ctor
    IntSetting openFromToast = {"/notifications/openFromToast",
                                static_cast<int>(ToastReaction::OpenInBrowser)};

    /// External tools
    // Streamlink
    BoolSetting streamlinkUseCustomPath = {"/external/streamlink/useCustomPath",
                                           false};
    QStringSetting streamlinkPath = {"/external/streamlink/customPath", ""};
    EnumStringSetting<StreamLinkPreferredQuality> preferredQuality = {
        "/external/streamlink/quality",
        StreamLinkPreferredQuality::Choose,
    };
    QStringSetting streamlinkOpts = {"/external/streamlink/options", ""};

    // Custom URI Scheme
    QStringSetting customURIScheme = {"/external/urischeme"};

    // Image Uploader
    BoolSetting imageUploaderEnabled = {"/external/imageUploader/enabled",
                                        false};
    QStringSetting imageUploaderUrl = {"/external/imageUploader/url", ""};
    QStringSetting imageUploaderFormField = {
        "/external/imageUploader/formField", ""};
    QStringSetting imageUploaderHeaders = {"/external/imageUploader/headers",
                                           ""};
    QStringSetting imageUploaderLink = {"/external/imageUploader/link", ""};
    QStringSetting imageUploaderDeletionLink = {
        "/external/imageUploader/deletionLink", ""};

    /// Misc
    BoolSetting betaUpdates = {"/misc/beta", false};

    IntSetting startUpNotification = {"/misc/startUpNotification", 0};
    QStringSetting currentVersion = {"/misc/currentVersion", ""};
    IntSetting overlayKnowledgeLevel = {"/misc/overlayKnowledgeLevel", 0};

    BoolSetting loadTwitchMessageHistoryOnConnect = {
        "/misc/twitch/loadMessageHistoryOnConnect", true};
    QStringSetting messageHistoryUrl = {"/misc/twitch/messageHistoryUrl", ""};
    IntSetting twitchMessageHistoryLimit = {
        "/misc/twitch/messageHistoryLimit",
        500,
    };
    IntSetting scrollbackSplitLimit = {
        "/misc/scrollback/splitLimit",
        1000,
    };
    IntSetting scrollbackUsercardLimit = {
        "/misc/scrollback/usercardLimit",
        1000,
    };
    BoolSetting displaySevenTVAnimatedProfile = {
        "/misc/displaySevenTVAnimatedProfile", false};

    EnumStringSetting<KickConnectionPreference> kickConnectionPreference = {
        "/misc/x-7tv/kick/connectionPreference",
        KickConnectionPreference::Default,
    };

    BoolSetting connectAnonymouslyWhenBanned = {
        "/misc/connectAnonymouslyWhenBanned", true};

    EnumStringSetting<ChatSendProtocol> chatSendProtocol = {
        "/misc/chatSendProtocol", ChatSendProtocol::Default};

    BoolSetting openLinksIncognito = {"/misc/openLinksIncognito", false};

    EnumSetting<ThumbnailPreviewMode> emotesTooltipPreview = {
        "/misc/emotesTooltipPreview",
        ThumbnailPreviewMode::AlwaysShow,
    };
    QStringSetting cachePath = {"/cache/path", ""};
    BoolSetting attachExtensionToAnyProcess = {
        "/misc/attachExtensionToAnyProcess", false};
    BoolSetting askOnImageUpload = {"/misc/askOnImageUpload", true};
    BoolSetting informOnTabVisibilityToggle = {"/misc/askOnTabVisibilityToggle",
                                               true};
    BoolSetting lockNotebookLayout = {"/misc/lockNotebookLayout", false};
    BoolSetting showPronouns = {"/misc/showPronouns", false};
    BoolSetting showPronounsInChat = {"/misc/showPronounsInChat", false};
    BoolSetting showUsercardBadges = {"/usercard/showBadges", true};
    BoolSetting showUsercardSevenTVPaint = {"/usercard/showSevenTVPaint", true};
    BoolSetting showUsercardFollowerCount = {"/usercard/showFollowerCount",
                                             true};
    BoolSetting showUsercardCreatedDate = {"/usercard/showCreatedDate", true};
    BoolSetting showUsercardFollowage = {"/usercard/showFollowage", true};
    BoolSetting showUsercardFollowageRelativeTime = {
        "/usercard/showFollowageRelativeTime", true};
    BoolSetting showUsercardSubage = {"/usercard/showSubage", true};
    BoolSetting showUsercardSubageRelativeTime = {
        "/usercard/showSubageRelativeTime", true};
    BoolSetting showUsercardSubGiftSource = {"/usercard/showSubGiftSource",
                                             true};
    BoolSetting showUsercardChatterCount = {"/usercard/showChatterCount", true};
    BoolSetting showUsercardLastLive = {"/usercard/showLastLive", true};
    BoolSetting showUsercardColor = {"/usercard/showColor", true};
    BoolSetting showUsercardStatus = {"/usercard/showStatus", true};
    BoolSetting showSevenTVUsercardButton = {"/usercard/showSevenTVButton",
                                             true};
    BoolSetting showUsercardNameHistoryButton = {
        "/usercard/showNameHistoryButton", true};
    BoolSetting showUsercardLoadMoreMessagesButton = {
        "/usercard/showLoadMoreMessagesButton", true};
    BoolSetting alwaysLoadMoreUsercardMessages = {
        "/usercard/alwaysLoadMoreMessages", false};
    QStringSetting usercardUsercardActionPlacement = {
        "/usercard/actions/usercard", "menu"};
    QStringSetting usercardCommentsActionPlacement = {
        "/usercard/actions/comments", "bar"};
    QStringSetting usercardLogsActionPlacement = {"/usercard/actions/logs",
                                                  "bar"};
    QStringSetting usercardRolesActionPlacement = {"/usercard/actions/roles",
                                                   "bar"};
    QStringSetting usercardBadgesActionPlacement = {"/usercard/actions/badges",
                                                    "bar"};
    QStringSetting usercardSevenTVActionPlacement = {
        "/usercard/actions/seventv", "bar"};
    QStringSetting usercardNotesActionPlacement = {"/usercard/actions/notes",
                                                   "bar"};
    QStringSetting usercardBlockActionPlacement = {"/usercard/actions/block",
                                                   "menu"};
    QStringSetting usercardHideActionPlacement = {"/usercard/actions/hide",
                                                  "menu"};
    QStringSetting usercardIgnoreHighlightsActionPlacement = {
        "/usercard/actions/ignoreHighlights", "menu"};
    QStringSetting usercardCrossBanActionPlacement = {
        "/usercard/actions/crossBan", "menu"};
    QStringSetting usercardCrossUnbanActionPlacement = {
        "/usercard/actions/crossUnban", "menu"};
    BoolSetting showCrossActionsInUnmoderatedChannels = {
        "/usercard/showCrossActionsInUnmoderatedChannels", true};
    BoolSetting showUserLogsDate = {"/usercard/logs/showDate", true};
    BoolSetting userLogsNewestAtBottom = {"/usercard/logs/newestAtBottom",
                                          true};
    QStringSetting userLogsDateStyle = {"/usercard/logs/dateStyle", "compact"};
    QStringSetting userLogsTimeStyle = {"/usercard/logs/timeStyle",
                                        "24h-minute"};
    BoolSetting showModeratorCommentsButton = {
        "/usercard/showModeratorCommentsButton", true};
    BoolSetting showUsercardCrossBanAction = {"/usercard/showCrossBanAction",
                                              true};
    BoolSetting showUsercardCrossUnbanAction = {
        "/usercard/showCrossUnbanAction", true};
    BoolSetting showLeadModRoleButtons = {"/usercard/showLeadModRoleButtons",
                                          true};
    BoolSetting showUsercardRoleManagementMenu = {
        "/usercard/showRoleManagementMenu", false};
    BoolSetting hideModActionsOnModUsercards = {
        "/misc/hideModActionsOnModUsercards", true};
    BoolSetting showModActionsOnModUsercardsAsLeadMod = {
        "/usercard/showModActionsOnModUsercardsAsLeadMod", false};
    BoolSetting hideEmojiButton = {"/misc/hideEmojiButton", false};
    BoolSetting showTitleInLiveMessage = {
        "/extraChannels/live/showTitle",
        false,
    };

    /// UI

    BoolSetting showSendButton = {"/ui/showSendButton", false};

    struct {
        // this isn't shown in the UI
        BoolSetting enabled = {"/plugins/repl/enabled", false};
        // An empty string implies the default monospace font
        QStringSetting fontFamily = {"/plugins/repl/fontFamily", {}};
        QStringSetting fontStyle = {"/plugins/repl/fontStyle", "Regular"};
        IntSetting fontSize = {"/plugins/repl/fontSize", 10};
    } pluginRepl;

    // Similarity
    BoolSetting similarityEnabled = {"/similarity/similarityEnabled", false};
    BoolSetting colorSimilarDisabled = {"/similarity/colorSimilarDisabled",
                                        true};
    BoolSetting hideSimilar = {"/similarity/hideSimilar", false};
    BoolSetting hideSimilarBySameUser = {"/similarity/hideSimilarBySameUser",
                                         true};
    BoolSetting hideSimilarMyself = {"/similarity/hideSimilarMyself", false};
    BoolSetting shownSimilarTriggerHighlights = {
        "/similarity/shownSimilarTriggerHighlights", false};
    FloatSetting similarityPercentage = {"/similarity/similarityPercentage",
                                         0.9F};
    IntSetting hideSimilarMaxDelay = {"/similarity/hideSimilarMaxDelay", 5};
    IntSetting hideSimilarMaxMessagesToCheck = {
        "/similarity/hideSimilarMaxMessagesToCheck", 3};

    /// Timeout buttons

    ChatterinoSetting<std::vector<TimeoutButton>> timeoutButtons = {
        "/timeouts/timeoutButtons",
        {{"s", 1},
         {"s", 30},
         {"m", 1},
         {"m", 5},
         {"m", 30},
         {"h", 1},
         {"d", 1},
         {"w", 1}}};
    ChatterinoSetting<std::vector<QString>> timeoutButtonReasons = {
        "/timeouts/timeoutButtonReasons", {}};
    QStringSetting timeoutBanReason = {"/timeouts/banReason", {}};
    BoolSetting timeoutReasonPromptOnRightClick = {
        "/timeouts/reasonPromptOnRightClick", true};
    BoolSetting timeoutReasonPromptOnModifier = {
        "/timeouts/reasonPromptOnModifier", true};
    QStringSetting timeoutReasonPromptModifier = {
        "/timeouts/reasonPromptModifier", "Shift"};
    BoolSetting timeoutReasonPromptShowSendButton = {
        "/timeouts/reasonPromptShowSendButton", false};
    BoolSetting timeoutReasonPromptPrefillSavedReason = {
        "/timeouts/reasonPromptPrefillSavedReason", true};

    BoolSetting pluginsEnabled = {"/plugins/supportEnabled", false};
    ChatterinoSetting<QStringList> enabledPlugins = {
        "/plugins/enabledPlugins",
        {},
    };

    // Sound
    EnumStringSetting<SoundBackend> soundBackend = {
        "/sound/backend",
        SoundBackend::Miniaudio,
    };

    BoolSetting soundMiniaudioKeepEngineAlive = {
        "/sound/miniaudio/keepEngineAlive",
        false,
    };

    // Advanced
    QStringSetting additionalExtensionIDs{"/misc/additionalExtensionIDs", ""};

#ifndef Q_OS_WIN
    QStringSetting customNativeMessagingManifestPath{
        "/misc/extension/customManifestPath",
        "",
    };
    EnumStringSetting<BrowserManifestFormat>
        customNativeMessagingManifestFormat = {
            "/misc/extension/customManifestFormat",
            BrowserManifestFormat::Chrome,
    };
#endif

    BoolSetting xChatterino7NoHttp2{"/x-chatterino7/no-http2", false};

    /// Moltorino Settings
    BoolSetting enablePinnedMessages{"/moltorino/pinnedMessages/enabled", true};
    BoolSetting showUnpinNotifications{
        "/moltorino/pinnedMessages/showUnpinNotifications", true};
    IntSetting defaultPinDuration{"/moltorino/pinnedMessages/defaultDuration",
                                  -1};
    BoolSetting enablePinCommandMessages{
        "/moltorino/pinnedMessages/enablePinCommandMessages", true};
    BoolSetting enablePinUserCommand{
        "/moltorino/pinnedMessages/enablePinUserCommand", true};
    BoolSetting requireAtForPinUserCommand{
        "/moltorino/pinnedMessages/requireAtForPinUserCommand", false};
    BoolSetting alwaysExpandPinnedMessages{
        "/moltorino/pinnedMessages/alwaysExpand", false};

    FloatSetting pinnedMessageScale{"/moltorino/pinnedMessages/scale", 1.f};

    FloatSetting pinnedContentScale{"/moltorino/pinnedMessages/contentScale",
                                    1.1f};

    IntSetting pinCloseButtonAction{
        "/moltorino/pinnedMessages/closeButtonAction", 1};

    IntSetting pinTimerDisplay{"/moltorino/pinnedMessages/timerDisplay", 0};

    QStringSetting pinTimestampFormat{
        "/moltorino/pinnedMessages/timestampFormat", "Relative"};

    QStringSetting pinBannerBackgroundColor{
        "/moltorino/pinnedMessages/customBackgroundColor", ""};
    BoolSetting enablePinUnpinning{
        "/moltorino/pinnedMessages/enablePinUnpinning", true};
    BoolSetting movePinToModerateMenu{
        "/moltorino/pinnedMessages/movePinToModerateMenu", false};
    IntSetting showPinButtonOnModeratorsMode{
        "/moltorino/pinnedMessages/showPinButtonOnModeratorsMode", 1};
    QStringSetting customPinAuthToken{
        "/moltorino/pinnedMessages/customAuthToken", ""};
    QStringSetting moltorinoAuthAccounts{"/moltorino/auth/accounts", ""};
    BoolSetting moltorinoAllowSeparateAuthAccounts{
        "/moltorino/auth/allowSeparateAccounts", false};

    QStringSetting botBadgeClientID{"/moltorino/botBadge/clientId", ""};
    QStringSetting botBadgeClientSecret{"/moltorino/botBadge/clientSecret", ""};
    QStringSetting botBadgeAppAccessToken{"/moltorino/botBadge/appAccessToken",
                                          ""};
    QStringSetting botBadgeAppTokenExpiry{"/moltorino/botBadge/appTokenExpiry",
                                          ""};
    QStringSetting botBadgeUserID{"/moltorino/botBadge/userId", ""};
    QStringSetting botBadgeUserLogin{"/moltorino/botBadge/userLogin", ""};
    QStringSetting botBadgeUserName{"/moltorino/botBadge/userName", ""};
    BoolSetting botBadgeAlwaysUse{"/moltorino/botBadge/alwaysUse", false};
    BoolSetting botBadgeOverrideAllAccounts{
        "/moltorino/botBadge/overrideAllAccounts", false};

    BoolSetting enablePredictions{"/moltorino/predictions/enabled", true};
    BoolSetting enablePolls{"/moltorino/polls/enabled", true};
    BoolSetting showPredictionButton{"/moltorino/predictions/showButton", true};
    BoolSetting showPollButton{"/moltorino/polls/showButton", true};

    BoolSetting enableChannelPointsDisplay{
        "/moltorino/predictions/showChannelPoints", true};
    BoolSetting openRewardsWithChannelPointsClick{
        "/moltorino/channelPoints/openRewardsWithBalanceClick", true};
    BoolSetting rewardsCloseOnFocusLoss{
        "/moltorino/channelPoints/closeOnFocusLoss", true};
    BoolSetting rewardsCloseAfterRedeem{
        "/moltorino/channelPoints/closeAfterRedeem", true};
    BoolSetting rewardsReturnToListAfterRedeem{
        "/moltorino/channelPoints/returnToListAfterRedeem", false};
    BoolSetting autoClaimChannelPoints{"/moltorino/channelPoints/autoClaim",
                                       true};
    BoolSetting farmChannelPoints{"/moltorino/channelPoints/farm", false};
    IntSetting maxFarmedChannels{"/moltorino/channelPoints/maxFarmed", 2};
    BoolSetting enableGigantifyEmotes{
        "/moltorino/channelPoints/enableGigantifyEmotes", true};
    BoolSetting twitchGifsAsEmotes{"/moltorino/chat/twitchGifsAsEmotes", false};
    BoolSetting highQualityTwitchGifs{"/moltorino/chat/highQualityTwitchGifs",
                                      false};
    FloatSetting twitchGifScale{"/moltorino/chat/twitchGifScaleV2", 0.75f};

    FloatSetting predictionBannerContentScale{
        "/moltorino/predictions/bannerContentScale", 1.F};
    FloatSetting pollBannerContentScale{"/moltorino/polls/bannerContentScale",
                                        1.F};
    IntSetting predictionModAction{"/moltorino/predictions/modAction", 0};
    BoolSetting showPredictionSystemMessages{
        "/moltorino/predictions/showSystemMessages", true};
    BoolSetting predictionAutoCloseDialog{
        "/moltorino/predictions/autoCloseDialog", true};
    BoolSetting pollAutoCloseDialog{"/moltorino/polls/autoCloseDialog", false};
    IntSetting predictionAutoDismissSeconds{
        "/moltorino/predictions/autoDismissSeconds", 300};
    BoolSetting limitPredictionDialogs{"/moltorino/predictions/limitPopups",
                                       true};
    BoolSetting predictionDialogsPerChannel{
        "/moltorino/predictions/limitPerChannel", true};
    BoolSetting predictionCloseOnFocusLoss{
        "/moltorino/predictions/closeOnFocusLoss", false};
    IntSetting bannerStackMode{"/moltorino/banners/stackModeV2", 3};

    BoolSetting enableRepeatedMessageDetector{
        "/moltorino/moderation/repeatedMessages/enabled", true};
    BoolSetting repeatedMessagesShowOnlyModerationMode{
        "/moltorino/moderation/repeatedMessages/showOnlyModerationMode", true};
    BoolSetting repeatedMessagesShowInUsercards{
        "/moltorino/moderation/repeatedMessages/showInUsercards", true};
    BoolSetting repeatedMessagesOnlyModChannels{
        "/moltorino/moderation/repeatedMessages/onlyModChannels", true};
    BoolSetting repeatedMessagesIgnoreVips{
        "/moltorino/moderation/repeatedMessages/ignoreVips", false};
    BoolSetting autoModReviewShowReason{
        "/moltorino/moderation/autoModReview/showReason", true};
    BoolSetting autoModReviewShowContext{
        "/moltorino/moderation/autoModReview/showContext", true};
    BoolSetting autoModReviewShowShortcutHints{
        "/moltorino/moderation/autoModReview/showShortcutHints", false};

    IntSetting autoModReviewQueueOrder{
        "/moltorino/moderation/autoModReview/queueOrder", 1};

    IntSetting autoModReviewScrollPosition{
        "/moltorino/moderation/autoModReview/scrollPosition", 0};

    IntSetting autoModReviewContextOrder{
        "/moltorino/moderation/autoModReview/contextOrder", 0};

    /// 0 = Loose (60%), 1 = Soft (70%), 2 = Default (80%),
    /// 3 = Strict (90%), 4 = Exact only (100%)
    IntSetting repeatedMessagesSensitivity{
        "/moltorino/moderation/repeatedMessages/sensitivity", 2};
    IntSetting repeatedMessagesRepetitionThreshold{
        "/moltorino/moderation/repeatedMessages/repetitionThreshold", 2};
    QStringSetting repeatedMessagesCounterColor{
        "/moltorino/moderation/repeatedMessages/counterColor", "#ff3b3b"};
    IntSetting showSelfDeleteButton{
        "/moltorino/moderation/showSelfDeleteButton", 1};
    BoolSetting showModerationButtonsWithoutPermission{
        "/moltorino/moderation/showButtonsWithoutPermission", false};
    BoolSetting nukePreviewEnabled{"/moltorino/moderation/nuke/previewEnabled",
                                   true};
    BoolSetting nukeShowSummary{"/moltorino/moderation/nuke/showSummary", true};
    BoolSetting nukeSkipVips{"/moltorino/moderation/nuke/skipVips", false};
    QStringSetting nukeModerationMessage{
        "/moltorino/moderation/nuke/moderationMessage", ""};
    BoolSetting showRaidStatusAboveInput{
        "/moltorino/moderation/raid/showStatusAboveInput", true};

    BoolSetting spoofIrcMessagesAsWeb{"/moltorino/client/spoofIrcMessagesAsWeb",
                                      true};
    BoolSetting showClientDetectionHighlights{
        "/moltorino/client/showDetectionHighlights", false};

    QStringSetting clientDetectionDisplayMode{
        "/moltorino/client/clientDetectionDisplayMode", ""};
    BoolSetting showAbnormalClientDetectionHighlights{
        "/moltorino/client/showAbnormalNonceHighlights", false};
    QStringSetting clientDetectionWebColor{
        "/moltorino/client/webHighlightColor", "#33ff9f1a"};
    QStringSetting clientDetectionAndroidColor{
        "/moltorino/client/androidHighlightColor", "#3325d366"};
    QStringSetting clientDetectionIosColor{
        "/moltorino/client/iosHighlightColor", "#33ff69b4"};
    QStringSetting clientDetectionAbnormalColor{
        "/moltorino/client/abnormalNonceHighlightColor", "#44ff4d4d"};
    BoolSetting showTranslateMessageContextAction{
        "/moltorino/client/showTranslateMessageContextAction", true};
    QStringSetting messageTranslationTargetLanguage{
        "/moltorino/client/messageTranslationTargetLanguage", "en"};
    QStringSetting translationProvider{"/moltorino/client/translationProvider",
                                       "google"};
    QStringSetting translationLibreEndpoint{
        "/moltorino/client/translationLibreEndpoint",
        "https://libretranslate.com"};
    QStringSetting translationAzureRegion{
        "/moltorino/client/translationAzureRegion", ""};
    QStringSetting translationAzureEndpoint{
        "/moltorino/client/translationAzureEndpoint", ""};
    BoolSetting showTranslatedMessageIndicator{
        "/moltorino/client/showTranslatedMessageIndicator", true};
    BoolSetting showOutgoingTranslationButton{
        "/moltorino/client/showOutgoingTranslationButton", true};
    QStringSetting outgoingTranslationMode{
        "/moltorino/client/outgoingTranslationMode", "off"};
    QStringSetting outgoingTranslationTargetLanguage{
        "/moltorino/client/outgoingTranslationTargetLanguage", "it"};
    QStringSetting recentMessagesProvider{
        "/moltorino/client/recentMessagesProvider", "robotty"};

    BoolSetting chatAutomationsRunInBackground{
        "/moltorino/chatAutomations/runInBackground", true};
    IntSetting chatAutomationsMaxRunsPer30Seconds{
        "/moltorino/chatAutomations/maxRunsPer30Seconds", 6};

    IntSetting spamCommandIntervalMs{"/moltorino/fun/spam/intervalMs", 30};
    BoolSetting spamCommandUseIrc{"/moltorino/fun/spam/useIrc", false};
    BoolSetting showSpamPyramidStatusMessages{
        "/moltorino/fun/spam/showStatusMessages", true};
    BoolSetting sendMessageAsWarnings{"/moltorino/fun/sendMessageAsWarnings",
                                      false};

    BoolSetting showInputPlaceholder{"/moltorino/showInputPlaceholder", true};
    BoolSetting showMultiChannelDestinationSelector{
        "/moltorino/showMultiChannelDestinationSelector", true};
    BoolSetting showCommandSuggestions{"/moltorino/showCommandSuggestions",
                                       true};
    BoolSetting showCommandArgumentHints{"/moltorino/showCommandArgumentHints",
                                         true};
    BoolSetting showEmoteTabCarousel{"/moltorino/showEmoteTabCarousel", false};
    QStringSetting emoteTabCarouselSize{"/moltorino/emoteTabCarouselSize",
                                        "standard"};
    BoolSetting showEmoteTabCarouselNames{
        "/moltorino/showEmoteTabCarouselNames", true};
    BoolSetting includePotatCommands{"/moltorino/includePotatCommands", true};
    BoolSetting showPotatCommandAliases{"/moltorino/showPotatCommandAliases",
                                        true};
    BoolSetting hideUnavailableModCommands{
        "/moltorino/hideUnavailableModCommands", true};
    BoolSetting showFollowButtonInSplitHeader{
        "/moltorino/showFollowButtonInSplitHeader", true};
    BoolSetting showSharedChatChannelSelector{
        "/moltorino/showSharedChatChannelSelector", true};
    BoolSetting showFollowButtonInUsercard{
        "/moltorino/showFollowButtonInUsercard", true};
    BoolSetting showDailyPositiveMessage{"/moltorino/dailyMessage/enabled",
                                         false};
    QStringSetting dailyPositiveMessageLastDate{
        "/moltorino/dailyMessage/lastShownDate", {}};
    BoolSetting showEditStreamInfoButtonInSplitHeader{
        "/moltorino/showEditStreamInfoButtonInSplitHeader", true};
    BoolSetting confirmUnfollowFromSplitHeader{
        "/moltorino/confirmUnfollowFromSplitHeader", true};
    BoolSetting macosKeepRunningAfterClose{
        "/moltorino/macos/keepRunningAfterClose", true};
    BoolSetting trayHideOnClose{"/moltorino/tray/hideOnClose",
#ifdef Q_OS_MACOS
                                false
#else
                                true
#endif
    };
    BoolSetting trayNotifyOnSoundHighlights{
        "/moltorino/tray/notifyOnSoundHighlights",
#ifdef Q_OS_MACOS
        false
#else
        true
#endif
    };

private:
    ChatterinoSetting<std::vector<HighlightBlacklistUser>>
        blacklistedUsersSetting = {"/highlighting/blacklist"};
    ChatterinoSetting<std::vector<IgnorePhrase>> ignoredMessagesSetting = {
        "/ignore/phrases"};
    ChatterinoSetting<std::vector<HiddenUser>> hiddenUsersSetting = {
        "/moltorino/hiddenUsers"};
    ChatterinoSetting<std::vector<QString>> mutedChannelsSetting = {
        "/pings/muted"};
    ChatterinoSetting<std::vector<QString>> autoTranslateChannelsSetting = {
        "/moltorino/translation/autoTranslateChannels"};
    ChatterinoSetting<std::vector<QString>>
        outgoingTranslationChannelSettingsSetting = {
            "/moltorino/translation/outgoingChannelSettings"};
    ChatterinoSetting<std::vector<FilterRecordPtr>> filterRecordsSetting = {
        "/filtering/filters"};
    ChatterinoSetting<std::vector<Nickname>> nicknamesSetting = {"/nicknames"};
    ChatterinoSetting<std::vector<ModerationAction>> moderationActionsSetting =
        {"/moderation/actions"};
    ChatterinoSetting<std::vector<ChannelLog>> loggedChannelsSetting = {
        "/logging/channels"};
    SignalVector<QString> mutedChannels;
    SignalVector<QString> autoTranslateChannels;

    IntSetting settingsVersion = {
        "/misc/settingsVersion",
        0,
    };

    /// Migrate settings between versions
    void migrate(bool isTest);
    void migrateChannelModes(bool isTest);

    /// Migration 0 -> 1
    ///
    /// This migration takes all of our custom highlight values & turns them into real highlight rows
    /// It also merges messages, user, and badge highlights into a single vector.
    void migrateHighlights(bool isTest);

    /// Run cleanup on existing settings. This could mean removing duplicate values that are not valid.
    void cleanup();

    /// Delete any duplicate or invalid highlights.
    ///
    /// Returns true if any changes were made.
    bool cleanupHighlights();

    /// Return a set of built-in highlight IDs (e.g. {"whispers", "yourusername"}) that are missing from the user's list of highlights.
    /// This can happen if a user does some manual cursed surgery on their settings.json file.
    std::unordered_set<QStringView> missingBillTinHighlights();

    /// Recreates any missing built-in highlights.
    ///
    /// Returns true if any changes were made.
    bool recreateMissingHighlights();

public:
    SignalVector<highlights::AnyHighlight> sharedHighlights;
    SignalVector<HighlightBlacklistUser> blacklistedUsers;
    SignalVector<IgnorePhrase> ignoredMessages;
    SignalVector<HiddenUser> hiddenUsers;
    SignalVector<FilterRecordPtr> filterRecords;
    SignalVector<Nickname> nicknames;
    SignalVector<ModerationAction> moderationActions;
    SignalVector<ChannelLog> loggedChannels;

    bool isBlacklistedUser(const QString &username);
    bool isMutedChannel(const QString &channelName);
    bool toggleMutedChannel(const QString &channelName);
    bool isAutoTranslateChannel(const QString &channelName);
    bool toggleAutoTranslateChannel(const QString &channelName);
    QString outgoingTranslationModeForChannel(const QString &channelName);
    QString outgoingTranslationTargetLanguageForChannel(
        const QString &channelName);
    void setOutgoingTranslationModeForChannel(const QString &channelName,
                                              const QString &mode);
    void setOutgoingTranslationTargetLanguageForChannel(
        const QString &channelName, const QString &targetLanguage);
    std::optional<QString> matchNickname(const QString &username);
    void mute(const QString &channelName);
    void unmute(const QString &channelName);
    void enableAutoTranslateChannel(const QString &channelName);
    void disableAutoTranslateChannel(const QString &channelName);

    BoolSetting allowPopupResize = {"/moltorino/appearance/allowPopupResize",
                                    false};
    BoolSetting continuousSplitBackground = {
        "/moltorino/appearance/continuousSplitBackground", false};

    ChatterinoSetting<QStringList> disabledEmoteModifiers = {
        "/emotes/disabledModifiers", {}};
    bool isEmoteModifierEnabled(const QString &name) const;

private:
    void updateModerationActions();

    std::unique_ptr<rapidjson::Document> snapshot_;
    std::unordered_set<std::string> snapshotUnsetPaths_;

    pajlada::Signals::SignalHolder signalHolder;

    std::unique_ptr<SettingsPrivate> p;

#ifdef FRIEND_TEST
    friend class HighlightControllerTest;

    FRIEND_TEST(HighlightControllerTest, BillTinHighlights);
#endif
};

Settings *getSettings();

}  // namespace chatterino

template <>
constexpr magic_enum::customize::customize_t
    magic_enum::customize::enum_name<chatterino::StreamLinkPreferredQuality>(
        chatterino::StreamLinkPreferredQuality value) noexcept
{
    using chatterino::StreamLinkPreferredQuality;
    switch (value)
    {
        case chatterino::StreamLinkPreferredQuality::Choose:
        case chatterino::StreamLinkPreferredQuality::Source:
        case chatterino::StreamLinkPreferredQuality::High:
        case chatterino::StreamLinkPreferredQuality::Medium:
        case chatterino::StreamLinkPreferredQuality::Low:
            return default_tag;

        case chatterino::StreamLinkPreferredQuality::AudioOnly:
            return "Audio only";

        default:
            return default_tag;
    }
}
