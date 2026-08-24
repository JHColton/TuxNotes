#include "application.h"

#include "notewindow.h"
#include "theme.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWindow>
#include <QTimer>

namespace {
constexpr int kFlushDelayMs = 600;
constexpr QSize kDefaultNoteSize(200, 300);

QString welcomeHtml()
{
    return QStringLiteral(
        "<p><b>Welcome to TuxNotes!</b></p>"
        "<p>&bull; Drag the title bar to move me around<br/>"
        "&bull; Resize from any edge or the corner grip<br/>"
        "&bull; Double-click the title bar to roll me up<br/>"
        "&bull; Right-click anywhere for options &mdash; try changing my color</p>"
        "<p>Everything saves itself automatically. Enjoy!</p>");
}

bool isWayland()
{
    return QGuiApplication::platformName() == QLatin1String("wayland");
}
} // namespace

TuxNotesApplication::TuxNotesApplication(QObject *parent)
    : QObject(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kFlushDelayMs);
    connect(m_flushTimer, &QTimer::timeout, this, [this] { flush(); });

    // Coalesce all placement/flag changes into one scripted batch.
    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(30);
    connect(m_applyTimer, &QTimer::timeout, this, [this] { applyTick(); });

    // Self-heal missed windowAdded events while any note awaits correlation.
    m_reconcileTimer = new QTimer(this);
    m_reconcileTimer->setSingleShot(true);
    m_reconcileTimer->setInterval(2500);
    connect(m_reconcileTimer, &QTimer::timeout, this, [this] {
        if (m_pendingCorrelation.isEmpty())
            return;
        m_helper.reconcile();
        m_reconcileTimer->start(); // keep checking until the queue drains
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        flush();
        m_helper.unloadWatcher();
    });

    // Watcher events: correlate newly mapped windows (FIFO against creation
    // order) and track live drags for crash-safe persistence.
    m_helper.onWindowAppeared = [this](const QString &kwinId) {
        if (m_pendingCorrelation.isEmpty())
            return;
        // Ignore duplicates (e.g. a stale watcher instance re-reporting).
        for (const auto &ptr : std::as_const(m_windows)) {
            if (ptr && ptr->property("kwinId").toString() == kwinId)
                return;
        }
        NoteWindow *w = m_pendingCorrelation.takeFirst();
        if (!w)
            return;
        w->setProperty("kwinId", kwinId);
        scheduleApply();
        if (!m_pendingCorrelation.isEmpty())
            m_reconcileTimer->start();
    };
    m_helper.onWindowMoved = [this](const QString &kwinId, const QRect &frame) {
        // Ignore echoes of our own batch writes: they fire via the watcher's
        // debounce right after placement and would feed compositor-chosen or
        // mid-transition geometry back into the saved state.
        if (m_helper.batchActive() || m_helper.msSinceLastBatch() < 700)
            return;
        for (const auto &ptr : std::as_const(m_windows)) {
            if (ptr && ptr->property("kwinId").toString() == kwinId) {
                syncFrame(ptr.data(), frame);
                scheduleFlush();
                return;
            }
        }
    };
}

void TuxNotesApplication::scheduleApply()
{
    m_applyTimer->start();
}

void TuxNotesApplication::applyTick()
{
    if (!m_helper.isWayland())
        return;

    QList<QPointer<NoteWindow>> targets;
    QList<KWinHelper::Request> requests;
    for (const auto &ptr : std::as_const(m_windows)) {
        if (!ptr || ptr->property("kwinId").isNull())
            continue;
        KWinHelper::Request r;
        r.kwinId = ptr->property("kwinId").toString();
        const QVariant zoom = ptr->property("zoomTarget");
        if (zoom.isValid()) {
            const QRect zr = zoom.toRect();
            if (zr.isNull())
                r.maximizeArea = true; // panel-aware fullscreen fill (Wayland)
            else
                r.frame = zr;
        } else {
            r.frame = ptr->desiredFrame();
        }
        r.keepAbove = ptr->isFloating();
        r.opacity = ptr->isTranslucent() ? std::optional<double>(0.8) : std::optional<double>(1.0);
        targets.append(ptr);
        requests.append(r);
    }
    if (requests.isEmpty())
        return;

    m_helper.applyRequests(requests, [this, targets](const QVector<QRect> &confirmed, bool ok) {
        Q_UNUSED(ok);
        for (int i = 0; i < targets.size(); ++i) {
            NoteWindow *w = targets.at(i).data();
            if (!w)
                continue;
            QRect f = i < confirmed.size() ? confirmed.at(i) : QRect();
            if (!f.isValid())
                f = w->desiredFrame(); // best effort
            if (w->property("zoomTarget").isValid())
                w->setProperty("zoomTarget", QVariant()); // zoom applied
            syncFrame(w, f);
            w->revealAt(f);
        }
        if (m_focusAfterReveal && m_focusAfterReveal->isVisible()) {
            m_focusAfterReveal->raise();
            m_focusAfterReveal->activateWindow();
            m_focusAfterReveal->setFocus();
            m_focusAfterReveal.clear();
        }
        scheduleFlush();
    });
}

