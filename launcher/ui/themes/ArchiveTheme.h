// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include "DarkTheme.h"

class ArchiveTheme : public DarkTheme {
public:
    QString id() override { return "archive"; }
    QString name() override { return QObject::tr("Stellar"); }
    QString tooltip() override { return QObject::tr("Black and grey surfaces with violet and blue accents."); }
    QPalette colorScheme() override;
    bool hasStyleSheet() override { return true; }
    QString appStyleSheet() override;
};
