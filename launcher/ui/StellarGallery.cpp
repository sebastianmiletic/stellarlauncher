// SPDX-License-Identifier: GPL-3.0-only
#include "StellarGallery.h"

#include <QAbstractButton>
#include <QApplication>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCursor>
#include <QComboBox>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QDialog>
#include <QFormLayout>
#include <QDirListing>
#include <QDynamicPropertyChangeEvent>
#include <QFileDialog>
#include <QFrame>
#include <QImageReader>
#include <QInputDialog>
#include <QMenu>
#include <QTabBar>
#include <toml++/toml.h>
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
#include <QThreadPool>
#include <QRandomGenerator>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
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
#include <memory>

#include <utility>

#include "Application.h"
#include "ui/InstanceWindow.h"
#include "ui/themes/ThemeManager.h"
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

namespace {
constexpr int windowWidth = StellarGallery::windowSize.width();
constexpr int windowHeight = StellarGallery::windowSize.height();
constexpr int gap = 5;
const QColor canvas("#141416");

struct InstanceVersion {
    QString minecraft;
    QString loader;
};

InstanceVersion versionOf(const MinecraftInstance* instance)
{
    if (instance->isHorizon())
        return {"26.3", "Horizon"};
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
    const auto source = bounds.size().scaled(picture.size(), Qt::KeepAspectRatio);
    painter.drawPixmap(bounds, picture, QRect(QPoint((picture.width() - source.width()) / 2,
                                                       (picture.height() - source.height()) / 2), source));
}

bool motionEnabled()
{
    return APPLICATION->settings()->get("StellarMotion").toBool();
}

QThreadPool* wallpaperPool()
{
    static auto* pool = [] {
        auto* worker = new QThreadPool(qApp);
        worker->setMaxThreadCount(2);
        worker->setExpiryTimeout(3000);
        return worker;
    }();
    return pool;
}

QImage readWallpaper(const QString& path, const QSize& bounds, bool cover = false)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || bounds.isEmpty() || qint64(size.width()) * size.height() > 48'000'000)
        return {};
    auto image = reader.read();
    if (image.isNull())
        return {};
    if (cover) {
        const auto crop = bounds.scaled(image.size(), Qt::KeepAspectRatio);
        if (crop != image.size())
            image = image.copy((image.width() - crop.width()) / 2, (image.height() - crop.height()) / 2, crop.width(), crop.height());
    }
    return image.scaled(bounds.boundedTo(image.size()), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

const QPixmap& wallpaperPlaceholder()
{
    static const QPixmap preview = QPixmap::fromImage(readWallpaper(":/stellar-gallery/fallback.jpg", QSize(640, 400)));
    return preview;
}

int pixmapKiB(const QImage& image)
{
    return qMax(1, int((qint64(image.width()) * image.height() * 4 + 1023) / 1024));
}

class WallpaperPlayback final : public QObject {
public:
    explicit WallpaperPlayback(QWidget* host) : QObject(host), m_host(host)
    {
        host->installEventFilter(this);
        if (host->window() != host)
            host->window()->installEventFilter(this);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] { readFrame(); });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { sync(); });
    }

    void setFile(const QString& path, const QSize& target)
    {
        const auto animated = path.endsWith(".gif", Qt::CaseInsensitive) ? path : QString();
        if (m_path == animated)
            return;
        m_path = animated;
        m_frame = {};
        m_decoder.reset();
        m_timer.stop();
        if (!animated.isEmpty())
            m_decoder = std::make_shared<Decoder>(animated, target);
        sync();
    }

    void setSuspended(bool suspended)
    {
        if (m_suspended != suspended) {
            m_suspended = suspended;
            sync();
        }
    }

    const QPixmap& currentPixmap() const { return m_frame; }
    std::function<void()> frameChanged;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
            m_timer.stop();
            break;
        case QEvent::Show:
        case QEvent::WindowActivate:
        case QEvent::WindowStateChange:
            QTimer::singleShot(0, this, [this] { sync(); });
            break;
        default:
            break;
        }
        return false;
    }

private:
    struct Decoder {
        Decoder(QString filename, QSize target) : path(std::move(filename)), bounds(target), reader(path, "gif") {}
        QString path;
        QSize bounds;
        QImageReader reader;
        bool initialized = false;
        bool hadFrame = false;
    };

    struct Frame {
        QImage image;
        int delay = 100;
    };

    bool active() const
    {
        return !m_suspended && motionEnabled() && m_host->isVisible() && !m_host->window()->isMinimized()
            && m_host->window()->isActiveWindow() && QGuiApplication::applicationState() == Qt::ApplicationActive;
    }

    void sync()
    {
        if (!m_decoder || !active()) {
            m_timer.stop();
        } else if (!m_timer.isActive() && !m_pending) {
            m_timer.start(0);
        }
    }

    void readFrame()
    {
        if (!m_decoder || !active() || m_pending)
            return;
        m_pending = true;
        auto work = m_decoder;
        QtConcurrent::run(wallpaperPool(), [work]() -> Frame {
            const auto configure = [&work] {
                const auto size = work->reader.size();
                if (QFileInfo(work->path).size() > 64 * 1024 * 1024 || !size.isValid() ||
                    size.width() > 4096 || size.height() > 4096)
                    return false;
                work->reader.setScaledSize(size.boundedTo(size.scaled(work->bounds, Qt::KeepAspectRatio)));
                return true;
            };
            if (!work->initialized) {
                work->initialized = true;
                if (!configure())
                    return {};
            }
            auto image = work->reader.read();
            if (image.isNull() && work->hadFrame) {
                work->reader.setFileName(work->path);
                if (configure())
                    image = work->reader.read();
            }
            work->hadFrame = !image.isNull();
            return {std::move(image), qMax(34, work->reader.nextImageDelay())};
        }).then(this, [this, work](const Frame& frame) {
            m_pending = false;
            if (work != m_decoder) {
                sync();
            } else if (frame.image.isNull()) {
                m_decoder.reset();
            } else if (active()) {
                m_frame = QPixmap::fromImage(frame.image);
                if (frameChanged)
                    frameChanged();
                m_timer.start(frame.delay);
            }
        });
    }

    QWidget* m_host;
    QTimer m_timer;
    std::shared_ptr<Decoder> m_decoder;
    QPixmap m_frame;
    QString m_path;
    bool m_pending = false;
    bool m_suspended = false;
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

class CardOutline final : public QWidget {
public:
    explicit CardOutline(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Active, QPalette::Highlight), 2));
        painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 13, 13);
    }
};

class DetailSlide final : public QWidget {
public:
    DetailSlide(QWidget* parent, QPixmap previous, QPixmap next, int direction)
        : QWidget(parent), m_previous(std::move(previous)), m_next(std::move(next)), m_direction(direction)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parent->rect());
        auto* motion = new QVariantAnimation(this);
        motion->setDuration(245);
        motion->setEasingCurve(QEasingCurve::OutCubic);
        motion->setStartValue(0.0);
        motion->setEndValue(1.0);
        connect(motion, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_progress = value.toReal();
            update();
        });
        connect(motion, &QVariantAnimation::finished, this, &QObject::deleteLater);
        show();
        raise();
        motion->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const int shift = m_direction * qRound(width() * m_progress);
        QPainter painter(this);
        painter.drawPixmap(QPoint(-shift, 0), m_previous);
        painter.drawPixmap(QPoint(m_direction * width() - shift, 0), m_next);
    }

private:
    QPixmap m_previous;
    QPixmap m_next;
    int m_direction;
    qreal m_progress = 0;
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
    }

    void setAccountCard(QPixmap skin, QString username, QString played, QString count)
    {
        m_accountCard = true;
        m_accountSkin = std::move(skin);
        m_accountName = std::move(username);
        m_accountPlaytime = std::move(played);
        m_accountInstances = std::move(count);
        update();
    }

    void setVersion(const QString& version)
    {
        if (m_version != version) {
            m_version = version;
            update();
        }
    }

    bool hasArtwork() const { return !m_image.isNull(); }

    void setArtwork(const QPixmap& image)
    {
        if (image.cacheKey() == m_image.cacheKey())
            return;
        m_image = image;
        update();
    }
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
            QRadialGradient glow(bounds.center() - QPoint(width() / 6, 38), qMax(135, width() / 2));
            glow.setColorAt(0, QColor("#33313b"));
            glow.setColorAt(1, QColor("#1c1c20"));
            painter.fillRect(bounds, glow);
            const int innerWidth = qMin(420, width() - 32);
            const int left = (width() - innerWidth) / 2;
            const int leftColumn = innerWidth * 42 / 100;
            const int scale = qBound(3, leftColumn / 18, 7);
            const int centerY = bounds.center().y() - 12;
            const QRect figure(left + (leftColumn - 16 * scale) / 2, centerY - 16 * scale, 16 * scale, 32 * scale);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            if (m_accountSkin.isNull()) {
                const auto face = accountFace(nullptr).pixmap(8 * scale, 8 * scale);
                painter.drawPixmap(QRect(figure.x() + 4 * scale, figure.y(), 8 * scale, 8 * scale), face);
                painter.fillRect(figure.x(), figure.y() + 8 * scale, 16 * scale, 12 * scale, QColor("#464651"));
                painter.fillRect(figure.x() + 4 * scale, figure.y() + 20 * scale, 8 * scale, 12 * scale, QColor("#353541"));
            } else {
                painter.drawPixmap(figure, m_accountSkin);
            }

            const int textLeft = left + leftColumn + 12;
            const int textWidth = width() - left - textLeft;
            const auto line = [&](int y, const QString& value, int size, QFont::Weight weight, const QColor& color) {
                auto style = font();
                style.setPixelSize(size);
                style.setWeight(weight);
                painter.setFont(style);
                painter.setPen(color);
                painter.drawText(QRect(textLeft, y, textWidth, 24), Qt::AlignVCenter,
                                 painter.fontMetrics().elidedText(value, Qt::ElideRight, textWidth));
            };
            line(centerY - 67, m_accountName, 17, QFont::DemiBold, QColor("#ecebf0"));
            line(centerY - 31, QObject::tr("Launcher playtime"), 12, QFont::Normal, QColor("#bcb9c5"));
            line(centerY - 12, m_accountPlaytime, 15, QFont::DemiBold, QColor("#ecebf0"));
            line(centerY + 24, QObject::tr("Instances"), 12, QFont::Normal, QColor("#bcb9c5"));
            line(centerY + 43, m_accountInstances, 15, QFont::DemiBold, QColor("#ecebf0"));

            auto heading = font();
            heading.setPixelSize(17);
            heading.setWeight(QFont::DemiBold);
            painter.setFont(heading);
            painter.setPen(QColor("#ecebf0"));
            const QRect title(12, height() - 62, width() - 24, 26);
            painter.drawText(title, Qt::AlignCenter, painter.fontMetrics().elidedText(text(), Qt::ElideRight, title.width()));
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
    }

