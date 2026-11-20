// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2023-2024 Trial97 <alexandru.tripon97@gmail.com>
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
 */

#include "SkinManageDialog.h"
#include "net/RPCSink.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"
#include "ui_SkinManageDialog.h"

#include <FileSystem.h>
#include <QAction>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QListView>
#include <QMenu>
#include <QMimeDatabase>
#include <QPainter>
#include <QUrl>

#include "Application.h"
#include "DesktopServices.h"
#include "QObjectPtr.h"
#include "settings/SettingsObject.h"

#include "minecraft/auth/Parsers.h"
#include "minecraft/skins/SkinList.h"
#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinRequests.h"

#include "net/NetJob.h"
#include "net/Request.h"
#include "tasks/Task.h"

#include "Json.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
class SkinFilter final : public QSortFilterProxyModel {
public:
    explicit SkinFilter(QObject* parent) : QSortFilterProxyModel(parent) {}
    void setCollection(int collection)
    {
        m_collection = collection;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const auto skin = sourceModel()->index(row, 0, parent);
        return (m_collection < 0 || skin.data(SkinList::CollectionRole).toInt() == m_collection) &&
            QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }

private:
    int m_collection = -1;
};
}

SkinManageDialog::SkinManageDialog(QWidget* parent, MinecraftAccountPtr acct)
    : QDialog(parent), m_acct(acct), m_ui(new Ui::SkinManageDialog),
      m_list(this, APPLICATION->settings()->get("SkinsDir").toString(), acct)
{
    m_ui->setupUi(this);
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);
    resize(729, 460);
    setMinimumSize(628, 392);
    setStyleSheet(
        "QDialog { background: #1c1c20; border: 1px solid #37363d; }"
        "QLabel#title { font-size: 20px; font-weight: 650; }"
        "QLabel#skinName { font-weight: 600; padding: 3px; }"
        "QLabel#status, QLabel#empty { color: #bcb9c5; }"
        "QFrame#preview { background: #141416; border-radius: 12px; }"
        "QListView { background: #141416; border: 0; border-radius: 10px; padding: 5px; }"
        "QListView::item { border: 1px solid transparent; border-radius: 8px; padding: 4px; }"
        "QListView::item:hover { background: #28272e; }"
        "QListView::item:selected { background: #303038; color: #f0eff3; border-color: #b8b5c2; }"
        "QPushButton#applyBtn:enabled { background: #e9e7f1; color: #18171c; }");

    if (SkinOpenGLWindow::hasOpenGL()) {
        m_skinPreview = new SkinOpenGLWindow(this, QColor("#141416"));
        m_previewContainer = QWidget::createWindowContainer(m_skinPreview, this);
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        policy.setRetainSizeWhenHidden(true);
        m_previewContainer->setSizePolicy(policy);
        m_ui->skinLayout->addWidget(m_previewContainer);
    } else {
        m_skinPreviewLabel = new QLabel(this);
        m_skinPreviewLabel->setAlignment(Qt::AlignCenter);
        m_skinPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_ui->skinLayout->addWidget(m_skinPreviewLabel);
    }

    auto* list = m_ui->listView;
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(98, 78));
    list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    list->addActions({m_ui->actionRename, m_ui->actionDelete});
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setAcceptDrops(true);
    list->setDragDropMode(QAbstractItemView::DropOnly);
    list->setDefaultDropAction(Qt::CopyAction);
    auto* filter = new SkinFilter(this);
    filter->setSourceModel(&m_list);
    filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    list->setModel(filter);
    m_ui->collection->addItem(tr("All skins"), -1);
    m_ui->collection->addItem(tr("Minecraft defaults"), SkinList::MinecraftDefaults);
    m_ui->collection->addItem(tr("Downloaded"), SkinList::Downloaded);
    m_ui->collection->addItem(tr("My skins"), SkinList::Personal);
    const auto ensureSelection = [this, list, filter] {
        const bool empty = filter->rowCount() == 0;
        m_ui->empty->setVisible(empty);
        m_ui->empty->setText(m_ui->collection->currentData().toInt() == SkinList::MinecraftDefaults && m_ui->search->text().isEmpty()
                            ? tr("Minecraft defaults are loaded from an installed Java Edition client.")
                            : tr("No matching skins"));
        if (!empty && !list->currentIndex().isValid())
            list->setCurrentIndex(filter->index(0, 0));
        selectionChanged(list->currentIndex());
    };
    connect(m_ui->collection, &QComboBox::currentIndexChanged, this, [this, filter, ensureSelection] {
        filter->setCollection(m_ui->collection->currentData().toInt());
        ensureSelection();
    });
    connect(m_ui->search, &QLineEdit::textChanged, this, [filter, ensureSelection](const QString& text) {
        filter->setFilterFixedString(text);
        ensureSelection();
    });
    connect(filter, &QAbstractItemModel::modelReset, this, ensureSelection);
    connect(filter, &QAbstractItemModel::dataChanged, this, ensureSelection);
    connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this, &SkinManageDialog::selectionChanged);
    connect(list, &QListView::customContextMenuRequested, this, &SkinManageDialog::show_context_menu);
    connect(m_ui->applyBtn, &QPushButton::clicked, this, &SkinManageDialog::accept);
    connect(m_ui->closeBtn, &QPushButton::clicked, this, &SkinManageDialog::reject);
    connect(m_ui->actionRename, &QAction::triggered, this, &SkinManageDialog::on_action_Rename_Skin_triggered);
    connect(m_ui->actionDelete, &QAction::triggered, this, &SkinManageDialog::on_action_Delete_Skin_triggered);

    auto* more = new QMenu(this);
    more->addAction(tr("Import from URL…"), this, &SkinManageDialog::on_urlBtn_clicked);
    more->addAction(tr("Import by username…"), this, &SkinManageDialog::on_userBtn_clicked);
    more->addAction(tr("Open skins folder"), this, &SkinManageDialog::on_openDirBtn_clicked);
    more->addAction(tr("Browse skins online"), this, [] { DesktopServices::openUrl(QUrl("https://handmadeskins.com/")); });
    if (m_acct)
        more->addAction(tr("Reset Minecraft profile skin…"), this, &SkinManageDialog::on_resetBtn_clicked);
    connect(m_ui->moreBtn, &QPushButton::clicked, this, [this, more] {
        more->popup(m_ui->moreBtn->mapToGlobal(QPoint(0, m_ui->moreBtn->height())));
    });
    connect(m_ui->elytraCB, &QCheckBox::checkStateChanged, this, [this] {
        if (m_skinPreview)
            m_skinPreview->setElytraVisible(m_ui->elytraCB->isChecked());
        on_capeCombo_currentIndexChanged(m_ui->capeCombo->currentIndex());
    });

    m_ui->status->setText(m_acct ? m_acct->profileName() : tr("Sign in to apply skins"));
    m_ui->capeCombo->setVisible(m_acct != nullptr);
    m_ui->elytraCB->setVisible(false);
    if (m_acct) {
        connect(m_acct.get(), &MinecraftAccount::activityChanged, this, [this, ensureSelection](bool active) {
            m_ui->status->setText(active ? tr("Refreshing account…") : m_acct->profileName());
            ensureSelection();
        });
        setupCapes();
    }
    const auto active = filter->mapFromSource(m_list.index(m_list.getSelectedAccountSkin()));
    if (active.isValid())
        list->setCurrentIndex(active);
    ensureSelection();
}

