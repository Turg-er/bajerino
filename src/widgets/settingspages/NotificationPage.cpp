#include "NotificationPage.hpp"

#include "Application.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "singletons/Settings.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/helper/EditableModelView.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QTableView>
#include <QTimer>

namespace chatterino {

NotificationPage::NotificationPage()
    : SettingsPage("Notifications", "")
{
    LayoutCreator<NotificationPage> layoutCreator(this);

    auto layout = layoutCreator.emplace<QVBoxLayout>().withoutMargin();
    {
        auto tabs = layout.emplace<QTabWidget>();
        {
            auto settings = tabs.appendTab(new QVBoxLayout, "Options");
            {
                settings.emplace<QLabel>(
                    "Enable for channel next to channel name");
                settings.append(this->createCheckBox(
                    "Flash taskbar",
                    getApp()->settings->notificationFlashTaskbar));
                settings.append(this->createCheckBox(
                    "Playsound", getApp()->settings->notificationPlaySound));

                settings->addStretch(1);
            }
            auto twitchChannels = tabs.appendTab(new QVBoxLayout, "Twitch");
            {
                int i = 0;
                EditableModelView *view =
                    twitchChannels
                        .emplace<EditableModelView>(
                            getApp()->notifications->createModel(nullptr, i))
                        .getElement();
                view->setTitles({"Twitch channels"});

                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                view->addButtonPressed.connect([] {
                    getApp()->notifications->twitchVector.appendItem("channel");
                });
            }
            auto mixerChannels = tabs.appendTab(new QVBoxLayout, "Mixer");
            {
                int i = 1;
                EditableModelView *view =
                    mixerChannels
                        .emplace<EditableModelView>(
                            getApp()->notifications->createModel(nullptr, i))
                        .getElement();
                view->setTitles({"Mixer channels"});

                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                view->addButtonPressed.connect([] {
                    getApp()->notifications->mixerVector.appendItem("channel");
                });
            }
        }
    }
}
}  // namespace chatterino
