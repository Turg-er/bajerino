// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SplitInput.hpp"

#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/completion/TabCompletionModel.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/plugins/PluginController.hpp"
#include "controllers/recording/ChatRecordingController.hpp"
#include "messages/Emote.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Channel.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/Helix.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPersonalEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"
#include "TwitchChannelTestAccess.hpp"
#include "util/Crypto.hpp"
#include "util/MultiChannel.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/dialogs/PollDialog.hpp"
#include "widgets/helper/NotebookTab.hpp"
#include "widgets/helper/ResizingTextEdit.hpp"
#include "widgets/listview/GenericListView.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/InputCompletionItem.hpp"
#include "widgets/splits/InputCompletionPopup.hpp"
#include "widgets/splits/PinnedMessageWidget.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/splits/SplitHeader.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCompleter>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QString>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <tuple>

using namespace chatterino;
using namespace chatterino::literals;
using ::testing::Exactly;

namespace {

class HeaderTestTwitchServer : public mock::MockTwitchIrcServer
{
public:
    ChannelPtr getOrAddChannel(const QString &name) override
    {
        return this->getChannelOrEmpty(name);
    }
};

class TestableSplitInput : public SplitInput
{
public:
    using SplitInput::SplitInput;
    using SplitInput::ui_;
    using SplitInput::updateActionRowCompactness;
};

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : windowManager(this->args_, this->paths_, this->settings, this->theme,
                        this->fonts)
        , commands(this->paths_)
    {
    }

    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }

    WindowManager *getWindows() override
    {
        return &this->windowManager;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    CommandController *getCommands() override
    {
        return &this->commands;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    HeaderTestTwitchServer twitch;

    BttvEmotes *getBttvEmotes() override
    {
        return &this->bttv;
    }

    FfzEmotes *getFfzEmotes() override
    {
        return &this->ffz;
    }

    SeventvEmotes *getSeventvEmotes() override
    {
        return &this->seventv;
    }

    SeventvPersonalEmotes *getSeventvPersonalEmotes() override
    {
        return &this->personalEmotes;
    }

    ChatRecordingController *getChatRecordings() override
    {
        return this->recordings.get();
    }

    std::unique_ptr<ChatRecordingController> recordings;

    SeventvPersonalEmotes personalEmotes;
    BttvEmotes bttv;
    FfzEmotes ffz;
    SeventvEmotes seventv;

#ifdef CHATTERINO_HAVE_PLUGINS
    PluginController *getPlugins() override
    {
        return &this->plugins;
    }

    PluginController plugins{this->paths_};
#endif

    HotkeyController hotkeys;
    WindowManager windowManager;
    AccountController accounts;
    CommandController commands;
    mock::EmoteController emotes;
};

class TestSplitInput : public SplitInput
{
public:
    using SplitInput::insertCompletionText;
    using SplitInput::SplitInput;

    void undoInput()
    {
        this->ui_.textEdit->undo();
    }
};

class SplitInputCompletionTest : public ::testing::Test
{
public:
    MockApplication mockApplication;
    Split split{nullptr};
    TestSplitInput input{&this->split};
};

class SplitInputTest
    : public ::testing::TestWithParam<std::tuple<QString, QString>>
{
public:
    SplitInputTest()
        : split(new Split(nullptr))
        , input(this->split)
    {
    }

    MockApplication mockApplication;
    Split *split;
    SplitInput input;
};

}  // namespace

