// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *  Copyright (C) 2023 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "InstanceWindow.h"
#include "Application.h"

#include <QAccessibleWidget>
#include <QCloseEvent>
#include <QTableView>
#include <QTreeView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>

#include "ui/widgets/PageContainer.h"
#include "ui/StellarGallery.h"

#include "InstancePageProvider.h"
#include "minecraft/PackProfile.h"
#include "minecraft/StellarClient.h"

#include "icons/IconList.h"

#if defined(Q_OS_MACOS) && QT_VERSION < QT_VERSION_CHECK(6, 12, 1)
namespace {
// QTBUG-149612: the Cocoa table bridge can free the interface it is querying.
// On affected versions expose editor tables as keyboard-operable panes.
class EditorTableAccessibility final : public QAccessibleWidget {
public:
    explicit EditorTableAccessibility(QAbstractItemView* view) : QAccessibleWidget(view, QAccessible::Pane) {}

    QString text(QAccessible::Text type) const override
    {
        auto* view = static_cast<QAbstractItemView*>(widget());
        if (type == QAccessible::Name && view->accessibleName().isEmpty())
            return QObject::tr("Editor items");
        if (type == QAccessible::Value) {
            const auto current = view->currentIndex();
            QStringList fields;
            if (current.isValid()) {
                for (int i = 0; i < current.model()->columnCount(current.parent()); ++i) {
                    const auto label = current.siblingAtColumn(i).data().toString();
                    if (!label.isEmpty())
                        fields.append(label);
                }
            }
            return fields.join(", ");
        }
        return QAccessibleWidget::text(type);
    }
};

QAccessibleInterface* editorTableAccessibility(const QString&, QObject* object)
{
    auto* view = qobject_cast<QAbstractItemView*>(object);
    if (!view || (!qobject_cast<QTreeView*>(view) && !qobject_cast<QTableView*>(view)))
        return nullptr;
    for (auto* parent = view->parentWidget(); parent; parent = parent->parentWidget()) {
        if (qobject_cast<InstanceWindow*>(parent))
            return new EditorTableAccessibility(view);
    }
    return nullptr;
}
}
#endif