private:
    QString m_version;
    QPixmap m_image;
    QPixmap m_accountSkin;
    QString m_accountName;
    QString m_accountPlaytime;
    QString m_accountInstances;
    bool m_accountCard = false;
    ButtonFeedback m_feedback;
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
        m_playback->frameChanged = [this] { update(); };
    }

    void setWallpaperFile(const QString& path)
    {
        m_playback->setFile(motionEnabled() ? path : QString(), size() * devicePixelRatioF());
    }

    void suspendWallpaper(bool suspended) { m_playback->setSuspended(suspended); }

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

    void setLaunchState(const QString& text, bool busy, bool animate, int progress = -1, bool canStop = false)
    {
        if (m_play->text() != text)
            m_play->setText(text);
        m_play->setEnabled(!busy || canStop);
        m_play->setToolTip(canStop ? tr("Stop Minecraft") : QString());
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
            const auto& frame = m_playback->currentPixmap();
            drawArtwork(painter, hero, frame.isNull() ? m_wallpaper : frame);
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
                  "QPushButton[stellarPrimary=true] { background: palette(highlight); color: palette(highlighted-text); }"
                  "QPushButton[stellarPrimary=true]:hover { background: palette(highlight); }"
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
    m_slide->setDuration(245);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    m_outline = new CardOutline(m_cards);
    m_outline->hide();
    m_outlineSlide = new QPropertyAnimation(m_outline, "geometry", this);
    m_outlineSlide->setDuration(195);
    m_outlineSlide->setEasingCurve(QEasingCurve::OutCubic);
    auto* thumbnailTimer = new QTimer(this);
    thumbnailTimer->setSingleShot(true);
    thumbnailTimer->setInterval(70);
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, [thumbnailTimer] { thumbnailTimer->start(); });
    connect(thumbnailTimer, &QTimer::timeout, this, &StellarGallery::scheduleThumbnails);
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

    m_collections.append({QStringLiteral("animated"), tr("My Wallpapers"), {}, {}, {}});
    auto* accounts = APPLICATION->accounts();
    connect(accounts, &AccountList::listChanged, this, &StellarGallery::updateAccountIcons);
    connect(accounts, &AccountList::defaultAccountChanged, this, &StellarGallery::updateAccountIcons);
    connect(accounts, &QAbstractItemModel::dataChanged, this, &StellarGallery::updateAccountIcons);

    m_selected = APPLICATION->settings()->get("SelectedInstance").toString();
    auto* list = APPLICATION->instances();
    auto* refresh = new QTimer(this);
    refresh->setSingleShot(true);
    refresh->setInterval(0);
    const auto queueRefresh = [refresh] { refresh->start(); };
    connect(refresh, &QTimer::timeout, this, &StellarGallery::rebuild);
    connect(list, &QAbstractItemModel::modelReset, this, queueRefresh);
    connect(list, &QAbstractItemModel::rowsInserted, this, queueRefresh);
    connect(list, &QAbstractItemModel::rowsRemoved, this, queueRefresh);
    connect(list, &QAbstractItemModel::dataChanged, this, queueRefresh);
    connect(list, &InstanceList::instanceSelectRequest, this, [this, list](const QString& id) {
        const auto* instance = list->getInstanceById(id);
        if (m_creation && instance && instance->isHorizon()) {
            auto* settings = APPLICATION->settings();
            const auto filter = settings->get("StellarLoaderFilter").toString();
            if (!filter.isEmpty() && filter != QStringLiteral("Horizon"))
                settings->set("StellarLoaderFilter", QString());
            m_selected = id;
            m_saveSelection->start();
            QTimer::singleShot(0, this, [this, id, progress = QPointer(m_panel)] {
                if (m_panel != progress)
                    return;
                if (progress)
                    dismissPanel();
                if (!m_ids.contains(id))
                    rebuild();
                openInstance(id);
            });
            return;
        }
        if (m_ids.contains(id))
            selectInstance(id);
        else
            m_selected = id;
    });
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
    m_outlineSlide->stop();
    QHash<QString, GalleryTile*> existing;
    for (auto* tile : std::as_const(m_tiles)) {
        m_row->removeWidget(tile);
        if (tile != m_accountTile) {
            auto* card = static_cast<GalleryTile*>(tile);
            existing.insert(card->instanceId, card);
        }
    }
    m_tiles.clear();
    m_ids.clear();

    const int columns = std::clamp(int(instances.size()), 1, 3);
    const int cardSpace = windowWidth - gap * (columns + 1);
    const auto cardWidth = [columns, cardSpace](int index) {
        return cardSpace / columns + (index % columns < cardSpace % columns ? 1 : 0);
    };
    for (int i = 0; i <= instances.size(); ++i) {
        const auto* instance = i < instances.size() ? instances[i] : nullptr;
        const auto id = instance ? instance->id() : QString();
        const auto title = instance ? instance->name() : tr("Create New Instance");
        auto version = versions.value(id).minecraft;
        const auto path = instance ? artworkPath(id, version) : QString();
        auto* tile = existing.take(id);
        if (instance && instance->isHorizon())
            version = tr("Rust · %1").arg(version);
        else if (instance && version.isEmpty())
            version = tr("Minecraft");
        const bool fresh = !tile;
        if (fresh) {
            const auto* thumbnail = m_thumbnails.object(path);
            const QPixmap artwork = thumbnail ? thumbnail->artwork : instance ? wallpaperPlaceholder() : QPixmap();
            tile = new GalleryTile(title, version, artwork, m_cards);
            tile->instanceId = id;
            connect(tile, &QAbstractButton::clicked, this, [this, tile] {
                const int index = m_tiles.indexOf(tile);
                if (index >= 0)
                    activate(index);
            });
            tile->installEventFilter(this);
        } else {
            tile->setText(title);
            tile->setAccessibleName(title);
            tile->setVersion(version);
        }
        tile->setProperty("wallpaperPath", path);
        tile->setFixedSize(cardWidth(i), windowHeight - gap * 2);
        m_row->addWidget(tile);
        m_tiles.append(tile);
        if (instance)
            m_ids.append(id);
        if (!fresh)
            continue;
        tile->open = [this, tile] {
            const int index = m_tiles.indexOf(tile);
            if (index >= 0)
                activate(index);
        };
        if (instance) {
            tile->play = [this, tile, id] {
                const int index = m_tiles.indexOf(tile);
                if (index < 0)
                    return;
                dismissPanel();
                selectIndex(index, false);
                openInstance(id);
                playInstance(id);
            };
        }
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
            const auto point = m_cards->mapFromGlobal(screenPoint);
            const auto match = std::find_if(m_tiles.cbegin(), m_tiles.cend(), [point](const auto* card) {
                return card->geometry().contains(point);
            });
            if (match == m_tiles.cend())
                return;
            const auto* target = static_cast<GalleryTile*>(*match);
            if (target->instanceId == source)
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
        tile->options = [this, tile](const QPoint& screen) {
            const int index = m_tiles.indexOf(tile);
            if (index >= 0)
                showActions(index, QRect(mapFromGlobal(screen), QSize(1, 1)));
        };
    }
    if (!m_accountTile) {
        auto* profile = new GalleryTile(tr("Accounts & Settings"), {}, {}, m_cards);
        profile->installEventFilter(this);
        profile->options = [this](const QPoint&) { showAccounts(); };
        connect(profile, &QAbstractButton::clicked, this, [this] { activate(m_tiles.size() - 1); });
        m_accountTile = profile;
    }
    m_accountTile->setFixedSize(cardWidth(m_tiles.size()), windowHeight - gap * 2);
    m_row->addWidget(m_accountTile);
    m_tiles.append(m_accountTile);
    for (auto* removed : std::as_const(existing)) {
        removed->hide();
        removed->deleteLater();
    }
    updateAccountIcons();
    int rowWidth = gap;
    for (auto* tile : std::as_const(m_tiles)) {
        tile->show();
        rowWidth += tile->width() + gap;
    }
    m_cards->setFixedSize(rowWidth, windowHeight);
    m_row->invalidate();
    m_row->activate();
    m_scroll->horizontalScrollBar()->setValue(oldOffset);
    const int selectedIndex = m_ids.indexOf(m_selected);
    selectIndex(selectedIndex >= 0 ? selectedIndex : 0, false);
    scheduleThumbnails();
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
    const auto tile = m_tiles[m_index]->geometry();
    const bool shouldAnimate = animate && motionEnabled();
    if (!shouldAnimate || m_outline->isHidden() || m_outlineSlide->endValue().toRect() != tile) {
        m_outlineSlide->stop();
        if (shouldAnimate && !m_outline->isHidden() && m_outline->geometry() != tile) {
            m_outlineSlide->setStartValue(m_outline->geometry());
            m_outlineSlide->setEndValue(tile);
            m_outlineSlide->start();
        } else {
            m_outlineSlide->setEndValue(tile);
            m_outline->setGeometry(tile);
        }
    }
    m_outline->show();
    m_outline->raise();
    if (m_index < m_ids.size() && m_selected != m_ids[m_index]) {
        m_selected = m_ids[m_index];
        m_saveSelection->start();
    }
    auto* bar = m_scroll->horizontalScrollBar();
    const int offset = bar->value();
    int target = offset;
    if (tile.right() + gap >= offset + m_scroll->viewport()->width())
        target = tile.right() + gap + 1 - m_scroll->viewport()->width();
    else if (tile.left() - gap < offset)
        target = tile.left() - gap;
    slideTo(target, shouldAnimate);
}