TEST(PinnedMessageWidget, PreservesEncryptedPinsAndManualVisibility)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    app.settings.encryptionKey = u"pin-regression-key"_s;
    app.settings.alwaysShowPinnedMessage = true;
    // Exercise expansion with a top-level widget as well as a split child.
    app.settings.alwaysExpandPinnedMessages = true;
    auto channel = std::make_shared<TwitchChannel>(u"pajlada"_s);
    PinnedMessageWidget banner;
    banner.resize(400, 180);
    banner.setChannel(channel.get());
    EXPECT_TRUE(banner.isHidden());

    TwitchChannel::PinnedMessage pin;
    pin.messageID = u"pin-1"_s;
    pin.messageText =
        encryptMessage(u"private pinned message", u"pin-regression-key");
    pin.startsAt = QDateTime::currentDateTimeUtc();
    pin.sender.login = u"sender"_s;
    pin.pinnedBy.login = u"moderator"_s;
    channel->setPinnedMessage(pin);
    EXPECT_TRUE(banner.hasPinnedMessage());
    EXPECT_FALSE(banner.isHidden());
    const auto labels = banner.findChildren<QLabel *>();
    EXPECT_TRUE(
        std::any_of(labels.begin(), labels.end(), [](const auto *label) {
            return label->text() == u"private pinned message";
        }));
    EXPECT_TRUE(
        std::any_of(labels.begin(), labels.end(), [](const auto *label) {
            return label->toolTip() == u"Encrypted" && !label->isHidden();
        }));

    banner.toggleUserPinned();
    banner.refreshLayout();
    EXPECT_TRUE(banner.isHidden());
    EXPECT_FALSE(banner.hasPinnedMessage());

    pin.messageID = u"pin-2"_s;
    channel->setPinnedMessage(pin);
    EXPECT_FALSE(banner.isHidden());
    EXPECT_TRUE(banner.hasPinnedMessage());

    pin.messageID = u"expired-pin"_s;
    pin.endsAt = QDateTime::currentDateTimeUtc().addSecs(-1);
    channel->setPinnedMessage(pin);
    EXPECT_TRUE(banner.isHidden());
    EXPECT_FALSE(banner.hasPinnedMessage());
    EXPECT_EQ(channel->getPinnedMessage(), nullptr);
}

TEST(SplitHeader, FollowButtonTracksSingleChannelAndStaysHiddenForMultiChannel)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    auto account = std::make_shared<TwitchAccount>("viewer", "native-token",
                                                   "client", "42");
    app.accounts.twitch.accounts.append(account);
    app.accounts.twitch.currentUsername = "viewer";
    app.accounts.twitch.load();
    app.settings.moltorinoAuthAccounts = R"([{
        "userId":"42", "login":"viewer", "token":"saved-token", "valid":true
    }])";
    app.settings.showFollowButtonInSplitHeader = true;

    auto channel = std::make_shared<TwitchChannel>(u"pajlada"_s);
    app.twitch.mockChannels[channel->getName()] = channel;
    Split split(nullptr);
    split.setChannel(IndirectChannel{channel});
    auto *header = new SplitHeader(&split);
    const auto buttons = header->findChildren<SvgButton *>();
    const auto follow =
        std::find_if(buttons.begin(), buttons.end(), [](auto *button) {
            return button->toolTip() == u"Follow pajlada";
        });
    ASSERT_NE(follow, buttons.end());
    EXPECT_FALSE((*follow)->isHidden());
    channel->setFollowingStatus(true);
    EXPECT_EQ((*follow)->toolTip(), u"Unfollow pajlada"_s);

    const std::array<MultiChannel::Spec, 1> specs{{
        {.platform = MultiChannel::Platform::Twitch, .name = u"pajlada"_s},
    }};
    auto multi = std::make_shared<MultiChannel>(
        specs, MultiChannelIndicatorMode::PlatformBadgeIfUnselected);
    split.setChannel(IndirectChannel{multi});
    EXPECT_TRUE((*follow)->isHidden());
    channel->setFollowingStatus(false);
    EXPECT_TRUE((*follow)->isHidden());

    split.setChannel(IndirectChannel{channel});
    EXPECT_FALSE((*follow)->isHidden());
    EXPECT_EQ((*follow)->toolTip(), u"Follow pajlada"_s);
}

TEST(PollDialog, CreatesPollUsingNativeAccountAndHelix)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    auto account = std::make_shared<TwitchAccount>(
        "broadcaster", "native-token", "client", "42");
    app.accounts.twitch.accounts.append(account);
    app.accounts.twitch.currentUsername = "broadcaster";
    app.accounts.twitch.load();

    auto channel = std::make_shared<TwitchChannel>(u"broadcaster"_s);
    TwitchChannelTestAccess::setRoomId(*channel, u"42"_s);
    PollDialog dialog(channel.get());
    auto *title = dialog.findChild<QLineEdit *>(u"PollTitleInput"_s);
    const auto choices = dialog.findChildren<QLineEdit *>(u"PollOptionInput"_s);
    auto *start = dialog.findChild<QPushButton *>(u"PollCreateStartButton"_s);
    ASSERT_NE(title, nullptr);
    ASSERT_GE(choices.size(), 2);
    ASSERT_NE(start, nullptr);
    title->setText(u"Use the native account?"_s);
    choices[0]->setText(u"Yes"_s);
    choices[1]->setText(u"No"_s);

    EXPECT_CALL(
        helix, createPoll(QString("42"), QString("Use the native account?"),
                          (QStringList{"Yes", "No"}), std::chrono::seconds(120),
                          0, testing::_, testing::_))
        .Times(1);
    start->click();
}
namespace chatterino {

class PopupCycleFixture : public ::testing::Test
{
public:
    PopupCycleFixture()
        : input(this->split.getInput())
    {
        this->app.settings.mentionUsersWithComma.setValue(true);
    }

