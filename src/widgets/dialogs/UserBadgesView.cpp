#include "widgets/dialogs/UserBadgesView.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Image.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/Label.hpp"
#include "widgets/TooltipWidget.hpp"

#include <QCache>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace chatterino {
namespace {

constexpr auto BADGEHQ_ORIGIN = "https://www.badgehq.xyz";
constexpr qsizetype MAX_PAYLOAD_SIZE = 4 * 1024 * 1024;
constexpr qsizetype MAX_BADGES_PER_SECTION = 2000;
constexpr qint64 CACHE_LIFETIME_MS = 5 * 60 * 1000;

struct CachedProfile {
    QByteArray payload;
    qint64 fetchedAt{};
};

QCache<QString, CachedProfile> &profileCache()
{
    static QCache<QString, CachedProfile> cache(24);
    return cache;
}

bool validLogin(const QString &login)
{
    return !login.isEmpty() && login.size() <= 25 &&
           std::ranges::all_of(login, [](QChar c) {
               return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                      (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                      c == QLatin1Char('_');
           });
}

QUrl profileUrl(const QString &login)
{
    return QUrl(QString::fromLatin1(BADGEHQ_ORIGIN) + QStringLiteral("/u/") +
                login);
}

void openExternalUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
    {
        return;
    }
    if (getSettings()->openLinksIncognito && supportsIncognitoLinks())
    {
        openLinkIncognito(url.toString());
        return;
    }
    QDesktopServices::openUrl(url);
}

/// BadgeHQ lists images from every badge provider's own CDN, and its own
/// images as paths on its site. Only plain https URLs are loaded.
QString imageUrl(const QString &value)
{
    if (value.isEmpty() || value.size() > 2048)
    {
        return {};
    }
    const auto absolute = value.startsWith(QLatin1Char('/')) &&
                                  !value.startsWith(QStringLiteral("//"))
                              ? QString::fromLatin1(BADGEHQ_ORIGIN) + value
                              : value;
    const QUrl url(absolute, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty())
    {
        return {};
    }
    return absolute;
}

}  // namespace

class UserBadgesGrid final : public BaseWidget
{
public:
    struct Badge {
        ImagePtr image;
        QString tooltip;
        QColor background;
    };

    struct Section {
        QString title;
        std::vector<Badge> badges;
    };

    explicit UserBadgesGrid(QWidget *parent)
        : BaseWidget(parent)
    {
        this->setMouseTracking(true);
    }

    void setSections(std::vector<Section> sections)
    {
        this->hideTooltip();
        this->sections_ = std::move(sections);
        this->relayout();
        this->update();
    }

    void setStyle(float scale, const QFont &font, const QColor &headerColor)
    {
        this->gridScale_ = scale;
        this->headerColor_ = headerColor;
        this->setFont(font);
        this->relayout();
        this->update();
    }

    bool hasAnimatedBadges() const
    {
        return std::ranges::any_of(this->sections_, [](const Section &section) {
            return std::ranges::any_of(section.badges, [](const Badge &badge) {
                return badge.image->animated();
            });
        });
    }

protected:
    // Hovering a badge shows what it is, the same way badges in chat do.
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const auto *badge = this->badgeAt(event->pos());
        if (badge == nullptr || badge->tooltip.isEmpty())
        {
            this->hideTooltip();
            return;
        }