void StellarGallery::slideTo(int target, bool animate)
{
    auto* bar = m_scroll->horizontalScrollBar();
    target = std::clamp(target, bar->minimum(), bar->maximum());
    if (animate && m_slide->state() == QAbstractAnimation::Running && m_slide->endValue().toInt() == target)
        return;
    m_slide->stop();
    if (animate && target != bar->value()) {
        m_slide->setStartValue(bar->value());
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

void StellarGallery::openInstance(const QString& id, int direction)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance)
        return;
    QPixmap previous;
    if (direction && m_detail && m_detailId != id && motionEnabled())
        previous = m_detailSlide ? m_detailSlide->grab() : m_detail->grab();
    delete m_detailSlide.data();
    if (m_detailId != id) {
        disconnect(m_detailRunningChanged);
        m_detailRunningChanged = connect(instance, &MinecraftInstance::runningStatusChanged, this,
                                         [this] { updateLaunch(); });
        m_detailId = id;
    }
    if (!m_detail) {
        auto* detail = new InstanceDetail(this, [this] { closeInstance(); },
                                         [this] { activatePlay(m_detailId); },
                                         [this] { showActions(m_ids.indexOf(m_detailId)); });
        detail->setGeometry(rect());
        detail->installEventFilter(this);
        m_detail = detail;
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
    if (!previous.isNull())
        m_detailSlide = new DetailSlide(this, std::move(previous), m_detail->grab(), direction);
    setFocus();
}

void StellarGallery::closeInstance()
{
    delete m_detailSlide.data();
    disconnect(m_detailRunningChanged);
    m_detailId.clear();
    m_detailArtworkPath.clear();
    if (auto* detail = std::exchange(m_detail, nullptr)) {
        detail->hide();
        detail->deleteLater();
        m_scroll->show();
        scheduleThumbnails();
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
    m_detailArtworkPath = path;
    detail->setInstance(instance->name(), info.join(QStringLiteral("  ·  ")), wallpaperPixmap(path));
    detail->setWallpaperFile(path);
    updateLaunch();
}

void StellarGallery::playInstance(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance || m_launches.contains(id) || m_preflight.contains(id))
        return;

    if (instance->isHorizon() || !instance->canLaunch()) {
        APPLICATION->launch(instance);
        return;
    }

    m_preflight.insert(id);
    updateLaunch();
    const QDir mods(QDir(instance->gameRoot()).filePath("mods"));
    const auto versions = StellarClient::versions(instance);
    QtConcurrent::run([mods, versions] { return StellarMods::incompatible(mods, versions); })
        .then(this, [this, id](const QStringList& issues) {
            m_preflight.remove(id);
            updateLaunch();
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

void StellarGallery::activatePlay(const QString& id)
{
    if (auto* instance = APPLICATION->instances()->getInstanceById(id); instance && instance->isRunning()) {
        showStop(id);
        return;
    }
    playInstance(id);
}

void StellarGallery::showStop(const QString& id)
{
    const auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance || !instance->isRunning())
        return;
    if (instance->isHorizon()) {
        auto* panel = makePanel(tr("Rust Minecraft is running"));
        panel->setFixedWidth(300);
        auto* hint = new QLabel(tr("Quit Minecraft to save and stop this instance."), panel);
        hint->setWordWrap(true);
        panel->layout()->addWidget(hint);
        placePanel();
        return;
    }
    auto* panel = makePanel(tr("Stop Minecraft?"));
    panel->setFixedWidth(326);
    auto* message = new QLabel(tr("Force stop %1? Unsaved progress might be lost.").arg(instance->name()), panel);
    message->setWordWrap(true);
    panel->layout()->addWidget(message);
    auto* stop = new GalleryButton(tr("Force Stop"), panel);
    panel->layout()->addWidget(stop);
    connect(stop, &QPushButton::clicked, panel, [this, id] {
        dismissPanel();
        if (auto* running = APPLICATION->instances()->getInstanceById(id); running && running->isRunning())
            APPLICATION->kill(running);
    });
    placePanel();
}

void StellarGallery::launchChecked(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance || !instance->canLaunch() || m_launches.contains(id))
        return;
    m_launches.insert(id, QElapsedTimer{});
    instance->setProperty("stellarLaunchState", "preparing");
    instance->setProperty("stellarGalleryFullscreen", APPLICATION->settings()->get("StellarFillScreen"));
    if (APPLICATION->launch(instance)) {
        instance->installEventFilter(this);
        m_launchTimer->start();
    } else {
        m_launches.remove(id);
    }
    updateLaunch();
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
            if (next != index) {
                selectIndex(next, false);
                openInstance(m_ids[next], next > index ? 1 : -1);
            }
            break;
        }
        selectIndex(event->key() == Qt::Key_Home ? 0 : event->key() == Qt::Key_End ? m_tiles.size() - 1 :
                    m_index + (event->key() == Qt::Key_Left ? -1 : 1), true);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!event->isAutoRepeat()) {
            if (m_detail)
                activatePlay(m_detailId);
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

bool StellarGallery::event(QEvent* event)
{
    if (event->type() == QEvent::DevicePixelRatioChange) {
        QTimer::singleShot(0, this, [this] {
            scheduleThumbnails();
            if (m_detail)
                refreshInstance();
        });
    }
    return QWidget::event(event);
}

bool StellarGallery::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::DynamicPropertyChange && !m_launches.isEmpty()) {
        const auto* change = static_cast<QDynamicPropertyChangeEvent*>(event);
        if (change->propertyName() == "stellarLaunchState" && object->property("stellarLaunchState").toString() == "ready")
            updateLaunch();
    }
    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const bool precise = !wheel->pixelDelta().isNull();
        const auto delta = precise ? wheel->pixelDelta() : wheel->angleDelta();
        const int scroll = delta.x() != 0 ? delta.x() : delta.y();
        if (m_detail)
            return false;
        const auto animate = !precise && motionEnabled();
        const int from = animate && m_slide->state() == QAbstractAnimation::Running ? m_slide->endValue().toInt()
                                                                                   : m_scroll->horizontalScrollBar()->value();
        slideTo(from - (precise ? scroll : scroll / 2), animate);
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
    if (m_detail)
        static_cast<InstanceDetail*>(m_detail)->suspendWallpaper(true);

}

