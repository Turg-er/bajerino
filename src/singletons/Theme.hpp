// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "singletons/Paths.hpp"
#include "singletons/ThemeCustomization.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/settings/setting.hpp>
#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

class WindowManager;

struct ThemeDescriptor {
    QString key;

    // Path to the theme on disk
    // Can be a Qt resource path
    QString path;

    // Name of the theme
    QString name;

    bool custom{};
    bool operator==(const ThemeDescriptor &) const = default;
};

class Theme final
{
public:
    static const std::vector<ThemeDescriptor> builtInThemes;

    // The built in theme that will be used if some theme parsing fails
    static const ThemeDescriptor fallbackTheme;

    static const int AUTO_RELOAD_INTERVAL_MS = 500;

    Theme(const Paths &paths);

    bool isLightTheme() const;
    bool isSystemTheme() const;

    struct TabColors {
        QColor text;
        struct {
            QColor regular;
            QColor hover;
            QColor unfocused;
        } backgrounds;
        struct {
            QColor regular;
            QColor hover;
            QColor unfocused;
        } line;
    };

    struct TextColors {
        QColor regular;
        QColor caret;
        QColor link;
        QColor system;
        QColor timestamp;
        QColor chatPlaceholder;
    };

    struct MessageBackgrounds {
        QColor regular;
        QColor alternate;
    };

    QColor accent{"#00aeef"};

    /// WINDOW
    struct {
        QColor background;
        QColor text;
    } window;

    /// TABS
    struct {
        TabColors regular;
        TabColors newMessage;
        TabColors highlighted;
        TabColors selected;
        QColor dividerLine;

        QColor liveIndicator;
        QColor rerunIndicator;
    } tabs;

    /// MESSAGES
    struct {
        TextColors textColors;
        MessageBackgrounds backgrounds;

        QColor disabled;
        QColor selection;

        QColor highlightAnimationStart;
        QColor highlightAnimationEnd;
    } messages;

    struct {
        TextColors textColors;
        MessageBackgrounds backgrounds;

        QColor disabled;
        QColor selection;
        QColor background;
    } overlayMessages;

    /// SCROLLBAR
    struct {
        QColor background;
        QColor thumb;
        QColor thumbSelected;
    } scrollbars;

    /// SPLITS
    struct {
        QColor messageSeperator;
        QColor background;
        QColor dropPreview;
        QColor dropPreviewBorder;
        QColor dropTargetRect;
        QColor dropTargetRectBorder;
        QColor resizeHandle;
        QColor resizeHandleBackground;

        struct {
            QColor border;
            QColor focusedBorder;
            QColor background;
            QColor focusedBackground;
            QColor text;
            QColor focusedText;
        } header;

        struct {
            QColor background;
            QColor backgroundPulse;
            QColor searchHighlightBackground;
            QColor searchFailText;
            QColor text;
            // Used by the ChatterListWidget & QuickSwitcherPopup
            QString styleSheet;
        } input;
    } splits;

    struct {
        QPixmap copy;
    } buttons;

    QPalette palette;
    ThemeCustomizationProfile customization;

    void normalizeColor(QColor &color) const;
    void update();

    bool isAutoReloading() const;
    void setAutoReload(bool autoReload);

    /**
     * Return a list of available themes
     **/
    std::vector<std::pair<QString, QVariant>> availableThemes() const;

    const std::vector<ThemeDescriptor> &availableThemeDescriptors() const;
    void reloadAvailableThemes();
    std::optional<QJsonObject> themeJson(const QString &key) const;
    const QString &themesDirectory() const;
    void selectTheme(const QString &key);

    pajlada::Signals::NoArgSignal updated;
    pajlada::Signals::NoArgSignal availableThemesChanged;

    QStringSetting themeName{"/appearance/theme/name", "Dark"};
    QStringSetting lightSystemThemeName{"/appearance/theme/lightSystem",
                                        "Light"};
    QStringSetting darkSystemThemeName{"/appearance/theme/darkSystem", "Dark"};

private:
    BoolSetting defaultApplied{"/appearance/theme/defaultApplied", false};
    BoolSetting bluzyrinoMigrated{"/appearance/theme/bluzyrinoMigrated", false};
    QStringSetting moltorinoSelection{"/appearance/theme/moltorinoSelection",
                                      ""};
    QString themesDirectory_;
    bool isLight_ = false;

    std::vector<ThemeDescriptor> availableThemes_;

    QString currentThemePath_;
    std::unique_ptr<QTimer> themeReloadTimer_;
    // This will only be populated when auto-reloading themes
    QJsonObject currentThemeJson_;

    QObject lifetime_;

    /**
     * Figure out which themes are available in the Themes directory
     *
     * NOTE: This is currently not built to be reloadable
     **/
    void loadAvailableThemes(const Paths &paths);
    void migrateBluzyrinoTheme(const Paths &paths, bool preserveSelection);

    std::optional<ThemeDescriptor> findThemeByKey(const QString &key);

    void parseFrom(const QJsonObject &root, bool isCustomTheme);

    pajlada::Signals::NoArgSignal repaintVisibleChatWidgets_;

    friend class WindowManager;
};

Theme *getTheme();
}  // namespace chatterino