    void setChannel(ChannelPtr channel)
    {
        this->split.setChannel(IndirectChannel(std::move(channel)));
        this->split.show();
    }

    void setChatters(std::initializer_list<QString> names)
    {
        auto channel = std::make_shared<TwitchChannel>("forsen");
        for (const auto &name : names)
        {
            channel->addRecentChatter(name);
        }
        this->setChannel(channel);
    }

    void setEmotes(std::initializer_list<QString> names, bool zeroWidth = false)
    {
        auto emotes = std::make_shared<EmoteMap>();
        for (const auto &name : names)
        {
            auto emote = std::make_shared<Emote>();
            emote->name = EmoteName{name};
            emote->zeroWidth = zeroWidth;
            emotes->emplace(emote->name, std::move(emote));
        }
        auto channel = std::make_shared<TwitchChannel>("forsen");
        channel->setBttvEmotes(std::move(emotes));
        this->setChannel(channel);
    }

    ResizingTextEdit *edit() const
    {
        return this->input.ui_.textEdit;
    }

    InputCompletionPopup *popup()
    {
        return this->input.inputCompletionPopup_;
    }

    GenericListView *list()
    {
        auto *result = this->popup();
        return result ? result->ui_.listView : nullptr;
    }

    void pressTab() const
    {
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, "\t");
        QApplication::sendEvent(this->edit(), &tab);
    }

    void pressBacktab() const
    {
        QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(this->edit(), &backtab);
    }

    MockApplication app;
    Split split{nullptr};
    SplitInput &input;
};

}  // namespace chatterino

TEST_F(SplitInputCompletionTest, EmoteCompletionPreservesUndoHistory)
{
    this->input.insertText("don't ping him :paja");
    ASSERT_EQ("don't ping him :paja", this->input.getInputText());

    this->input.insertCompletionText("pajaGIGA");
    ASSERT_EQ("don't ping him pajaGIGA ", this->input.getInputText());

    this->input.undoInput();
    EXPECT_EQ("don't ping him :paja", this->input.getInputText());
    this->input.undoInput();
    EXPECT_TRUE(this->input.getInputText().isEmpty());
}

TEST_F(SplitInputCompletionTest, UsernameCompletionPreservesUndoHistory)
{
    this->input.insertText("boring game @fors");
    ASSERT_EQ("boring game @fors", this->input.getInputText());

    this->input.insertCompletionText("forsen");
    ASSERT_EQ("boring game @forsen ", this->input.getInputText());

    this->input.undoInput();
    EXPECT_EQ("boring game @fors", this->input.getInputText());
    this->input.undoInput();
    EXPECT_TRUE(this->input.getInputText().isEmpty());
}

TEST_F(PopupCycleFixture, UsernameSelectionCyclesWithTab)
{
    this->setChatters({"zzpopupfirst", "zzpopupsecond"});
    ASSERT_NE(this->edit(), nullptr);

    this->input.insertText("@zzpopup");
    auto *list = this->list();
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(this->popup()->isVisible());
    ASSERT_EQ(list->model()->rowCount(), 2);
    list->setCurrentIndex(list->model()->index(1, 0));
    const auto *selected = dynamic_cast<const InputCompletionItem *>(
        GenericListItem::fromVariant(list->currentIndex().data()));
    ASSERT_NE(selected, nullptr);
    const auto selectedText = "@" + selected->insertionText() + ", ";

    // The first tab accepts the selected username
    this->pressTab();
    const auto first = this->input.getInputText();
    EXPECT_EQ(first, selectedText);

    // Further tabs cycle through the popup usernames
    this->pressTab();
    const auto second = this->input.getInputText();
    EXPECT_TRUE(second == "@zzpopupfirst, " || second == "@zzpopupsecond, ");
    EXPECT_NE(second, first);
    this->pressTab();
    EXPECT_EQ(this->input.getInputText(), first);
    this->pressBacktab();
    EXPECT_EQ(this->input.getInputText(), second);
}