void StellarGallery::dismissPanel()
{
    if (auto* panel = std::exchange(m_panel, nullptr)) {
        panel->hide();
        panel->deleteLater();
        if (m_detail)
            static_cast<InstanceDetail*>(m_detail)->suspendWallpaper(false);
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
    dismissPanel();
    auto* panel = new QFrame(this);
    m_panel = panel;
    panel->setObjectName("stellarPanel");
    panel->setAccessibleName(instance ? tr("Actions for %1").arg(instance->name()) : tr("Library actions"));
    auto* row = new QHBoxLayout(panel);
    row->setContentsMargins(7, 7, 7, 7);
    row->setSpacing(5);
    const auto icon = [](const QString& name) { return QIcon(":/icons/flat_white/scalable/" + name + ".svg"); };
    const auto add = [this, panel, row](const QIcon& image, const QString& label, std::function<void()> action) {
        auto* button = new GalleryButton(image, {}, panel);
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setFixedSize(35, 35);
        button->setIconSize(QSize(19, 19));
        row->addWidget(button);
        connect(button, &QPushButton::clicked, panel, [this, action = std::move(action)] {
            dismissPanel();
            action();
        });
        return button;
    };

    if (instance) {
        if (instance->isRunning())
            add(style()->standardIcon(QStyle::SP_MediaStop), tr("Stop Minecraft"), [this, id] { showStop(id); });
        else
            add(icon("launch"), tr("Play"), [this, id] {
                openInstance(id);
                playInstance(id);
            });
        add(icon("instance-settings"), tr("Edit"), [this, id] { editInstance(id); });
        add(icon("viewfolder"), tr("Open instance folder"), [id] {
            if (const auto* current = APPLICATION->instances()->getInstanceById(id))
                QDesktopServices::openUrl(QUrl::fromLocalFile(current->instanceRoot()));
        });
        auto* trash = add(icon("delete"), tr("Move instance to Trash"), [this, id] { trashInstance(id); });
        trash->setEnabled(!instance->isRunning() && !instance->launchController() && !m_launches.contains(id));
        if (!trash->isEnabled())
            trash->setToolTip(tr("Stop Minecraft before removing this instance"));
    } else {
        add(icon("new"), tr("Create Instance"), [this] { showCreator(); });
        add(icon("export"), tr("Import Instance"), [this] { showImport(); });
        add(icon("screenshots"), tr("Wallpapers"), [this] { showWallpapers({}); });
    }
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
    row->itemAt(0)->widget()->setFocus(Qt::PopupFocusReason);
}

void StellarGallery::trashInstance(const QString& id)
{
    auto* instances = APPLICATION->instances();
    const auto canTrash = [this, instances, id] {
        auto* current = instances->getInstanceById(id);
        return current && !current->isRunning() && !current->launchController() && !m_launches.contains(id);
    };
    if (!canTrash())
        return;

    const auto name = instances->getInstanceById(id)->name();
    auto* panel = makePanel(tr("Move to Trash?"));
    panel->setFixedWidth(350);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* title = new QLabel(name, panel);
    title->setTextFormat(Qt::PlainText);
    title->setStyleSheet("font-weight: 600;");
    title->setWordWrap(true);
    layout->addWidget(title);
    const auto linked = instances->getLinkedInstancesById(id);
    auto* hint = new QLabel(linked.isEmpty() ? tr("This moves its worlds, mods, and settings to the Trash.")
                                           : tr("Linked instances may lose access to files: %1.").arg(linked.join(", ")), panel);
    hint->setTextFormat(Qt::PlainText);
    hint->setStyleSheet("color: #c3beca;");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* controls = new QHBoxLayout;
    controls->addStretch();
    auto* cancel = new GalleryButton(tr("Cancel"), panel);
    auto* move = new GalleryButton(QIcon(":/icons/flat_white/scalable/delete.svg"), tr("Move to Trash"), panel);
    move->setStyleSheet("QPushButton { background: #522d36; border: 1px solid #814654; color: #f1dce2; }"
                        "QPushButton:focus { border-color: #e0a6b6; }");
    controls->addWidget(cancel);
    controls->addWidget(move);
    layout->addLayout(controls);
    connect(cancel, &QPushButton::clicked, panel, [this] { dismissPanel(); });
    connect(move, &QPushButton::clicked, panel, [this, instances, id, canTrash, hint, move, linked] {
        if (!canTrash()) {
            hint->setText(tr("Close the running game before moving this instance to the Trash."));
            move->setEnabled(false);
            return;
        }
        if (linked != instances->getLinkedInstancesById(id)) {
            trashInstance(id);
            return;
        }
        if (instances->trashInstance(id))
            dismissPanel();
        else
            hint->setText(tr("Stellar couldn’t move this instance to the Trash."));
    });
    placePanel();
    cancel->setFocus();
}

namespace {
struct RustModManifest {
    QString id;
    QString name;
    bool native = false;
};

RustModManifest readRustMod(const QString& path)
{
    const auto manifest = MMCZip::ArchiveReader(path).readFile("horizon-mod.toml", 128 * 1024);
    if (!manifest)
        return {};
    toml::table table;
#if TOML_EXCEPTIONS
    try {
        table = toml::parse(manifest.value().toStdString());
    } catch (const std::exception&) {
        return {};
    }
#else
    auto parsed = toml::parse(manifest.value().toStdString());
    if (!parsed)
        return {};
    table = parsed.table();
#endif
    const auto id = QString::fromStdString(table["id"].value_or(std::string{}));
    static const QRegularExpression validId("^(?!(?:con|prn|aux|nul|(?:com|lpt)[1-9])$)[a-z0-9_]{1,64}$");
    if (!validId.match(id).hasMatch())
        return {};
    return {id, QString::fromStdString(table["name"].value_or(std::string{})), table["native"].is_table()};
}

// The same switches the game's Mods screen reads, including explicit false for native mods.
bool setRustModEnabled(const QDir& root, const QString& id, bool enabled)
{
    const auto path = root.filePath("horizon-mods.txt");
    QFile previous(path);
    QByteArray contents;
    if (previous.exists()) {
        if (!previous.open(QIODevice::ReadOnly))
            return false;
        contents = previous.readAll();
        if (previous.error() != QFileDevice::NoError)
            return false;
        previous.close();
    }
    const auto key = "mod." + id.toUtf8();
    auto lines = contents.split('\n');
    lines.removeIf([&key](const QByteArray& line) {
        const auto separator = line.indexOf(':');
        return separator >= 0 && line.first(separator).trimmed() == key;
    });
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    lines.append(key + (enabled ? ":true" : ":false"));
    const auto updated = lines.join('\n') + '\n';
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(updated) == updated.size() && file.commit();
}

struct RustContent {
    QString file;
    QString title;
    QString id;
    QString project;
    bool enabled = false;
    bool native = false;
    bool unsupported = false;
};
}

void StellarGallery::editRustInstance(MinecraftInstance* instance)
{
    dismissPanel();
    QVBoxLayout* shell;
    auto* dialog = galleryDialog(instance->name(), shell);
    const QDir root(instance->gameRoot());
    const auto current = QPointer(instance);
    const auto nativeRemoval = std::make_shared<QString>();

    auto* header = new QHBoxLayout;
    auto* tabs = new QTabBar(dialog);
    tabs->addTab(tr("Mods"));
    tabs->addTab(tr("Resource Packs"));
    tabs->addTab(tr("Shaders"));
    tabs->setExpanding(false);
    tabs->setAccessibleName(tr("Content type"));
    tabs->setStyleSheet(
        "QTabBar::tab { color: #bfbec8; padding: 5px 12px; margin-right: 3px; border-radius: 6px; }"
        "QTabBar::tab:hover { background: #242429; }"
        "QTabBar::tab:selected { color: #f1f0f4; background: #303037; font-weight: 600; }"
        "QTabBar::tab:focus { border: 1px solid palette(highlight); }");
    header->addWidget(tabs, 1);
    auto* add = new GalleryButton(tr("+ Add"), dialog);
    add->setAccessibleName(tr("Add content"));
    header->addWidget(add);
    auto* options = new GalleryButton(QStringLiteral("⋯"), dialog);
    options->setAccessibleName(tr("Instance tools"));
    options->setToolTip(tr("Instance tools"));
    header->addWidget(options);
    shell->addLayout(header);

    auto* files = new QListWidget(dialog);
    files->setAccessibleName(tr("Installed content"));
    files->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    files->setStyleSheet(
        "QListWidget { background: #141418; color: #efedf3; border: 1px solid #28282d; border-radius: 8px; padding: 4px; outline: none; }"
        "QListWidget::item { min-height: 28px; padding: 1px 8px; border-radius: 5px; }"
        "QListWidget::item:hover { background: #212126; }"
        "QListWidget::item:selected { background: #2c2c34; color: #f1f0f4; }"
        "QListWidget::item:focus { border: 1px solid palette(highlight); }");
    shell->addWidget(files, 1);
    auto* footer = new QHBoxLayout;
    auto* status = new QLabel(dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    footer->addWidget(status, 1);
    auto* remove = new GalleryButton(tr("Remove"), dialog);
    remove->setEnabled(false);
    footer->addWidget(remove);
    shell->addLayout(footer);

    auto* tools = new QMenu(options);
    connect(tools->addAction(tr("Game folder")), &QAction::triggered, dialog, [root] {
        if (root.mkpath("."))
            QDesktopServices::openUrl(QUrl::fromLocalFile(root.absolutePath()));
    });
    connect(tools->addAction(tr("Wallpaper")), &QAction::triggered, dialog, [this, dialog, id = instance->id()] {
        dialog->close();
        showWallpapers(id);
    });
    connect(tools->addAction(tr("Rust setup")), &QAction::triggered, dialog, [] { APPLICATION->openHorizonSetup(); });
    options->setMenu(tools);

    auto* operation = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("HORIZON_HOME", root.absolutePath());
    environment.insert("HORIZON_LAUNCHER_NO_SELF_UPDATE", "1");
    operation->setProcessEnvironment(environment);
    operation->setProcessChannelMode(QProcess::MergedChannels);
    connect(dialog, &QObject::destroyed, operation, [operation] {
        if (operation->state() == QProcess::NotRunning)
            operation->deleteLater();
        else
            connect(operation, &QProcess::finished, operation, &QObject::deleteLater);
    });

    auto* index = new QProcess(dialog);
    index->setProcessEnvironment(environment);
    auto content = std::make_shared<QList<RustContent>>();
    const auto populate = [files, status, remove, content] {
        const auto selected = files->currentItem() ? files->currentItem()->data(Qt::UserRole).toString() : QString();
        const QSignalBlocker blocked(files);
        files->clear();
        for (const auto& entry : *content) {
            auto* item = new QListWidgetItem(entry.title, files);
            item->setData(Qt::UserRole, entry.file);
            if (!entry.unsupported && (!entry.id.isEmpty() || files->property("isPack").toBool())) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
            }
            item->setToolTip(entry.native ? tr("Native mod. Some changes may require rebuilding the engine.") : entry.file);
            if (entry.file == selected)
                files->setCurrentItem(item);
        }
        if (!files->currentItem() && files->count())
            files->setCurrentRow(0);
        remove->setEnabled(files->currentItem());
        status->setText(files->count() ? QString() : tr("Nothing installed"));
    };

    const auto category = [tabs] { return tabs->currentIndex() == 1 ? QStringLiteral("resource") : QStringLiteral("shader"); };
    const auto load = [index, operation, files, add, remove, status, tabs, category, content] {
        if (index->state() != QProcess::NotRunning || operation->state() != QProcess::NotRunning)
            return;
        const auto program = APPLICATION->horizonLauncher();
        if (program.isEmpty()) {
            status->setText(tr("Rust tools unavailable"));
            add->setEnabled(false);
            return;
        }
        content->clear();
        files->setProperty("isPack", tabs->currentIndex() != 0);
        files->setEnabled(false);
        add->setEnabled(false);
        remove->setEnabled(false);
        status->setText(tr("Loading…"));
        const QStringList command = tabs->currentIndex() == 0
            ? QStringList{"mods", "list", "--json", "--instance", "horizon"}
            : QStringList{"packs", "list", "--json", "--kind", category(), "--instance", "horizon"};
        index->start(program, command);
    };
    connect(index, &QProcess::finished, dialog, [=](int code, QProcess::ExitStatus exit) {
        add->setEnabled(true);
        files->setEnabled(true);
        if (code != 0 || exit != QProcess::NormalExit) {
            files->clear();
            status->setText(tr("Couldn’t load content"));
            return;
        }
        const auto entries = QJsonDocument::fromJson(index->readAllStandardOutput()).array();
        if (files->property("isPack").toBool()) {
            for (const auto& value : entries) {
                const auto item = value.toObject();
                const auto file = item["file"].toString();
                if (!file.isEmpty() && QFileInfo(file).fileName() == file && !file.startsWith('-'))
                    content->append({file, item["title"].toString(file), {}, {}, item["enabled"].toBool(), false,
                                     !item["unsupported"].toString().isEmpty()});
            }
        } else {
            QHash<QString, QJsonObject> managed;
            for (const auto& value : entries) {
                const auto item = value.toObject();
                if (item["kind"].toString() == "mod")
                    managed.insert(QDir::fromNativeSeparators(item["file"].toString()), item);
            }
            QHash<QString, bool> enabled;
            QFile switches(root.filePath("horizon-mods.txt"));
            if (switches.open(QIODevice::ReadOnly)) {
                while (!switches.atEnd()) {
                    const auto line = switches.readLine().trimmed();
                    const auto split = line.indexOf(':');
                    if (line.startsWith("mod.") && split > 4)
                        enabled.insert(QString::fromUtf8(line.mid(4, split - 4).trimmed()), line.mid(split + 1).trimmed() == "true");
                }
            }
            for (const auto& folder : {"horizon-mods", "horizon-native-mods"}) {
                const auto directory = QDir(root.filePath(folder));
                for (const auto& file : directory.entryInfoList({"*.hmod", "*.wasm"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
                    const auto path = root.relativeFilePath(file.absoluteFilePath());
                    const auto project = managed.value(path);
                    const auto mod = file.suffix() == "hmod" ? readRustMod(file.absoluteFilePath()) : RustModManifest{};
                    const auto title = project["title"].toString(mod.name.isEmpty() ? file.completeBaseName() : mod.name);
                    const auto native = mod.native || directory.dirName() == "horizon-native-mods";
                    content->append({path, title, mod.id, project["project"].toString(), enabled.value(mod.id, native), native});
                }
            }
        }
        populate();
    });
    connect(index, &QProcess::errorOccurred, dialog, [=](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            add->setEnabled(true);
            files->setEnabled(true);
            files->clear();
            status->setText(tr("Couldn’t load content"));
        }
    });
    const auto editable = [operation, index, current, status] {
        if (!current || current->isRunning()) {
            status->setText(tr("Close Minecraft to make changes"));
            return false;
        }
        return operation->state() == QProcess::NotRunning && index->state() == QProcess::NotRunning;
    };
    const auto run = [=](const QStringList& args) {
        if (!editable())
            return false;
        const auto program = APPLICATION->horizonLauncher();
        if (program.isEmpty())
            return false;
        add->setEnabled(false);
        remove->setEnabled(false);
        files->setEnabled(false);
        tabs->setEnabled(false);
        status->setText(tr("Working…"));
        operation->start(program, args);
        return true;
    };
    const auto done = [=](bool success) {
        const auto detail = QString::fromUtf8(operation->readAll()).trimmed();
        tabs->setEnabled(true);
        const auto removedId = std::exchange(*nativeRemoval, {});
        if (success && (removedId.isEmpty() || setRustModEnabled(root, removedId, false))) {
            load();
        } else {
            add->setEnabled(true);
            files->setEnabled(true);
            remove->setEnabled(files->currentItem());
            status->setText(tr("Couldn’t change content"));
            status->setToolTip(detail);
        }
    };
    connect(operation, &QProcess::finished, dialog, [done](int code, QProcess::ExitStatus state) {
        done(code == 0 && state == QProcess::NormalExit);
    });
    connect(operation, &QProcess::errorOccurred, dialog, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(false);
    });
    connect(tabs, &QTabBar::currentChanged, dialog, load);
    connect(index, &QProcess::stateChanged, tabs, [tabs](QProcess::ProcessState state) {
        tabs->setEnabled(state == QProcess::NotRunning);
    });
    connect(files, &QListWidget::currentItemChanged, remove, [remove](QListWidgetItem* item) { remove->setEnabled(item != nullptr); });

    const auto lookup = [content](const QListWidgetItem* item) -> RustContent {
        if (item) {
            const auto path = item->data(Qt::UserRole).toString();
            for (const auto& entry : *content) {
                if (entry.file == path)
                    return entry;
            }
        }
        return {};
    };
    connect(files, &QListWidget::itemChanged, dialog, [=](QListWidgetItem* item) {
        const auto entry = lookup(item);
        const auto enabled = item->checkState() == Qt::Checked;
        if (entry.file.isEmpty() || entry.enabled == enabled)
            return;
        {
            const QSignalBlocker blocked(files);
            item->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        }
        if (files->property("isPack").toBool()) {
            run({"packs", enabled ? "on" : "off", "--instance", "horizon", "--kind", category(), entry.file});
        } else if (editable() && !entry.id.isEmpty()) {
            // Native toggles need an explicit false, which the browser's generic disable drops.
            if (!setRustModEnabled(root, entry.id, enabled)) {
                status->setText(tr("Couldn’t change mod"));
                return;
            }
            for (auto& stored : *content) {
                if (stored.file == entry.file) {
                    stored.enabled = enabled;
                    break;
                }
            }
            const QSignalBlocker blocked(files);
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            status->clear();
        }
    });
    connect(remove, &QPushButton::clicked, dialog, [=] {
        if (!editable())
            return;
        const auto entry = lookup(files->currentItem());
        if (entry.file.isEmpty() ||
            QMessageBox::question(dialog, tr("Remove content?"), entry.title, QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        if (!editable())
            return;
        if (files->property("isPack").toBool()) {
            run({"packs", "remove", "--instance", "horizon", "--kind", category(), entry.file});
        } else if (!entry.project.isEmpty() && !entry.project.startsWith('-')) {
            if (entry.native && !entry.id.isEmpty() && !setRustModEnabled(root, entry.id, false)) {
                status->setText(tr("Couldn’t disable mod"));
                return;
            }
            if (run({"mods", "remove", "--instance", "horizon", entry.project}) && entry.native)
                *nativeRemoval = entry.id;
        } else {
            const auto path = root.filePath(entry.file);
            if (entry.native && !entry.id.isEmpty() && !setRustModEnabled(root, entry.id, false)) {
                status->setText(tr("Couldn’t disable mod"));
                return;
            }
            if (QFile::moveToTrash(path)) {
                load();
            } else {
                if (entry.native && entry.enabled && !entry.id.isEmpty())
                    setRustModEnabled(root, entry.id, true);
                status->setText(tr("Couldn’t move mod to Trash"));
            }
        }
    });

    auto* addMenu = new QMenu(add);
    connect(addMenu->addAction(tr("Browse")), &QAction::triggered, dialog, [] {
        QDesktopServices::openUrl(QUrl("https://playhorizon.dev/mods"));
    });
    connect(addMenu->addAction(tr("From link…")), &QAction::triggered, dialog, [=] {
        if (!editable())
            return;
        bool accepted = false;
        auto slug = QInputDialog::getText(dialog, tr("Add project"), tr("Project link or slug"), QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted)
            return;
        const QUrl url(slug);
        if (url.scheme() == "https" && url.host() == "playhorizon.dev" && url.path().startsWith("/mods/"))
            slug = url.path().mid(6).section('/', 0, 0);
        static const QRegularExpression valid("^[a-zA-Z0-9_][a-zA-Z0-9_-]*$");
        if (valid.match(slug).hasMatch())
            run({"mods", "install", "--source", "horizon", "--instance", "horizon", slug});
        else
            status->setText(tr("Enter a valid project link or slug"));
    });
    connect(addMenu->addAction(tr("From file…")), &QAction::triggered, dialog, [=] {
        if (!editable())
            return;
        const bool modTab = tabs->currentIndex() == 0;
        const auto path = QFileDialog::getOpenFileName(dialog, tr("Add content"), {},
            modTab ? tr("Rust mods (*.hmod)") : tr("Packs (*.zip *.hshader)"));
        if (path.isEmpty() || !editable())
            return;
        if (!modTab) {
            run({"packs", "add", "--instance", "horizon", path});
            return;
        }
        const auto mod = readRustMod(path);
        if (mod.id.isEmpty() || mod.native) {
            status->setText(tr("Choose a packaged content mod. Add native mods from a project link."));
            return;
        }
        const auto target = root.filePath("horizon-mods/" + mod.id + ".hmod");
        if (!root.mkpath("horizon-mods") || !QFile::copy(path, target)) {
            status->setText(tr("Couldn’t add mod. A copy may already be installed."));
            return;
        }
        if (!setRustModEnabled(root, mod.id, true)) {
            QFile::remove(target);
            status->setText(tr("Couldn’t enable mod"));
            return;
        }
        load();
    });
    add->setMenu(addMenu);
    load();
    dialog->show();
}

void StellarGallery::editInstance(const QString& id)
{
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    if (!instance)
        return;
    if (instance->isHorizon()) {
        editRustInstance(instance);
        return;
    }
    auto* editor = APPLICATION->showInstanceWindow(instance, "version", window());
    editor->setWallpaperAction([gallery = QPointer(this), editor = QPointer(editor), id] {
        if (gallery && editor && editor->close()) {
            gallery->window()->raise();
            gallery->window()->activateWindow();
            gallery->showWallpapers(id);
        }
    });
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
    if (m_creation && m_creation->isRunning()) {
        showCreationProgress();
        return;
    }
    auto* panel = makePanel(tr("Create New Instance"));
    panel->setFixedWidth(366);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* name = new QLineEdit(panel);
    name->setPlaceholderText(tr("Instance name"));
    name->setAccessibleName(tr("Instance name"));
    name->setMaxLength(80);
    layout->addWidget(name);
    auto* engineChoice = new QButtonGroup(panel);
    auto* javaEngine = new GalleryButton(tr("Minecraft Java"), panel);
    auto* horizonEngine = new GalleryButton(tr("Rust"), panel);
    javaEngine->setCheckable(true);
    horizonEngine->setCheckable(true);
    engineChoice->addButton(javaEngine);
    engineChoice->addButton(horizonEngine);
    javaEngine->setChecked(true);
    const auto choiceStyle = QStringLiteral("QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); }");
    javaEngine->setStyleSheet(choiceStyle);
    horizonEngine->setStyleSheet(choiceStyle);
    auto* engines = new QHBoxLayout;
    auto* engineLabel = new QLabel(tr("Engine"), panel);
    engineLabel->setFixedWidth(78);
    engines->addWidget(engineLabel);
    engines->addWidget(javaEngine, 1);
    engines->addWidget(horizonEngine, 1);
    layout->addLayout(engines);
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
    auto* horizonHint = new QLabel(tr("Play Minecraft 26.3 with the Rust engine. First-time setup is in Stellar."), panel);
    horizonHint->setWordWrap(true);
    horizonHint->hide();
    layout->addWidget(horizonHint);
    auto* compatible = new QCheckBox(tr("Add compatible performance mods"), panel);
    compatible->setToolTip(tr("Stellar selects matching stable builds of Sodium, Lithium and FerriteCore for this Fabric instance."));
    compatible->setChecked(true);
    compatible->hide();
    layout->addWidget(compatible);
    const auto updateEngine = [panel, horizonEngine, version, loader, compatible, horizonHint] {
        const bool horizon = horizonEngine->isChecked();
        version->setEnabled(!horizon);
        loader->setEnabled(!horizon);
        compatible->setVisible(!horizon && loader->currentIndex() == 1);
        horizonHint->setVisible(horizon);
        panel->adjustSize();
    };
    connect(horizonEngine, &QPushButton::toggled, panel, updateEngine);
    connect(loader, &QComboBox::currentIndexChanged, panel, updateEngine);
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
    const auto validate = [horizonEngine, version, loader, submit, java, fabric] {
        if (horizonEngine->isChecked()) {
            submit->setEnabled(bool(java->getVersion("26.3")));
            return;
        }
        const auto selected = version->currentData().toString();
        submit->setEnabled(!selected.isEmpty() &&
                           (loader->currentIndex() == 0 || (Version(selected) >= Version("1.14") && fabric->getRecommended())));
    };
    connect(horizonEngine, &QPushButton::toggled, panel, validate);
    connect(version, &QComboBox::currentIndexChanged, panel, validate);
    connect(loader, &QComboBox::currentIndexChanged, panel, validate);
    connect(import, &QPushButton::clicked, panel, [this] { showImport(); });
    connect(advanced, &QPushButton::clicked, panel, [this] { dismissPanel(); m_actions.create(); });
    connect(name, &QLineEdit::returnPressed, submit, &QPushButton::click);
    connect(submit, &QPushButton::clicked, panel, [this, horizonEngine, name, version, loader, java, fabric, fabricId, compatible] {
        const bool horizon = horizonEngine->isChecked();
        auto game = java->getVersion(horizon ? QStringLiteral("26.3") : version->currentData().toString());
        if (!game)
            return;
        VanillaCreationTask* creation = horizon || loader->currentIndex() == 0 ? new VanillaCreationTask(game) :
                                        new VanillaCreationTask(game, fabricId, fabric->getRecommended());
        auto title = name->text().trimmed();
        if (title.isEmpty())
            title = horizon ? tr("Minecraft %1 · Rust").arg(game->descriptor()) : tr("Minecraft %1").arg(game->descriptor());
        creation->setHorizon(horizon);
        creation->setPerformanceMods(!horizon && loader->currentIndex() == 1 && compatible->isChecked());
        creation->setName(title);
        creation->setOriginalName(title, game->descriptor());
        creation->setIcon("default");
        creation->setTargetDir(APPLICATION->instances()->primaryDir());
        auto* task = APPLICATION->instances()->wrapInstanceTask(creation);
        task->setParent(this);
        m_creation = task;
        showCreationProgress();
        connect(task, &Task::finished, this, [this, task] {
            QTimer::singleShot(0, this, [this, panel = QPointer(m_panel)] {
                if (panel && m_panel == panel && panel->property("stellarCreation").toBool())
                    dismissPanel();
            });
            if (task->getState() == Task::State::Failed)
                QMessageBox::warning(window(), tr("Couldn’t create instance"), task->failReason());
            m_creation.clear();
            task->deleteLater();
        });
        QTimer::singleShot(0, task, &Task::start);
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

void StellarGallery::showCreationProgress()
{
    auto* panel = makePanel(tr("Creating Instance"));
    panel->setProperty("stellarCreation", true);
    panel->setFixedWidth(380);
    auto* status = new QLabel(tr("Preparing instance…"), panel);
    status->setWordWrap(true);
    panel->layout()->addWidget(status);
    auto* progress = new QProgressBar(panel);
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    panel->layout()->addWidget(progress);
    connect(m_creation, &Task::status, status, &QLabel::setText);
    connect(m_creation, &Task::progress, progress, [progress](qint64 value, qint64 total) {
        progress->setRange(0, total > 0 ? 100 : 0);
        if (total > 0)
            progress->setValue(qBound(0, qRound(100.0 * value / total), 100));
    });
    if (!m_creation->getStatus().isEmpty())
        status->setText(m_creation->getStatus());
    placePanel();
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
    const auto* instances = APPLICATION->instances();
    qint64 played = 0;
    for (int i = 0; i < instances->count(); ++i)
        played += instances->at(i)->totalTimePlayed();
    static_cast<GalleryTile*>(m_accountTile)->setAccountCard(QPixmap::fromImage(preview),
        account ? account->displayName() : tr("Signed out"),
        played ? Time::prettifyDuration(played) : tr("0 min"),
        QLocale().toString(instances->count()));
}

QDialog* StellarGallery::galleryDialog(const QString& name, QVBoxLayout*& shell)
{
    auto* dialog = new QDialog(window(), Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(name);
    dialog->setFixedSize(windowSize);
    QPainterPath outline;
    outline.addRoundedRect(dialog->rect(), cornerRadius, cornerRadius);
    dialog->setMask(QRegion(outline.toFillPolygon().toPolygon()));
    dialog->move(window()->frameGeometry().topLeft());
    shell = new QVBoxLayout(dialog);
    shell->setContentsMargins(18, 10, 18, 13);
    shell->setSpacing(12);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(name, dialog);
    title->setStyleSheet("font-size: 17px; font-weight: 600;");
    header->addWidget(title, 1);
    auto* close = new GalleryButton(QStringLiteral("×"), dialog);
    close->setAccessibleName(tr("Close"));
    close->setFixedWidth(34);
    close->setAutoDefault(false);
    header->addWidget(close);
    shell->addLayout(header);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    auto* shortcut = new QShortcut(QKeySequence::Close, dialog);
    connect(shortcut, &QShortcut::activated, dialog, &QDialog::close);
    return dialog;
}

void StellarGallery::showAccounts()
{
    dismissPanel();
    if (m_accountsWindow) {
        m_accountsWindow->raise();
        m_accountsWindow->activateWindow();
        return;
    }
    QVBoxLayout* layout;
    auto* dialog = galleryDialog(tr("Accounts & Settings"), layout);
    m_accountsWindow = dialog;
    auto* content = new QHBoxLayout;
    content->setSpacing(16);
    layout->addLayout(content, 1);
    auto* accountList = new QVBoxLayout;
    content->addLayout(accountList, 1);
    auto* profiles = new QListWidget(dialog);
    profiles->setAccessibleName(tr("Minecraft accounts"));
    profiles->setIconSize(QSize(32, 32));
    profiles->setSpacing(3);
    accountList->addWidget(profiles);
    auto* empty = new QLabel(tr("No accounts yet"), dialog);
    empty->setAlignment(Qt::AlignCenter);
    accountList->addWidget(empty, 1);
    auto* controls = new QVBoxLayout;
    content->addLayout(controls);
    const auto add = [dialog, controls](const QString& label, const std::function<void()>& action) {
        auto* button = new GalleryButton(label, dialog);
        button->setMinimumWidth(174);
        button->setAutoDefault(false);
        controls->addWidget(button);
        connect(button, &QPushButton::clicked, dialog, action);
        return button;
    };
    auto* accounts = APPLICATION->accounts();
    const auto activate = [accounts, profiles] {
        const int row = profiles->currentRow();
        if (row >= 0 && row < accounts->count())
            accounts->setDefaultAccount(accounts->at(row));
    };
    add(tr("Add Microsoft Account…"), [dialog] { MSALoginDialog::addAccount(dialog); });
    auto* use = add(tr("Set Active"), activate);
    connect(profiles, &QListWidget::itemActivated, dialog, activate);
    add(tr("Skins…"), [this, dialog] { dialog->close(); m_actions.skins(); });
    add(tr("General Settings…"), [this, dialog] { dialog->close(); showPreferences(); });
    controls->addStretch();
    auto* remove = add(tr("Remove Account…"), [accounts, profiles, dialog] {
        const int row = profiles->currentRow();
        if (row < 0 || row >= accounts->count())
            return;
        const auto account = accounts->at(row);
        if (QMessageBox::question(dialog, tr("Remove Account"), tr("Remove %1 from Stellar?").arg(account->displayName()))
            == QMessageBox::Yes) {
            for (int i = 0; i < accounts->count(); ++i) {
                if (accounts->at(i) == account) {
                    accounts->removeAccount(accounts->index(i, 0));
                    break;
                }
            }
        }
    });
    const auto updateButtons = [profiles, accounts, use, remove] {
        const int row = profiles->currentRow();
        const bool selected = row >= 0 && row < accounts->count();
        use->setEnabled(selected && accounts->at(row) != accounts->defaultAccount());
        remove->setEnabled(selected);
    };
    const auto populate = [accounts, profiles, empty, updateButtons] {
        profiles->clear();
        for (int row = 0; row < accounts->count(); ++row) {
            const auto account = accounts->at(row);
            const bool active = account == accounts->defaultAccount();
            auto* item = new QListWidgetItem(accountFace(account.get()), active ? tr("%1  ·  Active").arg(account->displayName())
                                                                              : account->displayName(), profiles);
            if (active)
                profiles->setCurrentItem(item);
        }
        profiles->setVisible(profiles->count() > 0);
        empty->setVisible(profiles->count() == 0);
        updateButtons();
    };
    connect(profiles, &QListWidget::currentRowChanged, dialog, updateButtons);
    connect(accounts, &AccountList::listChanged, dialog, populate);
    connect(accounts, &AccountList::defaultAccountChanged, dialog, populate);
    populate();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void StellarGallery::showPreferences()
{
    dismissPanel();
    if (m_preferences) {
        m_preferences->raise();
        m_preferences->activateWindow();
        return;
    }
    QVBoxLayout* shell;
    auto* preferences = galleryDialog(tr("General Settings"), shell);
    m_preferences = preferences;

    auto* columns = new QHBoxLayout;
    columns->setSpacing(22);
    shell->addLayout(columns, 1);
    const auto section = [preferences, columns](const QString& heading) {
        auto* box = new QWidget(preferences);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 4, 0, 2);
        layout->setSpacing(8);
        auto* label = new QLabel(heading, box);
        label->setStyleSheet("font-weight: 600; color: #dedbe7;");
        layout->addWidget(label);
        columns->addWidget(box, 1);
        return layout;
    };
    auto* appearance = section(tr("Appearance"));
    auto* behavior = section(tr("Launcher"));
    const auto select = [preferences](QVBoxLayout* group, const QString& label, const QString& key,
                                     const QList<QPair<QString, QString>>& options, const std::function<void()>& changed = {}) {
        auto* row = new QHBoxLayout;
        auto* name = new QLabel(label, preferences);
        auto* field = new QComboBox(preferences);
        field->setAccessibleName(label);
        name->setBuddy(field);
        for (const auto& [name, id] : options)
            field->addItem(name, id);
        field->setCurrentIndex(qMax(0, field->findData(APPLICATION->settings()->get(key))));
        connect(field, &QComboBox::currentIndexChanged, preferences, [field, key, changed] {
            APPLICATION->settings()->set(key, field->currentData());
            if (changed)
                changed();
        });
        row->addWidget(name);
        row->addStretch(1);
        row->addWidget(field, 1);
        group->addLayout(row);
    };
    const auto check = [preferences](QVBoxLayout* group, const QString& label, const QString& key) {
        auto* button = new QCheckBox(label, preferences);
        button->setChecked(APPLICATION->settings()->get(key).toBool());
        connect(button, &QCheckBox::toggled, preferences, [key](bool value) { APPLICATION->settings()->set(key, value); });
        group->addWidget(button);
    };

    const QList<QPair<QString, QString>> colors{{tr("Silver"), "silver"}, {tr("Purple"), "violet"},
                                               {tr("Ice Blue"), "ice"}, {tr("Amber"), "amber"}};
    select(appearance, tr("Launcher accent"), "StellarAccent", colors, [this] {
        APPLICATION->themeManager()->setApplicationTheme("archive");
        m_outline->update();
    });
    select(appearance, tr("Minecraft title"), "StellarTitleTint", colors);
    select(appearance, tr("Black hole disk"), "StellarDiskTint", colors);
    check(appearance, tr("Stellar title screen"), "StellarTitleScreen");
    check(appearance, tr("Motion effects"), "StellarMotion");
    check(appearance, tr("Opening animation"), "StellarOpeningAnimation");
    appearance->addStretch();

    select(behavior, tr("Launch indicator"), "StellarLaunchAnimation",
           {{tr("Subtle progress"), "progress"}, {tr("Wallpaper wave"), "wave"}, {tr("Wallpaper fade"), "fade"},
            {tr("Horizontal wipe"), "wipe"}, {tr("Iris reveal"), "iris"}, {tr("Status only"), "off"}});
    select(behavior, tr("Loader filter"), "StellarLoaderFilter",
           {{tr("All loaders"), ""}, {tr("Vanilla"), "Vanilla"}, {tr("Rust"), "Horizon"}, {"Fabric", "Fabric"}, {"Forge", "Forge"},
            {"NeoForge", "NeoForge"}, {"Quilt", "Quilt"}, {"LiteLoader", "LiteLoader"}}, [this] { rebuild(); });
    check(behavior, tr("Fit game window to screen"), "StellarFillScreen");
    auto* updates = new GalleryButton(tr("Check for Updates…"), preferences);
    updates->setEnabled(APPLICATION->updatesAreAllowed());
    connect(APPLICATION, &Application::updateAllowedChanged, updates, &QWidget::setEnabled);
    connect(updates, &QPushButton::clicked, preferences, [] {
        if (APPLICATION->updater())
            APPLICATION->triggerUpdateCheck();
        else
            QDesktopServices::openUrl(QUrl("https://github.com/sebastianmiletic/stellarlauncher/releases/latest"));
    });
    behavior->addWidget(updates);
    check(behavior, tr("Match mods on version change"), "StellarAutoMatchMods");
    auto* account = new GalleryButton(tr("Microsoft App ID…"), preferences);
    behavior->addWidget(account);
    connect(account, &QPushButton::clicked, preferences, [preferences] { MSALoginDialog::configureMicrosoftApplication(preferences); });
#ifdef Q_OS_MACOS
    auto* compact = new GalleryButton(tr("Deduplicate Mods"), preferences);
    behavior->addWidget(compact);
    connect(compact, &QPushButton::clicked, preferences, [this, compact] {
        QList<QDir> allMods;
        QList<QDir> inactiveMods;
        auto* instances = APPLICATION->instances();
        for (int i = 0; i < instances->count(); ++i) {
            const auto* instance = instances->at(i);
            const QDir mods(QDir(instance->gameRoot()).filePath("mods"));
            allMods.append(mods);
            if (!instance->isRunning() && !m_launches.contains(instance->id()))
                inactiveMods.append(mods);
        }
        compact->setEnabled(false);
        compact->setText(tr("Looking for matching mods…"));
        QtConcurrent::run([allMods, inactiveMods] { return StellarMods::shareCopies(allMods, inactiveMods); })
            .then(compact, [compact](qsizetype count) {
                compact->setText(count ? tr("Deduplicated %n mod file(s)", nullptr, int(count))
                                       : tr("No matching mod files to deduplicate"));
                compact->setEnabled(true);
            });
    });
#endif
    behavior->addStretch();
    preferences->show();
    preferences->raise();
    preferences->activateWindow();
}

QStringList StellarGallery::images(const Collection& collection) const
{
    QStringList paths;
    QSet<QString> names;
    const auto appendImages = [&paths, &names](const QDir& directory) {
        const auto files = directory.entryInfoList({"*.jpg", "*.jpeg", "*.png", "*.webp", "*.gif"}, QDir::Files, QDir::Name);
        for (const auto& file : files) {
            if (!names.contains(file.fileName())) {
                paths.append(file.absoluteFilePath());
                names.insert(file.fileName());
            }
        }
    };
    appendImages(QDir(QDir(m_wallpapers).filePath(collection.id)));
    if (collection.id != "animated") {
        const auto legacy = QDir::home().filePath("Library/Application Support/Stellar Gallery/Wallpapers");
        appendImages(QDir(QDir(legacy).filePath(collection.id)));
    }
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
    if (QFileInfo(selected).isFile())
        return selected;
    const auto files = images(collection);
    if (files.isEmpty())
        fetchArtwork(collection);
    const auto best = [this](const QStringList& paths) {
        QString choice;
        qint64 resolution = 0;
        for (const auto& path : paths) {
            if (!m_wallpaperDimensions.contains(path))
                m_wallpaperDimensions.insert(path, QImageReader(path).size());
            const auto size = m_wallpaperDimensions.value(path);
            // The same landscape image is used for both the card and the full instance view.
            const auto usablePixels = qint64(size.width()) * qMin(size.height(), size.width() * 9 / 16);
            if (usablePixels > resolution) {
                resolution = usablePixels;
                choice = path;
            }
        }
        return choice;
    };
    if (const auto file = best(files); !file.isEmpty())
        return file;
    if (const auto file = best(images(m_collections.constFirst())); !file.isEmpty())
        return file;
    return QStringLiteral(":/stellar-gallery/fallback.jpg");
}

void StellarGallery::scheduleThumbnails()
{
    if (!m_scroll->isVisible() && m_detail)
        return;
    if (m_tiles.isEmpty())
        return;
    const auto target = m_tiles.first()->size() * devicePixelRatioF();
    m_thumbnailSize = target;
    const int offset = m_scroll->horizontalScrollBar()->value();
    const int left = offset - windowWidth;
    const int right = offset + 2 * windowWidth;
    QStringList paths;
    for (auto* tile : std::as_const(m_tiles)) {
        const auto path = tile->property("wallpaperPath").toString();
        auto* card = static_cast<GalleryTile*>(tile);
        if (tile->geometry().right() < left || tile->geometry().left() > right) {
            if (!path.isEmpty())
                card->setArtwork({});
            continue;
        }
        if (path.isEmpty())
            continue;
        const auto* cached = m_thumbnails.object(path);
        if (cached)
            card->setArtwork(cached->artwork);
        else if (!card->hasArtwork())
            card->setArtwork(wallpaperPlaceholder());
        if ((!cached || cached->target != target) && !m_pendingThumbnails.contains(path)) {
            m_pendingThumbnails.insert(path);
            paths.append(path);
        }
    }
    if (paths.isEmpty())
        return;

    QtConcurrent::run(wallpaperPool(), [paths, target] {
        QList<QPair<QString, QImage>> decoded;
        decoded.reserve(paths.size());
        for (const auto& path : paths) {
            auto image = readWallpaper(path, target, true);
            if (image.isNull())
                image = readWallpaper(":/stellar-gallery/fallback.jpg", target, true);
            decoded.append({path, std::move(image)});
        }
        return decoded;
    }).then(this, [this, target](const QList<QPair<QString, QImage>>& decoded) {
        for (const auto& [path, image] : decoded) {
            m_pendingThumbnails.remove(path);
            if (target == m_thumbnailSize || !m_thumbnails.contains(path)) {
                auto artwork = image.isNull() ? wallpaperPlaceholder() : QPixmap::fromImage(image);
                const auto cost = image.isNull() ? 1 : pixmapKiB(image);
                m_thumbnails.insert(path, new Thumbnail{artwork, target}, cost);
                if (target == m_thumbnailSize) {
                    const int left = m_scroll->horizontalScrollBar()->value() - windowWidth;
                    const int right = left + 3 * windowWidth;
                    for (auto* tile : std::as_const(m_tiles)) {
                        if (tile->geometry().right() >= left && tile->geometry().left() <= right &&
                            tile->property("wallpaperPath").toString() == path)
                            static_cast<GalleryTile*>(tile)->setArtwork(artwork);
                    }
                }
            }
        }
        if (target != m_thumbnailSize)
            scheduleThumbnails();
    });
}

QPixmap StellarGallery::wallpaperPixmap(const QString& file)
{
    const auto target = size() * devicePixelRatioF();
    if (target != m_detailImageSize) {
        m_detailImageSize = target;
        m_artwork.clear();
    }
    if (const auto* cached = m_artwork.object(file))
        return *cached;
    if (!file.isEmpty() && !m_detailDecoding) {
        m_detailDecoding = true;
        QtConcurrent::run(wallpaperPool(), [file, target] {
            auto image = readWallpaper(file, target, true);
            if (image.isNull())
                image = readWallpaper(":/stellar-gallery/fallback.jpg", target, true);
            return image;
        }).then(this, [this, file, target](const QImage& image) {
            m_detailDecoding = false;
            if (target == m_detailImageSize)
                m_artwork.insert(file, new QPixmap(QPixmap::fromImage(image)), pixmapKiB(image));
            if (m_detail && (m_detailArtworkPath == file || !m_artwork.contains(m_detailArtworkPath)))
                refreshInstance();
        });
    }
    if (const auto* thumbnail = m_thumbnails.object(file))
        return thumbnail->artwork;
    return wallpaperPlaceholder();
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
            m_wallpaperDimensions.clear();
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
            if (const auto* cached = m_thumbnails.object(path))
                static_cast<GalleryTile*>(m_tiles[i])->setArtwork(cached->artwork);
        }
    }
    scheduleThumbnails();
    if (m_detail)
        refreshInstance();
}

void StellarGallery::showWallpapers(const QString& instance)
{
    dismissPanel();
    if (m_wallpaperWindow) {
        m_wallpaperWindow->hide();
        m_wallpaperWindow->deleteLater();
    }
    QVBoxLayout* layout;
    auto* dialog = galleryDialog(tr("Wallpapers"), layout);
    m_wallpaperWindow = dialog;
    auto* toolbar = new QHBoxLayout;
    layout->addLayout(toolbar);
    auto* collections = new QComboBox(dialog);
    collections->setAccessibleName(tr("Wallpaper collection"));
    for (const auto& collection : std::as_const(m_collections))
        collections->addItem(collection.title, collection.id);
    toolbar->addWidget(collections);
    toolbar->addStretch();
    auto* download = new GalleryButton(tr("Download Collection"), dialog);
    auto* upload = new GalleryButton(tr("Upload…"), dialog);
    toolbar->addWidget(download);
    toolbar->addWidget(upload);

    auto* files = new QListWidget(dialog);
    files->setAccessibleName(tr("Wallpaper previews"));
    files->setViewMode(QListView::IconMode);
    files->setMovement(QListView::Static);
    files->setWrapping(true);
    files->setResizeMode(QListView::Adjust);
    files->setGridSize(QSize(190, 109));
    const QSize previewSize(180, 100);
    files->setIconSize(previewSize);
    files->setStyleSheet("QListWidget { background: #17171b; }"
        "QListWidget::item { border: 2px solid transparent; border-radius: 8px; }"
        "QListWidget::item:selected { border-color: palette(highlight); background: #1c1c20; }");
    auto* empty = new QLabel(dialog);
    empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(files, 1);
    layout->addWidget(empty, 1);

    auto* bottom = new QHBoxLayout;
    layout->addLayout(bottom);
    auto* shuffle = new QCheckBox(tr("Shuffle collection"), dialog);
    auto* status = new QLabel(dialog);
    bottom->addWidget(shuffle);
    bottom->addWidget(status);
    bottom->addStretch();
    auto* reset = new GalleryButton(tr("Reset"), dialog);
    auto* apply = new GalleryButton(tr("Use Wallpaper"), dialog);
    apply->setProperty("stellarPrimary", true);
    bottom->addWidget(reset);
    bottom->addWidget(apply);
    shuffle->setVisible(!instance.isEmpty());
    reset->setVisible(!instance.isEmpty());
    if (instance.isEmpty())
        apply->setText(tr("Open Image"));
    for (auto* button : {download, upload, reset, apply})
        button->setAutoDefault(false);

    const auto activeShuffle = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap().value(instance).toString();
    const auto currentCover = APPLICATION->settings()->get("StellarWallpaperCovers").toMap().value(instance).toString();
    auto activeCollection = activeShuffle.isEmpty() ? QFileInfo(currentCover).dir().dirName() : activeShuffle;
    if (currentCover.isEmpty() && activeShuffle.isEmpty()) {
        if (const auto* current = APPLICATION->instances()->getInstanceById(instance)) {
            const auto version = versionOf(current).minecraft;
            for (const auto& collection : std::as_const(m_collections)) {
                if (collection.versions.contains(version)) {
                    activeCollection = collection.id;
                    break;
                }
            }
        }
    }
    collections->setCurrentIndex(instance.isEmpty() ? collections->findData("animated") : qMax(0, collections->findData(activeCollection)));
    shuffle->setChecked(!activeShuffle.isEmpty());
    const auto setCover = [this, instance](const QString& path, const QString& collection) {
        auto covers = APPLICATION->settings()->get("StellarWallpaperCovers").toMap();
        auto shuffled = APPLICATION->settings()->get("StellarWallpaperShuffle").toMap();
        if (path.isEmpty())
            covers.remove(instance);
        else
            covers.insert(instance, path);
        if (collection.isEmpty())
            shuffled.remove(instance);
        else
            shuffled.insert(instance, collection);
        APPLICATION->settings()->set("StellarWallpaperCovers", covers);
        APPLICATION->settings()->set("StellarWallpaperShuffle", shuffled);
        m_detailWallpaper.clear();
        updateArtwork();
    };

    const auto previewCache = std::make_shared<QCache<QString, QPixmap>>(12 * 1024);
    const auto refresh = [this, dialog, collections, files, empty, download, apply, shuffle, currentCover, previewCache, previewSize] {
        const auto& collection = m_collections.at(collections->currentIndex());
        const auto paths = images(collection);
        const bool personal = collection.id == "animated";
        const bool loading = m_fetching.contains(collection.id);
        download->setVisible(!personal && paths.isEmpty());
        download->setEnabled(!loading);
        files->setVisible(!paths.isEmpty());
        empty->setVisible(paths.isEmpty());
        if (paths.isEmpty())
            empty->setText(loading ? tr("Downloading wallpapers…") : (personal ? tr("Upload a wallpaper to start your library.")
                                                                              : tr("Download this wallpaper collection.")));
        shuffle->setEnabled(!paths.isEmpty());
        if (files->property("displayedPaths").toStringList() == paths) {
            apply->setEnabled(files->currentItem());
            return;
        }
        files->setProperty("displayedPaths", paths);
        files->clear();
        QPixmap placeholder(previewSize);
        placeholder.fill(QColor("#26252b"));
        auto thumbnails = QStringList{};
        const qreal ratio = files->devicePixelRatioF();
        for (const auto& path : paths) {
            auto* item = new QListWidgetItem(files);
            item->setData(Qt::UserRole, path);
            const auto label = QFileInfo(path).completeBaseName();
            item->setData(Qt::AccessibleTextRole, label);
            item->setToolTip(label);
            if (auto* cached = previewCache->object(path)) {
                item->setIcon(*cached);
            } else {
                item->setIcon(placeholder);
                thumbnails.append(path);
            }
            if (path == currentCover)
                files->setCurrentItem(item);
        }
        if (!files->currentItem() && !paths.isEmpty())
            files->setCurrentRow(0);
        apply->setEnabled(files->currentItem());
        if (thumbnails.isEmpty())
            return;
        QtConcurrent::run(wallpaperPool(), [thumbnails, size = previewSize * ratio] {
            QList<QPair<QString, QImage>> images;
            for (const auto& path : thumbnails)
                images.append({path, readWallpaper(path, size, true)});
            return images;
        }).then(dialog, [files, previewCache, ratio](const QList<QPair<QString, QImage>>& images) {
            for (const auto& [path, image] : images) {
                if (image.isNull())
                    continue;
                auto pixmap = QPixmap::fromImage(image);
                pixmap.setDevicePixelRatio(ratio);
                previewCache->insert(path, new QPixmap(pixmap), pixmapKiB(image));
                for (int row = 0; row < files->count(); ++row) {
                    auto* item = files->item(row);
                    if (item->data(Qt::UserRole).toString() == path) {
                        item->setIcon(pixmap);
                        break;
                    }
                }
            }
        });
    };

    connect(collections, &QComboBox::currentIndexChanged, dialog, refresh);
    connect(files, &QListWidget::currentItemChanged, apply, [apply](const QListWidgetItem* item) { apply->setEnabled(item); });
    connect(shuffle, &QCheckBox::toggled, apply, [apply](bool on) { apply->setText(on ? tr("Use Shuffle") : tr("Use Wallpaper")); });
    if (shuffle->isChecked())
        apply->setText(tr("Use Shuffle"));
    const auto applyCover = [dialog, files, collections, shuffle, instance, setCover] {
        const auto* item = files->currentItem();
        if (!item)
            return;
        const auto path = item->data(Qt::UserRole).toString();
        if (instance.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
        setCover(path, shuffle->isChecked() ? collections->currentData().toString() : QString());
        dialog->close();
    };
    connect(apply, &QPushButton::clicked, dialog, applyCover);
    connect(files, &QListWidget::itemActivated, dialog, applyCover);
    connect(reset, &QPushButton::clicked, dialog, [dialog, setCover] {
        setCover({}, {});
        dialog->close();
    });
    connect(download, &QPushButton::clicked, dialog, [this, collections, refresh] {
        auto collection = m_collections.at(collections->currentIndex());
        if (official(collection.archive))
            collection.fallback = QUrl();
        m_attempted.remove(collection.id);
        fetchArtwork(collection);
        refresh();
    });
    connect(upload, &QPushButton::clicked, dialog, [this, dialog, files, collections, status, upload, refresh] {
        auto* picker = new QFileDialog(dialog, tr("Upload Wallpaper"));
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setFileMode(QFileDialog::ExistingFiles);
        picker->setNameFilters({tr("Images (*.png *.jpg *.jpeg *.webp *.gif)"), tr("All Files (*)")});
        const auto folder = QDir(m_wallpapers).filePath("animated");
        connect(picker, &QFileDialog::filesSelected, dialog, [dialog, files, collections, status, upload, folder, refresh](const QStringList& paths) {
            upload->setEnabled(false);
            status->setText(tr("Importing…"));
            QtConcurrent::run(wallpaperPool(), [paths, folder] {
                QStringList imported;
                if (!QDir().mkpath(folder))
                    return imported;
                for (const auto& path : paths) {
                    const QFileInfo file(path);
                    QImageReader reader(path);
                    const auto format = reader.format();
                    const auto size = reader.size();
                    if (!QList<QByteArray>{"png", "jpeg", "webp", "gif"}.contains(format) || !size.isValid()
                        || qint64(size.width()) * size.height() > 48'000'000 || file.size() > 64 * 1024 * 1024)
                        continue;
                    auto name = file.completeBaseName() + "." + QString::fromLatin1(format);
                    auto dest = QDir(folder).filePath(name);
                    if (QFileInfo::exists(dest)) {
                        name.prepend(QUuid::createUuid().toString(QUuid::Id128).left(8) + "-");
                        dest = QDir(folder).filePath(name);
                    }
                    if (QFile::copy(path, dest))
                        imported.append(dest);
                }
                return imported;
            }).then(dialog, [files, collections, status, upload, count = paths.size(), refresh](const QStringList& imported) {
                upload->setEnabled(true);
                status->setText(imported.size() == count ? QString() : tr("Imported %1 of %2.").arg(imported.size()).arg(count));
                if (imported.isEmpty())
                    return;
                collections->setCurrentIndex(collections->findData("animated"));
                refresh();
                for (int row = 0; row < files->count(); ++row) {
                    if (files->item(row)->data(Qt::UserRole).toString() == imported.first()) {
                        files->setCurrentRow(row);
                        break;
                    }
                }
            });
        });
        picker->open();
    });
    auto* timer = new QTimer(dialog);
    connect(timer, &QTimer::timeout, dialog, refresh);
    timer->start(850);
    refresh();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void StellarGallery::updateLaunch()
{
    auto* instances = APPLICATION->instances();
    for (auto entry = m_launches.begin(); entry != m_launches.end();) {
        auto* instance = instances->getInstanceById(entry.key());
        const auto state = instance ? instance->property("stellarLaunchState").toString() : QString();
        if (!instance || !instance->launchController() || state == "ready") {
            if (instance)
                instance->removeEventFilter(this);
            entry = m_launches.erase(entry);
        } else {
            if (state == "java" && !entry.value().isValid())
                entry.value().start();
            ++entry;
        }
    }
    if (m_launches.isEmpty())
        m_launchTimer->stop();
    auto* shown = instances->getInstanceById(m_detailId);
    if (!m_detail || !shown)
        return;
    auto* detail = static_cast<InstanceDetail*>(m_detail);
    const auto pending = m_launches.constFind(m_detailId);
    if (pending == m_launches.cend()) {
        const bool running = shown->isRunning();
        const bool checking = m_preflight.contains(m_detailId);
        detail->setLaunchState(running ? tr("Running · Stop…") : checking ? tr("Checking mods…") : tr("Play"),
                               running || checking, false, -1, running);
        return;
    }
    const auto state = shown->property("stellarLaunchState").toString();
    const bool showProgress = APPLICATION->settings()->get("StellarLaunchAnimation").toString() != "off";
    const bool animate = showProgress && motionEnabled();
    if (state.startsWith("download ")) {
        const int progress = qBound(0, qRound(state.sliced(9).toDouble() * 100), 100);
        detail->setLaunchState(tr("Downloading…"), true, animate, showProgress && progress > 0 ? progress : -1);
    } else {
        const bool java = state == "java";
        const bool slow = java && pending.value().hasExpired(90000);
        const bool cover = wallpaperEffect(APPLICATION->settings()->get("StellarLaunchAnimation").toString());
        detail->setLaunchState(slow ? tr("Minecraft is still starting…") : java ? tr("Starting Minecraft…") : tr("Preparing…"),
                               true, animate && !slow, java && cover && !slow ? 96 : -1);
    }
}
