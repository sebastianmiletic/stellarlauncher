#pragma once
#include <FileSystem.h>
#include <QVBoxLayout>
#include <ui/pages/instance/DataPackPage.h>
#include "minecraft/MinecraftInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/pages/instance/InstanceSettingsPage.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/pages/instance/ManagedPackPage.h"
#include "ui/pages/instance/ModFolderPage.h"
#include "ui/pages/instance/NotesPage.h"
#include "ui/pages/instance/OtherLogsPage.h"
#include "ui/pages/instance/ResourcePackPage.h"
#include "ui/pages/instance/ScreenshotsPage.h"
#include "ui/pages/instance/ServersPage.h"
#include "ui/pages/instance/ShaderPackPage.h"
#include "ui/pages/instance/TexturePackPage.h"
#include "ui/pages/instance/VersionPage.h"
#include "ui/pages/instance/WorldListPage.h"

class DeferredInstancePage final : public QWidget, public BasePage {
public:
    DeferredInstancePage(QString id, QString label, QString icon, std::function<BasePage*()> create,
                         std::function<bool()> visible = {})
        : m_id(std::move(id)), m_label(std::move(label)), m_icon(std::move(icon)),
          m_create(std::move(create)), m_visible(std::move(visible))
    {
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_label; }
    QIcon icon() const override { return QIcon::fromTheme(m_icon); }
    bool shouldDisplay() const override { return !m_visible || m_visible(); }
    QString helpPage() const override { return m_page ? m_page->helpPage() : QString(); }
    bool apply() override { return !m_page || m_page->apply(); }
    void openedImpl() override { materialize()->opened(); }
    void closedImpl() override { if (m_page) m_page->closed(); }
    void retranslate() override { if (m_page) m_page->retranslate(); }

    BasePage* materialize() override
    {
        if (!m_page) {
            m_page = m_create();
            m_page->setParentContainer(m_container);
            auto* widget = dynamic_cast<QWidget*>(m_page);
            widget->setParent(this);
            auto* layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(widget);
            widget->show();
        }
        return m_page;
    }

private:
    QString m_id, m_label, m_icon;
    std::function<BasePage*()> m_create;
    std::function<bool()> m_visible;
    BasePage* m_page = nullptr;
};

class InstancePageProvider : protected QObject, public BasePageProvider {
    Q_OBJECT
   public:
    explicit InstancePageProvider(MinecraftInstance* parent) { inst = parent; }

    virtual ~InstancePageProvider() = default;
    virtual QList<BasePage*> getPages() override
    {
        QList<BasePage*> values;
        auto* instance = inst;
        values.append(new VersionPage(instance));
        values.append(ManagedPackPage::createPage(instance));
        values.append(new DeferredInstancePage("mods", tr("Mods"), "loadermods", [instance] {
            auto* page = new ModFolderPage(instance, instance->loaderModList());
            page->setFilter("%1 (*.zip *.jar *.litemod *.nilmod)");
            return page;
        }));
        values.append(new DeferredInstancePage("resourcepacks", tr("Resource Packs"), "resourcepacks",
            [instance] { return new ResourcePackPage(instance, instance->resourcePackList()); }, [instance] {
                const auto traits = instance->traits();
                return !traits.contains("no-texturepacks") && !traits.contains("texturepacks");
            }));
        values.append(new DeferredInstancePage("texturepacks", tr("Texture Packs"), "resourcepacks",
            [instance] { return new TexturePackPage(instance, instance->texturePackList()); },
            [instance] { return instance->traits().contains("texturepacks"); }));
        values.append(new DeferredInstancePage("shaderpacks", tr("Shaders"), "shaderpacks",
            [instance] { return new ShaderPackPage(instance, instance->shaderPackList()); }));
        values.append(new DeferredInstancePage("worlds", tr("Worlds"), "worlds",
            [instance] { return new WorldListPage(instance, instance->worldList()); }));
        values.append(new DeferredInstancePage("settings", tr("Settings"), "instance-settings",
            [instance] { return new InstanceSettingsPage(instance); }));
        values.append(new DeferredInstancePage("datapacks", tr("Data Packs"), "datapacks",
            [instance] { return new GlobalDataPackPage(instance); },
            [instance] { return instance->settings()->get("GlobalDataPacksEnabled").toBool(); }));
        values.append(new DeferredInstancePage("coremods", tr("Core Mods"), "coremods",
            [instance] { return new CoreModFolderPage(instance, instance->coreModList()); },
            [instance] { return CoreModFolderPage::available(instance); }));
        values.append(new DeferredInstancePage("nilmods", tr("Nilmods"), "coremods",
            [instance] { return new NilModFolderPage(instance, instance->nilModList()); },
            [instance] { return QDir(instance->nilModsDir()).exists(); }));
        values.append(new DeferredInstancePage("notes", tr("Notes"), "notes", [instance] { return new NotesPage(instance); }));
        values.append(new DeferredInstancePage("servers", tr("Servers"), "server", [instance] { return new ServersPage(instance); }));
        values.append(new DeferredInstancePage("screenshots", tr("Screenshots"), "screenshots",
            [instance] { return new ScreenshotsPage(FS::PathCombine(instance->gameRoot(), "screenshots")); }));
        values.append(new DeferredInstancePage("console", tr("Minecraft Log"), "log", [instance] { return new LogPage(instance); }));
        values.append(new DeferredInstancePage("logs", tr("Other Logs"), "log",
            [instance] { return new OtherLogsPage("logs", tr("Other Logs"), "Other-Logs", instance); }));
        return values;
    }

    virtual QString dialogTitle() override { return tr("Edit Instance (%1)").arg(inst->name()); }

   protected:
    MinecraftInstance* inst;
};
