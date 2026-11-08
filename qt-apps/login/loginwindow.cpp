#include "loginwindow.h"
#include "auth.h"
#include "platform.h"
#include "theme.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCloseEvent>
#include <QtGui/QFont>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>

namespace purec {

LoginWindow::LoginWindow(QWidget *parent)
    : QWidget(parent), m_live(purec_auth_live_session())
{
    setWindowTitle(QStringLiteral("PureC OS - Login"));
    const Theme &t = theme();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    card->setFixedWidth(420);
    card->setStyleSheet(QStringLiteral(
        "QFrame#card { background: %1; border: 1px solid %2; border-radius: 16px; }"
        "QFrame#card QLabel, QFrame#card QLineEdit, QFrame#card QPushButton { }")
        .arg(t.surface.name(), t.muted.name()));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(36, 36, 36, 32);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("PureC OS"));
    QFont titleFont = title->font();
    titleFont.setPixelSize(30);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignHCenter);
    title->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(t.accent.name()));

    auto *subtitle = new QLabel(m_live ? QStringLiteral("Live session - no accounts yet")
                                       : QStringLiteral("Sign in to continue"));
    subtitle->setObjectName(QStringLiteral("muted"));
    subtitle->setAlignment(Qt::AlignHCenter);
    subtitle->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(t.muted.name()));

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(14);

    m_user = new QLineEdit;
    m_user->setPlaceholderText(QStringLiteral("User name"));
    m_user->setMaxLength(31);
    m_password = new QLineEdit;
    m_password->setPlaceholderText(QStringLiteral("Password"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMaxLength(63);
    if (!m_live) {
        layout->addWidget(m_user);
        layout->addWidget(m_password);
    }

    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("error"));
    m_message->setAlignment(Qt::AlignHCenter);
    m_message->setMinimumHeight(22);
    m_message->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(t.danger.name()));
    layout->addWidget(m_message);

    m_button = new QPushButton(m_live ? QStringLiteral("Enter as root") : QStringLiteral("Sign in"));
    m_button->setDefault(true);
    m_button->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_button);

    auto *hint = new QLabel(m_live ? QStringLiteral("Install PureC OS to create accounts")
                                   : QStringLiteral("Tab switches fields, Enter signs in"));
    hint->setAlignment(Qt::AlignHCenter);
    hint->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(t.muted.name()));
    layout->addSpacing(6);
    layout->addWidget(hint);

    outer->addStretch();
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();

    connect(m_button, &QPushButton::clicked, this, &LoginWindow::attempt);
    connect(m_user, &QLineEdit::returnPressed, this, [this] {
        if (m_user->text().isEmpty())
            return;
        m_password->setFocus();
    });
    connect(m_password, &QLineEdit::returnPressed, this, &LoginWindow::attempt);
}

void LoginWindow::showEvent(QShowEvent *)
{
    if (m_live)
        m_button->setFocus();
    else
        m_user->setFocus();
}

void LoginWindow::closeEvent(QCloseEvent *event)
{
    if (m_done)
        event->accept();
    else
        event->ignore();
}

void LoginWindow::fail(const QString &message)
{
    ++m_failures;
    m_message->setText(message);
    m_password->clear();
    m_button->setEnabled(false);
    m_user->setEnabled(false);
    m_password->setEnabled(false);
    QTimer::singleShot(std::min(m_failures, 5) * 600, this, [this] {
        m_button->setEnabled(true);
        m_user->setEnabled(true);
        m_password->setEnabled(true);
        m_password->setFocus();
    });
}

void LoginWindow::attempt()
{
    if (!m_button->isEnabled())
        return;
    uint32_t uid = 0;
    uint32_t gid = 0;
    if (!m_live) {
        QByteArray name = m_user->text().toUtf8();
        if (name.isEmpty()) {
            m_message->setText(QStringLiteral("Enter a user name."));
            m_user->setFocus();
            return;
        }
        m_message->setText(QStringLiteral("Checking..."));
        m_button->setEnabled(false);
        QCoreApplication::processEvents();
        QByteArray password = m_password->text().toUtf8();
        bool ok = purec_auth_check(name.constData(), password.constData(), &uid, &gid);
        password.fill('\0');
        m_password->clear();
        if (!ok) {
            m_button->setEnabled(true);
            fail(QStringLiteral("Login incorrect."));
            return;
        }
    }
    if (!purec_auth_begin_session(uid, gid)) {
        m_button->setEnabled(true);
        m_message->setText(QStringLiteral("Cannot start the session."));
        return;
    }
    finish();
}

void LoginWindow::finish()
{
    m_done = true;
    m_message->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(theme().success.name()));
    m_message->setText(QStringLiteral("Welcome"));
    QCoreApplication::processEvents();
    QTimer::singleShot(250, this, [this] {
        close();
        purec_auth_desktop_redraw();
        QCoreApplication::exit(0);
    });
}

}
