// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QFrame;
class QDialog;
class QHBoxLayout;
class QPropertyAnimation;
class QScrollArea;
class QTimer;
class MinecraftInstance;
class Task;

class StellarGallery : public QWidget {
public:
    static constexpr QSize windowSize{818, 414};
    static constexpr int cornerRadius = 17;

    struct Actions {
        std::function<void()> create;
        std::function<void(const QString&)> importPack;
        std::function<void()> accounts;
        std::function<void()> skins;
    };

    StellarGallery(Actions actions, QWidget* parent);
    void selectInstance(const QString& id);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Collection {
        QString id;
        QString title;
        QStringList versions;
        QUrl archive;
        QUrl fallback;
    };

    void rebuild();
    void selectIndex(int index, bool animate);
    void slideTo(int offset, bool animate);
    void activate(int index);
    void openInstance(const QString& id, int direction = 0);
    void closeInstance();
    void activatePlay(const QString& id);
    void showStop(const QString& id);
    void playInstance(const QString& id);
    void launchChecked(const QString& id);
    void refreshInstance();
    void editInstance(const QString& id);
    void showActions(int index, QRect anchor = {});
    void showPreferences();
    void showAccounts();
    void updateAccountIcons();
    void showCreator();
    void showCreationProgress();
    void showImport();
    void previewImport(const QString& source);
    void copyInstance(const QString& source, const QStringList& entries);
    void showWallpapers(const QString& instance);
    QFrame* makePanel(const QString& title);
    void dismissPanel();
    QStringList images(const Collection& collection) const;
    QString artworkPath(const QString& instance, const QString& version);
    QPixmap wallpaperPixmap(const QString& file);
    void scheduleThumbnails();
    void fetchArtwork(const Collection& collection);
    void updateArtwork();
    void updateLaunch();
    void placePanel(const QRect& anchor = {}, bool alignRight = false);

    Actions m_actions;
    QScrollArea* m_scroll;
    QWidget* m_cards;
    QHBoxLayout* m_row;
    QPropertyAnimation* m_slide;
    QPropertyAnimation* m_outlineSlide;
    QWidget* m_outline;
    QTimer* m_launchTimer;
    QTimer* m_dragScroll;
    QTimer* m_saveSelection;
    QMetaObject::Connection m_detailRunningChanged;
    QAbstractButton* m_accountTile = nullptr;
    QFrame* m_panel = nullptr;
    QPointer<QDialog> m_preferences;
    QWidget* m_detail = nullptr;
    QPointer<QWidget> m_detailSlide;
    QList<QAbstractButton*> m_tiles;
    QList<Collection> m_collections;
    QCache<QString, QPixmap> m_artwork{4};
    QCache<QString, QPixmap> m_thumbnails{24};
    QSet<QString> m_pendingThumbnails;
    QSet<QString> m_fetching;
    QSet<QString> m_attempted;
    QStringList m_ids;
    QStringList m_allIds;
    QString m_selected;
    QString m_detailId;
    QString m_detailWallpaper;
    QString m_detailArtworkPath;
    bool m_detailDecoding = false;
    QHash<QString, QElapsedTimer> m_launches;
    QSet<QString> m_preflight;
    QPointer<Task> m_creation;
    QString m_wallpapers;
    int m_index = 0;
    bool m_opened = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
