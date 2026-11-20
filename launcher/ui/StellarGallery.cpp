// SPDX-License-Identifier: GPL-3.0-only
#include "StellarGallery.h"

#include <QAbstractButton>
#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCursor>
#include <QComboBox>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QDirListing>
#include <QDynamicPropertyChangeEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>
#include <QPointer>
#include <QListWidget>
#include <QMouseEvent>
#include <QMovie>
#include <QRandomGenerator>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

#include <utility>

#include "Application.h"
#include "FileSystem.h"
#include "InstanceList.h"
#include "MMCTime.h"
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/auth/AccountList.h"
#include "ui/dialogs/MSALoginDialog.h"
#include "minecraft/PackProfile.h"
#include "minecraft/StellarClient.h"
#include "minecraft/StellarMods.h"
#include "minecraft/skins/SkinModel.h"
#include "settings/SettingsObject.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
constexpr int windowWidth = 818;
constexpr int windowHeight = 414;
constexpr int gap = 5;
const QColor canvas("#141416");

struct InstanceVersion {
    QString minecraft;
    QString loader;
};

InstanceVersion versionOf(const MinecraftInstance* instance)
{
    QFile file(QDir(instance->instanceRoot()).filePath("mmc-pack.json"));
    InstanceVersion version;
    if (file.open(QIODevice::ReadOnly)) {
        const auto components = QJsonDocument::fromJson(file.readAll()).object().value("components").toArray();
        static const QHash<QString, QString> loaders {
            {"net.fabricmc.fabric-loader", "Fabric"}, {"net.minecraftforge", "Forge"},
            {"net.neoforged", "NeoForge"}, {"org.quiltmc.quilt-loader", "Quilt"},
            {"com.mumfrey.liteloader", "LiteLoader"}
        };
        for (const auto& component : components) {
            const auto item = component.toObject();
            const auto id = item.value("uid").toString();
            if (id == "net.minecraft")
                version.minecraft = item.value("version").toString();
            else if (const auto loader = loaders.value(id); !loader.isEmpty())
                version.loader = loader;
        }
    }
    return version;
}

void drawArtwork(QPainter& painter, const QRect& bounds, const QPixmap& picture)
{
    if (picture.isNull())
        return;
    const auto target = bounds.size() / picture.devicePixelRatio();
    QSize source = target;
    source.scale(picture.size(), Qt::KeepAspectRatio);
    painter.drawPixmap(bounds, picture, QRect(QPoint((picture.width() - source.width()) / 2,
                                                       (picture.height() - source.height()) / 2), source));
}

bool motionEnabled()
{
    return APPLICATION->settings()->get("StellarMotion").toBool();
}

class WallpaperPlayback final : public QObject {
public:
    explicit WallpaperPlayback(QWidget* host) : QObject(host), m_host(host)
    {
        host->installEventFilter(this);
        if (host->window() != host)
            host->window()->installEventFilter(this);
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { sync(); });
        connect(&movie, &QMovie::finished, this, [this] { sync(); });
    }

    void setFile(const QString& path, const QSize& target)
    {
        if (movie.fileName() == path)
            return;
        movie.stop();
        movie.setFileName(QString());
        if (!path.endsWith(".gif", Qt::CaseInsensitive) || QFileInfo(path).size() > 64 * 1024 * 1024)
            return;
        const auto size = QImageReader(path, "gif").size();
        if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
            return;
        movie.setFileName(path);
        movie.setScaledSize(size.boundedTo(size.scaled(target, Qt::KeepAspectRatio)));
        sync();
    }

    QMovie movie;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Hide:
            movie.setPaused(true);
            break;
        case QEvent::Show:
        case QEvent::WindowStateChange:
            QTimer::singleShot(0, this, [this] { sync(); });
            break;
        default:
            break;
        }
        return false;
    }

private:
    void sync()
    {
        if (movie.fileName().isEmpty())
            return;
        const bool visible = motionEnabled() && m_host->isVisible() && !m_host->window()->isMinimized()
            && QGuiApplication::applicationState() == Qt::ApplicationActive;
        if (!visible) {
            movie.setPaused(true);
        } else if (movie.state() == QMovie::Paused) {
            movie.setPaused(false);
        } else if (movie.state() == QMovie::NotRunning) {
            movie.start();
        }
    }

    QWidget* m_host;
};

class ButtonFeedback final : public QObject {
public:
    explicit ButtonFeedback(QAbstractButton* button) : m_button(button)
    {
        m_button->installEventFilter(this);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_level = value.toReal();
            m_button->update();
        });
        connect(m_button, &QAbstractButton::pressed, this, [this] { transition(-1); });
        connect(m_button, &QAbstractButton::released, this, [this] { transition(m_hovered ? 1 : 0); });
    }

    void paint(QPainter& painter, const QPainterPath& surface, bool light = false) const
    {
        if (!m_button->isEnabled() || qFuzzyIsNull(m_level))
            return;
        QColor color = m_level < 0 || light ? QColor("#14111b") : QColor("#e9e3f6");
        color.setAlphaF(qAbs(m_level) * (m_level < 0 ? 0.20 : light ? 0.07 : 0.12));
        painter.fillPath(surface, color);
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Enter:
            m_hovered = true;
            transition(m_button->isDown() ? -1 : 1);
            break;
        case QEvent::Leave:
        case QEvent::Hide:
            m_hovered = false;
            transition(0);
            break;
        case QEvent::EnabledChange:
            transition(m_button->isEnabled() && m_hovered ? 1 : 0);
            break;
        default:
            break;
        }
        return false;
    }

private:
    void transition(qreal target)
    {
        m_animation.stop();
        if (!motionEnabled() || !m_button->isVisible() || !m_button->isEnabled() || m_level == target) {
            m_level = target;
            m_button->update();
            return;
        }
        m_animation.setDuration(target < 0 ? 75 : target == 0 ? 160 : 125);
        m_animation.setStartValue(m_level);
        m_animation.setEndValue(target);
        m_animation.start();
    }

    QAbstractButton* m_button;
    QVariantAnimation m_animation;
    qreal m_level = 0;
    bool m_hovered = false;
};

class GalleryButton final : public QPushButton {
public:
    explicit GalleryButton(QWidget* parent) : GalleryButton({}, {}, parent) {}
    GalleryButton(const QString& text, QWidget* parent) : GalleryButton({}, text, parent) {}
    GalleryButton(const QIcon& icon, const QString& text, QWidget* parent)
        : QPushButton(icon, text, parent), m_feedback(this)
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);
        option.state.setFlag(QStyle::State_MouseOver, false);
        option.state.setFlag(QStyle::State_Sunken, false);

        QPainter painter(this);
        style()->drawControl(QStyle::CE_PushButtonBevel, &option, &painter, this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath surface;
        surface.addRoundedRect(option.rect.adjusted(2, 2, -2, -2), 7, 7);
        m_feedback.paint(painter, surface, property("stellarPrimary").toBool());
        // Draw the label and focus indicator above the animated background.
        QStyleOptionButton label(option);
        label.rect = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        style()->drawControl(QStyle::CE_PushButtonLabel, &label, &painter, this);
        if (hasFocus()) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.state = option.state;
            focus.rect = style()->subElementRect(QStyle::SE_PushButtonFocusRect, &option, this);
            focus.backgroundColor = palette().color(QPalette::Button);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
        }
    }

private:
    ButtonFeedback m_feedback;
};

QIcon accountFace(const MinecraftAccount* account)
{
    if (account) {
        const auto face = account->getFace(36, 36);
        if (!face.isNull())
            return QIcon(face);
    }
    return QIcon(":/icons/multimc/128x128/instances/steve_legacy.png");
}

bool wallpaperEffect(const QString& effect)
{
    return effect == "wave" || effect == "fade" || effect == "wipe" || effect == "iris";
}

class LibraryReveal final : public QWidget {
public:
    explicit LibraryReveal(QWidget* parent)
        : QWidget(parent), m_logo(APPLICATION->logo().pixmap(66, 66))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parent->rect());
        auto* transition = new QVariantAnimation(this);
        transition->setDuration(155);
        transition->setStartValue(0.0);
        transition->setEndValue(1.0);
        transition->setEasingCurve(QEasingCurve::OutCubic);
        connect(transition, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_reveal = value.toReal();
            update();
        });
        connect(transition, &QVariantAnimation::finished, this, &QObject::deleteLater);
        show();
        transition->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter paint(this);
        QColor backdrop(canvas);
        backdrop.setAlphaF(1 - m_reveal);
        paint.fillRect(rect(), backdrop);
        paint.setOpacity((1 - m_reveal) * (1 - m_reveal));
        paint.drawPixmap(QRect(width() / 2 - 33, height() / 2 - 37 - qRound(7 * m_reveal), 66, 66), m_logo);
    }

private:
    QPixmap m_logo;
    qreal m_reveal = 0;
};