TEST_F(PopupCycleFixture, EnterAcceptanceCyclesWithTab)
{
    this->setChatters({"zzpopupfirst", "zzpopupsecond"});
    this->input.insertText("@zzpopup");
    auto *list = this->list();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->model()->rowCount(), 2);
    list->setCurrentIndex(list->model()->index(1, 0));

    // Enter accepts the popup item and tab continues through its list
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r");
    QApplication::sendEvent(this->edit(), &enter);
    const auto first = this->input.getInputText();
    EXPECT_TRUE(first == "@zzpopupfirst, " || first == "@zzpopupsecond, ");
    this->pressTab();
    EXPECT_NE(this->input.getInputText(), first);
}

TEST_F(PopupCycleFixture, OneCharacterSelectionCyclesAfterClick)
{
    this->setEmotes({"⚽", "⚽more"}, true);
    this->input.insertText(":~⚽");
    auto *list = this->list();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->model()->rowCount(), 2);
    const auto *first = dynamic_cast<const InputCompletionItem *>(
        GenericListItem::fromVariant(list->model()->index(0, 0).data()));
    ASSERT_NE(first, nullptr);
    const auto selection = list->model()->index(
        first->insertionText() == QString::fromUtf8("⚽") ? 0 : 1, 0);
    const auto *selected = dynamic_cast<const InputCompletionItem *>(
        GenericListItem::fromVariant(selection.data()));
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->insertionText(), QString::fromUtf8("⚽"));

    // Clicking a one-character result still allows tab to choose the next
    list->setCurrentIndex(selection);
    Q_EMIT list->clicked(selection);
    EXPECT_EQ(this->input.getInputText(), QString::fromUtf8("⚽ "));
    this->pressTab();
    EXPECT_EQ(this->input.getInputText(), QString::fromUtf8("⚽more "));
}

TEST_F(PopupCycleFixture, CompletionAfterNewlineKeepsPreviousLine)
{
    this->setChatters({"zzpopupfirst", "zzpopupsecond"});
    this->input.insertText("previous\n@zzpopup");
    auto *list = this->list();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->model()->rowCount(), 2);
    list->setCurrentIndex(list->model()->index(0, 0));

    // Cycling a completion on a new line keeps the text before the newline
    this->pressTab();
    const auto first = this->input.getInputText();
    ASSERT_TRUE(first.startsWith("previous\n@"));
    this->pressTab();
    const auto second = this->input.getInputText();
    EXPECT_TRUE(second.startsWith("previous\n@"));
    EXPECT_NE(second, first);
}

TEST_F(PopupCycleFixture, DuplicateEmoteNamesCycleOnce)
{
    this->setEmotes({"zzdupone", "zzduptwo"});
    auto duplicate = std::make_shared<Emote>();
    duplicate->name = EmoteName{"zzdupone"};
    auto globalEmotes = std::make_shared<EmoteMap>();
    globalEmotes->emplace(duplicate->name, duplicate);
    this->app.bttv.setEmotes(std::move(globalEmotes));

    this->input.insertText(":zzdup");
    auto *list = this->list();
    ASSERT_NE(list, nullptr);
    // The same emote name appears in both the channel and global results
    ASSERT_EQ(list->model()->rowCount(), 3);

    int lastDuplicateRow = -1;
    int duplicateCount = 0;
    for (int row = 0; row < list->model()->rowCount(); ++row)
    {
        const auto *item = dynamic_cast<const InputCompletionItem *>(
            GenericListItem::fromVariant(list->model()->index(row, 0).data()));
        ASSERT_NE(item, nullptr);
        if (item->insertionText() == "zzdupone")
        {
            lastDuplicateRow = row;
            ++duplicateCount;
        }
    }
    ASSERT_EQ(duplicateCount, 2);
    ASSERT_GE(lastDuplicateRow, 0);
    list->setCurrentIndex(list->model()->index(lastDuplicateRow, 0));

    // Selecting a duplicate skips the other copy on the next tab
    this->pressTab();
    EXPECT_EQ(this->input.getInputText(), "zzdupone ");
    this->pressTab();
    EXPECT_EQ(this->input.getInputText(), "zzduptwo ");
}

