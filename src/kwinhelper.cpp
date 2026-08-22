#include "kwinhelper.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <limits>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcKWin, "tuxnotes.kwin", QtWarningMsg)

namespace {
constexpr int kMaxAttempts = 10;
constexpr int kConfirmMs = 500;
constexpr int kRetryDelayMs = 450;
constexpr int kFirstAttemptDelayMs = 60;
constexpr int kGiveUpAfterMs = 15000;
constexpr int kTolerance = 1;

const QLatin1String kService("org.jhc.TuxNotes");
const QLatin1String kAppId("org.jhc.TuxNotes");
const QLatin1String kWatcherPlugin("org.jhc.TuxNotes.watch");

QString appId()
{
    return QGuiApplication::desktopFileName().isEmpty() ? QCoreApplication::applicationName()
                                                        : QGuiApplication::desktopFileName();
}

class ScriptReporter : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jhc.TuxNotes.KWin")

public:
    using Callback = std::function<void(const QString &)>;

    explicit ScriptReporter(Callback cb, QObject *parent = nullptr)
        : QObject(parent)
        , m_cb(std::move(cb))
    {
    }

public slots:
    void report(const QString &json) { m_cb(json); }

private:
    Callback m_cb;
};

QRect parseFrame(const QJsonObject &g)
{
    return QRect(g.value(QLatin1String("x")).toInt(), g.value(QLatin1String("y")).toInt(),
                 g.value(QLatin1String("width")).toInt(),
                 g.value(QLatin1String("height")).toInt());
}
} // namespace

KWinHelper::KWinHelper(QObject *parent)
    : QObject(parent)
    , m_wayland(QGuiApplication::platformName() == QLatin1String("wayland"))
{
    if (m_wayland) {
        auto *reporter = new ScriptReporter(
            [this](const QString &json) {
                const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
                const int gen = o.value(QLatin1String("gen")).toInt(-1);
                const QString type = o.value(QLatin1String("type")).toString();

                if ((type == QLatin1String("appeared") || type == QLatin1String("moved"))
                    && gen != m_watchGen)
                    return; // stale watcher instance from a previous run
                if (type == QLatin1String("ready") && gen == m_watchGen) {
                    m_watcherReady = true;
                    return;
                }
                if (type == QLatin1String("appeared")) {
                    const QString id = o.value(QLatin1String("id")).toString();
                    if (onWindowAppeared && !id.isEmpty())
                        onWindowAppeared(id);
                    return;
                }
                if (type == QLatin1String("moved")) {
                    const QString id = o.value(QLatin1String("id")).toString();
                    const QRect frame = parseFrame(o.value(QLatin1String("frame")).toObject());
                    if (onWindowMoved && !id.isEmpty() && frame.isValid())
                        onWindowMoved(id, frame);
                    return;
                }

                // One-shot apply protocol below.
                if (gen != m_generation)
                    return; // stale script
                if (type == QLatin1String("confirmed")) {
                    QVector<QRect> frames;
                    const QJsonArray arr = o.value(QLatin1String("results")).toArray();
                    for (int i = 0; i < arr.size(); ++i) {
                        const QJsonObject e = arr.at(i).toObject();
                        // Missing entries keep results aligned with requests.
                        if (e.value(QLatin1String("missing")).toBool())
                            frames.append(QRect());
                        else
                            frames.append(parseFrame(e));
                    }
                    evaluate(std::move(frames));
                } else if (type == QLatin1String("error")) {
                    qCWarning(lcKWin)
                        << "script error:" << o.value(QLatin1String("error")).toString();
                }
            },
            this);
        QDBusConnection::sessionBus().registerObject(QLatin1String("/kwin"), reporter,
                                                     QDBusConnection::ExportAllSlots);
    }
}

