// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/accounts/AccountModel.hpp"

#include "controllers/accounts/Account.hpp"
#include "Test.hpp"
#include "util/SharedPtrElementLess.hpp"

#include <QStringList>

using namespace chatterino;

namespace {

class TestAccount : public Account
{
public:
    TestAccount(ProviderId provider, QString username)
        : Account(provider)
        , username_(std::move(username))
    {
    }

    QString toString() const override
    {
        return this->username_;
    }

private:
    QString username_;
};

class AccountModelTest : public ::testing::Test
{
protected:
    SignalVector<std::shared_ptr<Account>> accounts{
        SharedPtrElementLess<Account>{}};
    AccountModel model{nullptr};

    void SetUp() override
    {
        this->model.initialize(&this->accounts);
    }

    std::shared_ptr<Account> add(ProviderId provider, const QString &username)
    {
        auto account = std::make_shared<TestAccount>(provider, username);
        this->accounts.insert(account);
        return account;
    }

    static QStringList rows(const AccountModel &model)
    {
        QStringList result;
        for (int row = 0; row < model.rowCount({}); ++row)
        {
            result.push_back(
                model.data(model.index(row, 0), Qt::DisplayRole).toString());
        }
        return result;
    }
};

TEST_F(AccountModelTest, AddingKickAfterTwitchKeepsMatchingUsernamesGrouped)
{
    this->add(ProviderId::Twitch, "testuser");
    this->add(ProviderId::Kick, "testuser");

    EXPECT_EQ(rows(this->model),
              (QStringList{"Kick", "testuser", "Twitch", "testuser"}));
    EXPECT_FALSE(this->model.flags(this->model.index(0, 0)) &
                 Qt::ItemIsSelectable);
    EXPECT_FALSE(this->model.flags(this->model.index(2, 0)) &
                 Qt::ItemIsSelectable);
    EXPECT_TRUE(this->model.flags(this->model.index(1, 0)) &
                Qt::ItemIsSelectable);
    EXPECT_TRUE(this->model.flags(this->model.index(3, 0)) &
                Qt::ItemIsSelectable);
}

TEST_F(AccountModelTest, AddingTwitchAfterKickKeepsMatchingUsernamesGrouped)
{
    this->add(ProviderId::Kick, "testuser");
    this->add(ProviderId::Twitch, "testuser");

    EXPECT_EQ(rows(this->model),
              (QStringList{"Kick", "testuser", "Twitch", "testuser"}));
}

TEST_F(AccountModelTest, LoadingExistingAccountsMatchesLiveInsertion)
{
    this->add(ProviderId::Twitch, "testuser");
    this->add(ProviderId::Kick, "testuser");

    AccountModel reopened(nullptr);
    reopened.initialize(&this->accounts);

    EXPECT_EQ(rows(reopened),
              (QStringList{"Kick", "testuser", "Twitch", "testuser"}));
    EXPECT_EQ(rows(this->model), rows(reopened));
}

TEST_F(AccountModelTest, InsertingAtCategoryBoundariesPreservesGroups)
{
    this->add(ProviderId::Kick, "middle");
    this->add(ProviderId::Twitch, "middle");
    this->add(ProviderId::Kick, "zulu");
    this->add(ProviderId::Kick, "alpha");
    this->add(ProviderId::Twitch, "zulu");
    this->add(ProviderId::Twitch, "alpha");

    EXPECT_EQ(rows(this->model),
              (QStringList{"Kick", "alpha", "middle", "zulu", "Twitch", "alpha",
                           "middle", "zulu"}));
}

TEST_F(AccountModelTest, RemovingMatchingUsernameRemovesCorrectProvider)
{
    auto twitch = this->add(ProviderId::Twitch, "testuser");
    auto kick = this->add(ProviderId::Kick, "testuser");
    ASSERT_EQ(rows(this->model),
              (QStringList{"Kick", "testuser", "Twitch", "testuser"}));

    ASSERT_TRUE(this->model.removeRow(3));
    EXPECT_EQ(rows(this->model), (QStringList{"Kick", "testuser"}));
    ASSERT_EQ(this->accounts.raw().size(), 1);
    EXPECT_EQ(this->accounts.raw().front(), kick);

    this->accounts.insert(twitch);
    ASSERT_TRUE(this->model.removeRow(1));
    EXPECT_EQ(rows(this->model), (QStringList{"Twitch", "testuser"}));
    ASSERT_EQ(this->accounts.raw().size(), 1);
    EXPECT_EQ(this->accounts.raw().front(), twitch);

    ASSERT_TRUE(this->model.removeRow(1));
    EXPECT_TRUE(rows(this->model).empty());
    EXPECT_TRUE(this->accounts.empty());

    this->accounts.insert(kick);
    this->accounts.insert(twitch);
    EXPECT_EQ(rows(this->model),
              (QStringList{"Kick", "testuser", "Twitch", "testuser"}));
}

TEST_F(AccountModelTest, RemovingOneAccountKeepsItsCategoryHeader)
{
    this->add(ProviderId::Kick, "alpha");
    this->add(ProviderId::Kick, "zulu");
    this->add(ProviderId::Twitch, "alpha");

    ASSERT_TRUE(this->model.removeRow(1));
    EXPECT_EQ(rows(this->model),
              (QStringList{"Kick", "zulu", "Twitch", "alpha"}));
    ASSERT_TRUE(this->model.removeRow(1));
    EXPECT_EQ(rows(this->model), (QStringList{"Twitch", "alpha"}));
}

}  // namespace