TEST_F(SplitInputCompletionTest, TabCompletionPreservesUndoHistory)
{
    ResizingTextEdit edit;
    QCompleter completer;
    edit.setCompleter(&completer);
    edit.insertPlainText("UR DONE BA");

    Q_EMIT completer.highlighted(QString("BAND "));
    ASSERT_EQ("UR DONE BAND ", edit.toPlainText());

    edit.undo();
    EXPECT_EQ("UR DONE BA", edit.toPlainText());
    edit.undo();
    EXPECT_TRUE(edit.toPlainText().isEmpty());
}

TEST_P(SplitInputTest, Reply)
{
    std::tuple<QString, QString> params = this->GetParam();
    auto [inputText, expected] = params;
    ASSERT_EQ("", this->input.getInputText());
    this->input.setInputText(inputText);
    ASSERT_EQ(inputText, this->input.getInputText());

    auto *message = new Message();
    message->displayName = "forsen";
    auto reply = MessagePtr(message);
    this->input.setReply(reply, {});
    QString actual = this->input.getInputText();
    ASSERT_EQ(expected, actual) << "Input text after setReply should be '"
                                << expected << "', but got '" << actual << "'";
}

TEST(SplitInput, ReplyCommandCompletion)
{
    MockApplication app;
    app.commands.items.append(Command{"/slashreplycompletion", "test"});
    app.commands.items.append(Command{".dotreplycompletion", "test"});
    Split split(nullptr);
    SplitInput input(&split);
    auto *textEdit = input.findChild<QTextEdit *>();
    // NOLINTNEXTLINE(clazy-unneeded-cast)
    auto *edit = dynamic_cast<ResizingTextEdit *>(textEdit);
    ASSERT_NE(edit, nullptr);
    mock::MockChannel channel("forsen");
    edit->setCompleter(new QCompleter(channel.getCompletionModel(), edit));

    // Slash and dot commands can be completed in replies
    for (const auto &trigger :
         {QString("/slashreplycomp"), QString(".dotreplycomp")})
    {
        auto message = std::make_shared<Message>();
        message->displayName = "forsen";
        input.setInputText("");
        input.setReply(message, {});
        input.insertText(trigger);

        // Commands are completed at the first word after the reply prefix `@username `
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(), "@forsen " + trigger + "letion ");

        // Commands are not completed later than the first word after the prefix
        input.setInputText("@forsen test " + trigger);
        edit->moveCursor(QTextCursor::End);
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(), "@forsen test " + trigger);

        // Manually restoring an edited prefix does not restore reply completion
        input.setInputText("@forsenX " + trigger);
        input.setInputText("@forsen " + trigger);
        edit->moveCursor(QTextCursor::End);
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(), "@forsen " + trigger);

        // Normal command completion works after cancelling a reply
        input.setReply(message, {});
        input.setReply(nullptr, {});
        input.setInputText(trigger);
        edit->moveCursor(QTextCursor::End);
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(), trigger + "letion ");
    }
}

TEST(SplitInput, EmptyReplyUsernameCompletion)
{
    MockApplication app;
    app.settings.mentionUsersWithComma = true;
    app.settings.alwaysIncludeBroadcasterInUserCompletions = true;
    app.commands.items.append(Command{"/slashreplycompletion", "test"});
    Split split(nullptr);
    SplitInput input(&split);
    auto *textEdit = input.findChild<QTextEdit *>();
    // NOLINTNEXTLINE(clazy-unneeded-cast)
    auto *edit = dynamic_cast<ResizingTextEdit *>(textEdit);
    ASSERT_NE(edit, nullptr);
    TwitchChannel channel("forsen");
    edit->setCompleter(new QCompleter(channel.getCompletionModel(), edit));

    auto message = std::make_shared<Message>();
    message->displayName = "forsen";
    input.setReply(message, {});

    // Completing the username in an empty reply does not add a comma
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(edit, &tab);
    EXPECT_EQ(input.getInputText(), "@forsen ");

    // Reply completion still works after completing the prefix username
    input.insertText("/slashreplycomp");
    QApplication::sendEvent(edit, &tab);
    EXPECT_EQ(input.getInputText(), "@forsen /slashreplycompletion ");
}