class GalleryTile final : public QAbstractButton {
public:
    GalleryTile(const QString& title, QString version, QPixmap image, QWidget* parent)
        : QAbstractButton(parent), m_version(std::move(version)), m_image(std::move(image)), m_feedback(this)
    {
        setText(title);
        setAccessibleName(title);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        m_focus.setDuration(155);
        m_focus.setEasingCurve(QEasingCurve::OutQuart);
        connect(&m_focus, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_border = value.toDouble();
            update();
        });
    }

    void setSelected(bool selected, bool animate)
    {
        const auto value = selected ? 1.0 : 0.0;
        if (animate && m_focus.endValue().isValid() && m_focus.endValue().toDouble() != value) {
            m_focus.stop();
            m_focus.setStartValue(m_border);
            m_focus.setEndValue(value);
            m_focus.start();
        } else if (!animate || !m_focus.endValue().isValid()) {
            m_focus.stop();
            m_focus.setStartValue(value);
            m_focus.setEndValue(value);
            m_border = value;
            update();
        }
    }
    void setAccountCard(QPixmap skin, QString username)
    {
        m_accountCard = true;
        m_accountSkin = std::move(skin);
        m_accountName = std::move(username);
        update();
    }

    void setArtwork(const QPixmap& image) { m_image = image; update(); }
    std::function<void(const QPoint&)> options;
    std::function<void()> open;
    std::function<void()> play;
    std::function<void(const QString&, const QPoint&)> reorder;
    std::function<void(bool)> dragging;
    QString instanceId;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_doubleClick = false;
            m_pressPosition = event->position().toPoint();
        }
        QAbstractButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!m_reordering && isDown() && !instanceId.isEmpty() && event->buttons().testFlag(Qt::LeftButton)
            && (event->position().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            ++m_clickNumber;
            m_reordering = true;
            setDown(false);
            setCursor(Qt::ClosedHandCursor);
            dragging(true);
            update();
        }
        if (!m_reordering)
            QAbstractButton::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_reordering) {
            m_reordering = false;
            setCursor(Qt::PointingHandCursor);
            dragging(false);
            update();
            reorder(instanceId, event->globalPosition().toPoint());
            return;
        }
        if (event->button() != Qt::LeftButton || !play) {
            QAbstractButton::mouseReleaseEvent(event);
            return;
        }
        const bool click = isDown() && hitButton(event->position().toPoint());
        const QSignalBlocker block(this);
        QAbstractButton::mouseReleaseEvent(event);
        if (click && !m_doubleClick) {
            const auto clickNumber = ++m_clickNumber;
            QTimer::singleShot(QApplication::doubleClickInterval(), this, [this, clickNumber] {
                if (isVisible() && m_clickNumber == clickNumber && open)
                    open();
            });
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && play) {
            ++m_clickNumber;
            m_doubleClick = true;
            event->accept();
            play();
        } else {
            QAbstractButton::mouseDoubleClickEvent(event);
        }
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        ++m_clickNumber;
        if (options) {
            event->accept();
            options(event->globalPos());
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto bounds = rect().adjusted(1, 1, -1, -1);
        QPainterPath shape;
        shape.addRoundedRect(bounds, 13, 13);
        painter.save();
        painter.setClipPath(shape);
        painter.fillRect(bounds, QColor("#1c1c20"));

        if (m_accountCard) {
            QRadialGradient glow(bounds.center() - QPoint(0, 22), qMax(125, width() / 2));
            glow.setColorAt(0, QColor("#33313b"));
            glow.setColorAt(1, QColor("#1c1c20"));
            painter.fillRect(bounds, glow);
            const int scale = qBound(3, width() / 44, 6);
            const QRect figure(bounds.center().x() - 8 * scale, bounds.center().y() - 16 * scale - 25, 16 * scale, 32 * scale);
            if (m_accountSkin.isNull()) {
                const auto face = accountFace(nullptr).pixmap(8 * scale, 8 * scale);
                painter.drawPixmap(QRect(figure.x() + 4 * scale, figure.y(), 8 * scale, 8 * scale), face);
                painter.fillRect(figure.x(), figure.y() + 8 * scale, 16 * scale, 12 * scale, QColor("#464651"));
                painter.fillRect(figure.x() + 4 * scale, figure.y() + 20 * scale, 8 * scale, 12 * scale, QColor("#353541"));
            } else {
                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                painter.drawPixmap(figure, m_accountSkin);
            }
            auto heading = font();
            heading.setPixelSize(17);
            heading.setWeight(QFont::DemiBold);
            painter.setFont(heading);
            painter.setPen(QColor("#ecebf0"));
            const QRect title(12, height() - 76, width() - 24, 26);
            painter.drawText(title, Qt::AlignCenter, painter.fontMetrics().elidedText(text(), Qt::ElideRight, title.width()));
            heading.setPixelSize(13);
            heading.setWeight(QFont::Normal);
            painter.setFont(heading);
            painter.setPen(QColor("#bcb9c5"));
            painter.drawText(title.translated(0, 24), Qt::AlignCenter,
                             painter.fontMetrics().elidedText(m_accountName, Qt::ElideRight, title.width()));
        } else if (m_version.isEmpty() && m_image.isNull()) {
            const QPoint center = bounds.center() - QPoint(0, 10);
            painter.setPen(QPen(QColor("#ecebf0"), 1.6, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(center - QPoint(8, 0), center + QPoint(8, 0));
            painter.drawLine(center - QPoint(0, 8), center + QPoint(0, 8));
            painter.drawText(QRect(0, center.y() + 18, width(), 24), Qt::AlignCenter, text());
        } else {
            drawArtwork(painter, bounds, m_image);
            QLinearGradient shade(0, height() - 120, 0, height());
            shade.setColorAt(0, QColor(15, 14, 17, 0));
            shade.setColorAt(1, QColor(15, 14, 17, 238));
            painter.fillRect(QRect(0, height() - 120, width(), 120), shade);
            auto heading = font();
            heading.setPixelSize(17);
            heading.setWeight(QFont::DemiBold);
            painter.setFont(heading);
            painter.setPen(QColor("#f0eff3"));
            painter.drawText(QRect(17, height() - 59, width() - 34, 24), Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(text(), Qt::ElideRight, width() - 34));
            heading.setPixelSize(12);
            heading.setWeight(QFont::Medium);
            painter.setFont(heading);
            painter.setPen(QColor("#cbc9d0"));
            painter.drawText(QRect(17, height() - 35, width() - 34, 19), Qt::AlignVCenter, m_version);
        }
        m_feedback.paint(painter, shape);
        if (m_reordering)
            painter.fillRect(bounds, QColor(20, 20, 22, 102));
        painter.restore();
        painter.setPen(QPen(QColor("#37363d"), 1));
        painter.drawPath(shape);
        QColor selection("#e9e7f1");
        selection.setAlphaF(m_border);
        painter.setPen(QPen(selection, 2));
        painter.drawPath(shape);
    }

private:
    QString m_version;
    QPixmap m_image;
    QPixmap m_accountSkin;
    QString m_accountName;
    bool m_accountCard = false;
    QVariantAnimation m_focus;
    ButtonFeedback m_feedback;
    double m_border = 0;
    int m_clickNumber = 0;
    QPoint m_pressPosition;
    bool m_reordering = false;
    bool m_doubleClick = false;
};

class InstanceDetail final : public QWidget {
public:
    InstanceDetail(QWidget* parent, const std::function<void()>& back, const std::function<void()>& play,
                   const std::function<void()>& options)
        : QWidget(parent), m_title(new QLabel(this)), m_description(new QLabel(this)),
          m_play(new GalleryButton(this)), m_progress(new QProgressBar(this))
    {
        setObjectName("stellarInstance");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(0);

        auto* header = new QHBoxLayout;
        auto* backButton = new GalleryButton(tr("‹  Library"), this);
        backButton->setAccessibleName(tr("Back to Library"));
        backButton->setObjectName("stellarArtButton");
        connect(backButton, &QPushButton::clicked, this, back);
        header->addWidget(backButton);
        header->addStretch();
        layout->addLayout(header);
        layout->addStretch();

        auto* footer = new QHBoxLayout;
        footer->setSpacing(9);
        auto* info = new QVBoxLayout;
        info->setSpacing(4);
        m_title->setObjectName("stellarInstanceName");
        m_description->setObjectName("stellarInstanceInfo");
        info->addWidget(m_title);
        info->addWidget(m_description);
        footer->addLayout(info, 1);

        auto* launch = new QVBoxLayout;
        launch->setContentsMargins(0, 3, 0, 0);
        launch->setSpacing(4);
        m_play->setProperty("stellarPrimary", true);
        m_play->setFixedSize(154, 39);
        connect(m_play, &QPushButton::clicked, this, play);
        launch->addWidget(m_play);
        m_progress->setObjectName("stellarLaunchProgress");
        m_progress->setFixedSize(154, 3);
        m_progress->setTextVisible(false);
        m_progress->setAccessibleName(tr("Minecraft launch progress"));
        auto policy = m_progress->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        m_progress->setSizePolicy(policy);
        m_progress->hide();
        launch->addWidget(m_progress);
        footer->addLayout(launch);

        auto* menu = new GalleryButton(QStringLiteral("•••"), this);
        menu->setObjectName("stellarInstanceOptions");
        menu->setAccessibleName(tr("Instance Options"));
        menu->setToolTip(tr("Instance Options"));
        menu->setFixedSize(44, 39);
        connect(menu, &QPushButton::clicked, this, options);
        footer->addWidget(menu, 0, Qt::AlignTop);
        layout->addLayout(footer);
        m_waveFill.setDuration(175);
        m_waveFill.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_waveFill, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_fill = value.toDouble();
            update();
        });
        m_wave.setDuration(1800);
        m_wave.setLoopCount(-1);
        m_wave.setStartValue(0.0);
        m_wave.setEndValue(1.0);
        connect(&m_wave, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&m_playback->movie, &QMovie::frameChanged, this, [this] { update(); });
    }

    void setWallpaperFile(const QString& path)
    {
        m_playback->setFile(motionEnabled() ? path : QString(), QSize(1400, 800));
    }

    void setInstance(const QString& name, const QString& info, const QPixmap& wallpaper)
    {
        m_title->setText(m_title->fontMetrics().elidedText(name, Qt::ElideRight, 535));
        m_title->setAccessibleName(name);
        m_description->setText(info);
        if (m_wallpaper.cacheKey() != wallpaper.cacheKey()) {
            m_wallpaper = wallpaper;
            m_monochrome = {};
            update();
        }
    }

    void setLaunchState(const QString& text, bool busy, bool animate, int progress = -1)
    {
        m_play->setText(text);
        m_play->setEnabled(!busy);
        static const QIcon playIcon(":/icons/flat/scalable/launch.svg");
        if (busy && !m_play->icon().isNull())
            m_play->setIcon(QIcon());
        else if (!busy && m_play->icon().isNull())
            m_play->setIcon(playIcon);
        m_effect = APPLICATION->settings()->get("StellarLaunchAnimation").toString();
        const bool cover = busy && animate && wallpaperEffect(m_effect);
        if (cover) {
            if (m_wave.state() != QAbstractAnimation::Running) {
                m_fill = 0.07;
                m_monochrome = QPixmap::fromImage(m_wallpaper.toImage().convertToFormat(QImage::Format_Grayscale8));
                m_wave.start();
            }
            const double targetFill = progress >= 0 ? 0.12 + 0.74 * progress / 100.0 : m_fill;
            if (targetFill > m_fill && m_waveFill.endValue().toDouble() != targetFill) {
                m_waveFill.stop();
                m_waveFill.setStartValue(m_fill);
                m_waveFill.setEndValue(targetFill);
                m_waveFill.start();
            }
        } else if (m_wave.state() == QAbstractAnimation::Running) {
            m_wave.stop();
            m_waveFill.stop();
            m_waveFill.setEndValue(0.0);
            update();
        }
        if (!cover && busy && (animate || progress >= 0) && m_effect != "off") {
            const int maximum = progress >= 0 ? 100 : 0;
            if (m_progress->maximum() != maximum)
                m_progress->setRange(0, maximum);
            if (progress >= 0 && m_progress->value() != progress)
                m_progress->setValue(progress);
            m_progress->show();
        } else {
            m_progress->hide();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect hero(0, 0, width(), height() - 79);
        if (m_wave.state() == QAbstractAnimation::Running) {
            if (m_monochrome.isNull() && !m_wallpaper.isNull())
                m_monochrome = QPixmap::fromImage(m_wallpaper.toImage().convertToFormat(QImage::Format_Grayscale8));
            drawArtwork(painter, hero, m_monochrome);
            const double phase = 6.283185307 * m_wave.currentValue().toDouble();
            painter.save();
            if (m_effect == "fade") {
                painter.setOpacity(std::clamp(m_fill + 0.045 * std::sin(phase), 0.0, 1.0));
            } else {
                QPainterPath color;
                if (m_effect == "wipe") {
                    color.addRect(QRectF(hero.left(), hero.top(), hero.width() * m_fill, hero.height()));
                } else if (m_effect == "iris") {
                    const auto radius = std::hypot(hero.width() / 2.0, hero.height() / 2.0) * m_fill;
                    color.addEllipse(QPointF(hero.center()), radius, radius);
                } else {
                    const double baseline = hero.bottom() - hero.height() * m_fill;
                    color.moveTo(0, hero.bottom());
                    for (int x = 0; x <= hero.width(); x += 8)
                        color.lineTo(x, baseline + 11 * std::sin(phase + 0.018 * x));
                    color.lineTo(hero.width(), hero.bottom());
                    color.closeSubpath();
                }
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setClipPath(color);
            }
            drawArtwork(painter, hero, m_wallpaper);
            painter.restore();
        } else {
            drawArtwork(painter, hero, m_playback->movie.currentPixmap().isNull() || m_playback->movie.fileName().isEmpty()
                ? m_wallpaper : m_playback->movie.currentPixmap());
        }
        QLinearGradient shade(0, hero.bottom() - 98, 0, hero.bottom() + 1);
        shade.setColorAt(0, QColor(20, 20, 22, 0));
        shade.setColorAt(1, canvas);
        painter.fillRect(QRect(0, hero.bottom() - 98, width(), 100), shade);
    }

private:
    QLabel* m_title;
    QLabel* m_description;
    QPushButton* m_play;
    QProgressBar* m_progress;
    WallpaperPlayback* m_playback = new WallpaperPlayback(this);
    QVariantAnimation m_wave;
    QVariantAnimation m_waveFill;
    double m_fill = 0.07;
    QString m_effect;
    QPixmap m_wallpaper;
    QPixmap m_monochrome;
};