InstanceWindow::InstanceWindow(MinecraftInstance* instance, const QString& page, QWidget* parent) : QMainWindow(nullptr), m_instance(instance)
{
#if defined(Q_OS_MACOS) && QT_VERSION < QT_VERSION_CHECK(6, 12, 1)
    static const auto installed = [] {
        QAccessible::installFactory(editorTableAccessibility);
        return true;
    }();
    Q_UNUSED(installed);
#endif
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::FramelessWindowHint);

    auto icon = APPLICATION->icons()->getIcon(m_instance->iconKey());
    QString windowTitle = m_instance->name();

    // Set window properties
    {
        setWindowIcon(icon);
        setWindowTitle(windowTitle);
    }

    // Add page container
    {
        auto provider = std::make_shared<InstancePageProvider>(m_instance);
        m_container = new PageContainer(provider.get(), page.isEmpty() ? "version" : page, this, true);
        m_container->setParentContainer(this);
        m_container->useStellarSidebar();
        auto* editor = new QWidget(this);
        editor->setObjectName("stellarEditor");
        auto* layout = new QVBoxLayout(editor);
        layout->setContentsMargins(14, 10, 14, 10);
        layout->setSpacing(10);
        auto* header = new QHBoxLayout;
        auto* name = new QLabel(m_instance->name(), editor);
        name->setObjectName("stellarEditorTitle");
        header->addWidget(name, 1);
        m_wallpaperButton = new QPushButton(tr("Wallpaper"), editor);
        m_loaderButton = new QPushButton(editor);
        updateLoaderLabel();
        connect(m_loaderButton, &QPushButton::clicked, this, [this] {
            if (auto* versions = dynamic_cast<VersionPage*>(m_container->getPage("version"))) {
                m_container->selectPage("version");
                versions->chooseLoader();
                updateLoaderLabel();
            }
        });
        auto* close = new QPushButton(QStringLiteral("×"), editor);
        close->setAccessibleName(tr("Close editor"));
        close->setFixedWidth(34);
        for (auto* button : {m_wallpaperButton, m_loaderButton, close})
            header->addWidget(button);
        m_wallpaperButton->hide();
        connect(close, &QPushButton::clicked, this, &InstanceWindow::close);
        for (const auto& key : {QKeySequence(Qt::Key_Escape), QKeySequence(QKeySequence::Close)}) {
            auto* shortcut = new QShortcut(key, this);
            connect(shortcut, &QShortcut::activated, this, &InstanceWindow::close);
        }
        layout->addLayout(header);
        layout->addWidget(m_container, 1);
        setCentralWidget(editor);
        setStyleSheet(QStringLiteral(
            "QWidget#stellarEditor { background: #141416; }"
            "QLabel#stellarEditorTitle { font-size: 17px; font-weight: 600; }"
            "QListView#stellarEditorSidebar { background: #1c1c20; border: 1px solid #37363d; border-radius: 9px; padding: 4px; }"
            "QListView#stellarEditorSidebar::item { min-height: 24px; padding: 2px 7px; border-radius: 5px; }"
            "QListView#stellarEditorSidebar::item:selected { background: #383541; color: #f0eff3; }"
            "QPushButton, QToolButton { padding: 5px 8px; border-radius: 6px; border: 1px solid #414049; background: #24242a; }"
            "QPushButton:hover, QToolButton:hover { background: #33323a; }"
            "QPushButton:disabled, QToolButton:disabled { color: #93919a; border-color: #303036; }"
            "QPushButton:focus, QToolButton:focus { border-color: #b6aace; }"
        ));

    }

    // Add custom buttons to the page container layout.
    {
        auto horizontalLayout = new QHBoxLayout;
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalLayout->setContentsMargins(0, 0, 6, 6);

        auto btnHelp = new QPushButton(this);
        btnHelp->setText(tr("Help"));
        horizontalLayout->addWidget(btnHelp);
        connect(btnHelp, &QPushButton::clicked, m_container, &PageContainer::help);

        auto spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        horizontalLayout->addSpacerItem(spacer);

        m_launchButton = new QToolButton(this);
        m_launchButton->setText(tr("&Launch"));
        m_launchButton->setToolTip(tr("Launch the instance"));
        m_launchButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_launchButton->setMinimumWidth(80);  // HACK!!
        horizontalLayout->addWidget(m_launchButton);
        connect(m_launchButton, &QPushButton::clicked, this, [this] { APPLICATION->launch(m_instance); });

        m_restartButton = new QPushButton(this);
        m_restartButton->setText(tr("&Restart"));
        m_restartButton->setToolTip(tr("Restart the running instance"));
        horizontalLayout->addWidget(m_restartButton);
        connect(m_restartButton, &QPushButton::clicked, this, &InstanceWindow::restartInstance);

        m_restartButton->hide();

        m_killButton = new QPushButton(this);
        m_killButton->setText(tr("Stop Minecraft"));
        m_killButton->setToolTip(tr("Kill the running instance"));
        m_killButton->setShortcut(QKeySequence(tr("Ctrl+K")));
        horizontalLayout->addWidget(m_killButton);
        connect(m_killButton, &QPushButton::clicked, this, [this] { APPLICATION->kill(m_instance); });

        updateButtons();

        m_container->addButtons(horizontalLayout);

        connect(m_instance, &BaseInstance::profilerChanged, this, &InstanceWindow::updateButtons);
        connect(APPLICATION, &Application::globalSettingsApplied, this, &InstanceWindow::updateButtons);
    }

    // restore window state
    {
        auto base64State = APPLICATION->settings()->get("ConsoleWindowState").toString().toUtf8();
        restoreState(QByteArray::fromBase64(base64State));
        auto base64Geometry = APPLICATION->settings()->get("ConsoleWindowGeometry").toString().toUtf8();
        restoreGeometry(QByteArray::fromBase64(base64Geometry));
        setFixedSize(StellarGallery::windowSize);
        QPainterPath outline;
        outline.addRoundedRect(rect(), StellarGallery::cornerRadius, StellarGallery::cornerRadius);
        setMask(QRegion(outline.toFillPolygon().toPolygon()));
    }

    // set up instance and launch process recognition
    {
        auto launchTask = m_instance->getLaunchTask();
        instanceLaunchTaskChanged(launchTask);
        connect(m_instance, &BaseInstance::launchTaskChanged, this, &InstanceWindow::instanceLaunchTaskChanged);
        connect(m_instance, &BaseInstance::runningStatusChanged, this, &InstanceWindow::runningStateChanged);
    }

    // set up instance destruction detection
    {
        connect(m_instance, &BaseInstance::statusChanged, this, &InstanceWindow::on_instanceStatusChanged);
    }

    // add ourself as the modpack page's instance window
    {
        static_cast<ManagedPackPage*>(m_container->getPage("managed_pack"))->setInstanceWindow(this);
    }

    placeAbove(parent);
    show();
}

