// SPDX-FileCopyrightText: 2022 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "buttons/SvgButton.hpp"
#include "common/ChatterinoSetting.hpp"
#include "widgets/BaseWindow.hpp"

#include <QPoint>
#include <QTimer>

#include <memory>

namespace chatterino {

class DraggablePopup : public BaseWindow
{
    Q_OBJECT

public:
    /// DraggablePopup implements the automatic dragging behavior when clicking
    /// anywhere in the window (that doesn't have some other widget).
    ///
    /// If closeAutomatically is set, the window will close when losing focus,
    /// and the window will be frameless.
    DraggablePopup(bool closeAutomatically, QWidget *parent);
    ~DraggablePopup() override;

protected:
    enum class ResizeMode { Setting, Always };

    void enableResize(QSizeSetting &setting, QSize defaultSize = {},
                      bool rememberSize = true,
                      ResizeMode resizeMode = ResizeMode::Setting);
    bool hasCustomSize() const;
    QSize preferredSize(QSize fallback) const;
    void applyPopupSize(QSize suggestedSize);
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void scaleChangedEvent(float scale) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    /// Creates a pin button that is scoped to this window.
    /// When clicked, the user can toggle whether the window is pinned.
    /// The window is considered unpinned at the start.
    Button *createPinButton();

    // lifetimeHack_ is used to check that the window hasn't been destroyed yet
    std::shared_ptr<bool> lifetimeHack_;

    // Toggles pin status updates action on focus loss, isPinned_ and the pin
    // button pixmap
    void togglePinned();

    /// Ensures that this popup is pinned (if it's expected to close automatically)
    ///
    /// @returns `true` if the popup was pinned as a result (i.e. if the popup
    ///          was unpinned and said to automatically close before)
    bool ensurePinned();

private:
    bool resizeEnabled() const;
    void savePopupSize();
    void positionSizeGrip();
    QSizeSetting *sizeSetting_ = nullptr;
    QWidget *sizeGrip_ = nullptr;
    QSize customSize_;
    bool resizing_ = false;
    bool applyingSize_ = false;
    bool sizeDirty_ = false;
    QTimer saveSizeTimer_;

    bool isMoving_ = false;

    bool closeAutomatically_ = false;

    // startPosDrag_ is the coordinates where the user originally pressed the mouse button down to start dragging
    QPoint startPosDrag_;

    // requestDragPos_ is the final screen coordinates where the widget should be moved to.
    // Takes the relative position of where the user originally clicked the widget into account
    QPoint requestedDragPos_;

    // dragTimer_ is called ~60 times per second once the user has initiated dragging
    QTimer dragTimer_;

    SvgButton *pinButton_{};
    SvgButton::Src pinDisabledSource_{
        .dark = ":/buttons/pinDisabled-darkMode.svg",
        .light = ":/buttons/pinDisabled-lightMode.svg",
    };
    SvgButton::Src pinEnabledSource_{
        .dark = ":/buttons/pinEnabled.svg",
        .light = ":/buttons/pinEnabled.svg",
    };
    bool isPinned_ = false;
    QSize defaultSize_;
    QSize suggestedSize_;
    bool rememberSize_ = true;
    ResizeMode resizeMode_ = ResizeMode::Setting;
    pajlada::Signals::SignalHolder resizeConnections_;
};

}  // namespace chatterino
