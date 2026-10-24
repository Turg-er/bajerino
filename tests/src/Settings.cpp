// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/types/AnyHighlight.hpp"
#include "mocks/BaseApplication.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "Test.hpp"
#include "util/SettingsTransfer.hpp"

#include <pajlada/settings/settingmanager.hpp>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>

namespace chatterino {
namespace {

struct TwitchChannelModeMigrationCase {
    bool bajerinoAnonymous;
    QString readMode;
    TwitchChannelMode expectedMode;
    bool expectedParallel;
};

class TwitchChannelModeMigrationTest
    : public ::testing::TestWithParam<TwitchChannelModeMigrationCase>
{
};

std::string migrationCaseName(
    const ::testing::TestParamInfo<TwitchChannelModeMigrationCase> &info)
{
    QString name = info.param.bajerinoAnonymous ? "BajerinoOn" : "BajerinoOff";
    if (info.param.readMode.compare("anonymousparallel", Qt::CaseInsensitive) ==
        0)
    {
        name += "AnonymousParallel";
    }
    else if (info.param.readMode.compare("anonymous", Qt::CaseInsensitive) == 0)
    {
        name += "Anonymous";
    }
    else
    {
        name += "Authenticated";
    }
    return name.toStdString();
}

TEST_P(TwitchChannelModeMigrationTest, MigratesLegacySettings)
{
    const auto &param = GetParam();
    const QJsonObject root{
        {"bajerino",
         QJsonObject{{"joinIrcAsAnonymous", param.bajerinoAnonymous}}},
        {"misc", QJsonObject{{"x-7tv", QJsonObject{{"twitchReadConnectionMode",
                                                    param.readMode}}}}},
    };

    mock::BaseApplication app(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
        /*runMigrations=*/true);

    EXPECT_EQ(app.settings.twitchDefaultChannelMode.getEnum(),
              param.expectedMode);
    EXPECT_EQ(app.settings.twitchAnonymousReadParallel.getValue(),
              param.expectedParallel);

    const auto manager = pajlada::Settings::SettingManager::getInstance();
    const auto *version = manager->get("/misc/settingsVersion");
    ASSERT_NE(version, nullptr);
    ASSERT_TRUE(version->IsInt());
    EXPECT_EQ(version->GetInt(), 2);
    EXPECT_EQ(manager->get("/bajerino/joinIrcAsAnonymous"), nullptr);
    EXPECT_EQ(manager->get("/misc/x-7tv/twitchReadConnectionMode"), nullptr);
}

INSTANTIATE_TEST_SUITE_P(
    LegacyCombinations, TwitchChannelModeMigrationTest,
    ::testing::Values(
        TwitchChannelModeMigrationCase{false, "authenticated",
                                       TwitchChannelMode::Authenticated, false},
        TwitchChannelModeMigrationCase{false, "anonymous",
                                       TwitchChannelMode::AnonymousRead, false},
        TwitchChannelModeMigrationCase{false, "anonymousparallel",
                                       TwitchChannelMode::AnonymousRead, true},
        TwitchChannelModeMigrationCase{
            true, "authenticated", TwitchChannelMode::BajerinoAnonymous, false},
        TwitchChannelModeMigrationCase{
            true, "anonymous", TwitchChannelMode::BajerinoAnonymous, false},
        TwitchChannelModeMigrationCase{true, "anonymousparallel",
                                       TwitchChannelMode::BajerinoAnonymous,
                                       true}),
    migrationCaseName);

TEST(Settings, TwitchChannelModeMigrationPreservesNewSettings)
{
    const QJsonObject root{
        {"bajerino",
         QJsonObject{
             {"joinIrcAsAnonymous", true},
             {"twitchDefaultChannelMode", "AnonymousRead"},
             {"twitchAnonymousReadParallel", false},
         }},
        {"misc", QJsonObject{{"x-7tv", QJsonObject{{"twitchReadConnectionMode",
                                                    "anonymousparallel"}}}}},
    };

    mock::BaseApplication app(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
        /*runMigrations=*/true);

    EXPECT_EQ(app.settings.twitchDefaultChannelMode.getEnum(),
              TwitchChannelMode::AnonymousRead);
    EXPECT_FALSE(app.settings.twitchAnonymousReadParallel.getValue());
}

TEST(Settings, TwitchChannelModeMigrationPreservesNewModeOnly)
{
    const QJsonObject root{
        {"bajerino",
         QJsonObject{
             {"joinIrcAsAnonymous", true},
             {"twitchDefaultChannelMode", "AnonymousRead"},
         }},
        {"misc", QJsonObject{{"x-7tv", QJsonObject{{"twitchReadConnectionMode",
                                                    "anonymousparallel"}}}}},
    };

    mock::BaseApplication app(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
        /*runMigrations=*/true);

    EXPECT_EQ(app.settings.twitchDefaultChannelMode.getEnum(),
              TwitchChannelMode::AnonymousRead);
    EXPECT_TRUE(app.settings.twitchAnonymousReadParallel.getValue());
}

TEST(Settings, TwitchChannelModeMigrationPreservesNewParallelOnly)
{
    const QJsonObject root{
        {"bajerino",
         QJsonObject{
             {"joinIrcAsAnonymous", true},
             {"twitchAnonymousReadParallel", false},
         }},
        {"misc", QJsonObject{{"x-7tv", QJsonObject{{"twitchReadConnectionMode",
                                                    "anonymousparallel"}}}}},
    };

    mock::BaseApplication app(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
        /*runMigrations=*/true);

    EXPECT_EQ(app.settings.twitchDefaultChannelMode.getEnum(),
              TwitchChannelMode::BajerinoAnonymous);
    EXPECT_FALSE(app.settings.twitchAnonymousReadParallel.getValue());
}

TEST(Settings, MigratesHighlightsFromExistingBajerinoVersionOne)
{
    mock::BaseApplication app(R"({
        "misc": {"settingsVersion": 1},
        "bajerino": {"twitchDefaultChannelMode": "anonymousread"},
        "highlighting": {"highlights": [{
            "pattern": "saved phrase", "regex": false, "case": false,
            "alert": true, "sound": false, "showInMentions": true
        }]}
    })",
                              true);
    EXPECT_EQ(app.settings.twitchDefaultChannelMode.getEnum(),
              TwitchChannelMode::AnonymousRead);
    const auto highlights = app.settings.sharedHighlights.readOnly();
    const auto migrated =
        std::ranges::find_if(*highlights, [](const auto &highlight) {
            const auto *message =
                std::get_if<highlights::MessageHighlight>(&highlight);
            return message && message->pattern == "saved phrase";
        });
    ASSERT_NE(migrated, highlights->end());
    EXPECT_TRUE(highlights::isEnabled(*migrated));
}

