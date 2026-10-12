#pragma once

#include <pajlada/signals/signalholder.hpp>
#include <QString>
#include <QWidget>

#include <cstdint>

class QPushButton;
class QScrollArea;

namespace chatterino {

class Label;
class UserBadgesGrid;

/// Shows every badge a Twitch user owns, grouped by provider.
///
/// The collection comes from the user's public BadgeHQ profile. Nothing is
/// requested until the view is activated, so opening a usercard alone never
/// contacts BadgeHQ.
class UserBadgesView final : public QWidget
{
public:
    explicit UserBadgesView(QWidget *parent = nullptr);

    void setContext(const QString &login);
    void activate();
    void deactivate();
    void refreshStyle(float scale);

private:
    void reset();
    void load();
    void applyProfile(const QByteArray &payload);
    void updateStatus();
    void openProfile() const;

    QString login_;
    QString error_;
    uint64_t generation_{};
    qsizetype badgeCount_{};
    bool activated_{};
    bool loaded_{};
    bool loading_{};

    Label *statusLabel_{};
    QPushButton *retryButton_{};
    QPushButton *profileButton_{};
    QScrollArea *scrollArea_{};
    UserBadgesGrid *grid_{};
    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
