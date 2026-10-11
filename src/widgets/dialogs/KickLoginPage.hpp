#pragma once
#include <QPointer>
class QDialog;

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

namespace chatterino {

class KickLoginPage : public QWidget
{
public:
    KickLoginPage();

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QPointer<QDialog> authDialog_;
    struct {
        QFormLayout *layout = nullptr;
        QLabel *topLabel = nullptr;
        QLineEdit *clientID = nullptr;
        QLineEdit *clientSecret = nullptr;
        QComboBox *methodCombo = nullptr;
    } ui;

    void refreshState() const;
};

}  // namespace chatterino