TEST(SplitInput, ReplyBodyUsernameCompletion)
{
    MockApplication app;
    Split split(nullptr);
    SplitInput input(&split);
    auto *textEdit = input.findChild<QTextEdit *>();
    // NOLINTNEXTLINE(clazy-unneeded-cast)
    auto *edit = dynamic_cast<ResizingTextEdit *>(textEdit);
    ASSERT_NE(edit, nullptr);
    TwitchChannel channel("forsen");
    channel.addRecentChatter("pajlada");
    edit->setCompleter(new QCompleter(channel.getCompletionModel(), edit));

    auto message = std::make_shared<Message>();
    message->displayName = "forsen";
    input.setReply(message, {});

    for (bool mentionComma : {true, false})
    {
        app.settings.mentionUsersWithComma = mentionComma;

        // The first username in the reply body respects the comma setting
        input.setInputText("@forsen @paj");
        edit->moveCursor(QTextCursor::End);
        EXPECT_TRUE(input.isEditFirstWord());
        QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(),
                  mentionComma ? "@forsen @pajlada, " : "@forsen @pajlada ");

        // Usernames later in the reply body do not get a comma
        input.setInputText("@forsen test @paj");
        edit->moveCursor(QTextCursor::End);
        EXPECT_FALSE(input.isEditFirstWord());
        QApplication::sendEvent(edit, &tab);
        EXPECT_EQ(input.getInputText(), "@forsen test @pajlada ");
    }
}

TEST(SplitInput, ReplyPrefixFormatting)
{
    MockApplication app;
    Split split(nullptr);
    SplitInput input(&split);
    auto *edit = input.findChild<QTextEdit *>();
    ASSERT_NE(edit, nullptr);

    auto message = std::make_shared<Message>();
    message->displayName = "forsen";
    input.setReply(message, {});
    input.insertText("pretty much everywhere, it's gonna be hot");

    auto prefixFormatting = [&](const QString &prefix) {
        QList<QTextEdit::ExtraSelection> result;
        const auto selections = edit->extraSelections();
        for (const auto &selection : selections)
        {
            if (selection.cursor.selectionStart() == 0 &&
                selection.cursor.selectionEnd() == prefix.size())
            {
                result.append(selection);
            }
        }
        return result;
    };

    // Reply prefix is set when replying
    ASSERT_EQ(prefixFormatting("@forsen ").size(), 1);
    EXPECT_EQ(prefixFormatting("@forsen ").constFirst().cursor.selectedText(),
              "@forsen ");

    // Reply prefix uses the theme's placeholder colour
    EXPECT_EQ(
        prefixFormatting("@forsen ").constFirst().format.foreground().color(),
        app.theme.messages.textColors.chatPlaceholder);

    // The cursor stays in position after the message
    EXPECT_EQ(edit->textCursor().position(), input.getInputText().size());

    // Changing the reply target updates the prefix
    auto other = std::make_shared<Message>();
    other->displayName = "pajlada";
    input.setReply(other, {});
    ASSERT_EQ(prefixFormatting("@pajlada ").size(), 1);
    EXPECT_EQ(prefixFormatting("@pajlada ").constFirst().cursor.selectedText(),
              "@pajlada ");

    // Editing the prefix invalidates it
    input.setInputText("@pajladaX pajaBing");
    EXPECT_TRUE(prefixFormatting("@pajlada ").isEmpty());

    // Restoring the prefix manually does not restore it on our end
    input.setInputText("@pajlada pajaBing");
    EXPECT_TRUE(prefixFormatting("@pajlada ").isEmpty());

    // Cancelling the reply invalidates the prefix
    input.setReply(message, {});
    input.setReply(nullptr, {});
    EXPECT_TRUE(prefixFormatting("@forsen ").isEmpty());

    // Clearing the input invalidates the reply prefix
    input.setReply(message, {});
    input.setInputText("");
    EXPECT_TRUE(prefixFormatting("@forsen ").isEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    SplitInput, SplitInputTest,
    testing::Values(
        // Ensure message is retained
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen,",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with comma is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma and space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen, ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure it works with no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "",
            // Expected text after replying to forsen
            "@forsen ")));

TEST(ResizingTextEditTest, HeightForWidthUsesRequestedWidth)
{
    MockApplication mockApplication;

    ResizingTextEdit input;
    input.setPlainText(
        "this is a long enough message to wrap across multiple lines");

    auto wideHeight = static_cast<const QWidget &>(input).heightForWidth(320);
    auto narrowHeight = static_cast<const QWidget &>(input).heightForWidth(120);

    EXPECT_GT(narrowHeight, wideHeight);
}