SkinManageDialog::~SkinManageDialog()
{
    delete m_ui;
}

void SkinManageDialog::selectionChanged(const QModelIndex& index)
{
    m_selectedSkinKey = index.data(Qt::UserRole).toString();
    const auto* skin = getSelectedSkin();
    m_ui->applyBtn->setEnabled(m_acct && !m_acct->isActive() && skin);
    m_ui->skinName->setText(skin ? skin->name() : QString());
    if (!skin) {
        updatePreview();
        return;
    }
    const QSignalBlocker modelSignals(m_ui->steveBtn);
    const QSignalBlocker capeSignals(m_ui->capeCombo);
    m_ui->steveBtn->setChecked(skin->getModel() == SkinModel::CLASSIC);
    m_ui->alexBtn->setChecked(skin->getModel() == SkinModel::SLIM);
    const auto capeId = SkinList::collection(*skin) == SkinList::Personal ? skin->getCapeId()
        : m_acct ? m_acct->accountData()->minecraftProfile.currentCape : QString();
    m_ui->capeCombo->setCurrentIndex(m_capesIdx.value(capeId));
    updatePreview();
}

void SkinManageDialog::updatePreview()
{
    auto* skin = getSelectedSkin();
    const auto cape = m_capes.value(m_ui->capeCombo->currentData().toString());
    m_ui->elytraCB->setVisible(!cape.isNull());
    m_ui->steveBtn->setEnabled(skin != nullptr);
    m_ui->alexBtn->setEnabled(skin != nullptr);
    if (m_skinPreview) {
        m_previewContainer->setVisible(skin != nullptr);
        m_skinPreview->updateCape(cape);
        m_skinPreview->updateScene(skin);
    } else {
        m_skinPreviewLabel->setPixmap(skin ? QPixmap::fromImage(skin->getPreview()).scaled(
            m_skinPreviewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation) : QPixmap());
    }
}