bool official(const QUrl& url)
{
    return url.scheme() == "https" && url.host() == "www.minecraft.net";
}

bool saveBytes(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

StellarGallery::StellarGallery(Actions actions, QWidget* parent)
    : QWidget(parent), m_actions(std::move(actions)), m_scroll(new QScrollArea(this)), m_cards(new QWidget),
      m_row(new QHBoxLayout(m_cards)), m_slide(new QPropertyAnimation(m_scroll->horizontalScrollBar(), "value", this)),
      m_launchTimer(new QTimer(this)), m_saveSelection(new QTimer(this))
{
    setFixedSize(windowWidth, windowHeight);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName("stellarGallery");
    setStyleSheet("QWidget#stellarGallery { background: #141416; }"
                  "QFrame#stellarPanel { background: #1c1c20; border: 1px solid #37363d; border-radius: 14px; }"
                  "QLabel#stellarHeading { font-size: 17px; font-weight: 600; }"
                  "QPushButton[stellarPrimary=true] { background: #e9e7f1; color: #18171c; }"
                  "QPushButton[stellarPrimary=true]:hover { background: #f5f3fb; }"
                  "QPushButton[stellarPrimary=true]:disabled { background: #29282f; color: #bcb9c5; }"
                  "QLabel#stellarInstanceName { font-size: 24px; font-weight: 650; }"
                  "QLabel#stellarInstanceInfo { font-size: 12px; color: #bcb9c5; }"
                  "QPushButton#stellarArtButton { background: #1c1c20; border-color: #4c4b54; padding: 6px 10px; }"
                  "QPushButton#stellarInstanceOptions { margin-top: 3px; }"
                  "QProgressBar#stellarLaunchProgress { border: 0; border-radius: 1px; background: #34323a; }"
                  "QProgressBar#stellarLaunchProgress::chunk { border-radius: 1px; background: #d0c9e3; }");
    m_scroll->setGeometry(rect());
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setWidget(m_cards);
    m_scroll->viewport()->installEventFilter(this);
    m_row->setContentsMargins(gap, gap, gap, gap);
    m_row->setSpacing(gap);
    m_slide->setDuration(220);
    m_slide->setEasingCurve(QEasingCurve::OutQuart);
    auto* preferences = new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this);
    connect(preferences, &QShortcut::activated, this, &StellarGallery::showPreferences);
    auto* create = new QShortcut(QKeySequence::New, this);
    connect(create, &QShortcut::activated, this, &StellarGallery::showCreator);
    auto* dismiss = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(dismiss, &QShortcut::activated, this, [this] {
        if (m_panel)
            dismissPanel();
        else
            closeInstance();
    });
    m_saveSelection->setSingleShot(true);
    m_saveSelection->setInterval(170);
    connect(m_saveSelection, &QTimer::timeout, this, [this] {
        APPLICATION->settings()->set("SelectedInstance", m_selected);
    });
    auto* dragScroll = new QTimer(this);
    dragScroll->setInterval(25);
    connect(dragScroll, &QTimer::timeout, this, [this] {
        const auto point = mapFromGlobal(QCursor::pos());
        if (!rect().contains(point))
            return;
        int movement = 0;
        if (point.x() < 34)
            movement = -14;
        else if (point.x() > width() - 34)
            movement = 14;
        auto* bar = m_scroll->horizontalScrollBar();
        if (movement)
            bar->setValue(bar->value() + movement);
    });
    m_dragScroll = dragScroll;
    m_launchTimer->setInterval(250);
    connect(m_launchTimer, &QTimer::timeout, this, &StellarGallery::updateLaunch);

    m_wallpapers = QDir(APPLICATION->dataRoot()).filePath("wallpapers");
    QFile catalog(":/stellar-gallery/wallpapers.json");
    if (catalog.open(QIODevice::ReadOnly)) {
        const auto entries = QJsonDocument::fromJson(catalog.readAll()).array();
        for (const auto& value : entries) {
            const auto entry = value.toObject();
            QStringList versions;
            for (const auto& version : entry.value("versions").toArray())
                versions.append(version.toString());
            m_collections.append({entry.value("id").toString(), entry.value("title").toString(), versions,
                                  QUrl(entry.value("archive").toString()), QUrl(entry.value("fallback").toString())});
        }
    }

    m_collections.append({QStringLiteral("animated"), tr("Animated GIFs"), {}, {}, {}});
    auto* accounts = APPLICATION->accounts();
    connect(accounts, &AccountList::listChanged, this, &StellarGallery::updateAccountIcons);
    connect(accounts, &AccountList::defaultAccountChanged, this, &StellarGallery::updateAccountIcons);
    connect(accounts, &QAbstractItemModel::dataChanged, this, &StellarGallery::updateAccountIcons);

    m_selected = APPLICATION->settings()->get("SelectedInstance").toString();
    auto* list = APPLICATION->instances();
    connect(list, &QAbstractItemModel::modelReset, this, &StellarGallery::rebuild);
    connect(list, &QAbstractItemModel::rowsInserted, this, &StellarGallery::rebuild);
    connect(list, &QAbstractItemModel::rowsRemoved, this, &StellarGallery::rebuild);
    connect(list, &QAbstractItemModel::dataChanged, this, &StellarGallery::rebuild);
    connect(list, &InstanceList::instanceSelectRequest, this, &StellarGallery::selectInstance);
    rebuild();
}

void StellarGallery::rebuild()
{
    auto* list = APPLICATION->instances();
    QList<MinecraftInstance*> instances;
    for (int i = 0; i < list->count(); ++i)
        instances.append(list->at(i));
    std::stable_sort(instances.begin(), instances.end(), [](const auto* left, const auto* right) {
        return QString::localeAwareCompare(left->name(), right->name()) < 0;
    });

    const auto savedOrder = APPLICATION->settings()->get("StellarInstanceOrder").toStringList();
    QHash<QString, qsizetype> order;
    for (qsizetype i = 0; i < savedOrder.size(); ++i)
        order.insert(savedOrder.at(i), i);
    std::stable_sort(instances.begin(), instances.end(), [&order, &savedOrder](const auto* left, const auto* right) {
        return order.value(left->id(), savedOrder.size()) < order.value(right->id(), savedOrder.size());
    });
    m_allIds.clear();
    QHash<QString, InstanceVersion> versions;
    for (const auto* instance : std::as_const(instances)) {
        m_allIds.append(instance->id());
        versions.insert(instance->id(), versionOf(instance));
    }
    const auto filter = APPLICATION->settings()->get("StellarLoaderFilter").toString();
    if (!filter.isEmpty()) {
        instances.removeIf([&](const auto* instance) {
            const auto loader = versions.value(instance->id()).loader;
            return filter != (loader.isEmpty() ? QStringLiteral("Vanilla") : loader);
        });
    }
    const int oldOffset = m_scroll->horizontalScrollBar()->value();
    m_slide->stop();
    for (auto* tile : std::as_const(m_tiles)) {
        m_row->removeWidget(tile);
        tile->hide();
        tile->deleteLater();
    }
    m_tiles.clear();
    m_ids.clear();

    const int columns = std::clamp(int(instances.size()) + 2, 2, 3);
    const int width = (windowWidth - gap * (columns + 1)) / columns;
    for (int i = 0; i <= instances.size(); ++i) {
        const auto* instance = i < instances.size() ? instances[i] : nullptr;
        const auto id = instance ? instance->id() : QString();
        const auto title = instance ? instance->name() : tr("Create New Instance");
        auto version = versions.value(id).minecraft;
        const auto path = instance ? artworkPath(id, version) : QString();
        auto pixmap = instance ? m_thumbnails.value(path) : QPixmap();
        if (instance && version.isEmpty())
            version = tr("Minecraft");
        auto* tile = new GalleryTile(title, version, pixmap, m_cards);
        tile->setProperty("wallpaperPath", path);
        tile->setFixedSize(width, windowHeight - gap * 2);
        m_row->addWidget(tile);
        m_tiles.append(tile);
        tile->open = [this, i] { activate(i); };
        connect(tile, &QAbstractButton::clicked, this, tile->open);
        if (instance) {
            m_ids.append(id);
            tile->play = [this, i, id] {
                dismissPanel();
                selectIndex(i, false);
                openInstance(id);
                playInstance(id);
            };
        }
        tile->instanceId = id;
        tile->dragging = [this](bool active) {
            if (active) {
                m_slide->stop();
                m_dragScroll->start();
            } else {
                m_dragScroll->stop();
            }
        };
        tile->reorder = [this](const QString& source, const QPoint& screenPoint) {
            if (!m_scroll->viewport()->rect().contains(m_scroll->viewport()->mapFromGlobal(screenPoint)))
                return;
            auto* target = dynamic_cast<GalleryTile*>(m_cards->childAt(m_cards->mapFromGlobal(screenPoint)));
            if (!target || target->instanceId == source)
                return;
            auto order = m_allIds;
            if (!order.removeOne(source))
                return;
            const bool after = target->mapFromGlobal(screenPoint).x() >= target->width() / 2;
            const auto id = target->instanceId;
            const auto position = id.isEmpty() ? order.size() : order.indexOf(id) + (after ? 1 : 0);
            order.insert(position, source);
            APPLICATION->settings()->set("StellarInstanceOrder", order);
            m_selected = source;
            m_saveSelection->start();
            QTimer::singleShot(0, this, &StellarGallery::rebuild);
        };
        tile->options = [this, i](const QPoint& screen) {
            showActions(i, QRect(mapFromGlobal(screen), QSize(1, 1)));
        };
        tile->installEventFilter(this);
    }
    auto* profile = new GalleryTile(tr("Accounts & Settings"), {}, {}, m_cards);
    profile->setFixedSize(width, windowHeight - gap * 2);
    profile->installEventFilter(this);
    profile->options = [this](const QPoint&) { showAccounts(); };
    m_accountTile = profile;
    m_row->addWidget(profile);
    m_tiles.append(profile);
    connect(profile, &QAbstractButton::clicked, this, [this] { activate(m_tiles.size() - 1); });
    updateAccountIcons();
    m_cards->setFixedSize(gap + int(m_tiles.size()) * (width + gap), windowHeight);
    m_row->activate();
    m_scroll->horizontalScrollBar()->setValue(oldOffset);
    scheduleThumbnails();
    const int selectedIndex = m_ids.indexOf(m_selected);
    selectIndex(selectedIndex >= 0 ? selectedIndex : 0, false);
    if (m_detail) {
        if (m_ids.contains(m_detailId))
            refreshInstance();
        else
            closeInstance();
    }
}