void KWinHelper::ensureWatcher()
{
    if (!m_wayland || m_watcherReady || m_watcherLoading)
        return;
    m_watcherLoading = true;
    ++m_watchGen;

    QFile f(QStringLiteral(":/kwin/tuxnotes-watch.js"));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcKWin) << "cannot read watcher template";
        m_watcherLoading = false;
        return;
    }
    QString js = renderTemplate(QString::fromUtf8(f.readAll()), m_watchGen);

    auto *iface = new QDBusInterface(QLatin1String("org.kde.KWin"), QLatin1String("/Scripting"),
                                     QLatin1String("org.kde.kwin.Scripting"),
                                     QDBusConnection::sessionBus(), this);
    // Drop any watcher left over from a previous app run first: it would still
    // target our service name and double-report. Also unload watchers from
    // earlier app identities.
    iface->asyncCall(QLatin1String("unloadScript"), kWatcherPlugin);
    iface->asyncCall(QLatin1String("unloadScript"), QStringLiteral("org.colton.TuxNotes.watch"));
    iface->asyncCall(QLatin1String("unloadScript"), QStringLiteral("org.colton.Stickies.watch"));

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    const QString path =
        cacheDir + QStringLiteral("/watch-%1.js").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcKWin) << "cannot write" << path;
            m_watcherLoading = false;
            return;
        }
        out.write(js.toUtf8());
    }

    QDBusPendingCall idCall = iface->asyncCall(QLatin1String("loadScript"), path, kWatcherPlugin);
    auto *watcher = new QDBusPendingCallWatcher(idCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, path](QDBusPendingCallWatcher *call) {
                call->deleteLater();
                m_watcherLoading = false;
                QDBusPendingReply<int> reply = *call;
                if (reply.isError() || reply.value() < 0) {
                    qCWarning(lcKWin) << "watcher loadScript failed";
                    return;
                }
                runLoadedScript(reply.value(), path, /*cleanup=*/false);
            });
}

void KWinHelper::runLoadedScript(int id, const QString &path, bool cleanup)
{
    const QString objPath = QStringLiteral("/Scripting/Script%1").arg(id);
    QDBusInterface(QLatin1String("org.kde.KWin"), objPath, QLatin1String("org.kde.kwin.Script"),
                   QDBusConnection::sessionBus())
        .asyncCall(QLatin1String("run"));

    if (!cleanup)
        return; // the persistent watcher must stay loaded

    // One-shots unload after their internal confirm timer has fired. The script
    // file cannot be deleted earlier — KWin reads it lazily at run().
    QTimer::singleShot(4000, this, [objPath, path] {
        QDBusInterface(QLatin1String("org.kde.KWin"), objPath, QLatin1String("org.kde.kwin.Script"),
                       QDBusConnection::sessionBus())
            .asyncCall(QLatin1String("stop"));
        QFile::remove(path);
    });
}

void KWinHelper::reconcile()
{
    if (!m_wayland)
        return;
    QFile f(QStringLiteral(":/kwin/tuxnotes-reconcile.js"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QString js = renderTemplate(QString::fromUtf8(f.readAll()), m_watchGen);

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    const QString path =
        cacheDir + QStringLiteral("/reconcile-%1.js").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        out.write(js.toUtf8());
    }

    auto *iface = new QDBusInterface(QLatin1String("org.kde.KWin"), QLatin1String("/Scripting"),
                                     QLatin1String("org.kde.kwin.Scripting"),
                                     QDBusConnection::sessionBus(), this);
    const QString pluginName =
        QStringLiteral("org.jhc.TuxNotes.reconcile.%1").arg(QDateTime::currentMSecsSinceEpoch());
    QDBusPendingCall idCall = iface->asyncCall(QLatin1String("loadScript"), path, pluginName);
    auto *watcher = new QDBusPendingCallWatcher(idCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, pluginName, path](QDBusPendingCallWatcher *call) {
                call->deleteLater();
                QDBusPendingReply<int> reply = *call;
                if (reply.isError() || reply.value() < 0) {
                    QFile::remove(path);
                    return;
                }
                // Reconcile is read-only and synchronous — unload immediately after.
                const int id = reply.value();
                const QString objPath = QStringLiteral("/Scripting/Script%1").arg(id);
                QDBusInterface(QLatin1String("org.kde.KWin"), objPath,
                               QLatin1String("org.kde.kwin.Script"),
                               QDBusConnection::sessionBus())
                    .asyncCall(QLatin1String("run"));
                QTimer::singleShot(1500, this, [objPath, pluginName, path] {
                    QDBusInterface(QLatin1String("org.kde.KWin"), objPath,
                                   QLatin1String("org.kde.kwin.Script"),
                                   QDBusConnection::sessionBus())
                        .asyncCall(QLatin1String("stop"));
                    QDBusInterface(QLatin1String("org.kde.KWin"), QLatin1String("/Scripting"),
                                   QLatin1String("org.kde.kwin.Scripting"),
                                   QDBusConnection::sessionBus())
                        .asyncCall(QLatin1String("unloadScript"), pluginName);
                    QFile::remove(path);
                });
            });
}

