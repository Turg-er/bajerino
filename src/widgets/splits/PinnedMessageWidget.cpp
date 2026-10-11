// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/PinnedMessageWidget.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/ignores/IgnoreController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "util/Crypto.hpp"
#include "util/FormatTime.hpp"
#include "widgets/buttons/DrawnButton.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/helper/ChannelView.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

using namespace std::chrono_literals;
using namespace Qt::Literals;

#include <chrono>
#include <memory>
#include <optional>

namespace chatterino {

namespace {

constexpr auto MUTED_STYLE = "color: #adadb8;";
constexpr qreal ENCRYPTION_ICON_SCALE = 0.25;

}  // namespace

PinnedMessageWidget::PinnedMessageWidget(QWidget *parent)
    : BaseWidget(parent)
    , pinnedByLabel_(new QLabel(this))
    , countdownLabel_(new QLabel(this))
    , menuButton_(new DrawnButton(DrawnButton::Symbol::Kebab, {}, this))
    , messageScrollArea_(new QScrollArea(this))
    , messageLabel_(new QLabel(this))
    , encryptionIcon_(new QLabel(this))
    , footerLabel_(new QLabel(this))
    , progressTimer_(new QTimer(this))
    , autoHideTimer_(new QTimer(this))
{
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *outerBox = new QVBoxLayout(this);
    outerBox->setContentsMargins(0, 0, 0, 0);
    outerBox->setSpacing(0);

    auto *contentBox = new QVBoxLayout();
    contentBox->setContentsMargins(8, 6, 8, 6);
    contentBox->setSpacing(3);

    // Header row: "Pinned by <user>"  [⋮]
    auto *headerRow = new QHBoxLayout();
    headerRow->setSpacing(4);

    headerRow->addWidget(this->pinnedByLabel_);
    headerRow->addStretch(1);
    this->menuButton_->setScaleIndependentSize(28, 28);
    this->menuButton_->setToolTip(u"Mod options"_s);
    this->menuButton_->setMenu(this->buildModMenu());
    this->menuButton_->hide();
    headerRow->addWidget(this->menuButton_);
    this->toggleButton_ = new LabelButton(u"↔"_s, this);
    this->toggleButton_->setToolTip(u"Cycle banners"_s);
    this->toggleButton_->hide();
    headerRow->addWidget(this->toggleButton_);
    QObject::connect(this->toggleButton_, &Button::leftClicked, this, [this] {
        this->toggleBannerRequested.invoke();
    });
    this->closeButton_ = new LabelButton(u"×"_s, this);
    headerRow->addWidget(this->closeButton_);
    QObject::connect(this->closeButton_, &Button::leftClicked, this, [this] {
        if (getSettings()->pinCloseButtonAction == 1 && this->channel_ &&
            this->channel_->canManagePinnedMessages())
        {
            this->channel_->unpinMessage();
        }
        else
        {
            this->userToggled_ = false;
            this->autoHideTimer_->stop();
            this->hide();
            this->dismissed.invoke();
        }
    });

    contentBox->addLayout(headerRow);

    // Message body
    this->messageLabel_->setWordWrap(true);
    this->messageLabel_->setTextFormat(Qt::PlainText);
    this->messageLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    this->messageLabel_->setStyleSheet("background: transparent;");
    this->messageLabel_->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Preferred);
    this->messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    this->messageScrollArea_->setWidgetResizable(true);
    this->messageScrollArea_->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
    this->messageScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->messageScrollArea_->setFrameShape(QFrame::NoFrame);
    this->messageScrollArea_->setFocusPolicy(Qt::NoFocus);
    this->messageScrollArea_->setStyleSheet(
        "QScrollArea { background: transparent; } "
        "QScrollArea > QWidget > QWidget { background: transparent; }");
    this->messageScrollArea_->viewport()->setAutoFillBackground(false);
    this->messageScrollArea_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Fixed);
    this->messageScrollArea_->setWidget(this->messageLabel_);
    contentBox->addWidget(this->messageScrollArea_);
    this->messageView_ =
        new ChannelView(this, ChannelView::Context::UserCard, 1);
    this->messageView_->setTransparentBackground(true);
    this->messageView_->setHighlightsEnabled(false);
    this->messageView_->setOverrideFlags(MessageElementFlags{
        MessageElementFlag::Text, MessageElementFlag::EmoteImage,
        MessageElementFlag::EmojiImage, MessageElementFlag::BitsStatic,
        MessageElementFlag::BitsAmount, MessageElementFlag::TwitchGif});
    this->messageView_->setEnableScrollingToBottom(false);
    this->displayChannel_ =
        std::make_shared<Channel>(QString{}, Channel::Type::None, 1);
    this->messageView_->setChannel(this->displayChannel_);
    this->messageView_->hide();
    contentBox->addWidget(this->messageView_);
    this->settingsConnections_.managedConnect(this->messageView_->layoutChanged,
                                              [this] {
                                                  this->updateMessageHeight();
                                              });
    this->expandButton_ = new LabelButton(u"Expand"_s, this);
    contentBox->addWidget(this->expandButton_, 0, Qt::AlignRight);
    QObject::connect(this->expandButton_, &Button::leftClicked, this, [this] {
        this->expanded_ = !this->expanded_;
        this->updateMessageHeight();
    });

    // Footer: [lock] [sender · time] ... [countdown]
    auto *footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 2, 0, 0);
    footerRow->setSpacing(4);

    this->encryptionIcon_->setToolTip(u"Encrypted"_s);
    this->encryptionIcon_->hide();
    footerRow->addWidget(this->encryptionIcon_, 0, Qt::AlignVCenter);

    this->footerLabel_->setStyleSheet(MUTED_STYLE);
    footerRow->addWidget(this->footerLabel_);
    footerRow->addStretch(1);

    this->countdownLabel_->setStyleSheet(MUTED_STYLE);
    this->countdownLabel_->hide();
    footerRow->addWidget(this->countdownLabel_);
    contentBox->addLayout(footerRow);

    outerBox->addLayout(contentBox);

    // 1px bottom border - separates pin widget from the chat view below
    auto *bottomBorder = new QWidget(this);
    bottomBorder->setFixedHeight(1);
    bottomBorder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bottomBorder->setAutoFillBackground(true);
    {
        QPalette pal = bottomBorder->palette();
        pal.setColor(QPalette::Window, pal.color(QPalette::Mid));
        bottomBorder->setPalette(pal);
    }
    outerBox->addWidget(bottomBorder);

    // Countdown timer (fires every second)
    this->progressTimer_->setInterval(1s);
    QObject::connect(this->progressTimer_, &QTimer::timeout, this, [this] {
        this->tickProgress();
    });

    // auto-hide timer
    this->autoHideTimer_->setSingleShot(true);
    QObject::connect(this->autoHideTimer_, &QTimer::timeout, this, [this] {
        if (!this->userToggled_.value_or(false))
        {
            this->hide();
        }
    });

    auto refreshAppearance = [this](const auto &, auto) {
        this->scaleChangedEvent(this->scale());
        this->refresh();
    };
    getSettings()->pinnedMessageScale.connect(
        refreshAppearance, this->settingsConnections_, false);
    getSettings()->pinnedContentScale.connect(
        refreshAppearance, this->settingsConnections_, false);
    getSettings()->pinTimerDisplay.connect(refreshAppearance,
                                           this->settingsConnections_, false);
    getSettings()->pinTimestampFormat.connect(
        refreshAppearance, this->settingsConnections_, false);
    getSettings()->pinCloseButtonAction.connect(
        refreshAppearance, this->settingsConnections_, false);
    getSettings()->alwaysShowPinnedMessage.connect(
        refreshAppearance, this->settingsConnections_, false);
    getSettings()->alwaysExpandPinnedMessages.connect(
        [this](bool expanded) {
            this->expanded_ = expanded;
            this->updateMessageHeight();
        },
        this->settingsConnections_);
    getSettings()->pinBannerBackgroundColor.connect(
        [this](const auto &) {
            this->update();
        },
        this->settingsConnections_, false);
    this->scaleChangedEvent(this->scale());
    this->hide();
}

