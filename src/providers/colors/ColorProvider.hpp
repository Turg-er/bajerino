// SPDX-FileCopyrightText: 2020 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>

#include <vector>

namespace chatterino {

class ColorProvider
{
public:
    static const ColorProvider &instance();

    /**
     * @brief Return a set of recently used colors used anywhere in Chatterino.
     */
    QSet<QColor> recentColors() const;

    /**
     * @brief Return a vector of colors that are good defaults for use
     *        throughout the program.
     */
    const std::vector<QColor> &defaultColors() const;

private:
    ColorProvider();

    void initDefaultColors();

    std::vector<QColor> defaultColors_;
};
}  // namespace chatterino

inline size_t qHash(const QColor &key, size_t seed = 0)
{
    return qHash(key.name(QColor::HexArgb), seed);
}
