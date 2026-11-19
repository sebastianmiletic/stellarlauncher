// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

#include "Application.h"

namespace StellarLaunchProgress {
inline QString fileFor(const QString& ticket)
{
    const auto id = QUuid::fromString(ticket);
    if (id.isNull())
        return {};
    return QDir(APPLICATION->dataRoot()).filePath("launch-progress/" + id.toString(QUuid::WithoutBraces).toUpper());
}

inline void write(const QString& file, const QByteArray& state)
{
    if (file.isEmpty() || !QFileInfo::exists(file))
        return;
    QSaveFile update(file);
    if (update.open(QIODevice::WriteOnly) && update.write(state) == state.size())
        update.commit();
}
}
