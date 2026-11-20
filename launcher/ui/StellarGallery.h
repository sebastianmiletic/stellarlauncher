// SPDX-License-Identifier: GPL-3.0-only
#pragma once

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
class QHBoxLayout;
class QPropertyAnimation;
class QScrollArea;
class QTimer;
class MinecraftInstance;

class StellarGallery : public QWidget {
public:
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
    void playInstance(const QString& id);
    void launchChecked(const QString& id);
    void refreshInstance();
    void finishLaunch();
    void showActions(int index, QRect anchor = {});
    void showPreferences();
    void showModSettings();
    void showAccounts();
    void updateAccountIcons();
    void showCreator();
    void showContent(const QString& id);
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
    QWidget* m_detail = nullptr;
    QPointer<QWidget> m_detailSlide;
    QList<QAbstractButton*> m_tiles;
    QList<Collection> m_collections;
    QHash<QString, QPixmap> m_artwork;
    QHash<QString, QPixmap> m_thumbnails;
    QSet<QString> m_pendingThumbnails;
    QSet<QString> m_fetching;
    QSet<QString> m_attempted;
    QStringList m_ids;
    QStringList m_allIds;
    QString m_selected;
    QString m_detailId;
    QString m_detailWallpaper;
    QString m_launching;
    QString m_preflight;
    QString m_wallpapers;
    QElapsedTimer m_javaStartup;
    int m_index = 0;
    bool m_opened = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