        if (this->tooltipWidget_ == nullptr)
        {
            this->tooltipWidget_ = new TooltipWidget(this);
        }
        if (badge != this->hoveredBadge_)
        {
            this->hoveredBadge_ = badge;
            this->tooltipWidget_->setOne(TooltipEntry::scaled(
                badge->image, badge->tooltip, 1.0F, TOOLTIP_IMAGE_SIZE));
            // Badge descriptions run to a full sentence, so they wrap
            // instead of stretching the tooltip across the screen.
            this->tooltipWidget_->setMaximumWidth(
                qRound(LOGICAL_TOOLTIP_WIDTH * this->gridScale_));
            this->tooltipWidget_->setWordWrap(true);
        }
        this->tooltipWidget_->moveTo(
            event->globalPosition().toPoint() + QPoint(16, 16),
            widgets::BoundsChecking::CursorPosition);
        this->tooltipWidget_->show();
    }

    void leaveEvent(QEvent *event) override
    {
        this->hideTooltip();
        BaseWidget::leaveEvent(event);
    }

    void hideEvent(QHideEvent *event) override
    {
        this->hideTooltip();
        BaseWidget::hideEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        BaseWidget::resizeEvent(event);
        this->relayout();
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto viewport =
            this->parentWidget()->rect().translated(-this->pos());
        const auto headerHeight = this->headerHeight();
        const auto radius = std::max(1.0, 3.0 * this->gridScale_);
        this->forEachItem(
            this->width(),
            [&](const Section &section, int y) {
                painter.setPen(this->headerColor_);
                painter.drawText(
                    QRect(this->margin(), y,
                          std::max(0, this->width() - (2 * this->margin())),
                          headerHeight),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("%1 (%2)").arg(
                        section.title,
                        QLocale().toString(
                            static_cast<qlonglong>(section.badges.size()))));
            },
            [&](const Badge &badge, const QRect &rect) {
                if (!rect.intersects(viewport))
                {
                    return true;
                }
                if (badge.background.isValid())
                {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(badge.background);
                    painter.drawRoundedRect(rect, radius, radius);
                }
                if (const auto pixmap = badge.image->pixmapOrLoad())
                {
                    painter.drawPixmap(rect, *pixmap, pixmap->rect());
                }
                return true;
            });
    }

private:
    static constexpr int LOGICAL_BADGE_SIZE = 24;
    static constexpr int LOGICAL_BADGE_SPACING = 6;
    static constexpr int LOGICAL_HEADER_HEIGHT = 24;
    static constexpr int LOGICAL_MARGIN = 8;
    static constexpr int TOOLTIP_IMAGE_SIZE = 72;
    static constexpr int LOGICAL_TOOLTIP_WIDTH = 260;

    int badgeSize() const
    {
        return std::max(1, qRound(LOGICAL_BADGE_SIZE * this->gridScale_));
    }

    int badgeSpacing() const
    {
        return std::max(1, qRound(LOGICAL_BADGE_SPACING * this->gridScale_));
    }

    int headerHeight() const
    {
        return std::max(QFontMetrics(this->font()).height(),
                        qRound(LOGICAL_HEADER_HEIGHT * this->gridScale_));
    }

    int margin() const
    {
        return std::max(1, qRound(LOGICAL_MARGIN * this->gridScale_));
    }

    /// Walks every section header and badge in layout order for the given
    /// width and returns the total height. `onBadge` returns false to stop.
    template <typename OnHeader, typename OnBadge>
    int forEachItem(int width, OnHeader &&onHeader, OnBadge &&onBadge) const
    {
        const auto size = this->badgeSize();
        const auto spacing = this->badgeSpacing();
        const auto margin = this->margin();
        const auto right = std::max(margin + size, width - margin);

        auto y = margin / 2;
        for (const auto &section : this->sections_)
        {
            if (section.badges.empty())
            {
                continue;
            }
            onHeader(section, y);
            y += this->headerHeight();

            auto x = margin;
            for (const auto &badge : section.badges)
            {
                if (x > margin && x + size > right)
                {
                    x = margin;
                    y += size + spacing;
                }
                if (!onBadge(badge, QRect(x, y, size, size)))
                {
                    return y;
                }
                x += size + spacing;
            }
            y += size + spacing;
        }
        return y + (margin / 2);
    }

    const Badge *badgeAt(const QPoint &position) const
    {
        const Badge *found = nullptr;
        this->forEachItem(
            this->width(), [](const Section &, int) {},
            [&](const Badge &badge, const QRect &rect) {
                if (rect.contains(position))
                {
                    found = &badge;
                    return false;
                }
                return true;
            });
        return found;
    }

    void hideTooltip()
    {
        this->hoveredBadge_ = nullptr;
        if (this->tooltipWidget_ != nullptr)
        {
            this->tooltipWidget_->hide();
        }
    }

    void relayout()
    {
        const auto height = this->forEachItem(
            this->width(), [](const Section &, int) {},
            [](const Badge &, const QRect &) {
                return true;
            });
        if (this->minimumHeight() != height)
        {
            this->setMinimumHeight(height);
        }
    }

    std::vector<Section> sections_;
    QColor headerColor_;
    TooltipWidget *tooltipWidget_ = nullptr;
    const Badge *hoveredBadge_ = nullptr;
    float gridScale_ = 1.0F;
};

