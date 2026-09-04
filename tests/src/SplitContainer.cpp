// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SplitContainer.hpp"

#include "common/WindowDescriptors.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"

#include <QCoreApplication>
#include <QEvent>

using namespace chatterino;

namespace {

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

    HotkeyController hotkeys;
    WindowManager windowManager;
    AccountController accounts;
    CommandController commands;
    mock::EmoteController emotes;
};

}  // namespace

TEST(SplitContainer, EmptyDescriptorCanAddAndRemoveSplit)
{
    MockApplication app;
    SplitContainer container(nullptr);

    container.applyFromDescriptor(ContainerNodeDescriptor{});

    EXPECT_EQ(container.getBaseNode()->getType(),
              SplitContainer::Node::Type::EmptyRoot);

    auto *split = container.appendNewSplit(false);
    container.deleteSplit(split);

    EXPECT_EQ(container.getBaseNode()->getType(),
              SplitContainer::Node::Type::EmptyRoot);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
