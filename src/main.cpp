#include "application.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QIcon>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int g_signalFds[2];

void handleSignal(int)
{
    const char c = 1;
    if (write(g_signalFds[1], &c, 1) == -1)
        _exit(1);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // applicationName doubles as the Wayland app_id on older Qt releases
    // (6.8.x ignores desktopFileName), so keep it equal to the app id.
    QCoreApplication::setOrganizationName(QStringLiteral("colton"));
    QCoreApplication::setApplicationName(QStringLiteral("org.jhc.TuxNotes"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.jhc.TuxNotes"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFds) == 0) {
        struct sigaction sa;
        sa.sa_handler = handleSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        auto *notifier = new QSocketNotifier(g_signalFds[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&notifier] {
            notifier->setEnabled(false);
            qApp->quit();
        });
    }

    // Single instance: a second launch just creates a new note.
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.jhc.TuxNotes"))) {
        QDBusInterface existing(QStringLiteral("org.jhc.TuxNotes"), QStringLiteral("/tuxnotes"),
                                QStringLiteral("org.jhc.TuxNotes"),
                                QDBusConnection::sessionBus());
        existing.asyncCall(QStringLiteral("NewNote"));
        return 0;
    }

    TuxNotesApplication app2;
    app2.start();

    return QCoreApplication::exec();
}