void PinnedMessageWidget::tickProgress()
{
    const auto now = QDateTime::currentDateTimeUtc();
    if (this->pinEndsAt_.isValid() && now >= this->pinEndsAt_)
    {
        this->progressTimer_->stop();
        if (this->channel_)
            this->channel_->clearPinnedMessage();
        return;
    }
    const int mode = getSettings()->pinTimerDisplay;
    QString timestamp;
    if (this->pinnedAt_.isValid())
    {
        const auto format = getSettings()->pinTimestampFormat.getValue();
        timestamp =
            format == u"Relative"
                ? formatLongFriendlyDuration(this->pinnedAt_, now) + u" ago"
                : this->pinnedAt_.toLocalTime().toString(format);
    }
    QString remaining = u"Indefinitely"_s;
    if (this->pinEndsAt_.isValid())
    {
        const auto seconds = (now.msecsTo(this->pinEndsAt_) + 999) / 1000;
        remaining = u"%1:%2"_s.arg(seconds / 60).arg(seconds % 60, 2, 10, u'0');
    }
    this->pinnedByLabel_->setText(
        this->pinnedByText_ + ((mode == 0 || mode == 1) && !timestamp.isEmpty()
                                   ? u" · " + timestamp.toHtmlEscaped()
                                   : QString{}));
    this->countdownLabel_->setText(remaining);
    this->countdownLabel_->setVisible(mode == 0 || mode == 2);
    this->pinnedByLabel_->setToolTip(mode == 3 ? timestamp + u" · " + remaining
                                               : QString{});
}

void PinnedMessageWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    auto *theme = getTheme();

    // Fill background (same color as the split header above)
    const QColor custom(getSettings()->pinBannerBackgroundColor.getValue());
    painter.fillRect(event->rect(), custom.isValid()
                                        ? custom
                                        : theme->splits.header.background);

    // Draw 1px top border
    painter.setPen(theme->splits.header.border);
    painter.drawLine(0, 0, this->width() - 1, 0);
}

void PinnedMessageWidget::setChannel(TwitchChannel *channel)
{
    if (this->channel_ == channel)
    {
        this->refresh();
        return;
    }
    this->signalHolder_.clear();
    this->pinIdentity_.clear();
    this->channel_ = channel;
    this->autoHideTimer_->stop();

    if (channel)
    {
        this->signalHolder_.managedConnect(channel->pinnedMessageChanged,
                                           [this] {
                                               this->refresh();
                                           });
        this->signalHolder_.managedConnect(channel->userStateChanged, [this] {
            this->refresh();
        });
    }

    this->refresh();
}

std::unique_ptr<QMenu> PinnedMessageWidget::buildModMenu()
{
    auto menu = std::make_unique<QMenu>(this);

    menu->addAction(u"Unpin this Message"_s, this, [this] {
        if (this->channel_)
        {
            this->channel_->unpinCurrentMessage();
        }
    });

    auto *unpinAfterMenu = menu->addMenu(u"Unpin After"_s);

    const auto addDuration = [&](const QString &label,
                                 std::optional<std::chrono::seconds> duration) {
        unpinAfterMenu->addAction(label, this, [this, duration] {
            if (!this->channel_)
            {
                return;
            }
            const auto *pin = this->channel_->getPinnedMessage();
            if (!pin)
            {
                return;
            }
            auto currentAccount = getApp()->getAccounts()->twitch.getCurrent();
            if (!currentAccount || currentAccount->isAnon())
            {
                return;
            }
            this->channel_->updatePinnedMessageAs(
                pin->messageID, duration, *currentAccount, pin->messageText);
        });
    };

    addDuration(u"1 minute"_s, 1min);
    addDuration(u"5 minutes"_s, 5min);
    addDuration(u"10 minutes"_s, 10min);
    addDuration(u"20 minutes"_s, 20min);
    addDuration(u"30 minutes"_s, 30min);
    unpinAfterMenu->addSeparator();
    addDuration(u"End of stream"_s, std::nullopt);

    menu->addSeparator();

    menu->addAction(u"Hide for Yourself"_s, this, [this] {
        this->userToggled_ = false;
        this->hide();
        this->dismissed.invoke();
    });

    return menu;
}

