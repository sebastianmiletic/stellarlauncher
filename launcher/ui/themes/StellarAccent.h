// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QString>

inline QColor stellarAccent(const QString& id)
{
    if (id == "violet")
        return QColor("#c5adfb");
    if (id == "ice")
        return QColor("#a5d5ef");
    if (id == "amber")
        return QColor("#e8bd82");
    return QColor("#eaeaea");
}
