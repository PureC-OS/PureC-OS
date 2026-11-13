#include "shellsession.h"

#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace purec {

ShellSession::ShellSession(QObject *parent)
    : QObject(parent)
{
    m_reaper.setInterval(50);
    connect(&m_reaper, &QTimer::timeout, this, &ShellSession::reap);
}

ShellSession::~ShellSession()
{
    if (m_pid > 0)
        ::kill(m_pid, SIGKILL);
    cleanup();
}

bool ShellSession::start(const QString &commandLine)
{
    if (running())
        return false;
    int outPipe[2];
    int inPipe[2];
    if (::pipe(outPipe) != 0)
        return false;
    if (::pipe(inPipe) != 0) {
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, inPipe[0], 0);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, outPipe[1], 2);
    posix_spawn_file_actions_addclose(&actions, outPipe[0]);
    posix_spawn_file_actions_addclose(&actions, inPipe[1]);
    QByteArray line = commandLine.toUtf8();
    char *argv[] = {const_cast<char *>("sh"), const_cast<char *>("-c"), line.data(), nullptr};
    pid_t pid = -1;
    int rc = posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(outPipe[1]);
    ::close(inPipe[0]);
    if (rc != 0) {
        ::close(outPipe[0]);
        ::close(inPipe[1]);
        return false;
    }
    m_pid = pid;
    m_outFd = outPipe[0];
    m_inFd = inPipe[1];
    int flags = ::fcntl(m_outFd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(m_outFd, F_SETFL, flags | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_outFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &ShellSession::readOutput);
    m_reaper.start();
    return true;
}

void ShellSession::write(const QByteArray &data)
{
    if (m_inFd < 0 || data.isEmpty())
        return;
    (void)::write(m_inFd, data.constData(), size_t(data.size()));
}

void ShellSession::closeInput()
{
    if (m_inFd >= 0) {
        ::close(m_inFd);
        m_inFd = -1;
    }
}

void ShellSession::interrupt()
{
    if (m_pid > 0)
        ::kill(m_pid, SIGINT);
}

void ShellSession::readOutput()
{
    char buffer[4096];
    for (;;) {
        ssize_t n = ::read(m_outFd, buffer, sizeof(buffer));
        if (n > 0) {
            emit output(QByteArray(buffer, int(n)));
            continue;
        }
        break;
    }
}

void ShellSession::reap()
{
    if (m_pid <= 0)
        return;
    int status = 0;
    pid_t done = ::waitpid(m_pid, &status, WNOHANG);
    if (done != m_pid)
        return;
    readOutput();
    m_pid = -1;
    m_reaper.stop();
    cleanup();
    emit finished(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
}

void ShellSession::cleanup()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_outFd >= 0) {
        ::close(m_outFd);
        m_outFd = -1;
    }
    closeInput();
}

}