TEST(Settings, SharedSettingsExcludeBajerinoEncryptionKey)
{
    mock::BaseApplication app;
    app.settings.encryptionKey = "private-test-key";
    app.settings.useLockIconForToggle = false;

    using namespace settingsbackup;
    EXPECT_FALSE(isSafeSettingPath("/encryption/encryptionKey"));
    EXPECT_FALSE(isSafeSettingPath("/encryption"));
    EXPECT_TRUE(isSafeSettingPath("/encryption/useLockIconForToggle"));

    QSet<QString> selected;
    for (const auto &category : categories())
    {
        selected.insert(category.id);
    }
    const auto result =
        createBundle(app.getPaths().settingsDirectory, selected);
    ASSERT_TRUE(result) << result.error();
    const auto encoded = QJsonDocument(result.value()).toJson();
    EXPECT_FALSE(encoded.contains("private-test-key"));
    EXPECT_FALSE(encoded.contains("encryptionKey"));
    EXPECT_TRUE(encoded.contains("/encryption/useLockIconForToggle"));

    auto unsafeBundle = result.value();
    auto settings = unsafeBundle["settings"].toObject();
    auto other = settings["other"].toObject();
    other.insert("/encryption/encryptionKey", "injected-key");
    settings["other"] = other;
    unsafeBundle["settings"] = settings;
    EXPECT_FALSE(parseBundle(QJsonDocument(unsafeBundle).toJson()));
    EXPECT_EQ(app.settings.encryptionKey.getValue(), "private-test-key");
}

}  // namespace
}  // namespace chatterino
