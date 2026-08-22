#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QVector>

struct NoteTheme
{
    QString name;
    QColor body;
    QColor titleBar;

    QColor border() const { return titleBar.darker(112); }
    QColor grip() const { return titleBar.darker(140); }
};

namespace Theme
{

inline const QVector<NoteTheme> &themes()
{
    static const QVector<NoteTheme> t = {
        {QLatin1String("Yellow"), QColor(0xFE, 0xF4, 0x9C), QColor(0xFE, 0xEA, 0x3D)},
        {QLatin1String("Blue"), QColor(0xAD, 0xF4, 0xFF), QColor(0x89, 0xF0, 0xFF)},
        {QLatin1String("Green"), QColor(0xB2, 0xFF, 0xA0), QColor(0x83, 0xFE, 0x83)},
        {QLatin1String("Pink"), QColor(0xFF, 0xC7, 0xC7), QColor(0xFF, 0xB2, 0xB2)},
        {QLatin1String("Purple"), QColor(0xB6, 0xCA, 0xFF), QColor(0x9B, 0xB6, 0xFE)},
        {QLatin1String("Gray"), QColor(0xEE, 0xEE, 0xEE), QColor(0xDA, 0xDA, 0xDA)},
    };
    return t;
}

inline const NoteTheme &theme(int index)
{
    const auto &all = themes();
    return all[qBound(0, index, all.size() - 1)];
}

inline QFont noteFont()
{
    static QFont cached = [] {
        QFont f(QStringLiteral("Helvetica"), 12);
        f.setStyleHint(QFont::SansSerif);
        return f;
    }();
    return cached;
}

} // namespace Theme
