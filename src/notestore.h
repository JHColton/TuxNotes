#pragma once

#include "note.h"

#include <QList>
#include <QObject>

class NoteStore : public QObject
{
    Q_OBJECT

public:
    explicit NoteStore(QObject *parent = nullptr);

    const QList<Note> &notes() const { return m_notes; }
    bool isEmpty() const { return m_notes.isEmpty(); }
    bool loaded() const { return m_loaded; }

    void load();
    void saveNow();

    void upsert(const Note &note);
    void remove(const QUuid &id);

    QPoint takeCascadePosition(const QSize &size, const QList<QRect> &workAreas);
    void setCascadePoint(const QPoint &p) { m_cascade = p; }

private:
    static QString filePath();
    static void migrateLegacyDataDir();

    QList<Note> m_notes;
    QPoint m_cascade{-1, -1};
    bool m_loaded = false;
};
