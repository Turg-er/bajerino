// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QCompleter>
#include <QKeyEvent>
#include <QPointer>
#include <QTextEdit>

namespace chatterino {

class TabCompletionModel;

class ResizingTextEdit : public QTextEdit
{
public:
    ResizingTextEdit();
    ~ResizingTextEdit() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    bool hasHeightForWidth() const override;
    bool isFirstWord() const;
    /// Ignore this prefix when determining the first word for completion.
    void setIgnoredCompletionPrefix(const QString &prefix);

    pajlada::Signals::Signal<QKeyEvent *> keyPressed;
    pajlada::Signals::NoArgSignal focused;
    pajlada::Signals::NoArgSignal focusLost;
    pajlada::Signals::Signal<const QMimeData *> imagePasted;
    pajlada::Signals::Signal<QMenu *, QPoint> contextMenuRequested;
    pajlada::Signals::Signal<TabCompletionModel *, int> tabCompletionChanged;
    pajlada::Signals::NoArgSignal tabCompletionHidden;

    void setCompleter(QCompleter *c);
    /**
     * Resets a completion for this text if one was is progress.
     * See `completionInProgress_`.
     */
    void resetCompletion();
    bool selectCompletionRow(int row);
    void setGhostText(QString text);
    const QString &ghostText() const;

protected:
    int heightForWidth(int /*unused*/) const override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    qreal documentHeightForWidth(int width) const;
    void invalidateAncestorLayouts();
    void finishCompletion();

    // hadSpace is set to true in case the "textUnderCursor" word was after a
    // space
    QString textUnderCursor(bool *hadSpace = nullptr) const;

    QPointer<QCompleter> completer_;
    pajlada::Signals::SignalHolder connections_;
    QString ignoredCompletionPrefix_;
    /**
     * This is true if a completion was done but the user didn't type yet,
     * and might want to press `Tab` again to get the next completion
     * on the original text.
     *
     * For example:
     *
     * input: "pog"
     * `Tab` pressed:
     *   - complete to "PogBones"
     *   - retain "pog" for next completion
     *   - set `completionInProgress_ = true`
     * `Tab` pressed again:
     *   - complete ["pog"] to "PogChamp"
     *
     * [other key] pressed or cursor moved - updating the input text:
     *   - set `completionInProgress_ = false`
     */
    bool completionInProgress_ = false;
    QString ghostText_;

    bool eventFilter(QObject *obj, QEvent *event) override;

    void insertCompletion(const QString &completion);
};

}  // namespace chatterino