void PinnedMessageWidget::refresh()
{
    if (!this->channel_)
    {
        this->progressTimer_->stop();
        this->autoHideTimer_->stop();
        this->encryptionIcon_->hide();
        this->hide();
        return;
    }

    const auto *pin = this->channel_->getPinnedMessage();
    if (!pin)
    {
        this->progressTimer_->stop();
        this->autoHideTimer_->stop();
        this->userToggled_ = std::nullopt;
        this->encryptionIcon_->hide();
        this->hide();
        return;
    }

    if (pin->endsAt && *pin->endsAt <= QDateTime::currentDateTimeUtc())
    {
        this->channel_->clearPinnedMessage();
        return;
    }

    const auto mode = static_cast<UsernameDisplayMode>(
        getSettings()->usernameDisplayMode.getValue());
    const auto identity =
        pin->messageID + pin->startsAt.toString(Qt::ISODateWithMs);
    if (this->pinIdentity_ != identity)
    {
        this->pinIdentity_ = identity;
        this->userToggled_.reset();
        this->expanded_ = getSettings()->alwaysExpandPinnedMessages;
    }
    this->pinnedByText_ = u"Pinned by <b>%1</b>"_s.arg(
        pin->pinnedBy.formatted(mode).toHtmlEscaped());
    this->pinnedAt_ = pin->startsAt;

    QString messageText = pin->messageText;
    bool decrypted = false;
    if (!pin->messageID.isEmpty())
    {
        const auto original = this->channel_->findMessageByID(pin->messageID);
        if (original && original->flags.has(MessageFlag::Decrypted))
        {
            messageText = original->messageText;
            decrypted = true;
        }
    }
    if (!decrypted && isMaybeEncrypted(messageText))
    {
        decrypted = decryptMessage(messageText,
                                   getSettings()->encryptionKey.getValue());
    }

    const auto original = this->channel_->findMessageByID(pin->messageID);
    const bool hidden =
        isIgnoredMessage({.message = messageText,
                          .twitchUserID = pin->sender.id,
                          .twitchUserLogin = pin->sender.login,
                          .isMod = this->channel_->isMod(),
                          .isBroadcaster = this->channel_->isBroadcaster()});
    this->displayChannel_->clearMessages();
    if (original && !hidden)
    {
        this->messageView_->setSourceChannel(this->channel_->sharedFromThis());
        this->displayChannel_->addMessage(original, MessageContext::Repost);
    }
    this->messageView_->setVisible(original != nullptr && !hidden);
    this->messageScrollArea_->setVisible(original == nullptr || hidden);
    this->messageLabel_->setText(
        hidden ? u"This pinned message is hidden by your ignore settings."_s
               : messageText);
    this->encryptionIcon_->setVisible(decrypted);
    this->updateMessageHeight();

    {
        const QString sentAt = pin->startsAt.toLocalTime().toString(
            getSettings()->timestampFormat);
        this->footerLabel_->setText(u"Sent by %1 \u00B7 %2"_s.arg(
            pin->sender.formatted(mode).toHtmlEscaped(), sentAt));
    }

    this->pinEndsAt_ = pin->endsAt.value_or(QDateTime{});
    this->tickProgress();
    if (this->pinnedAt_.isValid() || this->pinEndsAt_.isValid())
        this->progressTimer_->start();
    const bool isMod = this->channel_->canManagePinnedMessages();
    this->menuButton_->setVisible(isMod);
    this->closeButton_->setToolTip(getSettings()->pinCloseButtonAction == 1 &&
                                           isMod
                                       ? u"Unpin message"_s
                                       : u"Hide for yourself"_s);

    if (this->userToggled_.value_or(true))
    {
        this->show();
        this->updateMessageHeightIfNeeded();
    }

    this->autoHideTimer_->stop();
    if (!getSettings()->alwaysShowPinnedMessage &&
        !this->userToggled_.value_or(false))
    {
        this->autoHideTimer_->start(30s);
    }
}

void PinnedMessageWidget::toggleUserPinned()
{
    if (this->isVisible())
    {
        this->userToggled_ = false;
        this->autoHideTimer_->stop();
        this->hide();
    }
    else
    {
        this->userToggled_ = true;
        this->autoHideTimer_->stop();
        this->show();
        this->updateMessageHeightIfNeeded();
    }
}