void InstanceWindow::placeAbove(QWidget* anchor)
{
    if (anchor) {
        auto* library = anchor->window();
        library->winId();
        winId();
        windowHandle()->setTransientParent(library->windowHandle());
        move(library->frameGeometry().topLeft());
    }
    m_editing = anchor != nullptr;
    m_loaderButton->setVisible(m_editing);
    if (!m_editing && windowHandle())
        windowHandle()->setTransientParent(nullptr);
    m_container->setHiddenPages(m_editing ? QSet<QString>{"settings", "notes", "console", "logs"} : QSet<QString>{});
}

void InstanceWindow::setWallpaperAction(std::function<void()> wallpaper)
{
    disconnect(m_wallpaperButton, nullptr, this, nullptr);
    connect(m_wallpaperButton, &QPushButton::clicked, this, std::move(wallpaper));
    m_wallpaperButton->show();
}

void InstanceWindow::updateLoaderLabel()
{
    const auto versions = StellarClient::versions(m_instance);
    QString loader = tr("Vanilla");
    for (const auto& [id, label] : {std::pair{"net.fabricmc.fabric-loader", "Fabric"}, {"net.minecraftforge", "Forge"},
                                  {"net.neoforged", "NeoForge"}, {"org.quiltmc.quilt-loader", "Quilt"},
                                  {"com.mumfrey.liteloader", "LiteLoader"}}) {
        const auto current = m_instance->getPackProfile()->getComponentVersion(QString::fromLatin1(id));
        if (!current.isEmpty() || versions.contains(QString::fromLatin1(id))) {
            loader = QString::fromLatin1(label);
            break;
        }
    }
    m_loaderButton->setText(tr("Loader: %1").arg(loader));
    m_loaderButton->setToolTip(tr("Choose the mod loader for this instance"));
}

void InstanceWindow::on_instanceStatusChanged(BaseInstance::Status, BaseInstance::Status newStatus)
{
    if (newStatus == BaseInstance::Status::Gone) {
        m_doNotSave = true;
        close();
    }
}

void InstanceWindow::updateButtons()
{
    const bool running = m_instance->isRunning();

    m_launchButton->setVisible(!running);
    m_restartButton->setVisible(running);

    m_launchButton->setEnabled(m_instance->canLaunch());
    m_restartButton->setEnabled(running && !m_restartQueued);
    m_killButton->setVisible(running);

    QMenu* launchMenu = m_launchButton->menu();
    if (launchMenu)
        launchMenu->clear();
    else
        launchMenu = new QMenu(this);
    m_instance->populateLaunchMenu(launchMenu);
    m_launchButton->setMenu(launchMenu);
}

void InstanceWindow::instanceLaunchTaskChanged(LaunchTask* proc)
{
    m_proc = proc;
}

void InstanceWindow::runningStateChanged(bool running)
{
    updateButtons();
    m_container->refreshContainer();
    if (running && !m_editing) {
        selectPage("console");
    } else if (m_restartQueued) {
        m_restartQueued = false;
        // Wait until the current launch controller has handled the stopped instance before launching again.
        QTimer::singleShot(0, this, [this] {
            APPLICATION->launch(m_instance);
            updateButtons();
        });
    }
}

void InstanceWindow::restartInstance()
{
    if (!m_instance->isRunning()) {
        return;
    }

    m_restartQueued = APPLICATION->kill(m_instance);
    updateButtons();
}

void InstanceWindow::closeEvent(QCloseEvent* event)
{
    bool proceed = true;
    if (!m_doNotSave) {
        proceed &= m_container->prepareToClose();
    }

    if (!proceed) {
        return;
    }

    APPLICATION->settings()->set("ConsoleWindowState", QString::fromUtf8(saveState().toBase64()));
    APPLICATION->settings()->set("ConsoleWindowGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    emit isClosing();
    event->accept();
}

bool InstanceWindow::saveAll()
{
    return m_container->saveAll();
}

QString InstanceWindow::instanceId()
{
    return m_instance->id();
}

bool InstanceWindow::selectPage(QString pageId)
{
    return m_container->selectPage(pageId);
}

void InstanceWindow::refreshContainer()
{
    m_container->refreshContainer();
}

BasePage* InstanceWindow::selectedPage() const
{
    return m_container->selectedPage();
}

bool InstanceWindow::requestClose()
{
    if (m_container->prepareToClose()) {
        close();
        return true;
    }
    return false;
}
