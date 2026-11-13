#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <cstdio>

static int failures;

static void check(const char *name, bool ok)
{
    std::printf("qtcore %-34s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

class Counter : public QObject
{
    Q_OBJECT
public:
    int value = 0;
public slots:
    void bump(int by) { value += by; }
signals:
    void changed(int by);
};

class Worker : public QThread
{
public:
    int result = 0;
    void run() override
    {
        for (int i = 1; i <= 100; i++)
            result += i;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QString text = QStringLiteral("Hello Qt on PureC OS");
    check("QString length/upper", text.size() == 20 && text.toUpper().startsWith(QLatin1String("HELLO")));
    check("QString UTF-8 round trip", QString::fromUtf8("привет").toUtf8().size() == 12);

    QMap<QString, int> map;
    map["a"] = 1;
    map["b"] = 2;
    check("QMap", map.value("b") == 2 && map.size() == 2);

    QStringList list = QString("x,y,z").split(',');
    check("QStringList split/join", list.size() == 3 && list.join('-') == "x-y-z");

    Counter counter;
    QObject::connect(&counter, &Counter::changed, &counter, &Counter::bump);
    emit counter.changed(5);
    emit counter.changed(7);
    check("signal/slot via moc", counter.value == 12);

    QString path = QDir::homePath() + QStringLiteral("/qtcore-test.txt");
    QFile out(path);
    bool wrote = out.open(QIODevice::WriteOnly) && out.write("qt-file-ok") == 10;
    out.close();
    QFile in(path);
    bool read = in.open(QIODevice::ReadOnly) && in.readAll() == "qt-file-ok";
    in.close();
    check("QFile write/read", wrote && read);
    check("QFile remove", QFile::remove(path) && !QFile::exists(path));

    Worker worker;
    worker.start();
    check("QThread run", worker.wait(5000) && worker.result == 5050);

    QMutex mutex;
    int shared = 0;
    Worker w2;
    mutex.lock();
    shared = 1;
    mutex.unlock();
    check("QMutex", shared == 1);

    bool fired = false;
    QTimer::singleShot(20, &app, [&] {
        fired = true;
        QCoreApplication::quit();
    });
    int code = app.exec();
    check("event loop + QTimer", fired && code == 0);

    std::printf("qtcore done failures=%d\n", failures);
    return failures ? 1 : 0;
}

#include "main.moc"
