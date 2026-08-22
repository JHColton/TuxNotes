#pragma once

#include "kwinhelper.h"
#include "notestore.h"

#include <QObject>
#include <QPointer>

class QTimer;
class NoteWindow;

class TuxNotesApplication : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jhc.TuxNotes")

public:
    explicit TuxNotesApplication(QObject *parent = nullptr);

    void start();

public slots:
    void NewNote();
    void Quit();

private:
    void restoreNotes();
    void createWindow(const Note &note);
    void scheduleApply();
    void applyTick();
    void scheduleFlush();
    void flush();
    void syncFrame(NoteWindow *w, const QRect &globalFrame);

    NoteStore m_store;
    KWinHelper m_helper;
    QList<QPointer<NoteWindow>> m_windows;
    QList<QPointer<NoteWindow>> m_pendingCorrelation; // FIFO awaiting KWin ids
    QTimer *m_flushTimer = nullptr;
    QTimer *m_applyTimer = nullptr;
    QTimer *m_reconcileTimer = nullptr;
    QPointer<NoteWindow> m_focusAfterReveal;
};
