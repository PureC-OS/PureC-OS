#pragma once

#include <QtCore/QObject>
#include <QtCore/QSocketNotifier>
#include <QtCore/QString>
#include <QtCore/QTimer>

namespace purec {

class ShellSession : public QObject
{
    Q_OBJECT
public:
    explicit ShellSession(QObject *parent = nullptr);
    ~ShellSession() override;

    bool running() const { return m_pid > 0; }
    bool start(const QString &commandLine);
    void write(const QByteArray &data);
    void interrupt();
    void closeInput();

signals:
    void output(const QByteArray &data);
    void finished(int status);

private:
    void readOutput();
    void reap();
    void cleanup();

    int m_pid = -1;
    int m_outFd = -1;
    int m_inFd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_reaper;
};

}