namespace {

QString badgeTooltip(const QString &title, const QString &description)
{
    auto tooltip = QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped());
    if (!description.isEmpty() &&
        description.compare(title, Qt::CaseInsensitive) != 0)
    {
        tooltip += QStringLiteral("<br>") + description.toHtmlEscaped();
    }
    return tooltip;
}

UserBadgesGrid::Section parseSection(const QString &title,
                                     const QJsonValue &value,
                                     const QString &titlePrefix = {})
{
    UserBadgesGrid::Section section{.title = title, .badges = {}};
    for (const auto &entry : value.toArray())
    {
        if (std::cmp_greater_equal(section.badges.size(),
                                   MAX_BADGES_PER_SECTION))
        {
            break;
        }
        const auto object = entry.toObject();
        const auto url = imageUrl(object.value("image").toString());
        auto name = object.value("title").toString();
        if (name.isEmpty())
        {
            name = object.value("name").toString();
        }
        auto badgeUrl = url;
        if (object.value("founder").toBool())
        {
            name = QStringLiteral("Founder");
            // The profile lists the Founder badge's still artwork (the site
            // animates it itself); show the animated badge instead.
            badgeUrl = imageUrl(QStringLiteral("/badges/founder.webp"));
        }
        name = name.left(256);
        if (badgeUrl.isEmpty() || name.isEmpty())
        {
            continue;
        }

        // FrankerFaceZ badges are transparent glyphs drawn on a colour.
        QColor background;
        if (const auto color = object.value("color").toString();
            color.size() <= 9 && color.startsWith(QLatin1Char('#')))
        {
            background = QColor(color);
        }

        section.badges.push_back({
            .image = Image::fromUrl(Url{badgeUrl}),
            .tooltip =
                badgeTooltip(titlePrefix + name,
                             object.value("description").toString().left(512)),
            .background = background,
        });
    }
    return section;
}

}  // namespace

UserBadgesView::UserBadgesView(QWidget *parent)
    : QWidget(parent)
{
    this->setAttribute(Qt::WA_NoMousePropagation);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(5);
    this->statusLabel_ = new Label;
    this->statusLabel_->setPadding({9, 0, 0, 0});
    this->statusLabel_->setShouldElide(true);
    this->statusLabel_->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Preferred);
    statusRow->addWidget(this->statusLabel_, 1);
    this->retryButton_ = new QPushButton("Retry", this);
    this->retryButton_->setAutoDefault(false);
    this->retryButton_->hide();
    statusRow->addWidget(this->retryButton_);
    this->profileButton_ = new QPushButton("Open BadgeHQ Profile", this);
    this->profileButton_->setAutoDefault(false);
    this->profileButton_->setToolTip("Open this user's BadgeHQ profile");
    statusRow->addWidget(this->profileButton_);
    layout->addLayout(statusRow);

    this->scrollArea_ = new QScrollArea(this);
    this->scrollArea_->setFrameShape(QFrame::NoFrame);
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->grid_ = new UserBadgesGrid(this->scrollArea_);
    this->scrollArea_->setWidget(this->grid_);
    layout->addWidget(this->scrollArea_, 1);

    QObject::connect(this->retryButton_, &QPushButton::clicked, this, [this] {
        this->load();
    });
    QObject::connect(this->profileButton_, &QPushButton::clicked, this, [this] {
        this->openProfile();
    });

    // Badge images arrive after the first paint, and some of them animate.
    this->signalHolder_.managedConnect(getApp()->getWindows()->layoutRequested,
                                       [this](Channel *) {
                                           if (this->isVisible())
                                           {
                                               this->grid_->update();
                                           }
                                       });
    this->signalHolder_.managedConnect(
        getApp()->getWindows()->gifRepaintRequested, [this] {
            if (this->isVisible() && this->grid_->hasAnimatedBadges())
            {
                this->grid_->update();
            }
        });

    this->setMinimumSize(430, 275);
    this->updateStatus();
}