void PinnedMessageWidget::updateMessageHeight()
{
    if (!this->messageLabel_ || !this->messageScrollArea_)
    {
        return;
    }

    // Wrapped height of the label at the current viewport width.
    this->lastViewportWidth_ = this->messageScrollArea_->viewport()->width();
    int contentH =
        this->messageLabel_->heightForWidth(this->lastViewportWidth_);
    if (contentH <= 0)
    {
        contentH = this->messageLabel_->sizeHint().height();
    }

    if (!this->messageView_->isHidden())
        contentH = this->messageView_->contentHeight(10000);
    const auto maxHeight =
        this->expanded_
            ? std::max(this->messageMaxHeight_,
                       (this->parentWidget() ? this->parentWidget()->height()
                                             : this->height()) /
                           2)
            : this->messageMaxHeight_;
    this->messageView_->setFixedHeight(qBound(1, contentH, maxHeight));
    this->expandButton_->setVisible(contentH > this->messageMaxHeight_);
    this->expandButton_->setText(this->expanded_ ? u"Collapse"_s : u"Expand"_s);
    // Size to content, but never taller than the cap.
    this->messageScrollArea_->setFixedHeight(qBound(1, contentH, maxHeight));
}

void PinnedMessageWidget::updateMessageHeightIfNeeded()
{
    if (this->lastViewportWidth_ !=
        this->messageScrollArea_->viewport()->width())
    {
        this->updateMessageHeight();
    }
}

void PinnedMessageWidget::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    this->updateMessageHeight();
}

void PinnedMessageWidget::showEvent(QShowEvent *event)
{
    BaseWidget::showEvent(event);
    this->visibilityChanged.invoke();
}

void PinnedMessageWidget::hideEvent(QHideEvent *event)
{
    BaseWidget::hideEvent(event);
    this->visibilityChanged.invoke();
}

void PinnedMessageWidget::scaleChangedEvent(float newScale)
{
    newScale *=
        std::clamp(getSettings()->pinnedMessageScale.getValue(), 0.5F, 3.0F);
    const auto contentScale =
        std::clamp(getSettings()->pinnedContentScale.getValue(), 0.5F, 3.0F);
    this->messageView_->setOverrideScale(newScale * contentScale);
    QFont headerFont = this->pinnedByLabel_->font();
    headerFont.setPointSizeF(9.5F * newScale);
    this->pinnedByLabel_->setFont(headerFont);
    this->countdownLabel_->setFont(headerFont);

    QFont bodyFont = this->messageLabel_->font();
    bodyFont.setPointSizeF(11.0F * newScale * contentScale);
    this->messageLabel_->setFont(bodyFont);
    this->messageMaxHeight_ = int(110 * newScale);
    this->updateMessageHeight();

    QFont footerFont = this->footerLabel_->font();
    footerFont.setPointSizeF(9.0F * newScale);
    this->footerLabel_->setFont(footerFont);

    const auto &lockBadge = getResources().twitch.lockBadge;
    const auto lockSize = lockBadge.size() * (ENCRYPTION_ICON_SCALE * newScale);
    const auto scaledLock = lockBadge.scaled(lockSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
    this->encryptionIcon_->setFixedSize(scaledLock.size());
    this->encryptionIcon_->setPixmap(scaledLock);
}

void PinnedMessageWidget::mousePressEvent(QMouseEvent *event)
{
    // ignore to disable the parent's right click menu
}

void PinnedMessageWidget::setPinnedMessage(
    const std::optional<TwitchChannel::PinnedMessage> &pin,
    TwitchChannel *channel)
{
    this->setChannel(pin ? channel : nullptr);
}

bool PinnedMessageWidget::hasPinnedMessage() const
{
    return this->channel_ && this->channel_->getPinnedMessage() &&
           this->userToggled_.value_or(true);
}

void PinnedMessageWidget::setToggleButtonVisible(bool visible)
{
    this->toggleButton_->setVisible(visible);
}

void PinnedMessageWidget::refreshLayout()
{
    this->refresh();
}

}  // namespace chatterino
