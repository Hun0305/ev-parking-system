#include "logindialog.h"

#include "auth/authclient.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

LoginDialog::LoginDialog(AuthClient *authClient,
                         const QUrl &initialServerBaseUrl,
                         QWidget *parent)
    : QDialog(parent)
    , m_authClient(authClient)
{
    Q_ASSERT(m_authClient);

    setObjectName(QStringLiteral("loginDialog"));
    setWindowTitle(QStringLiteral("Smart Parking Sign in"));
    setModal(true);
    setMinimumWidth(460);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setStyleSheet(QStringLiteral(
        "QDialog { background:#eef3f8; }"
        "QFrame#loginCard { background:white; border:1px solid #d7e0ea; "
        "border-radius:10px; }"
        "QLabel#loginTitle { color:#172b4d; font-size:24px; font-weight:800; }"
        "QLabel#loginSubtitle { color:#5f6f82; font-size:12px; }"
        "QLineEdit { min-height:38px; border:1px solid #b8c6d4; "
        "border-radius:6px; padding:0 10px; background:#ffffff; }"
        "QLineEdit:focus { border:2px solid #1976d2; }"
        "QPushButton#loginSubmitButton { min-height:40px; color:white; "
        "background:#1565c0; border:0; border-radius:6px; font-weight:700; }"
        "QPushButton#loginSubmitButton:hover { background:#0d5bad; }"
        "QPushButton#loginSubmitButton:disabled { background:#9eb4c9; }"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(28, 28, 28, 28);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(30, 28, 30, 28);
    cardLayout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("Smart Parking"), card);
    title->setObjectName(QStringLiteral("loginTitle"));
    cardLayout->addWidget(title);

    auto *subtitle = new QLabel(
        QStringLiteral("Sign in with the monitoring application account."), card);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setWordWrap(true);
    cardLayout->addWidget(subtitle);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 6, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_serverInput = new QLineEdit(card);
    m_serverInput->setObjectName(QStringLiteral("loginServerInput"));
    m_serverInput->setPlaceholderText(
        QStringLiteral("https://<raspberry-pi-host>:8443"));
    if (initialServerBaseUrl.isValid() && !initialServerBaseUrl.isEmpty()) {
        m_serverInput->setText(initialServerBaseUrl.toString());
    }
    form->addRow(QStringLiteral("Server"), m_serverInput);

    m_accountInput = new QLineEdit(card);
    m_accountInput->setObjectName(QStringLiteral("loginAccountInput"));
    m_accountInput->setPlaceholderText(QStringLiteral("Account ID"));
    m_accountInput->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("Account"), m_accountInput);

    m_passwordInput = new QLineEdit(card);
    m_passwordInput->setObjectName(QStringLiteral("loginPasswordInput"));
    m_passwordInput->setPlaceholderText(QStringLiteral("Password"));
    m_passwordInput->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password"), m_passwordInput);
    cardLayout->addLayout(form);

    m_showPasswordCheck = new QCheckBox(QStringLiteral("Show password"), card);
    m_showPasswordCheck->setObjectName(QStringLiteral("loginShowPasswordCheck"));
    cardLayout->addWidget(m_showPasswordCheck, 0, Qt::AlignRight);

    m_statusLabel = new QLabel(card);
    m_statusLabel->setObjectName(QStringLiteral("loginStatusLabel"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(34);
    cardLayout->addWidget(m_statusLabel);

    m_submitButton = new QPushButton(QStringLiteral("Sign in"), card);
    m_submitButton->setObjectName(QStringLiteral("loginSubmitButton"));
    m_submitButton->setDefault(true);
    cardLayout->addWidget(m_submitButton);

    outerLayout->addWidget(card);

    connect(m_showPasswordCheck, &QCheckBox::toggled,
            this, [this](bool checked) {
        m_passwordInput->setEchoMode(
            checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(m_submitButton, &QPushButton::clicked,
            this, &LoginDialog::submit);
    connect(m_passwordInput, &QLineEdit::returnPressed,
            this, &LoginDialog::submit);

    connect(m_authClient, &AuthClient::loginSucceeded,
            this, [this](const AuthSession &session) {
        m_authSession = session;
        m_passwordInput->clear();
        accept();
    });
    connect(m_authClient, &AuthClient::loginFailed,
            this, [this](const QString &message, int httpStatus) {
        setSubmitting(false);
        showStatus(message, true);
        if (httpStatus == 401) {
            m_passwordInput->clear();
        }
        m_passwordInput->setFocus();
    });

    if (m_serverInput->text().isEmpty()) {
        m_serverInput->setFocus();
    } else {
        m_accountInput->setFocus();
    }
}

AuthSession LoginDialog::authSession() const
{
    return m_authSession;
}

QUrl LoginDialog::selectedServerBaseUrl() const
{
    return m_authClient ? m_authClient->baseUrl() : QUrl();
}

void LoginDialog::setNotice(const QString &message)
{
    showStatus(message, false);
}

void LoginDialog::submit()
{
    if (!m_authClient || m_authClient->isRequestInFlight()) {
        return;
    }

    QString serverText = m_serverInput->text().trimmed();
    if (!serverText.contains(QStringLiteral("://"))) {
        serverText.prepend(QStringLiteral("https://"));
        m_serverInput->setText(serverText);
    }

    QString configurationError;
    if (!m_authClient->setBaseUrl(QUrl(serverText), &configurationError)) {
        showStatus(configurationError, true);
        m_serverInput->setFocus();
        return;
    }

    const QString accountId = m_accountInput->text().trimmed();
    const QString password = m_passwordInput->text();
    if (accountId.isEmpty() || password.isEmpty()) {
        showStatus(QStringLiteral("Enter both account ID and password."), true);
        (accountId.isEmpty() ? m_accountInput : m_passwordInput)->setFocus();
        return;
    }

    setSubmitting(true);
    showStatus(QStringLiteral("Signing in..."), false);
    m_authClient->login(accountId, password);
}

void LoginDialog::setSubmitting(bool submitting)
{
    m_serverInput->setEnabled(!submitting);
    m_accountInput->setEnabled(!submitting);
    m_passwordInput->setEnabled(!submitting);
    m_showPasswordCheck->setEnabled(!submitting);
    m_submitButton->setEnabled(!submitting);
    m_submitButton->setText(submitting
                                ? QStringLiteral("Signing in...")
                                : QStringLiteral("Sign in"));
}

void LoginDialog::showStatus(const QString &message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError
        ? QStringLiteral("color:#b3261e; font-weight:600;")
        : QStringLiteral("color:#455a64;"));
}
