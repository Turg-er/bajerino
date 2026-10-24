// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <memory>
#include <ostream>
#include <vector>

namespace chatterino {

enum class HighlightMatchSource : std::uint8_t {
    Phrase,
    WordList,
    AutoMod,
};

enum class HighlightMatchStyle : std::uint8_t {
    None,
    Outline,
    Fill,
    OutlineAndFill,
    Underline,
};

QString highlightMatchStyleName(HighlightMatchStyle style);
QString highlightMatchAppearanceName(HighlightMatchStyle style, bool hasPaint);
QString highlightMatchAppearanceTooltip(const QColor &color,
                                        HighlightMatchStyle style,
                                        const QString &paintID);
HighlightMatchStyle highlightMatchStyleFromName(const QString &name);
QColor defaultHighlightMatchColor(QColor messageHighlightColor);
QColor defaultNewHighlightMatchColor();
QColor defaultAutoModMatchColor();

struct HighlightMatch {
    qsizetype start{};
    qsizetype length{};
    QColor color;
    QString ruleName;
    QString pattern;
    HighlightMatchSource source{HighlightMatchSource::Phrase};
    HighlightMatchStyle style{HighlightMatchStyle::Outline};
    QString paintID;

    bool operator==(const HighlightMatch &other) const = default;
};

struct HighlightResult {
    /**
     * @brief Construct an empty HighlightResult with all side-effects disabled
     **/
    static HighlightResult emptyResult();

    /**
     * @brief true if highlight should trigger the taskbar to flash
     **/
    bool alert{false};

    /// Sound to play
    /// Empty or invalid = no sound should play
    QUrl sound;

    /**
     * @brief set if highlight should set a background color
     **/
    std::shared_ptr<QColor> color;

    /**
     * @brief true if highlight should show message in the /mentions split
     **/
    bool showInMentions{false};

    bool operator==(const HighlightResult &other) const;
    bool operator!=(const HighlightResult &other) const;

    /**
     * @brief Returns true if no side-effect has been enabled
     **/
    [[nodiscard]] bool empty() const;

    /**
     * @brief Returns true if all side-effects have been enabled
     **/
    [[nodiscard]] bool full() const;

    friend std::ostream &operator<<(std::ostream &os,
                                    const HighlightResult &result);
};

}  // namespace chatterino
