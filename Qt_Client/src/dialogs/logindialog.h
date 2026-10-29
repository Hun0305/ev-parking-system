#pragma once

#include "auth/authsession.h"

#include <QDialog>
#include <QUrl>

class AuthClient;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class LoginDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(AuthClient *authClient,
                         const QUrl &initialServerBaseUrl = QUrl(),
                         QWidget *parent = nullptr);

    AuthSession authSession() const;
    QUrl selectedServerBaseUrl() const;
    void setNotice(const QString &message);

private:
    void submit();
    void setSubmitting(bool submitting);
    void showStatus(const QString &message, bool isError);

    AuthClient *m_authClient = nullptr;
    AuthSession m_authSession;
    QLineEdit *m_serverInput = nullptr;
    QLineEdit *m_accountInput = nullptr;
    QLineEdit *m_passwordInput = nullptr;
    QCheckBox *m_showPasswordCheck = nullptr;
    QPushButton *m_submitButton = nullptr;
    QLabel *m_statusLabel = nullptr;
};