TEST(SplitInputTest, ChannelPointsChromeStaysCompact)
{
    MockApplication mockApplication;

    auto split = std::make_unique<Split>(nullptr);
    TestableSplitInput input(split.get());

    EXPECT_EQ(input.ui_.channelPointsLabel->font().pointSizeF(),
              input.ui_.textEditLength->font().pointSizeF());
    EXPECT_EQ(input.ui_.channelPointsLabel->toolTip(),
              QString("Channel Points (click to open rewards)"));
    EXPECT_FALSE(input.ui_.channelPointsLabel->isVisible());
}

TEST(SplitInputTest, StatusLabelsReserveTextWidth)
{
    MockApplication mockApplication;

    auto split = std::make_unique<Split>(nullptr);
    TestableSplitInput input(split.get());

    input.ui_.textEditLength->setText("123");
    input.setSendWaitStatus("1:23");
    input.updateActionRowCompactness();

    EXPECT_EQ(input.ui_.buttonsRow->indexOf(input.ui_.textEditLength), -1);
    EXPECT_GT(input.ui_.textEditLength->minimumWidth(), 0);
    EXPECT_GT(input.ui_.sendWaitStatus->minimumWidth(), 0);

    input.ui_.textEditLength->clear();
    input.setSendWaitStatus({});
    input.updateActionRowCompactness();

    EXPECT_EQ(input.ui_.textEditLength->minimumWidth(), 0);
    EXPECT_EQ(input.ui_.sendWaitStatus->minimumWidth(), 0);
}

TEST(SplitHeader, EmoteSourcesDialogDefaultsAndIndividualOverrides)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    app.settings.enableBTTVChannelEmotes = false;
    app.settings.enableFFZChannelEmotes = false;
    app.settings.enableSevenTVChannelEmotes = false;
    auto channel = std::make_shared<TwitchChannel>("emote_dialog");
    app.twitch.mockChannels[channel->getName()] = channel;
    Split split(nullptr);
    split.setChannel(IndirectChannel{channel});
    auto *header = new SplitHeader(&split);

    QMouseEvent click(QEvent::MouseButtonPress, QPointF(4, 4), QPointF(4, 4),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(header, &click);
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    ASSERT_NE(menu, nullptr);
    QAction *editSources = nullptr;
    const auto actions = menu->actions();
    for (auto *action : actions)
    {
        if (action->text() == "Emote sources...")
        {
            editSources = action;
        }
    }
    ASSERT_NE(editSources, nullptr);
    editSources->trigger();
    menu->close();
    auto *dialog = header->findChild<QDialog *>();
    ASSERT_NE(dialog, nullptr);
    auto *shared = dialog->findChild<QLineEdit *>("SharedEmoteSourceID");
    auto *individual = dialog->findChild<QCheckBox *>("IndividualEmoteSources");
    auto *ffz = dialog->findChild<QLineEdit *>("EmoteSourceIDFFZ");
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(individual, nullptr);
    ASSERT_NE(ffz, nullptr);
    EXPECT_TRUE(shared->text().isEmpty());
    EXPECT_FALSE(individual->isChecked());
    EXPECT_TRUE(shared->placeholderText().isEmpty());
    EXPECT_FALSE(ffz->isVisibleTo(dialog));

    TwitchChannelTestAccess::setRoomId(*channel, "42");
    EXPECT_EQ(shared->placeholderText(), "42");
    shared->setText("99");
    individual->setChecked(true);
    EXPECT_FALSE(shared->isEnabled());
    EXPECT_TRUE(ffz->isVisibleTo(dialog));
    EXPECT_EQ(ffz->text(), "99");
    ffz->clear();
    EXPECT_EQ(ffz->placeholderText(), "42");
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Save)->click();
    EXPECT_EQ(channel->emoteSourceID(ChannelEmoteProvider::SevenTV), "99");
    EXPECT_EQ(channel->emoteSourceID(ChannelEmoteProvider::Bttv), "99");
    EXPECT_EQ(channel->emoteSourceID(ChannelEmoteProvider::Ffz), "42");
    EXPECT_TRUE(
        channel->emoteSourceOverride(ChannelEmoteProvider::Ffz).isEmpty());
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    TwitchChannelTestAccess::setRoomId(*channel, "43");
    EXPECT_EQ(channel->emoteSourceID(ChannelEmoteProvider::Ffz), "43");
}