void UserBadgesView::setContext(const QString &login)
{
    const auto normalized = login.trimmed().toLower();
    if (this->login_ == normalized)
    {
        return;
    }
    this->login_ = normalized;
    this->reset();
    if (this->activated_)
    {
        this->load();
    }
}

void UserBadgesView::activate()
{
    this->activated_ = true;
    if (!this->loaded_ && !this->loading_)
    {
        this->load();
    }
}

void UserBadgesView::deactivate()
{
    this->activated_ = false;
}

void UserBadgesView::prefetch()
{
    if (!this->loaded_ && !this->loading_ && validLogin(this->login_))
    {
        this->load();
    }
}

void UserBadgesView::refreshStyle(float scale)
{
    const auto font = getApp()->getFonts()->getFont(FontStyle::UiMedium, scale);
    const auto *theme = getTheme();
    auto palette = this->palette();
    palette.setColor(QPalette::Window, theme->window.background);
    palette.setColor(QPalette::WindowText, theme->window.text);
    palette.setColor(QPalette::Base, theme->messages.backgrounds.regular);
    palette.setColor(QPalette::Text, theme->messages.textColors.regular);
    palette.setColor(QPalette::Button,
                     theme->tabs.selected.backgrounds.regular);
    palette.setColor(QPalette::ButtonText, theme->window.text);
    this->setPalette(palette);
    this->setAutoFillBackground(true);

    for (auto *widget : this->findChildren<QWidget *>())
    {
        widget->setFont(font);
        widget->setPalette(palette);
    }
    this->setFont(font);

    // The collection sits on the same background as the messages it replaces.
    auto gridPalette = palette;
    gridPalette.setColor(QPalette::Window, theme->messages.backgrounds.regular);
    this->scrollArea_->viewport()->setPalette(gridPalette);
    this->scrollArea_->viewport()->setAutoFillBackground(true);
    this->grid_->setPalette(gridPalette);

    this->grid_->setStyle(scale, font,
                          theme->messages.textColors.regular.darker(145));
}

void UserBadgesView::reset()
{
    this->generation_++;
    this->error_.clear();
    this->badgeCount_ = 0;
    this->loaded_ = false;
    this->loading_ = false;
    this->grid_->setSections({});
    this->updateStatus();
    this->globalBadgeCountChanged.invoke(-1);
}

void UserBadgesView::load()
{
    if (this->loading_ || !validLogin(this->login_))
    {
        this->updateStatus();
        return;
    }

    if (const auto *cached = profileCache().object(this->login_);
        cached != nullptr &&
        QDateTime::currentMSecsSinceEpoch() - cached->fetchedAt <
            CACHE_LIFETIME_MS)
    {
        this->applyProfile(cached->payload);
        return;
    }

    this->error_.clear();
    this->loading_ = true;
    this->updateStatus();

    const auto generation = ++this->generation_;
    const auto login = this->login_;
    NetworkRequest(QString::fromLatin1(BADGEHQ_ORIGIN) +
                   QStringLiteral("/api/user/") + login)
        .header("Accept", "application/json")
        .maximumResponseSize(MAX_PAYLOAD_SIZE)
        .timeout(15000)
        .caller(this)
        .onSuccess([this, generation, login](const NetworkResult &result) {
            const auto payload = result.getData();
            profileCache().insert(
                login, new CachedProfile{
                           .payload = payload,
                           .fetchedAt = QDateTime::currentMSecsSinceEpoch(),
                       });
            if (generation != this->generation_)
            {
                return;
            }
            this->loading_ = false;
            this->applyProfile(payload);
        })
        .onError([this, generation](const NetworkResult &result) {
            if (generation != this->generation_)
            {
                return;
            }
            qCWarning(chatterinoApp)
                << "[BadgeHQ] Profile request failed:" << result.formatError();
            this->loading_ = false;
            this->error_ = result.status() == 404
                               ? QStringLiteral("BadgeHQ has no profile for "
                                                "this user")
                               : QStringLiteral("Couldn't load badges from "
                                                "BadgeHQ");
            this->updateStatus();
        })
        .execute();
}