void StellarGallery::selectInstance(const QString& id)
{
    if (const auto index = m_ids.indexOf(id); index >= 0)
        selectIndex(index, true);
}

void StellarGallery::selectIndex(int index, bool animate)
{
    if (m_tiles.isEmpty())
        return;
    m_index = std::clamp(index, 0, int(m_tiles.size()) - 1);
    for (int i = 0; i < m_tiles.size(); ++i)
        static_cast<GalleryTile*>(m_tiles[i])->setSelected(i == m_index, animate && motionEnabled());
    if (m_index < m_ids.size() && m_selected != m_ids[m_index]) {
        m_selected = m_ids[m_index];
        m_saveSelection->start();
    }
    auto* bar = m_scroll->horizontalScrollBar();
    const auto tile = m_tiles[m_index]->geometry();
    const int offset = bar->value();
    int target = offset;
    if (tile.right() + gap >= offset + m_scroll->viewport()->width())
        target = tile.right() + gap + 1 - m_scroll->viewport()->width();
    else if (tile.left() - gap < offset)
        target = tile.left() - gap;
    m_slide->stop();
    target = std::clamp(target, bar->minimum(), bar->maximum());
    if (animate && target != offset && motionEnabled()) {
        m_slide->setStartValue(offset);
        m_slide->setEndValue(target);
        m_slide->start();
    } else {
        bar->setValue(target);
    }
}

void StellarGallery::activate(int index)
{
    dismissPanel();
    selectIndex(index, true);
    if (index < m_ids.size())
        openInstance(m_ids.at(index));
    else if (index == m_ids.size())
        showCreator();
    else
        showAccounts();
}

void StellarGallery::openInstance(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance)
        return;
    if (m_detailId != id) {
        disconnect(m_detailRunningChanged);
        m_detailRunningChanged = connect(instance, &MinecraftInstance::runningStatusChanged, this,
                                         [this] { updateLaunch(); });
        m_detailId = id;
    }
    if (!m_detail) {
        auto* detail = new InstanceDetail(this, [this] { closeInstance(); },
                                         [this] { playInstance(m_detailId); },
                                         [this] { showActions(m_ids.indexOf(m_detailId)); });
        detail->setGeometry(rect());
        detail->installEventFilter(this);
        m_detail = detail;
        if (motionEnabled()) {
            auto* effect = new QGraphicsOpacityEffect(detail);
            detail->setGraphicsEffect(effect);
            auto* transition = new QPropertyAnimation(effect, "opacity", detail);
            transition->setDuration(155);
            transition->setStartValue(0.58);
            transition->setEndValue(1.0);
            transition->setEasingCurve(QEasingCurve::OutCubic);
            connect(transition, &QPropertyAnimation::finished, detail, [detail] { detail->setGraphicsEffect(nullptr); });
            transition->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
    m_detailWallpaper.clear();
    const auto shuffle = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap().value(id).toString();
    auto collection = std::find_if(m_collections.cbegin(), m_collections.cend(),
                                   [&shuffle](const auto& item) { return item.id == shuffle; });
    if (collection != m_collections.cend()) {
        const auto paths = images(*collection);
        if (!paths.isEmpty())
            m_detailWallpaper = paths.at(QRandomGenerator::global()->bounded(int(paths.size())));
    }
    m_scroll->hide();
    m_detail->show();
    m_detail->raise();
    refreshInstance();
    setFocus();
}

void StellarGallery::closeInstance()
{
    disconnect(m_detailRunningChanged);
    m_detailId.clear();
    if (auto* detail = std::exchange(m_detail, nullptr)) {
        detail->hide();
        detail->deleteLater();
        m_scroll->show();
    }
    setFocus();
}

void StellarGallery::refreshInstance()
{
    auto* instance = APPLICATION->instances()->getInstanceById(m_detailId);
    if (!m_detail || !instance)
        return;
    auto* detail = static_cast<InstanceDetail*>(m_detail);
    const auto version = versionOf(instance);
    QStringList info;
    info.append(version.minecraft.isEmpty() ? tr("Minecraft Java Edition") : tr("Minecraft %1").arg(version.minecraft));
    info.append(version.loader.isEmpty() ? tr("Vanilla") : version.loader);
    const auto played = instance->totalTimePlayed();
    info.append(played > 0 ? tr("%1 played").arg(Time::prettifyDuration(played)) : tr("Never played"));
    const auto path = m_detailWallpaper.isEmpty() ? artworkPath(instance->id(), version.minecraft) : m_detailWallpaper;
    detail->setInstance(instance->name(), info.join(QStringLiteral("  ·  ")), wallpaperPixmap(path));
    detail->setWallpaperFile(path);
    updateLaunch();
}

void StellarGallery::playInstance(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance || !m_launching.isEmpty())
        return;

    if (!instance->canLaunch()) {
        APPLICATION->launch(instance);
        return;
    }

    if (!m_preflight.isEmpty())
        return;
    m_preflight = id;
    const QDir mods(QDir(instance->gameRoot()).filePath("mods"));
    const auto versions = StellarClient::versions(instance);
    QtConcurrent::run([mods, versions] { return StellarMods::incompatible(mods, versions); })
        .then(this, [this, id](const QStringList& issues) {
            m_preflight.clear();
            if (issues.isEmpty()) {
                launchChecked(id);
                return;
            }
            auto* panel = makePanel(tr("Mod Compatibility"));
            panel->setFixedWidth(545);
            auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
            auto* problems = new QListWidget(panel);
            problems->setAccessibleName(tr("Incompatible mods"));
            problems->setFixedHeight(124);
            problems->addItems(issues);
            for (int i = 0; i < problems->count(); ++i)
                problems->item(i)->setToolTip(problems->item(i)->text());
            layout->addWidget(problems);
            auto* launch = new GalleryButton(tr("Launch Anyway"), panel);
            layout->addWidget(launch, 0, Qt::AlignRight);
            connect(launch, &QPushButton::clicked, panel, [this, id] {
                dismissPanel();
                launchChecked(id);
            });
            placePanel();
        });
}

void StellarGallery::launchChecked(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance || !m_launching.isEmpty())
        return;
    m_launching = id;
    m_javaStartup.invalidate();
    instance->setProperty("stellarLaunchState", "preparing");
    instance->setProperty("stellarGalleryFullscreen", APPLICATION->settings()->get("StellarFillScreen"));
    if (m_detailId == id)
        static_cast<InstanceDetail*>(m_detail)->setLaunchState(tr("Preparing…"), true,
            motionEnabled() &&
            APPLICATION->settings()->get("StellarLaunchAnimation").toString() != "off");
    if (!APPLICATION->launch(instance)) {
        finishLaunch();
        return;
    }
    instance->installEventFilter(this);
    m_launchTimer->start();
}

void StellarGallery::finishLaunch()
{
    m_launchTimer->stop();
    if (auto* launching = APPLICATION->instances()->getInstanceById(m_launching))
        launching->removeEventFilter(this);
    m_launching.clear();
    m_javaStartup.invalidate();
    if (m_detail) {
        const auto* instance = APPLICATION->instances()->getInstanceById(m_detailId);
        const bool running = instance && instance->isRunning();
        static_cast<InstanceDetail*>(m_detail)->setLaunchState(running ? tr("Running") : tr("Play"), running, false);
    }
}

void StellarGallery::keyPressEvent(QKeyEvent* event)
{
    if (m_panel) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
        dismissPanel();
        if (m_detail && !m_ids.isEmpty()) {
            const int index = m_ids.indexOf(m_detailId);
            const int next = event->key() == Qt::Key_Home ? 0 : event->key() == Qt::Key_End ? m_ids.size() - 1 :
                std::clamp(index + (event->key() == Qt::Key_Left ? -1 : 1), 0, int(m_ids.size() - 1));
            selectIndex(next, false);
            openInstance(m_ids[next]);
            break;
        }
        selectIndex(event->key() == Qt::Key_Home ? 0 : event->key() == Qt::Key_End ? m_tiles.size() - 1 :
                    m_index + (event->key() == Qt::Key_Left ? -1 : 1), true);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!event->isAutoRepeat()) {
            if (m_detail)
                playInstance(m_detailId);
            else
                activate(m_index);
        }
        break;
    case Qt::Key_Menu:
        showActions(m_index);
        break;
    case Qt::Key_F10:
        if (event->modifiers().testFlag(Qt::ShiftModifier))
            showActions(m_index);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

bool StellarGallery::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::DynamicPropertyChange && !m_launching.isEmpty()) {
        const auto* change = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (change->propertyName() == "stellarLaunchState" && object->property("stellarLaunchState").toString() == "ready")
            updateLaunch();
    }
    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const bool precise = !wheel->pixelDelta().isNull();
        const auto delta = precise ? wheel->pixelDelta() : wheel->angleDelta();
        const int scroll = delta.x() != 0 ? delta.x() : delta.y();
        auto* bar = m_scroll->horizontalScrollBar();
        m_slide->stop();
        bar->setValue(bar->value() - (precise ? scroll : scroll / 2));
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_panel && mouse->button() == Qt::LeftButton)
            dismissPanel();
        const auto point = mapFromGlobal(mouse->globalPosition().toPoint());
        if (mouse->button() == Qt::LeftButton && point.y() < gap && window()->windowHandle()) {
            if (!window()->windowHandle()->startSystemMove()) {
                m_dragging = true;
                m_dragOffset = mouse->globalPosition().toPoint() - window()->frameGeometry().topLeft();
            }
            return true;
        }
    }
    if (m_dragging && event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton))
            window()->move(mouse->globalPosition().toPoint() - m_dragOffset);
        else
            m_dragging = false;
        return true;
    }
    if (m_dragging && event->type() == QEvent::MouseButtonRelease) {
        m_dragging = false;
        return true;
    }
    return QWidget::eventFilter(object, event);
}

void StellarGallery::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        dismissPanel();
    QWidget::mousePressEvent(event);
}

void StellarGallery::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (std::exchange(m_opened, true) || !motionEnabled() ||
        !APPLICATION->settings()->get("StellarOpeningAnimation").toBool())
        return;
    new LibraryReveal(this);
}

QFrame* StellarGallery::makePanel(const QString& title)
{
    dismissPanel();
    m_panel = new QFrame(this);
    m_panel->setObjectName("stellarPanel");
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(15, 13, 15, 15);
    layout->setSpacing(9);
    auto* header = new QHBoxLayout;
    auto* heading = new QLabel(title, m_panel);
    heading->setObjectName("stellarHeading");
    header->addWidget(heading, 1);
    auto* close = new GalleryButton(QStringLiteral("×"), m_panel);
    close->setAccessibleName(tr("Close"));
    close->setFixedWidth(34);
    connect(close, &QPushButton::clicked, this, &StellarGallery::dismissPanel);
    header->addWidget(close);
    layout->addLayout(header);
    return m_panel;
}

