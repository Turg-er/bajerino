#pragma once

#include <pajlada/signals/signal.hpp>
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
/// requested until the view is activated or prefetch() is called (the
/// usercard does that only while BadgeHQ badges are enabled, to show the
/// global badge count).
class UserBadgesView final : public QWidget
{
public:
    explicit UserBadgesView(QWidget *parent = nullptr);

    void setContext(const QString &login);
    void activate();
    void deactivate();
    /// Loads the profile without showing the view, so the global badge count
    /// can be shown on the usercard.
    void prefetch();
    void refreshStyle(float scale);
    void openProfile() const;

    /// How many global Twitch badges the user owns, or -1 when unknown.
    pajlada::Signals::Signal<qsizetype> globalBadgeCountChanged;

private:
    void reset();
    void load();
    void applyProfile(const QByteArray &payload);
    void updateStatus();

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