void KWinHelper::unloadWatcher()
{
    if (!m_wayland)
        return;
    QDBusInterface(QLatin1String("org.kde.KWin"), QLatin1String("/Scripting"),
                   QLatin1String("org.kde.kwin.Scripting"), QDBusConnection::sessionBus())
        .asyncCall(QLatin1String("unloadScript"), kWatcherPlugin);
}

QString KWinHelper::captionForText(const QString &plainText)
{
    const QStringList lines = plainText.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        if (!t.isEmpty()) {
            QString caption = t.simplified();
            if (caption.size() > 48)
                caption = caption.left(47) + QStringLiteral("\u2026");
            return caption;
        }
    }
    return QObject::tr("TuxNote");
}

QString KWinHelper::renderTemplate(const QString &tpl, int generation) const
{
    QString js = tpl;
    js.replace(QStringLiteral("__SERVICE__"), QStringLiteral("org.jhc.TuxNotes"));
    js.replace(QStringLiteral("__APP_ID__"), appId());
    js.replace(QStringLiteral("__GEN__"), QString::number(generation));
    return js;
}

void KWinHelper::applyRequests(const QList<Request> &requests, ResultHandler handler)
{
    if (requests.isEmpty())
        return;
    if (!m_wayland) {
        // X11: callers position windows natively; nothing to do here.
        if (handler)
            handler({}, true);
        return;
    }

    ++m_generation;
    m_requests = requests;
    m_expectedFrames.clear();
    for (const Request &r : requests)
        m_expectedFrames.append(r.maximizeArea ? QRect() : r.frame);
    m_handler = std::move(handler);
    m_attempt = 0;
    m_lastFrames.clear();

    QFile f(QStringLiteral(":/kwin/tuxnotes-apply.js"));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcKWin) << "cannot read apply template";
        finish(m_generation, {}, false);
        return;
    }
    QString js = renderTemplate(QString::fromUtf8(f.readAll()), m_generation);

    QJsonArray targets;
    for (const Request &r : std::as_const(m_requests)) {
        QJsonObject t;
        t.insert(QLatin1String("id"), r.kwinId);
        QJsonObject frame;
        frame.insert(QLatin1String("x"), r.frame.x());
        frame.insert(QLatin1String("y"), r.frame.y());
        frame.insert(QLatin1String("width"), r.frame.width());
        frame.insert(QLatin1String("height"), r.frame.height());
        t.insert(QLatin1String("frame"), frame);
        if (r.maximizeArea)
            t.insert(QLatin1String("maximizeArea"), true);
        if (r.keepAbove.has_value())
            t.insert(QLatin1String("keepAbove"), r.keepAbove.value());
        if (r.opacity.has_value())
            t.insert(QLatin1String("opacity"), r.opacity.value());
        targets.append(t);
    }
    js.replace(QStringLiteral("__GEN__"), QString::number(m_generation));
    js.replace(QStringLiteral("__TARGETS__"),
               QString::fromUtf8(QJsonDocument(targets).toJson(QJsonDocument::Compact)));
    js.replace(QStringLiteral("__CONFIRM_MS__"), QString::number(kConfirmMs));
    m_pendingJs = js;

    const int generation = m_generation;
    QTimer::singleShot(kGiveUpAfterMs, this, [this, generation] {
        if (generation == m_generation && !m_requests.isEmpty()) {
            qCWarning(lcKWin) << "giving up after" << m_attempt << "attempts";
            finish(generation, m_lastFrames, false);
        }
    });

    QTimer::singleShot(kFirstAttemptDelayMs, this, [this, generation] {
        if (generation != m_generation || m_requests.isEmpty())
            return;
        attempt(m_pendingJs);
    });
}

