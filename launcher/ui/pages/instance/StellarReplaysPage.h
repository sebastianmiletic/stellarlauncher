// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QSet>
#include <QWidget>

#include "tasks/Task.h"
#include "ui/pages/BasePage.h"

class QFileSystemWatcher;
class QLabel;
class QListWidget;
class MinecraftInstance;
class QPushButton;

class StellarReplaysPage final : public QWidget, public BasePage {
public:
    explicit StellarReplaysPage(MinecraftInstance* instance);
    QString id() const override { return "stellar-replays"; }
    QString displayName() const override { return tr("Replays"); }
    QIcon icon() const override { return QIcon::fromTheme("screenshots"); }
    void openedImpl() override;
    void closedImpl() override;

private:
    void refresh();
    void updateButtons();
    void installFlashback();
    void playReplay();
    QString selectedReplay() const;

    MinecraftInstance* m_instance;
    QListWidget* m_replays;
    QLabel* m_status;
    QPushButton* m_watch;
    QPushButton* m_rename;
    QPushButton* m_remove;
    QPushButton* m_install;
    QFileSystemWatcher* m_files;
    Task::Ptr m_task;
    QSet<QString> m_installed;
    bool m_checking = false;
};