void StellarGallery::placePanel(const QRect& anchor, bool alignRight)
{
    m_panel->adjustSize();
    const QSize size = m_panel->size();
    QPoint position((width() - size.width()) / 2, (height() - size.height()) / 2);
    if (anchor.isValid()) {
        position.setX(alignRight ? anchor.right() + 1 - size.width() : anchor.left());
        position.setY(anchor.bottom() + 7);
        if (position.y() + size.height() > height() - 8)
            position.setY(anchor.top() - 7 - size.height());
    }
    position.setX(qBound(8, position.x(), qMax(8, width() - size.width() - 8)));
    position.setY(qBound(8, position.y(), qMax(8, height() - size.height() - 8)));
    m_panel->move(position);
    m_panel->show();
    m_panel->raise();
    if (motionEnabled()) {
        auto* panel = m_panel;
        auto* opacity = new QGraphicsOpacityEffect(panel);
        opacity->setOpacity(0.75);
        panel->setGraphicsEffect(opacity);
        auto* entrance = new QPropertyAnimation(opacity, "opacity", panel);
        entrance->setDuration(135);
        entrance->setEasingCurve(QEasingCurve::OutCubic);
        entrance->setStartValue(0.75);
        entrance->setEndValue(1.0);
        connect(entrance, &QPropertyAnimation::finished, panel, [panel] { panel->setGraphicsEffect(nullptr); });
        entrance->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void StellarGallery::dismissPanel()
{
    if (auto* panel = std::exchange(m_panel, nullptr)) {
        panel->hide();
        panel->deleteLater();
        setFocus();
    }
}

void StellarGallery::showActions(int index, QRect anchor)
{
    selectIndex(index, false);
    if (!m_detail && index == m_tiles.size() - 1) {
        showAccounts();
        return;
    }
    const auto id = m_ids.value(index);
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    auto* panel = makePanel(instance ? instance->name() : tr("Library"));
    auto* actions = new QGridLayout;
    actions->setSpacing(7);
    int count = 0;
    const auto add = [this, panel, actions, &count](const QString& title, const QString& icon, std::function<void()> action) {
        const QIcon artwork(QStringLiteral(":/icons/flat_white/scalable/%1.svg").arg(icon));
        auto* button = new GalleryButton(artwork, QString(), panel);
        button->setIconSize(QSize(17, 17));
        button->setAccessibleName(title);
        button->setToolTip(title);
        button->setFixedSize(62, 36);
        actions->addWidget(button, count / 4, count % 4);
        ++count;
        connect(button, &QPushButton::clicked, this, [this, action = std::move(action)] {
            dismissPanel();
            action();
        });
    };

    if (instance) {
        add(tr("Play"), "launch", [this, id] {
            if (m_ids.contains(id)) {
                openInstance(id);
                playInstance(id);
            }
        });
        add(tr("Edit Instance"), "instance-settings", [id] {
            if (auto* current = APPLICATION->instances()->getInstanceById(id))
                APPLICATION->showInstanceWindow(current, "version");
        });
        add(tr("Mods, Packs & Shaders"), "loadermods", [this, id] { showContent(id); });
        add(tr("Minecraft Accounts"), "accounts", [this] { showAccounts(); });
        add(tr("Change Skin"), "minecraft", m_actions.skins);
        add(tr("Show Instance Folder"), "viewfolder", [id] {
            if (const auto* current = APPLICATION->instances()->getInstanceById(id))
                QDesktopServices::openUrl(QUrl::fromLocalFile(current->instanceRoot()));
        });
        add(tr("Change Wallpaper"), "screenshots", [this, id] { showWallpapers(id); });
    } else {
        add(tr("Create New Instance"), "new", [this] { showCreator(); });
        add(tr("Import Instance"), "export", [this] { showImport(); });
        add(tr("Minecraft Accounts"), "accounts", [this] { showAccounts(); });
        add(tr("Wallpapers"), "screenshots", [this] { showWallpapers({}); });
    }
    add(tr("Settings"), "settings", [this] { showPreferences(); });
    panel->layout()->addItem(actions);
    if (!anchor.isValid()) {
        if (m_detail) {
            anchor = QRect(width() - 95, height() - 57, 42, 36);
        } else if (auto* tile = m_tiles.value(index)) {
            const QRect card(tile->mapTo(this, QPoint()), tile->size());
            const QRect visibleCard = card.intersected(rect());
            if (visibleCard.isValid())
                anchor = QRect(visibleCard.left() + 14, visibleCard.top() + 32, 1, 1);
        }
    }
    placePanel(anchor, m_detail && anchor.width() > 1);
}

void StellarGallery::showContent(const QString& id)
{
    auto* panel = makePanel(tr("Mods, Packs & Shaders"));
    panel->setFixedWidth(300);
    const auto add = [this, panel, id](const QString& title, const QString& page) {
        auto* button = new GalleryButton(title, panel);
        panel->layout()->addWidget(button);
        connect(button, &QPushButton::clicked, panel, [this, id, page] {
            auto* instance = APPLICATION->instances()->getInstanceById(id);
            dismissPanel();
            if (instance)
                APPLICATION->showInstanceWindow(instance, page);
        });
    };
    add(tr("Mods"), "mods");
    add(tr("Resource Packs"), "resourcepacks");
    add(tr("Shaders"), "shaderpacks");
    auto* look = new GalleryButton(tr("Enable Stellar title screen"), panel);
    panel->layout()->addWidget(look);
    connect(look, &QPushButton::clicked, panel, [this, id] {
        dismissPanel();
        if (auto* instance = APPLICATION->instances()->getInstanceById(id)) {
            if (instance->isRunning() || !StellarClient::hasSupportedTitle(StellarClient::versions(instance).value("net.minecraft"))) {
                QMessageBox::information(this, tr("Stellar title screen"),
                                         tr("With Minecraft closed, select a Java release from 1.21.9 through 26.3."));
                return;
            }
            const auto result = StellarClient::prepareTitle(instance, true);
            if (result)
                QMessageBox::information(this, tr("Stellar title screen"), tr("The Stellar title screen will appear next time you play."));
            else
                QMessageBox::warning(this, tr("Stellar title screen"), result.error());
        }
    });
    auto* client = new GalleryButton(tr("Install in-game tools"), panel);
    panel->layout()->addWidget(client);
    connect(client, &QPushButton::clicked, panel, [this, id] {
        auto* instance = APPLICATION->instances()->getInstanceById(id);
        if (!instance)
            return;
        const auto versions = StellarClient::versions(instance);
        if (versions.value("net.minecraft") != "26.3" ||
            QVersionNumber::fromString(versions.value("net.fabricmc.fabric-loader")) < QVersionNumber(0, 19, 5)) {
            QMessageBox::information(this, tr("Stellar in-game tools"), tr("Use Minecraft Java 26.3 with Fabric Loader 0.19.5 or newer."));
            return;
        }
        if (instance->isRunning()) {
            QMessageBox::information(this, tr("Stellar in-game tools"), tr("Close Minecraft before installing the Stellar Client mod."));
            return;
        }
        const auto installed = StellarClient::installTools(instance);
        if (installed) {
            dismissPanel();
            QMessageBox::information(this, tr("Stellar in-game tools"), tr("Open Stellar on Minecraft’s title or pause menu."));
        } else {
            QMessageBox::warning(this, tr("Stellar in-game tools"), installed.error());
        }
    });
    auto* settings = new GalleryButton(tr("Mod Settings"), panel);
    panel->layout()->addWidget(settings);
    connect(settings, &QPushButton::clicked, panel, [this] { showModSettings(); });
    placePanel();
}

void StellarGallery::showImport()
{
    auto* panel = makePanel(tr("Import Instance"));
    panel->setFixedWidth(384);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* profiles = new QListWidget(panel);
    profiles->setAccessibleName(tr("Prism Launcher instances"));
    profiles->setFixedHeight(139);
    layout->addWidget(profiles);

    QStringList locations;
#ifdef Q_OS_WIN
    if (const auto appData = qEnvironmentVariable("APPDATA"); !appData.isEmpty())
        locations.append(QDir(appData).filePath("PrismLauncher/instances"));
#endif
    for (const auto& directory : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        locations << QDir(directory).filePath("PrismLauncher/instances")
                  << QDir(directory).filePath("prismlauncher/instances");
    }
    locations << QDir::home().filePath(".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances");
    QSet<QString> seen;
    for (const auto& location : locations) {
        const QDir root(location);
        for (const auto& entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            const auto path = entry.canonicalFilePath();
            const QDir instance(path);
            if (path.isEmpty() || seen.contains(path) || !instance.exists("instance.cfg") || !instance.exists("mmc-pack.json"))
                continue;
            seen.insert(path);
            QString name = entry.fileName();
            QFile settings(instance.filePath("instance.cfg"));
            if (settings.open(QIODevice::ReadOnly)) {
                const auto lines = settings.readAll().split('\n');
                for (const auto& line : lines) {
                    if (line.startsWith("name=")) {
                        const auto label = QString::fromUtf8(line.sliced(5).trimmed());
                        if (!label.isEmpty())
                            name = label;
                        break;
                    }
                }
            }
            auto* item = new QListWidgetItem(name, profiles);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
    }
    auto* note = new QLabel(profiles->count() ? tr("Close Minecraft before copying an instance.")
                                              : tr("No Prism instances found. Browse for an instance folder or archive."), panel);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* row = new QHBoxLayout;
    auto* browse = new GalleryButton(tr("Folder…"), panel);
    auto* archive = new GalleryButton(tr("Archive…"), panel);
    auto* copy = new GalleryButton(tr("Preview Import"), panel);
    copy->setProperty("stellarPrimary", true);
    copy->setEnabled(profiles->count() > 0);
    profiles->setVisible(profiles->count() > 0);
    profiles->setCurrentRow(0);
    row->addWidget(browse);
    row->addWidget(archive);
    row->addStretch();
    row->addWidget(copy);
    layout->addLayout(row);
    connect(browse, &QPushButton::clicked, panel, [this] {
        const auto source = QFileDialog::getExistingDirectory(window(), tr("Choose a Prism or MultiMC instance folder"));
        if (!source.isEmpty()) {
            dismissPanel();
            previewImport(source);
        }
    });
    connect(archive, &QPushButton::clicked, panel, [this] {
        const auto file = QFileDialog::getOpenFileName(window(), tr("Choose an instance or modpack archive"), {},
            tr("Minecraft packs (*.zip *.mrpack)"));
        if (!file.isEmpty())
            previewImport(file);
    });
    connect(copy, &QPushButton::clicked, panel, [this, profiles] {
        if (const auto* item = profiles->currentItem()) {
            const auto source = item->data(Qt::UserRole).toString();
            dismissPanel();
            previewImport(source);
        }
    });
    placePanel();
}

void StellarGallery::previewImport(const QString& source)
{
    const bool archive = QFileInfo(source).isFile();
    auto* panel = makePanel(tr("Review Instance Import"));
    panel->setFixedWidth(560);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* summary = new QLabel(tr("Reading instance files…"), panel);
    summary->setWordWrap(true);
    auto* paths = new QListWidget(panel);
    paths->setAccessibleName(tr("Files and folders to copy"));
    paths->setFixedHeight(178);
    paths->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* copy = new GalleryButton(archive ? tr("Continue Import") : tr("Copy Instance"), panel);
    copy->setProperty("stellarPrimary", true);
    copy->setEnabled(false);
    layout->addWidget(summary);
    layout->addWidget(paths);
    layout->addWidget(copy, 0, Qt::AlignRight);
    placePanel();

    struct Manifest {
        QStringList paths;
        qint64 bytes = 0;
        qsizetype files = 0;
    };
    QtConcurrent::run([source, archive]() -> Result<Manifest> {
        if (archive) {
            MMCZip::ArchiveReader pack(source);
            TRY(pack.collectFiles(false))
            Manifest manifest;
            manifest.paths = pack.getFiles();
            if (pack.exists("modrinth.index.json")) {
                TRY_INTO(const auto index, pack.readFile("modrinth.index.json"))
                const auto files = QJsonDocument::fromJson(index).object().value("files").toArray();
                for (const auto& entry : files) {
                    const auto file = entry.toObject();
                    const auto env = file.value("env").toObject();
                    const auto client = env.isEmpty() ? QStringLiteral("required")
                                                      : env.value("client").toString("unsupported");
                    const auto path = file.value("path").toString();
                    if (path.isEmpty() || client == "unsupported")
                        continue;
                    manifest.paths.append(client == "optional" ? tr("Optional download — %1").arg(path)
                                                                : tr("Download — %1").arg(path));
                }
            }
            return manifest;
        }
        const QDir folder(source);
        if (!folder.exists("instance.cfg") || !folder.exists("mmc-pack.json"))
            return std::unexpected(tr("Choose a Prism or MultiMC instance folder containing instance.cfg and mmc-pack.json."));
        Manifest manifest;
        using Flags = QDirListing::IteratorFlag;
        for (const auto& entry : QDirListing(source, Flags::Recursive | Flags::IncludeHidden)) {
            if (entry.isSymLink() || (!entry.isDir() && !entry.isFile()))
                return std::unexpected(tr("This instance contains links or special files. Export an archive and try again."));
            auto path = folder.relativeFilePath(entry.absoluteFilePath());
            if (entry.isDir()) {
                path += '/';
            } else {
                manifest.bytes += entry.size();
                ++manifest.files;
            }
            manifest.paths.append(path);
        }
        std::sort(manifest.paths.begin(), manifest.paths.end());
        return manifest;
    }).then(this, [this, guarded = QPointer(panel), paths, summary, copy, source, archive](const Result<Manifest>& manifest) {
        if (!guarded || m_panel != guarded)
            return;
        if (!manifest) {
            summary->setText(manifest.error());
            return;
        }
        summary->setText(archive ? tr("Archive contents and any listed Modrinth downloads. The pack installer selects what applies.")
            : tr("%1 files · %2 · Includes the saves, mods and settings listed below.")
                .arg(QLocale().toString(manifest->files), QLocale().formattedDataSize(manifest->bytes)));
        paths->addItems(manifest->paths);
        copy->setEnabled(true);
        connect(copy, &QPushButton::clicked, guarded, [this, source, archive, entries = manifest->paths] {
            dismissPanel();
            if (archive)
                m_actions.importPack(source);
            else
                copyInstance(source, entries);
        });
    });
}

void StellarGallery::copyInstance(const QString& source, const QStringList& entries)
{
    const auto root = APPLICATION->instances()->primaryDir();
#ifdef Q_OS_MACOS
    QList<QDir> referenceMods;
    auto* instances = APPLICATION->instances();
    for (int i = 0; i < instances->count(); ++i)
        referenceMods.append(QDir(QDir(instances->at(i)->gameRoot()).filePath("mods")));
#endif
    QtConcurrent::run([source, root, entries
#ifdef Q_OS_MACOS
        , referenceMods
#endif
        ]() -> Result<QString> {
        if (root.isEmpty() || !QDir().mkpath(root))
            return std::unexpected(tr("Couldn’t open the Stellar instance library."));
        const auto id = FS::DirNameFromString(QFileInfo(source).fileName(), {root});
        if (id.isEmpty())
            return std::unexpected(tr("Couldn’t find a free folder name for the copy."));
        QTemporaryDir staging(QDir(root).filePath(".stellar-copy-XXXXXX"));
        const QDir payload(QDir(staging.path()).filePath("instance"));
        if (!staging.isValid() || !QDir().mkpath(payload.absolutePath()))
            return std::unexpected(tr("Couldn’t prepare the new instance."));
        const auto sourcePrefix = QDir(source).canonicalPath() + '/';
        for (const auto& relative : entries) {
            const auto from = QDir(source).filePath(relative);
            const auto to = payload.filePath(relative);
            const QFileInfo file(QDir::cleanPath(from));
            if (file.isSymLink() || !file.canonicalFilePath().startsWith(sourcePrefix) ||
                (relative.endsWith('/') ? !file.isDir() || !QDir().mkpath(to) :
                    !file.isFile() || !QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to)))
                return std::unexpected(tr("The instance changed or could not be read. Review it again before copying."));
        }
#ifdef Q_OS_MACOS
        QList<QDir> importedMods;
        for (const auto& relative : {QStringLiteral("minecraft/mods"), QStringLiteral(".minecraft/mods")}) {
            if (payload.exists(relative))
                importedMods.append(QDir(payload.filePath(relative)));
        }
        StellarMods::shareCopies(referenceMods, importedMods);
#endif
        if (!QDir().rename(payload.absolutePath(), QDir(root).filePath(id)))
            return std::unexpected(tr("Couldn’t move the copied instance into the Stellar library."));
        return id;
    }).then(this, [this](const Result<QString>& copied) {
        if (!copied) {
            QMessageBox::warning(window(), tr("Couldn’t import instance"), copied.error());
            return;
        }
        APPLICATION->instances()->loadList();
        selectInstance(*copied);
    });
}

void StellarGallery::showCreator()
{
    auto* panel = makePanel(tr("Create New Instance"));
    panel->setFixedWidth(366);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* name = new QLineEdit(panel);
    name->setPlaceholderText(tr("Instance name"));
    name->setAccessibleName(tr("Instance name"));
    name->setMaxLength(80);
    layout->addWidget(name);
    auto* version = new QComboBox(panel);
    version->setAccessibleName(tr("Minecraft version"));
    version->setMaxVisibleItems(10);
    version->addItem(tr("Loading versions…"));
    auto* loader = new QComboBox(panel);
    loader->setAccessibleName(tr("Mod loader"));
    loader->addItems({tr("Vanilla"), tr("Fabric")});
    const auto field = [panel, layout](const QString& text, QComboBox* combo) {
        auto* row = new QHBoxLayout;
        auto* label = new QLabel(text, panel);
        label->setBuddy(combo);
        label->setFixedWidth(78);
        row->addWidget(label);
        row->addWidget(combo, 1);
        layout->addLayout(row);
    };
    field(tr("Minecraft"), version);
    field(tr("Loader"), loader);
    auto* compatible = new QCheckBox(tr("Add compatible performance mods"), panel);
    compatible->setToolTip(tr("Stellar selects matching stable builds of Sodium, Lithium and FerriteCore for this Fabric instance."));
    compatible->setChecked(true);
    compatible->hide();
    layout->addWidget(compatible);
    connect(loader, &QComboBox::currentIndexChanged, panel, [compatible, panel](int index) {
        compatible->setVisible(index == 1);
        panel->adjustSize();
    });
    auto* controls = new QHBoxLayout;
    auto* import = new GalleryButton(tr("Import…"), panel);
    auto* advanced = new GalleryButton(tr("More Options…"), panel);
    auto* submit = new GalleryButton(tr("Create"), panel);
    submit->setProperty("stellarPrimary", true);
    submit->setEnabled(false);
    controls->addWidget(import);
    controls->addWidget(advanced);
    controls->addStretch();
    controls->addWidget(submit);
    layout->addLayout(controls);

    const auto java = APPLICATION->metadataIndex()->get("net.minecraft");
    const QString fabricId = "net.fabricmc.fabric-loader";
    const auto fabric = APPLICATION->metadataIndex()->get(fabricId);
    const auto validate = [version, loader, submit, fabric] {
        const auto selected = version->currentData().toString();
        submit->setEnabled(!selected.isEmpty() &&
                           (loader->currentIndex() == 0 || (Version(selected) >= Version("1.14") && fabric->getRecommended())));
    };
    connect(version, &QComboBox::currentIndexChanged, panel, validate);
    connect(loader, &QComboBox::currentIndexChanged, panel, validate);
    connect(import, &QPushButton::clicked, panel, [this] { showImport(); });
    connect(advanced, &QPushButton::clicked, panel, [this] { dismissPanel(); m_actions.create(); });
    connect(name, &QLineEdit::returnPressed, submit, &QPushButton::click);
    connect(submit, &QPushButton::clicked, panel, [this, name, version, loader, java, fabric, fabricId, compatible] {
        auto game = java->getVersion(version->currentData().toString());
        if (!game)
            return;
        VanillaCreationTask* creation = loader->currentIndex() == 0 ? new VanillaCreationTask(game) :
                                        new VanillaCreationTask(game, fabricId, fabric->getRecommended());
        auto title = name->text().trimmed();
        if (title.isEmpty())
            title = tr("Minecraft %1").arg(game->descriptor());
        creation->setPerformanceMods(loader->currentIndex() == 1 && compatible->isChecked());
        creation->setName(title);
        creation->setOriginalName(title, game->descriptor());
        creation->setIcon("default");
        creation->setTargetDir(APPLICATION->instances()->primaryDir());
        unique_qobject_ptr<Task> task(APPLICATION->instances()->wrapInstanceTask(creation));
        dismissPanel();
        ProgressDialog progress(window());
        progress.execWithTask(task.get());
        if (task->getState() == Task::State::Failed)
            QMessageBox::warning(window(), tr("Couldn’t create instance"), task->failReason());
    });

    const auto load = [panel](const Meta::VersionList::Ptr& list, std::function<void()> done) {
        if (list->isLoaded()) {
            done();
            return;
        }
        const auto task = list->getLoadTask();
        QObject::connect(task.get(), &Task::finished, panel, [task, done = std::move(done)] { done(); });
        task->start();
    };
    load(java, [java, version, validate] {
        version->clear();
        for (const auto& item : java->versions()) {
            if (item->typeString() == "release")
                version->addItem(item->descriptor(), item->descriptor());
        }
        if (!version->count())
            version->addItem(QObject::tr("Versions unavailable"));
        validate();
    });
    load(fabric, validate);
    placePanel();
    name->setFocus();
}

void StellarGallery::updateAccountIcons()
{
    const auto account = APPLICATION->accounts()->defaultAccount();
    if (!m_accountTile)
        return;
    const auto data = account ? &account->accountData()->minecraftProfile.skin : nullptr;
    QImage preview;
    if (data && !data->data.isEmpty()) {
        preview = SkinModel::frontPreview(QImage::fromData(data->data, "PNG"),
            data->variant.compare("SLIM", Qt::CaseInsensitive) == 0 ? SkinModel::SLIM : SkinModel::CLASSIC);
    }
    if (preview.isNull()) {
        const QDir skins(APPLICATION->settings()->get("SkinsDir").toString());
        preview = SkinModel::frontPreview(QImage(skins.filePath("Minecraft Defaults/Steve.png")), SkinModel::CLASSIC);
    }
    static_cast<GalleryTile*>(m_accountTile)->setAccountCard(QPixmap::fromImage(preview),
        account ? account->displayName() : tr("No account connected"));
}

void StellarGallery::showAccounts()
{
    if (m_panel && m_panel->property("accountsDropdown").toBool()) {
        dismissPanel();
        return;
    }
    auto* panel = makePanel(tr("Minecraft Accounts"));
    panel->setProperty("accountsDropdown", true);
    panel->setFixedWidth(248);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    const auto add = [this, panel, layout](const QIcon& icon, const QString& label, const std::function<void()>& action) {
        auto* button = new GalleryButton(icon, label, panel);
        layout->addWidget(button);
        connect(button, &QPushButton::clicked, panel, [this, action] {
            dismissPanel();
            action();
        });
    };
    auto* accounts = APPLICATION->accounts();
    auto* profiles = new QListWidget(panel);
    profiles->setAccessibleName(tr("Connected accounts"));
    profiles->setMaximumHeight(128);
    for (int i = 0; i < accounts->count(); ++i) {
        const auto account = accounts->at(i);
        auto* item = new QListWidgetItem(accountFace(account.get()), account->displayName(), profiles);
        if (accounts->defaultAccount() == account)
            profiles->setCurrentItem(item);
    }
    if (profiles->count()) {
        layout->addWidget(profiles);
        connect(profiles, &QListWidget::itemClicked, panel, [this, profiles, accounts](QListWidgetItem* item) {
            const int index = profiles->row(item);
            if (index < accounts->count())
                accounts->setDefaultAccount(accounts->at(index));
            dismissPanel();
        });
    } else {
        profiles->hide();
        layout->addWidget(new QLabel(tr("No Microsoft account connected"), panel));
    }
    add(accountFace(nullptr), tr("Add Microsoft account…"), [this] { MSALoginDialog::addAccount(this); });
    add({}, tr("Manage accounts"), m_actions.accounts);
    add({}, tr("Change skin"), m_actions.skins);
    add({}, tr("General settings"), [this] { showPreferences(); });
    placePanel();
}

void StellarGallery::showPreferences()
{
    auto* panel = makePanel(tr("General settings"));
    panel->setFixedWidth(352);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Launch indicator"), panel));
    auto* animation = new QComboBox(panel);
    animation->setAccessibleName(tr("Launch indicator"));
    animation->addItem(tr("Subtle progress bar"), "progress");
    animation->addItem(tr("Wallpaper wave"), "wave");
    animation->addItem(tr("Wallpaper fade"), "fade");
    animation->addItem(tr("Horizontal wipe"), "wipe");
    animation->addItem(tr("Iris reveal"), "iris");
    animation->addItem(tr("Status only"), "off");
    animation->setCurrentIndex(std::max(0, animation->findData(APPLICATION->settings()->get("StellarLaunchAnimation"))));
    connect(animation, &QComboBox::currentIndexChanged, panel, [animation] {
        APPLICATION->settings()->set("StellarLaunchAnimation", animation->currentData());
    });
    row->addWidget(animation, 1);
    layout->addLayout(row);
    const auto option = [panel, layout](const QString& text, const QString& setting) {
        auto* checkbox = new QCheckBox(text, panel);
        checkbox->setChecked(APPLICATION->settings()->get(setting).toBool());
        layout->addWidget(checkbox);
        QObject::connect(checkbox, &QCheckBox::toggled, panel, [setting](bool value) { APPLICATION->settings()->set(setting, value); });
    };
    option(tr("Fill screen when Minecraft opens"), "StellarFillScreen");
    option(tr("Animate app opening"), "StellarOpeningAnimation");
    option(tr("Use Stellar title on first Minecraft launch"), "StellarTitleScreen");
    option(tr("Enable motion effects"), "StellarMotion");
    auto* mods = new GalleryButton(tr("Mod Settings"), panel);
    layout->addWidget(mods);
    connect(mods, &QPushButton::clicked, panel, [this] { showModSettings(); });
    auto* microsoft = new GalleryButton(tr("Microsoft application ID…"), panel);
    layout->addWidget(microsoft);
    connect(microsoft, &QPushButton::clicked, panel, [this] {
        dismissPanel();
        MSALoginDialog::configureMicrosoftApplication(this);
    });
    placePanel();
}

void StellarGallery::showModSettings()
{
    auto* panel = makePanel(tr("Mod Settings"));
    panel->setFixedWidth(352);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Library loader filter"), panel));
    auto* filter = new QComboBox(panel);
    filter->setAccessibleName(tr("Library loader filter"));
    filter->addItem(tr("All loaders"), QString());
    filter->addItem(tr("Vanilla"), "Vanilla");
    for (const auto* loader : {"Fabric", "Forge", "NeoForge", "Quilt", "LiteLoader"})
        filter->addItem(QString::fromLatin1(loader), QString::fromLatin1(loader));
    filter->setCurrentIndex(qMax(0, filter->findData(APPLICATION->settings()->get("StellarLoaderFilter"))));
    connect(filter, &QComboBox::currentIndexChanged, panel, [this, filter] {
        APPLICATION->settings()->set("StellarLoaderFilter", filter->currentData());
        rebuild();
    });
    row->addWidget(filter, 1);
    layout->addLayout(row);
    auto* matching = new QCheckBox(tr("Update matching mods when switching versions"), panel);
    matching->setChecked(APPLICATION->settings()->get("StellarAutoMatchMods").toBool());
    layout->addWidget(matching);
    connect(matching, &QCheckBox::toggled, panel, [](bool value) {
        APPLICATION->settings()->set("StellarAutoMatchMods", value);
    });
#ifdef Q_OS_MACOS
    auto* compact = new GalleryButton(tr("Deduplicate identical mods"), panel);
    layout->addWidget(compact);
    connect(compact, &QPushButton::clicked, panel, [this, compact] {
        QList<QDir> allMods;
        QList<QDir> inactiveMods;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); ++i) {
            const auto* instance = instances->at(i);
            const QDir mods(QDir(instance->gameRoot()).filePath("mods"));
            allMods.append(mods);
            if (!instance->isRunning() && instance->id() != m_launching)
                inactiveMods.append(mods);
        }
        compact->setEnabled(false);
        compact->setText(tr("Looking for matching mods…"));
        QtConcurrent::run([allMods, inactiveMods] { return StellarMods::shareCopies(allMods, inactiveMods); })
            .then(this, [guarded = QPointer(compact)](qsizetype count) {
                if (!guarded)
                    return;
                guarded->setText(count ? tr("Deduplicated %n mod file(s)", nullptr, int(count))
                                       : tr("No matching mod files to deduplicate"));
                guarded->setEnabled(true);
            });
    });
#endif
    placePanel();
}

