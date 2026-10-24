// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/Sounds.hpp"
#include "controllers/highlights/types/Common.hpp"
#include "Test.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

namespace chatterino::highlights {

TEST(HighlightSounds, Resolve)
{
    auto oPing2 = resolveDefaultSound(u"001-ping2");
    if (!oPing2)
    {
        FAIL() << "The built-in ping sound was not found";
        return;
    }
    const auto &ping2 = *oPing2;
    ASSERT_EQ(ping2.id, "001-ping2");
    ASSERT_EQ(ping2.displayName, "Chatterino default");
    ASSERT_EQ(ping2.resourcePath, QUrl{"qrc:/sounds/ping2.wav"});
}

}  // namespace chatterino::highlights