TEST(NotebookTab, EmoteSourcesFollowClickedTabAndChannelChanges)
{
    MockApplication app;
    testing::NiceMock<mock::Helix> helix;
    initializeHelix(&helix);
    app.recordings = std::make_unique<ChatRecordingController>(app.paths_);
    app.settings.enableBTTVChannelEmotes = false;
    app.settings.enableFFZChannelEmotes = false;
    app.settings.enableSevenTVChannelEmotes = false;
    const auto first = std::make_shared<TwitchChannel>("emote_tab_first");
    const auto second = std::make_shared<TwitchChannel>("emote_tab_second");
    for (const auto &channel : {first, second})
    {
        app.twitch.mockChannels[channel->getName()] = channel;
    }
    TwitchChannelTestAccess::setRoomId(*first, "42");
    TwitchChannelTestAccess::setRoomId(*second, "43");

    Notebook notebook(nullptr);
    notebook.setAllowUserTabManagement(true);
    auto *page = new SplitContainer(&notebook);
    auto *tab = notebook.addPage(page);
    page->setTab(tab);
    auto *split = new Split(page);
    split->setChannel(IndirectChannel{first});
    page->insertSplit(split);
    auto *otherPage = new SplitContainer(&notebook);
    otherPage->setTab(notebook.addPage(otherPage, "Other", true));
    auto *otherSplit = new Split(otherPage);
    otherSplit->setChannel(IndirectChannel{second});
    otherPage->insertSplit(otherSplit);

    const auto openMenu = [tab] {
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(4, 4),
                          QPointF(4, 4), Qt::RightButton, Qt::RightButton,
                          Qt::NoModifier);
        QApplication::sendEvent(tab, &click);
        return qobject_cast<QMenu *>(QApplication::activePopupWidget());
    };
    const auto findAction = [](QMenu *menu, const QString &text) -> QAction * {
        const auto actions = menu->actions();
        for (auto *action : actions)
        {
            if (action->isVisible() && action->text() == text)
            {
                return action;
            }
        }
        return nullptr;
    };

    auto *menu = openMenu();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(menu->actions().constFirst()->text(), "Close Tab");
    auto *record = findAction(menu, "Record tab");
    ASSERT_NE(record, nullptr);
    EXPECT_GT(menu->actions().indexOf(record),
              menu->actions().indexOf(findAction(menu, "Tab Color")));
    auto *edit = findAction(menu, "Emote sources...");
    ASSERT_NE(edit, nullptr);
    edit->trigger();
    menu->close();
    auto *dialog = tab->findChild<QDialog *>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->windowTitle(), "Emote sources for #emote_tab_first");
    auto *shared = dialog->findChild<QLineEdit *>("SharedEmoteSourceID");
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(shared->placeholderText(), "42");
    shared->setText("99");
    dialog->reject();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(first->emoteSources().sharedTwitchID.isEmpty());

    const std::array<MultiChannel::Spec, 2> specs{{
        {.platform = MultiChannel::Platform::Twitch, .name = first->getName()},
        {.platform = MultiChannel::Platform::Twitch, .name = second->getName()},
    }};
    auto multi = std::make_shared<MultiChannel>(
        specs, MultiChannelIndicatorMode::PlatformBadgeIfUnselected);
    split->setChannel(IndirectChannel{multi});
    auto *duplicate = new Split(page);
    duplicate->setChannel(IndirectChannel{second});
    page->insertSplit(duplicate);

    menu = openMenu();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(findAction(menu, "Emote sources..."), nullptr);
    auto *choose = findAction(menu, "Emote sources");
    ASSERT_NE(choose, nullptr);
    ASSERT_NE(choose->menu(), nullptr);
    EXPECT_EQ(choose->menu()->actions().size(), 2);
    edit = findAction(choose->menu(), "#emote_tab_second");
    ASSERT_NE(edit, nullptr);
    edit->trigger();
    menu->close();
    dialog = tab->findChild<QDialog *>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->windowTitle(), "Emote sources for #emote_tab_second");
    shared = dialog->findChild<QLineEdit *>("SharedEmoteSourceID");
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(shared->placeholderText(), "43");
    shared->setText("99");
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Save)->click();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(second->emoteSources().sharedTwitchID, "99");
    EXPECT_TRUE(first->emoteSources().sharedTwitchID.isEmpty());

    split->setChannel(IndirectChannel{Channel::getEmpty()});
    duplicate->setChannel(IndirectChannel{Channel::getEmpty()});
    menu = openMenu();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(findAction(menu, "Emote sources..."), nullptr);
    EXPECT_EQ(findAction(menu, "Emote sources"), nullptr);
    menu->close();
}