QStringList StellarGallery::images(const Collection& collection) const
{
    QDir directory(QDir(m_wallpapers).filePath(collection.id));
    if (collection.id != "animated" && !directory.exists()) {
        const auto legacy = QDir::home().filePath("Library/Application Support/Stellar Gallery/Wallpapers");
        directory = QDir(QDir(legacy).filePath(collection.id));
    }
    QStringList paths;
    const auto contents = directory.entryInfoList({ "*.jpg", "*.jpeg", "*.png", "*.webp", "*.gif" }, QDir::Files, QDir::Name);
    for (const auto& entry : contents)
        paths.append(entry.absoluteFilePath());
    return paths;
}

QString StellarGallery::artworkPath(const QString& instance, const QString& version)
{
    if (m_collections.isEmpty())
        return {};
    const auto selected = APPLICATION->settings()->get("StellarWallpaperCovers").toMap().value(instance).toString();
    const auto found = std::find_if(m_collections.cbegin(), m_collections.cend(),
                                    [&version](const Collection& item) { return item.versions.contains(version); });
    const auto& collection = found == m_collections.cend() ? m_collections.constFirst() : *found;
    QString file = selected;
    if (!QFileInfo::exists(file)) {
        const auto files = images(collection);
        if (files.isEmpty())
            fetchArtwork(collection);
        for (const auto* label : { "3840x2160", "2560x1440", "1920x1080", "desktop", "landscape" }) {
            const auto match = std::find_if(files.cbegin(), files.cend(), [label](const auto& path) {
                return QFileInfo(path).fileName().contains(QString::fromLatin1(label), Qt::CaseInsensitive);
            });
            if (match != files.cend()) {
                file = *match;
                break;
            }
        }
        if (file.isEmpty()) {
            const auto defaults = images(m_collections.constFirst());
            file = files.isEmpty() ? defaults.value(0, QString(":/stellar-gallery/fallback.jpg")) : files.first();
        }
    }
    return file;
}

