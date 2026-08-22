#pragma once

#include <QDateTime>
#include <QRect>
#include <QString>
#include <QUuid>

struct Note
{
    QUuid id;
    int colorIndex = 0;
    QString html;
    QRect frame{200, 200, 200, 300};
    bool collapsed = false;
    bool floating = false;
    bool translucent = false;
    QDateTime created = QDateTime::currentDateTime();
    QDateTime modified = QDateTime::currentDateTime();
};
