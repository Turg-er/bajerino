// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "mocks/BaseApplication.hpp"
#include "providers/twitch/TwitchCommon.hpp"
#include "Test.hpp"

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
    EXPECT_EQ(version->GetInt(), 1);
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

}  // namespace
}  // namespace chatterino