void StellarGallery::scheduleThumbnails()
{
    QStringList paths;
    for (auto* tile : std::as_const(m_tiles)) {
        const auto path = tile->property("wallpaperPath").toString();
        if (!path.isEmpty() && !m_thumbnails.contains(path) && !m_pendingThumbnails.contains(path)) {
            m_pendingThumbnails.insert(path);
            paths.append(path);
        }
    }
    if (paths.isEmpty())
        return;

    QtConcurrent::run([paths] {
        QList<QPair<QString, QImage>> decoded;
        decoded.reserve(paths.size());
        for (const auto& path : paths) {
            QImageReader reader(path);
            const auto size = reader.size();
            if (size.isValid())
                reader.setScaledSize(size.scaled(650, 650, Qt::KeepAspectRatio));
            auto image = reader.read();
            if (image.isNull()) {
                QImageReader fallback(":/stellar-gallery/fallback.jpg");
                fallback.setScaledSize(QSize(650, 365));
                image = fallback.read();
            }
            decoded.append({path, std::move(image)});
        }
        return decoded;
    }).then(this, [this](const QList<QPair<QString, QImage>>& decoded) {
        for (const auto& [path, image] : decoded) {
            m_pendingThumbnails.remove(path);
            m_thumbnails.insert(path, QPixmap::fromImage(image));
        }
        for (auto* tile : std::as_const(m_tiles)) {
            const auto path = tile->property("wallpaperPath").toString();
            if (m_thumbnails.contains(path))
                static_cast<GalleryTile*>(tile)->setArtwork(m_thumbnails.value(path));
        }
    });
}

QPixmap StellarGallery::wallpaperPixmap(const QString& file)
{
    if (!m_artwork.contains(file)) {
        QImageReader reader(file);
        const auto size = reader.size();
        if (size.isValid())
            reader.setScaledSize(size.scaled(1600, 1200, Qt::KeepAspectRatio));
        const auto pixmap = QPixmap::fromImage(reader.read());
        m_artwork.insert(file, pixmap.isNull() ? QPixmap(QStringLiteral(":/stellar-gallery/fallback.jpg")) : pixmap);
    }
    return m_artwork.value(file);
}