void KWinHelper::attempt(const QString &js)
{
    ++m_attempt;
    runScript(js);
}

void KWinHelper::evaluate(QVector<QRect> frames)
{
    m_lastFrames = frames;

    bool allSatisfied = true;
    for (int i = 0; i < m_requests.size(); ++i) {
        const QRect want = m_expectedFrames.at(i);
        const QRect got = i < frames.size() ? frames.at(i) : QRect();
        if (!want.isValid()) {
            allSatisfied = allSatisfied && got.isValid();
            continue;
        }
        if (!got.isValid() || qAbs(got.x() - want.x()) > kTolerance
            || qAbs(got.y() - want.y()) > kTolerance
            || qAbs(got.width() - want.width()) > kTolerance
            || qAbs(got.height() - want.height()) > kTolerance) {
            allSatisfied = false;
            break;
        }
    }

    if (allSatisfied) {
        finish(m_generation, frames, true);
        return;
    }

    const int generation = m_generation;
    const QString js = m_pendingJs;
    if (m_attempt < kMaxAttempts)
        QTimer::singleShot(kRetryDelayMs, this, [this, generation, js] {
            if (generation == m_generation && !m_requests.isEmpty())
                attempt(js);
        });
    // else: the give-up timer fires eventually
}
void KWinHelper::runScript(const QString &js)
{
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    const QString path =
        cacheDir + QStringLiteral("/apply-%1.js").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcKWin) << "cannot write" << path;
            finish(m_generation, {}, false);
            return;
        }
        f.write(js.toUtf8());
    }

    auto *iface = new QDBusInterface(QLatin1String("org.kde.KWin"), QLatin1String("/Scripting"),
                                     QLatin1String("org.kde.kwin.Scripting"),
                                     QDBusConnection::sessionBus(), this);
    const QString pluginName =
        QStringLiteral("org.jhc.TuxNotes.apply.%1").arg(QDateTime::currentMSecsSinceEpoch());
    QDBusPendingCall idCall = iface->asyncCall(QLatin1String("loadScript"), path, pluginName);
    auto *watcher = new QDBusPendingCallWatcher(idCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, pluginName, path](QDBusPendingCallWatcher *call) {
                call->deleteLater();
                // NOTE: never delete the script file before run() — KWin reads it lazily.
                QDBusPendingReply<int> reply = *call;
                if (reply.isError() || reply.value() < 0) {
                    qCWarning(lcKWin) << "loadScript failed — KWin scripting unavailable";
                    finish(m_generation, {}, false);
                    return;
                }
                runLoadedScript(reply.value(), path, /*cleanup=*/true);
            });
}

int KWinHelper::msSinceLastBatch() const
{
    if (!m_lastFinishTimer.isValid())
        return std::numeric_limits<int>::max();
    return static_cast<int>(m_lastFinishTimer.elapsed());
}

void KWinHelper::finish(int generation, const QVector<QRect> &frames, bool ok)
{
    if (generation != m_generation || m_requests.isEmpty())
        return; // stale report
    m_requests.clear();
    m_lastFinishTimer.start();
    ++m_generation; // invalidate pending timers
    if (m_handler) {
        ResultHandler h;
        h.swap(m_handler);
        h(frames, ok);
    }
}

#include "kwinhelper.moc"
