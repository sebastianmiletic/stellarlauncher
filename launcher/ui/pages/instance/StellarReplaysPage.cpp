// SPDX-License-Identifier: GPL-3.0-only
#include "StellarReplaysPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>
#include <tuple>

#include "Application.h"
#include "FileSystem.h"
#include "ResourceDownloadTask.h"
#include "archive/ArchiveReader.h"
#include "minecraft/StellarClient.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tasks/ConcurrentTask.h"

namespace {
const QString flashback = QStringLiteral("flashback");
const QString fabricApi = QStringLiteral("fabric-api");

QDir replayFolder(MinecraftInstance* instance)
{
    return QDir(QDir(instance->gameRoot()).filePath("flashback/replays"));
}

bool supported(MinecraftInstance* instance)
{
    const auto versions = StellarClient::versions(instance);
    return versions.value("net.minecraft") == "26.3" &&
        QVersionNumber::fromString(versions.value("net.fabricmc.fabric-loader")) >= QVersionNumber(0, 19, 5);
}

struct ReplayScan {
    QFileInfoList files;
    QSet<QString> mods;
};
}

StellarReplaysPage::StellarReplaysPage(MinecraftInstance* instance)
    : QWidget(), m_instance(instance), m_replays(new QListWidget(this)), m_status(new QLabel(this)),
      m_watch(new QPushButton(tr("Watch / Edit"), this)), m_rename(new QPushButton(tr("Rename"), this)),
      m_remove(new QPushButton(tr("Move to Trash"), this)), m_install(new QPushButton(tr("Install Flashback"), this)),
      m_files(new QFileSystemWatcher(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* actions = new QHBoxLayout;
    actions->addWidget(m_watch);
    actions->addWidget(m_rename);
    actions->addWidget(m_remove);
    actions->addStretch();
    auto* folder = new QPushButton(tr("Open Folder"), this);
    actions->addWidget(folder);
    layout->addLayout(actions);
    m_replays->setAccessibleName(tr("Recorded Flashback replays"));
    layout->addWidget(m_replays, 1);
    auto* footer = new QHBoxLayout;
    m_status->setWordWrap(true);
    footer->addWidget(m_status, 1);
    footer->addWidget(m_install);
    layout->addLayout(footer);
    connect(m_replays, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(m_replays, &QListWidget::itemActivated, this, [this] { if (m_watch->isEnabled()) playReplay(); });
    connect(m_watch, &QPushButton::clicked, this, &StellarReplaysPage::playReplay);
    connect(m_install, &QPushButton::clicked, this, &StellarReplaysPage::installFlashback);
    connect(m_files, &QFileSystemWatcher::directoryChanged, this, [this] { if (isOpened) refresh(); });
    connect(instance, &MinecraftInstance::runningStatusChanged, this, [this] { updateButtons(); });

    connect(folder, &QPushButton::clicked, this, [this] {
        const auto root = replayFolder(m_instance);
        if (QDir().mkpath(root.absolutePath())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(root.absolutePath()));
            refresh();
        }
    });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        const auto file = selectedReplay();
        if (file.isEmpty())
            return;
        auto* form = new QInputDialog(this);
        form->setAttribute(Qt::WA_DeleteOnClose);
        form->setWindowTitle(tr("Rename Replay"));
        form->setLabelText(tr("Replay name"));
        form->setTextValue(QFileInfo(file).completeBaseName());
        connect(form, &QInputDialog::textValueSelected, this, [this, file](const QString& value) {
            const auto name = value.trimmed();
            if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\')) {
                m_status->setText(tr("Choose a valid replay name."));
                return;
            }
            const auto target = replayFolder(m_instance).filePath(name + ".zip");
            if (target != file && !QFile::rename(file, target)) {
                m_status->setText(tr("Couldn’t rename the replay. Check whether the name is already taken."));
                return;
            }
            refresh();
        });
        form->open();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const auto file = selectedReplay();
        if (file.isEmpty())
            return;
        auto* confirm = new QMessageBox(QMessageBox::Question, tr("Move Replay to Trash?"),
            tr("Move %1 to the system Trash?").arg(QFileInfo(file).completeBaseName()),
            QMessageBox::Yes | QMessageBox::Cancel, this);
        confirm->setAttribute(Qt::WA_DeleteOnClose);
        connect(confirm, &QMessageBox::finished, this, [this, file](int result) {
            if (result != QMessageBox::Yes)
                return;
            if (!FS::trash(file)) {
                m_status->setText(tr("Couldn’t move that replay to the Trash."));
                return;
            }
            refresh();
        });
        confirm->open();
    });
    updateButtons();
}

