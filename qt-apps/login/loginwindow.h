#pragma once

#include <QtCore/QTimer>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>

namespace purec {

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void attempt();
    void fail(const QString &message);
    void finish();

    QLabel *m_message;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QPushButton *m_button;
    bool m_live;
    bool m_done = false;
    int m_failures = 0;
};

}
