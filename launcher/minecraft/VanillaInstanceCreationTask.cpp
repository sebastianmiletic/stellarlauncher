#include "VanillaInstanceCreationTask.h"

#include <utility>

#include "Application.h"
#include "FileSystem.h"
#include "ResourceDownloadTask.h"
#include "minecraft/mod/ModFolderModel.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tasks/ConcurrentTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

VanillaCreationTask::VanillaCreationTask(BaseVersion::Ptr version, QString loader, BaseVersion::Ptr loaderVersion)
    : m_version(std::move(version)), m_usingLoader(true), m_loader(std::move(loader)), m_loaderVersion(std::move(loaderVersion))
{}

void VanillaCreationTask::executeTask()
{
    setStatus(tr("Creating instance from version %1").arg(m_version->name()));

    m_instance = std::make_unique<MinecraftInstance>(
        m_globalSettings, std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg")), m_stagingPath);
    {
        const SettingsObject::Lock lock(m_instance->settings());

        auto* components = m_instance->getPackProfile();
        components->buildingFromScratch();
        components->setComponentVersion("net.minecraft", m_version->descriptor(), true);
        if (m_usingLoader) {
            components->setComponentVersion(m_loader, m_loaderVersion->descriptor());
        }

        m_instance->setName(name());
        m_instance->setIconKey(m_instIcon);
        m_instance->settings()->set("StellarHorizon", m_horizon);

        components->saveNow();
    }

    if (m_horizon) {
        const auto gameDir = m_instance->gameRoot();
        if (!FS::ensureFolderPathExists(gameDir)) {
            emitFailed(tr("Couldn’t create the Horizon game folder."));
            return;
        }
        const auto options = FS::PathCombine(gameDir, "options.txt");
        // Horizon uses 260 for Unlimited, as Minecraft's video settings do.
        const auto written = FS::write(options, "maxFps:260\nenableVsync:false\n");
        if (!written) {
            emitFailed(tr("Couldn’t write Horizon’s initial video settings: %1").arg(written.error()));
            return;
        }
        emitSucceeded();
    } else if (m_performanceMods && m_loader == "net.fabricmc.fabric-loader") {
        installPerformanceMods();
    } else {
        downloadFiles(m_instance.get());
    }
}

void VanillaCreationTask::installPerformanceMods()
{
    using namespace ModPlatform;
    const auto version = m_version->descriptor();
    struct Project { const char* id; const char* slug; const char* name; };
    const std::array projects{
        Project{"AANobbMI", "sodium", "Sodium"},
        Project{"gvQqBUqZ", "lithium", "Lithium"},
        Project{"uXXizFIs", "ferrite-core", "FerriteCore"}
    };
    using Candidate = std::pair<IndexedPack::Ptr, IndexedVersion>;
    auto candidates = std::make_shared<QList<Candidate>>();
    auto lookup = makeShared<ConcurrentTask>(tr("Find compatible performance mods"), int(projects.size()));
    for (const auto& [id, slug, name] : projects) {
        auto pack = std::make_shared<IndexedPack>();
        pack->addonId = QString::fromLatin1(id);
        pack->provider = ResourceProvider::MODRINTH;
        pack->name = QString::fromLatin1(name);
        pack->slug = QString::fromLatin1(slug);
        pack->resourceType = ModPlatform::ResourceType::Mod;
        const auto collect = [candidates, pack, version](QVector<IndexedVersion>& available) {
            const auto match = std::find_if(available.cbegin(), available.cend(), [&version](const auto& mod) {
                const QUrl url(mod.downloadUrl);
                return mod.versionType == IndexedVersionType::Release && mod.mcVersion.contains(version) &&
                       mod.loaders.testFlag(Fabric) && !mod.hash.isEmpty() &&
                       QFileInfo(mod.fileName).fileName() == mod.fileName && mod.fileName.endsWith(".jar") &&
                       url.scheme() == "https" && url.host() == "cdn.modrinth.com" &&
                       std::none_of(mod.dependencies.cbegin(), mod.dependencies.cend(), [](const Dependency& dependency) {
                           return dependency.type == DependencyType::REQUIRED;
                       });
            });
            if (match != available.cend())
                candidates->append({ pack, *match });
        };
        auto request = ModrinthAPI::get().getProjectVersions(
            {.pack = pack, .mcVersions = std::vector{Version(version)}, .loaders = ModLoaderTypes(Fabric),
             .resourceType = ModPlatform::ResourceType::Mod},
            {.onSucceed = collect, .onFail = [](const QString&, int) {}});
        if (auto* job = qobject_cast<NetJob*>(request.get()))
            job->setAskRetry(false);
        if (request)
            lookup->addTask(request);
    }
    connect(lookup.get(), &Task::finished, this, [this, candidates] {
        if (candidates->isEmpty()) {
            qInfo() << "No compatible standalone performance mods found for" << m_version->descriptor();
            downloadFiles(m_instance.get());
            return;
        }
        auto downloads = makeShared<ConcurrentTask>(tr("Install compatible performance mods"), 3);
        const bool indexed = !APPLICATION->settings()->get("ModMetadataDisabled").toBool();
        for (const auto& [pack, version] : std::as_const(*candidates))
            downloads->addTask(makeShared<ResourceDownloadTask>(pack, version, m_instance->loaderModList(), indexed));
        connect(downloads.get(), &Task::finished, this, [this] { downloadFiles(m_instance.get()); }, Qt::QueuedConnection);
        m_modTask = downloads;
        propagateFromOther(downloads.get());
        downloads->start();
    }, Qt::QueuedConnection);
    m_modTask = lookup;
    propagateFromOther(lookup.get());
    lookup->start();
}