void UserBadgesView::applyProfile(const QByteArray &payload)
{
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
    {
        profileCache().remove(this->login_);
        this->error_ = QStringLiteral("BadgeHQ sent an unexpected response");
        this->updateStatus();
        return;
    }
    const auto root = document.object();

    // Same grouping and order as a profile on BadgeHQ.
    auto other = parseSection("Other Badges", root.value("ffzBadges"));
    const auto appendOther = [&other](UserBadgesGrid::Section section) {
        std::ranges::move(section.badges, std::back_inserter(other.badges));
    };
    appendOther(parseSection({}, root.value("bttvBadges")));
    // A developer wears the Developer badge next to their supporter badge.
    if (const auto developer = root.value("developerBadge");
        developer.isObject())
    {
        appendOther(parseSection({}, QJsonArray{developer},
                                 QStringLiteral("BadgeHQ ")));
    }
    appendOther(parseSection({}, root.value("badgehqBadges"),
                             QStringLiteral("BadgeHQ ")));
    appendOther(parseSection({}, root.value("chatterinoBadges")));
    appendOther(parseSection({}, root.value("homiesBadges")));
    appendOther(parseSection({}, root.value("moltorinoBadges")));

    std::vector<UserBadgesGrid::Section> sections;
    sections.push_back(
        parseSection("Global Badges", root.value("twitchBadges")));
    sections.push_back(parseSection(
        "7TV Badges", root.value("seventv").toObject().value("badges")));
    sections.push_back(std::move(other));

    this->badgeCount_ = 0;
    for (const auto &section : sections)
    {
        this->badgeCount_ += static_cast<qsizetype>(section.badges.size());
    }
    this->error_.clear();
    this->loaded_ = true;
    this->grid_->setSections(std::move(sections));
    this->updateStatus();
    this->globalBadgeCountChanged.invoke(
        root.value("twitchBadges").toArray().size());
}

void UserBadgesView::updateStatus()
{
    const bool hasLogin = validLogin(this->login_);
    this->profileButton_->setEnabled(hasLogin);
    this->retryButton_->setVisible(!this->error_.isEmpty() && hasLogin);

    if (!hasLogin)
    {
        this->statusLabel_->setText("Badges are unavailable for this user");
    }
    else if (!this->error_.isEmpty())
    {
        this->statusLabel_->setText(this->error_);
    }
    else if (this->loading_ || !this->loaded_)
    {
        this->statusLabel_->setText("Loading badges from BadgeHQ...");
    }
    else if (this->badgeCount_ == 0)
    {
        this->statusLabel_->setText("No badges found");
    }
    else
    {
        this->statusLabel_->setText(
            QStringLiteral("%1 %2 via BadgeHQ")
                .arg(QLocale().toString(
                         static_cast<qlonglong>(this->badgeCount_)),
                     this->badgeCount_ == 1 ? QStringLiteral("badge")
                                            : QStringLiteral("badges")));
    }
}

void UserBadgesView::openProfile() const
{
    if (validLogin(this->login_))
    {
        openExternalUrl(profileUrl(this->login_));
    }
}

}  // namespace chatterino