void TuxNotesApplication::start(bool createNew)
{
    m_store.load();

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(QStringLiteral("org.jhc.TuxNotes"));
    bus.registerObject(QStringLiteral("/tuxnotes"), this, QDBusConnection::ExportAllSlots);

    m_helper.ensureWatcher();

    if (!m_store.loaded()) {
        // First run: create the classic welcome note; restoreNotes() shows it.
        Note n;
        n.id = QUuid::createUuid();
        n.colorIndex = 0;
        n.html = welcomeHtml();
        const QRect workArea = QGuiApplication::primaryScreen()->availableGeometry();
        n.frame = QRect(workArea.x() + 60, workArea.y() + 80, kDefaultNoteSize.width() + 20,
                        kDefaultNoteSize.height());
        m_store.upsert(n);
        m_store.saveNow();
    }

    restoreNotes();

    // Desktop "New Note" action / --new-note on a cold start.
    if (createNew)
        NewNote();
}

void TuxNotesApplication::NewNote()
{
    Note note;
    note.id = QUuid::createUuid();
    note.colorIndex = 0;
    QList<QRect> areas;
    for (QScreen *s : QGuiApplication::screens())
        areas << s->availableGeometry();
    const QPoint pos = m_store.takeCascadePosition(kDefaultNoteSize, areas);
    note.frame = QRect(pos, kDefaultNoteSize);

    createWindow(note);

    // Focus the newest note once it lands.
    if (!m_windows.isEmpty()) {
        m_focusAfterReveal = m_windows.last();
        if (!m_helper.isWayland()) {
            m_focusAfterReveal->raise();
            m_focusAfterReveal->activateWindow();
            m_focusAfterReveal->setFocus();
            m_focusAfterReveal.clear();
        }
    }
}

void TuxNotesApplication::Quit()
{
    flush();
    qApp->quit();
}

void TuxNotesApplication::restoreNotes()
{
    const auto notes = m_store.notes();
    for (const Note &n : notes) {
        Note note = n;

        // Clamp restored frames onto a connected screen.
        bool onScreen = false;
        const QPoint center = note.frame.center();
        for (QScreen *s : QGuiApplication::screens()) {
            if (s->geometry().contains(center))
                onScreen = true;
        }
        if (!onScreen) {
            const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
            note.frame.moveTopLeft(area.topLeft() + QPoint(30, 30));
        }

        createWindow(note);
    }
}

void TuxNotesApplication::createWindow(const Note &note)
{
    auto *w = new NoteWindow();
    w->loadNote(note);

    connect(w, &NoteWindow::changed, this, [this](NoteWindow *self) {
        m_store.upsert(self->toNote());
        scheduleFlush();
    });
    connect(w, &NoteWindow::deleteRequested, this, [this](NoteWindow *self) {
        m_store.remove(self->property("noteId").toUuid());
        m_pendingCorrelation.removeAll(QPointer<NoteWindow>(self));
        m_windows.removeAll(QPointer<NoteWindow>(self));
        self->deleteLater();
        scheduleFlush();
        if (m_windows.isEmpty())
            Quit();
    });
    connect(w, &NoteWindow::newNoteRequested, this, [this] {
        if (!m_helper.watcherReady())
            m_helper.ensureWatcher();
        NewNote();
    });
    connect(w, &NoteWindow::windowFlagsChanged, this, [this](NoteWindow *self) {
        Q_UNUSED(self);
        scheduleApply();
    });
    connect(w, &NoteWindow::frameChangeRequested, this, [this](NoteWindow *self, QRect frame) {
        if (isWayland()) {
            self->setProperty("zoomTarget", frame); // null rect = maximize area
            scheduleApply();
        } else {
            QRect target = frame;
            if (target.isNull()) {
                QScreen *screen = self->screen()
                                      ? self->screen()
                                      : QGuiApplication::primaryScreen();
                target = screen->availableGeometry();
            }
            self->setGeometry(target);
            syncFrame(self, target);
            scheduleFlush();
        }
    });

    w->setProperty("noteId", note.id);
    m_windows.append(w);
    if (isWayland()) {
        m_pendingCorrelation.append(w); // placed when the watcher reports it
        m_reconcileTimer->start();      // safety net for missed events
    }

    if (isWayland()) {
        // Position can only be corrected after mapping via the KWin helper;
        // the window may briefly appear at the compositor-chosen spot.
        w->show();
    } else {
        w->winId(); // force native window handle
        w->windowHandle()->setFramePosition(note.frame.topLeft());
        w->show();
        syncFrame(w, note.frame);
    }
}

void TuxNotesApplication::syncFrame(NoteWindow *w, const QRect &globalFrame)
{
    w->setSavedFrame(globalFrame);
    m_store.upsert(w->toNote());
    m_store.setCascadePoint(globalFrame.topLeft());
}

void TuxNotesApplication::scheduleFlush()
{
    m_flushTimer->start();
}

void TuxNotesApplication::flush()
{
    for (const auto &ptr : std::as_const(m_windows)) {
        if (ptr)
            m_store.upsert(ptr->toNote());
    }
    m_store.saveNow();
}
