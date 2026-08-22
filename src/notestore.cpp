#include "notestore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcStore, "tuxnotes.store", QtWarningMsg)

NoteStore::NoteStore(QObject *parent)
    : QObject(parent)
{
}

QString NoteStore::filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return dir + QLatin1String("/notes.json");
}

// Legacy data directories from earlier identities; migrate once.
void NoteStore::migrateLegacyDataDir()
{
    const QString newPath =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (QFileInfo::exists(newPath + QLatin1String("/notes.json")))
        return;
    QDir parent(newPath);
    if (parent.cdUp()) {
        for (const QString &legacyName : {QStringLiteral("Stickies"),
                                          QStringLiteral("org.colton.Stickies"),
                                          QStringLiteral("org.colton.TuxNotes")}) {
            const QString legacy = parent.filePath(legacyName);
            if (QFileInfo::exists(legacy + QLatin1String("/notes.json"))
                && !QDir(newPath).exists()) {
                QDir(parent.absolutePath()).rename(legacyName,
                                                   QFileInfo(newPath).fileName());
                break;
            }
        }
    }
}

void NoteStore::load()
{
    migrateLegacyDataDir();
    m_notes.clear();

    QFile file(filePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject root = doc.object();
        m_cascade = QPoint(root.value(QLatin1String("cascadeX")).toInt(-1),
                           root.value(QLatin1String("cascadeY")).toInt(-1));
        const QJsonArray arr = root.value(QLatin1String("notes")).toArray();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            Note n;
            n.id = QUuid(o.value(QLatin1String("id")).toString());
            if (n.id.isNull())
                continue;
            n.colorIndex = o.value(QLatin1String("color")).toInt(0);
            n.html = o.value(QLatin1String("html")).toString();
            const QJsonArray f = o.value(QLatin1String("frame")).toArray();
            if (f.size() == 4)
                n.frame = QRect(f.at(0).toInt(), f.at(1).toInt(), f.at(2).toInt(), f.at(3).toInt());
            n.collapsed = o.value(QLatin1String("collapsed")).toBool(false);
            n.floating = o.value(QLatin1String("floating")).toBool(false);
            n.translucent = o.value(QLatin1String("translucent")).toBool(false);
            n.created = QDateTime::fromString(o.value(QLatin1String("created")).toString(),
                                              Qt::ISODateWithMs);
            n.modified = QDateTime::fromString(o.value(QLatin1String("modified")).toString(),
                                               Qt::ISODateWithMs);
            if (!n.created.isValid())
                n.created = QDateTime::currentDateTime();
            if (!n.modified.isValid())
                n.modified = n.created;
            m_notes.append(n);
        }
        m_loaded = true;
    }
    qCDebug(lcStore) << "loaded" << m_notes.size() << "notes";
}

void NoteStore::saveNow()
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));

    QJsonArray arr;
    for (const Note &n : std::as_const(m_notes)) {
        QJsonObject o;
        o.insert(QLatin1String("id"), n.id.toString());
        o.insert(QLatin1String("color"), n.colorIndex);
        o.insert(QLatin1String("html"), n.html);
        QJsonArray f;
        f.append(n.frame.x());
        f.append(n.frame.y());
        f.append(n.frame.width());
        f.append(n.frame.height());
        o.insert(QLatin1String("frame"), f);
        o.insert(QLatin1String("collapsed"), n.collapsed);
        o.insert(QLatin1String("floating"), n.floating);
        o.insert(QLatin1String("translucent"), n.translucent);
        o.insert(QLatin1String("created"), n.created.toString(Qt::ISODateWithMs));
        o.insert(QLatin1String("modified"), n.modified.toString(Qt::ISODateWithMs));
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("cascadeX"), m_cascade.x());
    root.insert(QLatin1String("cascadeY"), m_cascade.y());
    root.insert(QLatin1String("notes"), arr);

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcStore) << "cannot open" << filePath() << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qCWarning(lcStore) << "commit failed:" << file.errorString();
}

void NoteStore::upsert(const Note &note)
{
    for (Note &n : m_notes) {
        if (n.id == note.id) {
            n = note;
            return;
        }
    }
    m_notes.append(note);
}

void NoteStore::remove(const QUuid &id)
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).id == id) {
            m_notes.removeAt(i);
            return;
        }
    }
}

QPoint NoteStore::takeCascadePosition(const QSize &size, const QList<QRect> &areas)
{
    if (areas.isEmpty())
        return QPoint(20, 20);

    if (m_cascade.x() < 0) {
        const QRect &a = areas.first();
        m_cascade = a.topLeft() + QPoint(qMax(0, (a.width() - size.width()) / 2),
                                         qMax(24, a.height() / 4));
    } else {
        m_cascade += QPoint(16, 16);
    }

    // Host the note on the area containing the cascade point; otherwise the
    // area whose center is nearest (multi-monitor aware).
    const QRect *host = &areas.first();
    qint64 bestDist = -1;
    for (const QRect &a : areas) {
        if (a.contains(m_cascade)) {
            host = &a;
            break;
        }
        const QPoint c = a.center();
        const qint64 dx = c.x() - m_cascade.x();
        const qint64 dy = c.y() - m_cascade.y();
        const qint64 d = dx * dx + dy * dy;
        if (bestDist < 0 || d < bestDist) {
            bestDist = d;
            host = &a;
        }
    }

    m_cascade.setX(qBound(host->left(), m_cascade.x(),
                          qMax(host->left(), host->right() + 1 - size.width())));
    m_cascade.setY(qBound(host->top(), m_cascade.y(),
                          qMax(host->top(), host->bottom() + 1 - size.height())));
    return m_cascade;
}
