#pragma once

#include "modplatform/CheckUpdateTask.h"

class FlameCheckUpdate : public CheckUpdateTask {
    Q_OBJECT

   public:
    FlameCheckUpdate(QList<Resource*>& resources,
                     std::vector<Version>& mcVersions,
                     QList<ModPlatform::ModLoaderType> loadersList,
                     ResourceFolderModel* resourceModel,
                     std::vector<ModPlatform::IndexedVersionType> releaseTypes = {},
                     bool strictLoaders = false)
        : CheckUpdateTask(resources, mcVersions, std::move(loadersList), resourceModel, std::move(releaseTypes))
        , m_strictLoaders(strictLoaders)
    {}

   public slots:
    bool abort() override;

   protected slots:
    void executeTask() override;
   private slots:
    void getLatestVersionCallback(Resource* resource, QByteArray* response);
    void collectBlockedMods();

   private:
    bool m_strictLoaders;
    Task::Ptr m_task = nullptr;

    QHash<Resource*, QString> m_blocked;
};