void SkinManageDialog::on_openDirBtn_clicked()
{
    DesktopServices::openPath(m_list.getDir(), true);
}

void SkinManageDialog::on_fileBtn_clicked()
{
    auto filter = QMimeDatabase().mimeTypeForName("image/png").filterString();
    QString rawPath = QFileDialog::getOpenFileName(this, tr("Select Skin Texture"), QString(), filter);
    if (rawPath.isNull()) {
        return;
    }
    auto message = m_list.installSkin(rawPath, {});
    if (!message.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Selected file is not a valid skin"), message, QMessageBox::Critical)->show();
        return;
    }
}
namespace {
QPixmap previewCape(const QImage& capeImage, bool elytra = false)
{
    if (elytra) {
        auto wing = capeImage.copy(34, 2, 12, 20);
        QImage mirrored = wing.mirrored(true, false);

        QImage combined((wing.width() * 2) + 1, wing.height() + 14, capeImage.format());
        combined.fill(Qt::transparent);

        QPainter painter(&combined);
        painter.drawImage(0, 7, wing);
        painter.drawImage(wing.width() + 1, 7, mirrored);
        painter.end();
        return QPixmap::fromImage(combined.scaled(84, 128, Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    return QPixmap::fromImage(capeImage.copy(1, 1, 10, 16).scaled(80, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation));
}
}  // namespace

void SkinManageDialog::setupCapes()
{
    // FIXME: add a model for this, download/refresh the capes on demand
    auto& accountData = *m_acct->accountData();
    int index = 0;
    m_ui->capeCombo->addItem(tr("No Cape"), QVariant());
    auto currentCape = accountData.minecraftProfile.currentCape;
    if (currentCape.isEmpty()) {
        m_ui->capeCombo->setCurrentIndex(index);
    }

    auto capesDir = FS::PathCombine(m_list.getDir(), "capes");
    NetJob::Ptr job{ new NetJob(tr("Download capes"), APPLICATION->network()) };
    bool needsToDownload = false;
    for (auto& cape : accountData.minecraftProfile.capes) {
        auto path = FS::PathCombine(capesDir, cape.id + ".png");
        if (!cape.data.isEmpty()) {
            QImage capeImage;
            if (capeImage.loadFromData(cape.data, "PNG") && capeImage.save(path)) {
                m_capes[cape.id] = capeImage;
                continue;
            }
        }
        if (QFileInfo(path).exists()) {
            continue;
        }
        if (!cape.url.isEmpty()) {
            needsToDownload = true;
            job->addNetAction(Net::Request::makeFile(cape.url, path));
        }
    }
    if (needsToDownload) {
        ProgressDialog dlg(this);
        dlg.execWithTask(job.get());
    }
    for (auto& cape : accountData.minecraftProfile.capes) {
        index++;
        QImage capeImage;
        if (!m_capes.contains(cape.id)) {
            auto path = FS::PathCombine(capesDir, cape.id + ".png");
            if (QFileInfo(path).exists() && capeImage.load(path)) {
                m_capes[cape.id] = capeImage;
            }
        }
        if (!capeImage.isNull()) {
            m_ui->capeCombo->addItem(previewCape(capeImage, m_ui->elytraCB->isChecked()), cape.alias, cape.id);
        } else {
            m_ui->capeCombo->addItem(cape.alias, cape.id);
        }

        m_capesIdx[cape.id] = index;
    }
}

void SkinManageDialog::on_capeCombo_currentIndexChanged(int)
{
    updatePreview();
}

void SkinManageDialog::on_steveBtn_toggled(bool checked)
{
    if (auto* skin = getSelectedSkin()) {
        skin->setModel(checked ? SkinModel::CLASSIC : SkinModel::SLIM);
        updatePreview();
    }
}

void SkinManageDialog::accept()
{
    auto* skin = getSelectedSkin();
    if (!m_acct || m_acct->isActive() || !skin)
        return;
    auto path = skin->getPath();

    ProgressDialog prog(this);
    NetJob::Ptr skinUpload{ new NetJob(tr("Change skin"), APPLICATION->network(), 1) };

    if (!QFile::exists(path)) {
        CustomMessageBox::selectable(this, tr("Skin Upload"), tr("Skin file does not exist!"), QMessageBox::Warning)->exec();
        reject();
        return;
    }

    skinUpload->addNetAction(makeSkinUploadRequest(m_acct->accessToken(), skin->getPath(), skin->getModelString()));

    const auto selectedCape = m_ui->capeCombo->currentData().toString();
    if (selectedCape != m_acct->accountData()->minecraftProfile.currentCape) {
        skinUpload->addNetAction(makeCapeChangeRequest(m_acct->accessToken(), selectedCape));
    }

    skinUpload->addTask(m_acct->refresh().staticCast<Task>());
    if (prog.execWithTask(skinUpload.get()) != QDialog::Accepted) {
        CustomMessageBox::selectable(this, tr("Skin Upload"), tr("Could not apply the skin to your Minecraft profile."), QMessageBox::Warning)->exec();
        return;
    }
    if (auto* applied = m_list.skin(path)) {
        applied->setURL(m_acct->accountData()->minecraftProfile.skin.url);
        applied->setCapeId(selectedCape);
    }
    QDialog::accept();
}

void SkinManageDialog::on_resetBtn_clicked()
{
    if (!m_acct || m_acct->isActive())
        return;
    if (QMessageBox::question(this, tr("Reset profile skin"),
                              tr("Restore your Minecraft account’s default skin?")) != QMessageBox::Yes)
        return;
    ProgressDialog prog(this);
    NetJob::Ptr skinReset{ new NetJob(tr("Reset skin"), APPLICATION->network(), 1) };
    skinReset->addNetAction(makeSkinDeleteRequest(m_acct->accessToken()));
    skinReset->addTask(m_acct->refresh().staticCast<Task>());
    if (prog.execWithTask(skinReset.get()) != QDialog::Accepted) {
        CustomMessageBox::selectable(this, tr("Skin Delete"), tr("Failed to delete current skin!"), QMessageBox::Warning)->exec();
        reject();
        return;
    }
    QDialog::accept();
}

void SkinManageDialog::show_context_menu(const QPoint& pos)
{
    const auto item = m_ui->listView->indexAt(pos);
    if (!item.isValid() || !item.flags().testFlag(Qt::ItemIsEditable))
        return;
    m_ui->listView->setCurrentIndex(item);
    auto* menu = new QMenu(this);
    menu->addActions({m_ui->actionRename, m_ui->actionDelete});
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->popup(m_ui->listView->viewport()->mapToGlobal(pos));
}

void SkinManageDialog::on_action_Rename_Skin_triggered(bool /*unused*/)
{
    if (m_ui->listView->currentIndex().flags().testFlag(Qt::ItemIsEditable)) {
        m_ui->listView->edit(m_ui->listView->currentIndex());
    }
}

void SkinManageDialog::on_action_Delete_Skin_triggered(bool /*unused*/)
{
    if (!m_ui->listView->currentIndex().flags().testFlag(Qt::ItemIsEditable)) {
        return;
    }

    if (m_list.getSkinIndex(m_selectedSkinKey) == m_list.getSelectedAccountSkin()) {
        CustomMessageBox::selectable(this, tr("Delete error"), tr("Can not delete skin that is in use."), QMessageBox::Warning)->exec();
        return;
    }

    auto* skin = m_list.skin(m_selectedSkinKey);
    if (!skin) {
        return;
    }

    auto response = CustomMessageBox::selectable(this, tr("Confirm Deletion"),
                                                 tr("You are about to delete \"%1\".\n"
                                                    "Are you sure?")
                                                     .arg(skin->name()),
                                                 QMessageBox::Warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                        ->exec();

    if (response == QMessageBox::Yes) {
        if (!m_list.deleteSkin(m_selectedSkinKey, true))
            CustomMessageBox::selectable(this, tr("Delete skin"), tr("Could not move this skin to the Trash."), QMessageBox::Warning)->show();
    }
}

void SkinManageDialog::on_urlBtn_clicked()
{
    const auto input = QInputDialog::getText(this, tr("Import skin"), tr("HTTPS image URL")).trimmed();
    if (input.isEmpty())
        return;
    const QUrl url(input);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()) {
        CustomMessageBox::selectable(this, tr("Import skin"), tr("Enter a valid HTTPS image URL."), QMessageBox::Warning)->show();
        return;
    }
    QTemporaryDir download;
    if (!download.isValid())
        return;

    NetJob::Ptr job{ new NetJob(tr("Download skin"), APPLICATION->network()) };
    job->setAskRetry(false);

    const auto path = download.filePath("skin.png");
    job->addNetAction(Net::Request::makeFile(url, path));
    ProgressDialog dlg(this);
    dlg.execWithTask(job.get());
    auto name = QFileInfo(url.fileName()).completeBaseName();
    if (name.isEmpty())
        name = tr("Imported skin");
    const auto error = m_list.installSkin(path, name + ".png");
    if (!error.isEmpty())
        CustomMessageBox::selectable(this, tr("Import skin"), error, QMessageBox::Warning)->show();
}

void SkinManageDialog::on_userBtn_clicked()
{
    const auto user = QInputDialog::getText(this, tr("Import skin"), tr("Minecraft username")).trimmed();
    if (user.isEmpty()) {
        return;
    }
    static const QRegularExpression username("^[A-Za-z0-9_]{1,16}$");
    if (!username.match(user).hasMatch()) {
        CustomMessageBox::selectable(this, tr("Import skin"), tr("Enter a valid Minecraft username."), QMessageBox::Warning)->show();
        return;
    }
    QTemporaryDir download;
    if (!download.isValid())
        return;
    MinecraftProfile mcProfile;
    auto path = download.filePath(user + ".png");

    NetJob::Ptr job{ new NetJob(tr("Download user skin"), APPLICATION->network(), 1) };
    job->setAskRetry(false);

    auto downloadSkin = Net::Request::makeFile(QUrl(), path);
    // use a valid url to trick the make to actually create the task
    // the actual url will be set in the getUUID parse function
    auto [getProfile, a] = Net::RPC::make<bool>({ { .url = { "https://sessionserver.mojang.com/session/minecraft/profile/" } },
                                                  [downloadSkin, &mcProfile](const auto& v) -> Result<bool> {
                                                      TRY(Parsers::parseMinecraftProfileMojang(v, mcProfile))
                                                      downloadSkin->setUrl(mcProfile.skin.url);
                                                      return {};
                                                  } });
    auto [getUUID, b] = Net::RPC::make<bool>({ { .url = "https://api.minecraftservices.com/minecraft/profile/lookup/name/" + user },
                                               [&getProfile](const auto& v) -> Result<bool> {
                                                   TRY_INTO(auto root, Json::requireObject(v, "Minecraft profile lookup"))
                                                   auto id = root["id"].toString();
                                                   if (id.isEmpty()) {
                                                       return std::unexpected(tr("user id is empty"));
                                                   }
                                                   getProfile->setUrl("https://sessionserver.mojang.com/session/minecraft/profile/" + id);
                                                   return {};
                                               } });

    QString failReason;

    connect(getUUID.get(), &Task::failed, this, [&failReason](const QString& reason) {
        qCritical() << "Couldn't get user UUID:" << reason;
        failReason = tr("failed to get user UUID");
    });
    connect(getProfile.get(), &Task::failed, this, [&failReason](const QString& reason) {
        qCritical() << "Couldn't get user profile:" << reason;
        failReason = tr("failed to get user profile");
    });
    connect(downloadSkin.get(), &Task::failed, this, [&failReason](const QString& reason) {
        qCritical() << "Couldn't download skin:" << reason;
        failReason = tr("failed to download skin");
    });

    job->addNetAction(getUUID);
    job->addNetAction(getProfile);
    job->addNetAction(downloadSkin);
    ProgressDialog dlg(this);
    dlg.execWithTask(job.get());

    SkinModel s(path);
    if (!s.isValid()) {
        if (failReason.isEmpty()) {
            failReason = tr("the skin is invalid");
        }
        CustomMessageBox::selectable(this, tr("Username not found"),
                                     tr("Unable to find the skin for '%1'\n because: %2.").arg(user, failReason), QMessageBox::Critical)
            ->show();
        QFile::remove(path);
        return;
    }
    QString installed;
    const auto error = m_list.installSkin(path, user + ".png", &installed);
    if (!error.isEmpty()) {
        CustomMessageBox::selectable(this, tr("Import skin"), error, QMessageBox::Warning)->show();
        return;
    }
    s = SkinModel(installed);
    s.setModel(mcProfile.skin.variant.toUpper() == "SLIM" ? SkinModel::SLIM : SkinModel::CLASSIC);
    s.setURL(mcProfile.skin.url);
    if (m_capes.contains(mcProfile.currentCape)) {
        s.setCapeId(mcProfile.currentCape);
    }
    m_list.updateSkin(&s);
}

void SkinManageDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_skinPreviewLabel)
        updatePreview();
}

SkinModel* SkinManageDialog::getSelectedSkin()
{
    if (auto* skin = m_list.skin(m_selectedSkinKey); skin && skin->isValid()) {
        return skin;
    }
    return nullptr;
}

QHash<QString, QImage> SkinManageDialog::capes()
{
    return m_capes;
}
