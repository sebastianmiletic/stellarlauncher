// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QPixmap>
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
        std::function<void()> importPack;
        std::function<void()> accounts;
        std::function<void()> skins;
        std::function<void(const QString&)> installLook;
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
    void activate(int index);
    void openInstance(const QString& id);
    void closeInstance();
    void playInstance(const QString& id);
    void refreshInstance();
    void finishLaunch();
    void showActions(int index);
    void showPreferences();
    void showCreator();
    void showContent(const QString& id);
    void copyInstance();
    void showWallpapers(const QString& instance);
    QFrame* makePanel(const QString& title);
    void dismissPanel();
    QStringList images(const Collection& collection) const;
    QPixmap artwork(const QString& instance, const QString& version);
    void fetchArtwork(const Collection& collection);
    void updateArtwork();
    void updateLaunch();
    void placePanel();

    Actions m_actions;
    QScrollArea* m_scroll;
    QWidget* m_cards;
    QHBoxLayout* m_row;
    QPropertyAnimation* m_slide;
    QTimer* m_launchTimer;
    QTimer* m_saveSelection;
    QMetaObject::Connection m_detailRunningChanged;
    QFrame* m_panel = nullptr;
    QWidget* m_detail = nullptr;
    QList<QAbstractButton*> m_tiles;
    QList<Collection> m_collections;
    QHash<QString, QPixmap> m_artwork;
    QSet<QString> m_fetching;
    QSet<QString> m_attempted;
    QStringList m_ids;
    QString m_selected;
    QString m_detailId;
    QString m_launching;
    QString m_wallpapers;
    QElapsedTimer m_javaStartup;
    int m_index = 0;
    bool m_opened = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