void StellarReplaysPage::openedImpl()
{
    refresh();
}

void StellarReplaysPage::closedImpl()
{
    const auto watching = m_files->directories();
    if (!watching.isEmpty())
        m_files->removePaths(watching);
}

void StellarReplaysPage::refresh()
{
    if (m_checking)
        return;
    m_checking = true;
    const auto root = replayFolder(m_instance);
    const QDir mods(QDir(m_instance->gameRoot()).filePath("mods"));
    for (const auto& directory : {root, mods, QDir(m_instance->gameRoot()), QDir(QDir(m_instance->gameRoot()).filePath("flashback"))}) {
        const auto path = directory.absolutePath();
        if (directory.exists() && !m_files->directories().contains(path))
            m_files->addPath(path);
    }
    QtConcurrent::run([root, mods] {
        ReplayScan scan;
        scan.files = root.entryInfoList({"*.zip"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
        for (const auto& file : mods.entryInfoList({"*.jar"}, QDir::Files | QDir::NoSymLinks)) {
            MMCZip::ArchiveReader jar(file.absoluteFilePath());
            if (const auto manifest = jar.readFile("fabric.mod.json")) {
                const auto id = QJsonDocument::fromJson(*manifest).object().value("id").toString();
                if (id == flashback || id == fabricApi)
                    scan.mods.insert(id);
                if (scan.mods.size() == 2)
                    break;
            }
        }
        return scan;
    }).then(this, [this](const ReplayScan& scan) {
        m_checking = false;
        m_installed = scan.mods;
        const auto selected = selectedReplay();
        m_replays->clear();
        for (const auto& file : scan.files) {
            auto* item = new QListWidgetItem(file.completeBaseName(), m_replays);
            item->setData(Qt::UserRole, file.absoluteFilePath());
            item->setToolTip(file.fileName());
            if (file.absoluteFilePath() == selected)
                m_replays->setCurrentItem(item);
        }
        if (m_replays->currentRow() < 0 && m_replays->count())
            m_replays->setCurrentRow(0);
        if (!m_task) {
            m_status->setText(!supported(m_instance) ? tr("Launch from Stellar requires Minecraft 26.3 and Fabric Loader 0.19.5 or newer.")
                : !m_installed.contains(flashback) || !m_installed.contains(fabricApi) ? tr("Install Flashback to record and open replays.")
                : m_replays->count() ? tr("Watch or edit in Flashback’s Minecraft workspace.")
                                     : tr("Record in Minecraft with Flashback to see replays here."));
        }
        updateButtons();
    });
}

QString StellarReplaysPage::selectedReplay() const
{
    const auto* item = m_replays->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void StellarReplaysPage::updateButtons()
{
    const bool selected = !selectedReplay().isEmpty();
    const bool ready = m_instance->canLaunch() && !m_instance->launchController();
    const bool installed = m_installed.contains(flashback) && m_installed.contains(fabricApi);
    m_watch->setEnabled(selected && supported(m_instance) && ready && installed && !m_task);
    m_install->setVisible(supported(m_instance) && !installed);
    m_install->setEnabled(!m_checking && ready && !m_task);
    m_rename->setEnabled(selected && !m_instance->isRunning());
    m_remove->setEnabled(selected && !m_instance->isRunning());
}

void StellarReplaysPage::installFlashback()
{
    if (m_task || !supported(m_instance) || m_instance->launchController())
        return;
    using namespace ModPlatform;
    using Candidate = std::pair<IndexedPack::Ptr, IndexedVersion>;
    auto matches = std::make_shared<QList<Candidate>>();
    auto lookup = makeShared<ConcurrentTask>(tr("Find compatible Flashback files"), 2);
    int expected = 0;
    for (const auto& [key, id, label] : {std::tuple{fabricApi, "P7dR8mSH", "Fabric API"},
                                         std::tuple{flashback, "4das1Fjq", "Flashback"}}) {
        if (m_installed.contains(key))
            continue;
        ++expected;
        auto pack = std::make_shared<IndexedPack>();
        pack->addonId = QString::fromLatin1(id);
        pack->provider = ResourceProvider::MODRINTH;
        pack->name = QString::fromLatin1(label);
        pack->slug = key;
        pack->resourceType = ModPlatform::ResourceType::Mod;
        auto request = ModrinthAPI::get().getProjectVersions(
            {.pack = pack, .mcVersions = std::vector{Version("26.3")}, .loaders = ModLoaderTypes(Fabric),
             .resourceType = ModPlatform::ResourceType::Mod},
            {.onSucceed = [matches, pack](QVector<IndexedVersion>& versions) {
                 const auto match = std::find_if(versions.cbegin(), versions.cend(), [](const auto& version) {
                     const QUrl url(version.downloadUrl);
                     return version.versionType == IndexedVersionType::Release && version.mcVersion.contains("26.3")
                         && version.loaders.testFlag(Fabric) && !version.hash.isEmpty()
                         && QFileInfo(version.fileName).fileName() == version.fileName && version.fileName.endsWith(".jar")
                         && url.scheme() == "https" && url.host() == "cdn.modrinth.com";
                 });
                 if (match != versions.cend())
                     matches->append({pack, *match});
             }, .onFail = [](const QString&, int) {}});
        if (auto* job = qobject_cast<NetJob*>(request.get()))
            job->setAskRetry(false);
        if (request)
            lookup->addTask(request);
    }
    if (!expected)
        return;
    m_status->setText(tr("Finding compatible releases on Modrinth…"));
    m_task = lookup;
    updateButtons();
    connect(lookup.get(), &Task::finished, this, [this, matches, expected] {
        if (matches->size() != expected || m_instance->launchController()) {
            m_task.reset();
            updateButtons();
            m_status->setText(tr("Compatible files couldn’t be installed. Close Minecraft and try again."));
            return;
        }
        const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        auto downloads = makeShared<ConcurrentTask>(tr("Install Flashback"), 2);
        for (const auto& [pack, version] : std::as_const(*matches))
            downloads->addTask(makeShared<ResourceDownloadTask>(pack, version, m_instance->loaderModList(), indexed));
        m_task = downloads;
        m_status->setText(tr("Downloading Flashback from Modrinth…"));
        connect(downloads.get(), &Task::finished, this, [this] {
            const bool succeeded = m_task->wasSuccessful();
            m_task.reset();
            updateButtons();
            if (succeeded) {
                const auto tools = StellarClient::installTools(m_instance);
                refresh();
                if (!tools)
                    m_status->setText(tools.error());
            } else
                m_status->setText(tr("The download didn’t finish. Try installing again."));
        }, Qt::QueuedConnection);
        downloads->start();
    }, Qt::QueuedConnection);
    lookup->start();
}

void StellarReplaysPage::playReplay()
{
    const QFileInfo file(selectedReplay());
    if (!file.isFile() || file.isSymLink() || file.absolutePath() != replayFolder(m_instance).absolutePath()
        || !m_watch->isEnabled())
        return;
    const auto installed = StellarClient::installTools(m_instance);
    if (!installed) {
        m_status->setText(installed.error());
        return;
    }
    m_instance->setProperty("stellarReplayFile", file.fileName());
    if (!APPLICATION->launch(m_instance)) {
        m_instance->setProperty("stellarReplayFile", {});
        m_status->setText(tr("Couldn’t start Minecraft."));
    }
    updateButtons();
}