void StellarGallery::fetchArtwork(const Collection& collection)
{
    if (m_attempted.contains(collection.id) || m_fetching.contains(collection.id) ||
        !official(collection.fallback.isValid() ? collection.fallback : collection.archive))
        return;
    const auto source = collection.fallback.isValid() ? collection.fallback : collection.archive;
    m_fetching.insert(collection.id);
    m_attempted.insert(collection.id);
    QNetworkRequest request(source);
    request.setTransferTimeout(20000);
    auto* reply = APPLICATION->network()->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (std::max(received, total) > 96 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection] {
        const bool okay = reply->error() == QNetworkReply::NoError && official(reply->url());
        auto bytes = okay ? reply->readAll() : QByteArray();
        reply->deleteLater();
        const auto destination = QDir(m_wallpapers).filePath(collection.id);
        QtConcurrent::run([data = std::move(bytes), destination] {
            if (data.isEmpty() || !QDir().mkpath(destination))
                return;
            if (QImage::fromData(data).size().isValid()) {
                saveBytes(QDir(destination).filePath("Official artwork.jpg"), data);
                return;
            }
            QTemporaryFile archive;
            if (!archive.open() || archive.write(data) != data.size() || !archive.flush())
                return;
            MMCZip::ArchiveReader reader(archive.fileName());
            // Keep only images and discard archive-provided folder paths.
            reader.parse(std::function<Result<>(MMCZip::ArchiveReader::File*)>{[&destination](auto* entry) -> Result<> {
                const auto name = QFileInfo(entry->filename()).fileName();
                const auto ext = QFileInfo(name).suffix().toLower();
                if (entry->isFile() && QStringList{"jpg", "jpeg", "png", "webp"}.contains(ext)) {
                    if (auto image = entry->readAll(); image.has_value() && image.value().size() <= 20 * 1024 * 1024)
                        saveBytes(QDir(destination).filePath(name), image.value());
                }
                return {};
            }});
        }).then(this, [this, id = collection.id] {
            m_fetching.remove(id);
            updateArtwork();
        });
    });
}

void StellarGallery::updateArtwork()
{
    for (int i = 0; i < m_ids.size(); ++i) {
        if (auto* instance = APPLICATION->instances()->getInstanceById(m_ids[i])) {
            const auto path = artworkPath(m_ids[i], versionOf(instance).minecraft);
            m_tiles[i]->setProperty("wallpaperPath", path);
            if (m_thumbnails.contains(path))
                static_cast<GalleryTile*>(m_tiles[i])->setArtwork(m_thumbnails.value(path));
        }
    }
    scheduleThumbnails();
    if (m_detail)
        refreshInstance();
}

void StellarGallery::showWallpapers(const QString& instance)
{
    auto* panel = makePanel(tr("Wallpapers"));
    panel->setFixedWidth(598);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* collections = new QComboBox(panel);
    collections->setAccessibleName(tr("Wallpaper collection"));
    for (const auto& item : m_collections)
        collections->addItem(item.title);
    auto* files = new QListWidget(panel);
    files->setFixedHeight(176);
    files->setAccessibleName(tr("Available wallpapers"));
    auto* apply = new GalleryButton(tr("Use Wallpaper"), panel);
    apply->setProperty("stellarPrimary", true);
    layout->addWidget(collections);
    auto* body = new QHBoxLayout;
    body->addWidget(files, 1);
    auto* preview = new QLabel(panel);
    preview->setAccessibleName(tr("Wallpaper preview"));
    preview->setAlignment(Qt::AlignCenter);
    preview->setFixedSize(308, 176);
    preview->setStyleSheet("background: #141416; border: 1px solid #37363d; border-radius: 9px;");
    auto* playback = new WallpaperPlayback(preview);
    connect(&playback->movie, &QMovie::frameChanged, preview, [preview, playback] {
        preview->setPixmap(playback->movie.currentPixmap());
    });
    body->addWidget(preview);
    layout->addLayout(body);
    connect(files, &QListWidget::currentItemChanged, panel, [preview, playback](const QListWidgetItem* item) {
        playback->setFile({}, {});
        preview->setText(QObject::tr("No preview available"));
        if (!item)
            return;
        const auto path = item->data(Qt::UserRole).toString();
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto bounds = preview->size() - QSize(6, 6);
        if (const auto size = reader.size(); size.isValid()) {
            reader.setScaledSize(size.boundedTo(size.scaled(bounds, Qt::KeepAspectRatio)));
            const auto image = reader.read();
            if (!image.isNull())
                preview->setPixmap(QPixmap::fromImage(image));
        }
        if (motionEnabled())
            playback->setFile(path, bounds);
    });
    preview->setText(tr("No wallpaper selected"));
    auto* shuffle = new QCheckBox(tr("Shuffle this collection when opening the instance"), panel);
    const auto activeShuffle = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap().value(instance).toString();
    shuffle->setVisible(!instance.isEmpty());
    layout->addWidget(shuffle);
    auto* buttons = new QHBoxLayout;
    auto* import = new GalleryButton(tr("Add GIFs…"), panel);
    buttons->addWidget(import);
    auto* refresh = new GalleryButton(tr("Fetch Collection"), panel);
    buttons->addWidget(refresh);
    buttons->addStretch();
    if (!instance.isEmpty()) {
        auto* reset = new GalleryButton(tr("Reset"), panel);
        buttons->addWidget(reset);
        connect(reset, &QPushButton::clicked, panel, [this, instance] {
            auto covers = APPLICATION->settings()->get("StellarWallpaperCovers").toMap();
            covers.remove(instance);
            APPLICATION->settings()->set("StellarWallpaperCovers", covers);
            auto shuffled = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap();
            shuffled.remove(instance);
            APPLICATION->settings()->set("StellarWallpaperShuffle", shuffled);
            m_detailWallpaper.clear();
            updateArtwork();
            dismissPanel();
        });
    }
    buttons->addWidget(apply);
    layout->addLayout(buttons);
    const auto populate = [this, collections, files, apply, refresh, import, shuffle] {
        const int index = collections->currentIndex();
        if (index < 0 || index >= m_collections.size())
            return;
        const auto& collection = m_collections[index];
        const auto paths = images(collection);
        if (files->count() != paths.size()) {
            files->clear();
            for (const auto& path : paths) {
                auto* item = new QListWidgetItem(QFileInfo(path).fileName(), files);
                item->setData(Qt::UserRole, path);
            }
            if (files->count())
                files->setCurrentRow(0);
        }
        const bool gif = collection.id == "animated";
        import->setVisible(gif);
        refresh->setVisible(!gif);
        refresh->setEnabled(!m_fetching.contains(collection.id));
        shuffle->setEnabled(!paths.isEmpty());
        apply->setEnabled(files->currentItem() != nullptr);
    };
    connect(collections, &QComboBox::currentIndexChanged, panel, [files, populate] { files->clear(); populate(); });
    connect(shuffle, &QCheckBox::toggled, apply, [apply](bool enabled) {
        apply->setText(enabled ? tr("Use Shuffle") : tr("Use Wallpaper"));
    });
    connect(import, &QPushButton::clicked, panel, [this, panel, collections, files, populate] {
        const auto paths = QFileDialog::getOpenFileNames(panel, tr("Add animated wallpapers"), {}, tr("GIF images (*.gif)"));
        const auto folder = QDir(m_wallpapers).filePath("animated");
        if (paths.isEmpty())
            return;
        int imported = 0;
        if (QDir().mkpath(folder)) {
            for (const auto& path : paths) {
                QFileInfo file(path);
                QImageReader reader(path, "gif");
                const auto size = reader.size();
                if (file.size() > 64 * 1024 * 1024 || !size.isValid() || size.width() > 4096 || size.height() > 4096)
                    continue;
                QDir directory(folder);
                auto target = directory.filePath(file.fileName());
                if (QFileInfo::exists(target))
                    target = directory.filePath(file.completeBaseName() + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".gif");
                if (QFile::copy(path, target))
                    ++imported;
            }
        }
        if (imported != paths.size())
            QMessageBox::information(panel, tr("Animated wallpapers"), tr("Imported %1 of %2 GIFs. Each GIF must be at most 64 MB and 4096 pixels wide or tall.").arg(imported).arg(paths.size()));
        collections->setCurrentIndex(m_collections.size() - 1);
        files->clear();
        populate();
    });
    connect(refresh, &QPushButton::clicked, panel, [this, collections, populate] {
        const int index = collections->currentIndex();
        if (index < 0)
            return;
        auto collection = m_collections[index];
        collection.fallback = QUrl();
        m_attempted.remove(collection.id);
        fetchArtwork(collection);
        populate();
    });
    connect(apply, &QPushButton::clicked, panel, [this, files, instance, shuffle, collections] {
        if (auto* item = files->currentItem()) {
            const auto path = item->data(Qt::UserRole).toString();
            if (instance.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            } else {
                auto covers = APPLICATION->settings()->get("StellarWallpaperCovers").toMap();
                covers.insert(instance, path);
                APPLICATION->settings()->set("StellarWallpaperCovers", covers);
                auto shuffled = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap();
                if (shuffle->isChecked())
                    shuffled.insert(instance, m_collections.at(collections->currentIndex()).id);
                else
                    shuffled.remove(instance);
                APPLICATION->settings()->set("StellarWallpaperShuffle", shuffled);
                m_detailWallpaper.clear();
                updateArtwork();
                dismissPanel();
            }
        }
    });
    auto* refreshTimer = new QTimer(panel);
    connect(refreshTimer, &QTimer::timeout, panel, populate);
    refreshTimer->start(350);
    const auto selected = APPLICATION->settings()->get("StellarWallpaperCovers").toMap().value(instance).toString();
    auto collection = activeShuffle.isEmpty() ? QFileInfo(selected).dir().dirName() : activeShuffle;
    if (selected.isEmpty() && activeShuffle.isEmpty()) {
        if (const auto* current = APPLICATION->instances()->getInstanceById(instance)) {
            const auto version = versionOf(current).minecraft;
            for (const auto& candidate : std::as_const(m_collections)) {
                if (candidate.versions.contains(version)) {
                    collection = candidate.id;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id == collection) {
            collections->setCurrentIndex(i);
            break;
        }
    }
    if (instance.isEmpty())
        collections->setCurrentIndex(m_collections.size() - 1);
    shuffle->setChecked(!activeShuffle.isEmpty());
    populate();
    for (int i = 0; i < files->count(); ++i) {
        if (files->item(i)->data(Qt::UserRole).toString() == selected) {
            files->setCurrentRow(i);
            break;
        }
    }
    placePanel();
}

void StellarGallery::updateLaunch()
{
    const auto* shown = APPLICATION->instances()->getInstanceById(m_detailId);
    auto* pending = m_launching.isEmpty() ? nullptr : APPLICATION->instances()->getInstanceById(m_launching);
    if (!pending || !pending->launchController()) {
        finishLaunch();
        return;
    }
    const auto state = pending->property("stellarLaunchState").toString();
    if (state == "java" && !m_javaStartup.isValid())
        m_javaStartup.start();
    if (state == "ready") {
        finishLaunch();
        return;
    }
    if (!m_detail || !shown)
        return;
    auto* detail = static_cast<InstanceDetail*>(m_detail);
    if (shown != pending) {
        detail->setLaunchState(shown->isRunning() ? tr("Running") : tr("Launching another…"), true, false);
        return;
    }
    const bool showProgress = APPLICATION->settings()->get("StellarLaunchAnimation").toString() != "off";
    const bool animate = showProgress && motionEnabled();
    if (state.startsWith("download ")) {
        const int progress = qBound(0, qRound(state.sliced(9).toDouble() * 100), 100);
        detail->setLaunchState(tr("Downloading…"), true, animate, showProgress && progress > 0 ? progress : -1);
    } else {
        const bool java = state == "java";
        const bool slow = java && m_javaStartup.hasExpired(90000);
        const bool cover = wallpaperEffect(APPLICATION->settings()->get("StellarLaunchAnimation").toString());
        detail->setLaunchState(slow ? tr("Minecraft is still starting…") : java ? tr("Starting Minecraft…") : tr("Preparing…"),
                               true, animate && !slow, java && cover && !slow ? 96 : -1);
    }
}
