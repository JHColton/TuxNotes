#pragma once

#include <QList>
#include <QObject>
#include <QElapsedTimer>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

#include <optional>

class QTimer;

// Bridges this app to KWin via one-shot and persistent scripting so notes can
// be positioned exactly, kept above, made translucent and tracked while dragged
// on Wayland (impossible from a client under xdg-shell). Falls back gracefully.
class KWinHelper : public QObject
{
    Q_OBJECT

public:
    struct Request
    {
        QString kwinId; // KWin Window::internalId as a string
        QRect frame;
        bool maximizeArea = false; // fill the compositor's maximize area (panel-aware)
        std::optional<bool> keepAbove;
        std::optional<double> opacity; // 0.0 .. 1.0
    };    // confirmedFrames is aligned with the request list.
    using ResultHandler = std::function<void(const QVector<QRect> &confirmedFrames, bool ok)>;

    // Emitted when the watcher sees one of our windows map. `frame` is its
    // current compositor-assigned geometry.
    std::function<void(const QString &kwinId)> onWindowAppeared;
    std::function<void(const QString &kwinId, const QRect &frame)> onWindowMoved;

    explicit KWinHelper(QObject *parent = nullptr);

    bool isWayland() const { return m_wayland; }
    bool watcherReady() const { return m_watcherReady; }
    bool batchActive() const { return !m_requests.isEmpty(); }
    int msSinceLastBatch() const; // large when no batch has finished recently

    void ensureWatcher();
    void unloadWatcher();
    void reconcile(); // re-enumerate our windows (self-heal missed events)

    // Asynchronously applies frame/keepAbove/opacity for each request, matching
    // windows by internalId. Retries until confirmed or given up.
    void applyRequests(const QList<Request> &requests, ResultHandler handler);

    static QString captionForText(const QString &plainText);

private:
    void attempt(const QString &js);
    void evaluate(QVector<QRect> frames);
    void finish(int generation, const QVector<QRect> &frames, bool ok);
    void runScript(const QString &js);
    void runLoadedScript(int id, const QString &path, bool cleanup);
    QString renderTemplate(const QString &tpl, int generation) const;

    bool m_wayland = false;
    int m_generation = 0; // apply-request generations
    int m_attempt = 0;
    QList<Request> m_requests;
    QVector<QRect> m_expectedFrames; // invalid entry = no position expectation
    QVector<QRect> m_lastFrames;
    ResultHandler m_handler;
    QString m_pendingJs;

    QElapsedTimer m_lastFinishTimer;

    int m_watchGen = 0;
    bool m_watcherReady = false;
    bool m_watcherLoading = false;
};
