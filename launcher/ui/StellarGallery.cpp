// SPDX-License-Identifier: GPL-3.0-only
#include "StellarGallery.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QDirListing>
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
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
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
#include "archive/ArchiveReader.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
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

QString versionOf(MinecraftInstance* instance)
{
    QFile file(QDir(instance->instanceRoot()).filePath("mmc-pack.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const auto components = QJsonDocument::fromJson(file.readAll()).object().value("components").toArray();
        for (const auto& component : components) {
            const auto item = component.toObject();
            if (item.value("uid").toString() == "net.minecraft")
                return item.value("version").toString();
        }
    }
    return {};
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

class GalleryTile final : public QAbstractButton {
public:
    GalleryTile(const QString& title, QString version, QPixmap image, QWidget* parent)
        : QAbstractButton(parent), m_version(std::move(version)), m_image(std::move(image))
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
    void setArtwork(const QPixmap& image) { m_image = image; update(); }
    std::function<void()> open;
    std::function<void()> options;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && open)
            open();
    }

    void contextMenuEvent(QContextMenuEvent*) override { if (options) options(); }

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

        if (m_version.isEmpty() && m_image.isNull()) {
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
        if (underMouse())
            painter.fillRect(bounds, QColor(236, 235, 240, 12));
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
    QVariantAnimation m_focus;
    double m_border = 0;
};

class LaunchCover final : public QWidget {
public:
    LaunchCover(const QPixmap& artwork, QString mode, QWidget* parent)
        : QWidget(parent), m_image(artwork), m_mode(std::move(mode))
    {
        m_gray = QPixmap::fromImage(artwork.toImage().convertToFormat(QImage::Format_Grayscale8));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAccessibleName(tr("Minecraft is loading"));
        m_progress.setDuration(220);
        m_progress.setEasingCurve(QEasingCurve::OutQuart);
        connect(&m_progress, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_fill = value.toDouble();
            update();
        });
    }

    void advance(double amount)
    {
        if (amount <= m_progress.endValue().toDouble())
            return;
        m_progress.stop();
        m_progress.setStartValue(m_fill);
        m_progress.setEndValue(amount);
        m_progress.start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(), canvas);
        drawArtwork(painter, rect(), m_gray);
        if (m_mode == "fade") {
            painter.setOpacity(m_fill);
        } else if (m_fill < 1.0) {
            QPainterPath clip;
            const double amplitude = m_mode == "wave" ? std::min({1.0, m_fill * 10, (1 - m_fill) * 10}) * 8 : 0;
            clip.moveTo(0, height());
            for (int step = 0; step <= 48; ++step) {
                const double fraction = step / 48.0;
                clip.lineTo(width() * fraction, height() * (1 - m_fill) + std::sin(fraction * 9.42 + m_fill * 12) * amplitude);
            }
            clip.lineTo(width(), height());
            clip.closeSubpath();
            painter.setClipPath(clip);
        }
        drawArtwork(painter, rect(), m_image);
    }

private:
    QPixmap m_image;
    QPixmap m_gray;
    QString m_mode;
    QVariantAnimation m_progress;
    double m_fill = 0.0;
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
      m_launchTimer(new QTimer(this))
{
    setFixedSize(windowWidth, windowHeight);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName("stellarGallery");
    setStyleSheet("QWidget#stellarGallery { background: #141416; }"
                  "QFrame#stellarPanel { background: #1c1c20; border: 1px solid #37363d; border-radius: 14px; }"
                  "QLabel#stellarHeading { font-size: 17px; font-weight: 600; }"
                  "QPushButton[stellarPrimary=true] { background: #e9e7f1; color: #18171c; }");
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
        dismissPanel();
        if (auto* cover = std::exchange(m_launchCover, nullptr))
            cover->deleteLater();
        m_launchTimer->stop();
    });
    m_launchTimer->setInterval(160);
    connect(m_launchTimer, &QTimer::timeout, this, &StellarGallery::updateLaunch);

    m_wallpapers = QDir(APPLICATION->dataRoot()).filePath("wallpapers");
    const auto previous = QDir::home().filePath("Library/Application Support/Stellar Gallery/Wallpapers");
    if (QDir(previous).exists() && !QDir(m_wallpapers).exists())
        m_wallpapers = previous;
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

    const int oldOffset = m_scroll->horizontalScrollBar()->value();
    m_slide->stop();
    for (auto* tile : std::as_const(m_tiles)) {
        m_row->removeWidget(tile);
        tile->hide();
        tile->deleteLater();
    }
    m_tiles.clear();
    m_ids.clear();

    const int columns = std::clamp(int(instances.size()), 1, 3);
    const int width = (windowWidth - gap * (columns + 1)) / columns;
    for (int i = 0; i <= instances.size(); ++i) {
        const auto* instance = i < instances.size() ? instances[i] : nullptr;
        const auto id = instance ? instance->id() : QString();
        const auto title = instance ? instance->name() : tr("Create New Instance");
        auto version = instance ? versionOf(instances[i]) : QString();
        auto pixmap = instance ? artwork(id, version) : QPixmap();
        if (instance && version.isEmpty())
            version = tr("Minecraft");
        auto* tile = new GalleryTile(title, version, pixmap, m_cards);
        tile->setFixedSize(width, windowHeight - gap * 2);
        m_row->addWidget(tile);
        m_tiles.append(tile);
        connect(tile, &QAbstractButton::clicked, this, [this, i] {
            dismissPanel();
            selectIndex(i, true);
            if (i == m_ids.size())
                activate(i);
        });
        if (instance) {
            m_ids.append(id);
            tile->open = [this, i] { activate(i); };
        }
        tile->options = [this, i] { showActions(i); };
        tile->installEventFilter(this);
    }
    m_cards->setFixedSize(gap + int(m_tiles.size()) * (width + gap), windowHeight);
    m_row->activate();
    m_scroll->horizontalScrollBar()->setValue(oldOffset);
    const int selectedIndex = m_ids.indexOf(m_selected);
    selectIndex(selectedIndex >= 0 ? selectedIndex : 0, false);
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
        static_cast<GalleryTile*>(m_tiles[i])->setSelected(i == m_index, animate && APPLICATION->settings()->get("StellarMotion").toBool());
    if (m_index < m_ids.size() && m_selected != m_ids[m_index]) {
        m_selected = m_ids[m_index];
        APPLICATION->settings()->set("SelectedInstance", m_selected);
        m_actions.select(m_selected);
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
    if (animate && target != offset && APPLICATION->settings()->get("StellarMotion").toBool()) {
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
    if (index == m_ids.size()) {
        showCreator();
        return;
    }
    auto* instance = APPLICATION->instances()->getInstanceById(m_ids.value(index));
    if (!instance)
        return;
    if (instance->canLaunch()) {
        const auto mode = APPLICATION->settings()->get("StellarLaunchAnimation").toString();
        if (mode != "off" && APPLICATION->settings()->get("StellarMotion").toBool() && !m_launchCover) {
            m_launching = instance->id();
            m_launchStarted = QDateTime::currentDateTime();
            m_launchCover = new LaunchCover(artwork(instance->id(), versionOf(instance)), mode, this);
            m_launchCover->setGeometry(rect());
            m_launchCover->show();
            m_launchTimer->start();
        }
        instance->setProperty("stellarLaunchState", "preparing");
        instance->setProperty("stellarGalleryFullscreen", APPLICATION->settings()->get("StellarFillScreen"));
    }
    if (!APPLICATION->launch(instance) && m_launchCover)
        updateLaunch();
}

void StellarGallery::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_End:
        dismissPanel();
        selectIndex(event->key() == Qt::Key_Home ? 0 : event->key() == Qt::Key_End ? m_tiles.size() - 1 :
                    m_index + (event->key() == Qt::Key_Left ? -1 : 1), true);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!event->isAutoRepeat())
            activate(m_index);
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
    if (std::exchange(m_opened, true) || !APPLICATION->settings()->get("StellarMotion").toBool() ||
        !APPLICATION->settings()->get("StellarOpeningAnimation").toBool())
        return;
    auto* logo = new QLabel(this);
    logo->setGeometry(rect());
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet("background: #141416; border-radius: 16px;");
    logo->setPixmap(APPLICATION->logo().pixmap(92, 92));
    logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    logo->show();
    auto* opacity = new QGraphicsOpacityEffect(logo);
    logo->setGraphicsEffect(opacity);
    opacity->setOpacity(1.0);
    auto* animation = new QPropertyAnimation(opacity, "opacity", logo);
    animation->setDuration(260);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::OutQuart);
    connect(animation, &QPropertyAnimation::finished, logo, &QObject::deleteLater);
    QTimer::singleShot(90, logo, [animation] { animation->start(); });
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
    auto* close = new QPushButton(QStringLiteral("×"), m_panel);
    close->setAccessibleName(tr("Close"));
    close->setFixedWidth(34);
    connect(close, &QPushButton::clicked, this, &StellarGallery::dismissPanel);
    header->addWidget(close);
    layout->addLayout(header);
    return m_panel;
}

void StellarGallery::placePanel()
{
    m_panel->adjustSize();
    m_panel->move((width() - m_panel->width()) / 2, (height() - m_panel->height()) / 2);
    m_panel->show();
    m_panel->raise();
}

void StellarGallery::dismissPanel()
{
    if (auto* panel = std::exchange(m_panel, nullptr)) {
        panel->hide();
        panel->deleteLater();
        setFocus();
    }
}

void StellarGallery::showActions(int index)
{
    selectIndex(index, false);
    const auto id = m_ids.value(index);
    auto* instance = APPLICATION->instances()->getInstanceById(id);
    auto* panel = makePanel(instance ? instance->name() : tr("Library"));
    auto* actions = new QGridLayout;
    actions->setSpacing(7);
    int count = 0;
    const auto add = [this, panel, actions, &count](const QString& title, const QString& icon, std::function<void()> action) {
        const QIcon artwork(QStringLiteral(":/icons/flat_white/scalable/%1.svg").arg(icon));
        auto* button = new QPushButton(artwork, QString(), panel);
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
            if (const auto index = m_ids.indexOf(id); index >= 0)
                activate(index);
        });
        add(tr("Edit Instance"), "instance-settings", [id] {
            if (auto* current = APPLICATION->instances()->getInstanceById(id))
                APPLICATION->showInstanceWindow(current, "version");
        });
        add(tr("Mods, Packs & Shaders"), "loadermods", [this, id] { showContent(id); });
        add(tr("Minecraft Accounts"), "accounts", m_actions.accounts);
        add(tr("Change Skin"), "minecraft", m_actions.skins);
        add(tr("Show Instance Folder"), "viewfolder", [id] {
            if (const auto* current = APPLICATION->instances()->getInstanceById(id))
                QDesktopServices::openUrl(QUrl::fromLocalFile(current->instanceRoot()));
        });
        add(tr("Change Wallpaper"), "screenshots", [this, id] { showWallpapers(id); });
    } else {
        add(tr("Create New Instance"), "new", [this] { showCreator(); });
        add(tr("Import Instance"), "export", m_actions.importPack);
        add(tr("Copy Existing Instance"), "copy", [this] { copyInstance(); });
        add(tr("Minecraft Accounts"), "accounts", m_actions.accounts);
        add(tr("Wallpapers"), "screenshots", [this] { showWallpapers({}); });
    }
    add(tr("Settings"), "settings", [this] { showPreferences(); });
    panel->layout()->addItem(actions);
    placePanel();
}

void StellarGallery::showContent(const QString& id)
{
    auto* panel = makePanel(tr("Mods, Packs & Shaders"));
    panel->setFixedWidth(300);
    const auto add = [this, panel, id](const QString& title, const QString& page) {
        auto* button = new QPushButton(title, panel);
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
    auto* look = new QPushButton(tr("Install Stellar Look"), panel);
    panel->layout()->addWidget(look);
    connect(look, &QPushButton::clicked, panel, [this, id] {
        dismissPanel();
        m_actions.installLook(id);
    });
    placePanel();
}

void StellarGallery::copyInstance()
{
    const auto source = QFileDialog::getExistingDirectory(window(), tr("Copy Existing Instance"));
    if (source.isEmpty())
        return;
    const auto root = APPLICATION->instances()->primaryDir();
    const auto id = QFileInfo(source).fileName();
    const auto target = QDir(root).filePath(id);
    QtConcurrent::run([source, root, target] {
        const QDir folder(source);
        if (!folder.exists("instance.cfg") || !folder.exists("mmc-pack.json"))
            return tr("Choose a Minecraft instance folder containing instance.cfg and mmc-pack.json.");
        if (QFileInfo::exists(target))
            return tr("An instance with that folder name already exists.");
        using Flags = QDirListing::IteratorFlag;
        for (const auto& entry : QDirListing(source, Flags::Recursive | Flags::IncludeHidden)) {
            if (entry.isSymLink())
                return tr("This instance contains links to files. Export it as an archive before importing it.");
        }
        if (!QDir().mkpath(root))
            return tr("Couldn’t open the Stellar instance library.");
        QTemporaryDir staging(QDir(root).filePath(".stellar-copy-XXXXXX"));
        if (!staging.isValid())
            return tr("Couldn’t prepare the new instance.");
        const auto payload = QDir(staging.path()).filePath("instance");
        if (!FS::copy(source, payload).copyDirectories(true).followSymlinks(false)() ||
            !QDir().rename(payload, target))
            return tr("Couldn’t copy the instance into the Stellar library.");
        return QString();
    }).then(this, [this, id](const QString& error) {
        if (!error.isEmpty()) {
            QMessageBox::warning(window(), tr("Couldn’t copy instance"), error);
            return;
        }
        APPLICATION->instances()->loadList();
        selectInstance(id);
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
    auto* controls = new QHBoxLayout;
    auto* advanced = new QPushButton(tr("More Options…"), panel);
    auto* submit = new QPushButton(tr("Create"), panel);
    submit->setProperty("stellarPrimary", true);
    submit->setEnabled(false);
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
    connect(advanced, &QPushButton::clicked, panel, [this] { dismissPanel(); m_actions.create(); });
    connect(name, &QLineEdit::returnPressed, submit, &QPushButton::click);
    connect(submit, &QPushButton::clicked, panel, [this, name, version, loader, java, fabric, fabricId] {
        auto game = java->getVersion(version->currentData().toString());
        if (!game)
            return;
        VanillaCreationTask* creation = loader->currentIndex() == 0 ? new VanillaCreationTask(game) :
                                        new VanillaCreationTask(game, fabricId, fabric->getRecommended());
        auto title = name->text().trimmed();
        if (title.isEmpty())
            title = tr("Minecraft %1").arg(game->descriptor());
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

void StellarGallery::showPreferences()
{
    auto* panel = makePanel(tr("Settings"));
    panel->setFixedWidth(352);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Launch animation"), panel));
    auto* animation = new QComboBox(panel);
    animation->setAccessibleName(tr("Launch animation"));
    animation->addItem(tr("Color Wave"), "wave");
    animation->addItem(tr("Color Reveal"), "wipe");
    animation->addItem(tr("Fade to Color"), "fade");
    animation->addItem(tr("Off"), "off");
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
    option(tr("Enable motion effects"), "StellarMotion");
    placePanel();
}

QStringList StellarGallery::images(const Collection& collection) const
{
    const QDir directory(QDir(m_wallpapers).filePath(collection.id));
    QStringList paths;
    const auto contents = directory.entryInfoList({ "*.jpg", "*.jpeg", "*.png", "*.webp" }, QDir::Files, QDir::Name);
    for (const auto& entry : contents)
        paths.append(entry.absoluteFilePath());
    return paths;
}

QPixmap StellarGallery::artwork(const QString& instance, const QString& version)
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
    if (!m_artwork.contains(file)) {
        QImageReader reader(file);
        const auto size = reader.size();
        if (size.isValid())
            reader.setScaledSize(size.scaled(1600, 1200, Qt::KeepAspectRatio));
        m_artwork.insert(file, QPixmap::fromImage(reader.read()));
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
        if (auto* instance = APPLICATION->instances()->getInstanceById(m_ids[i]))
            static_cast<GalleryTile*>(m_tiles[i])->setArtwork(artwork(m_ids[i], versionOf(instance)));
    }
}

void StellarGallery::showWallpapers(const QString& instance)
{
    auto* panel = makePanel(tr("Wallpapers"));
    panel->setFixedWidth(380);
    auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
    auto* collections = new QComboBox(panel);
    collections->setAccessibleName(tr("Wallpaper collection"));
    for (const auto& item : m_collections)
        collections->addItem(item.title);
    auto* files = new QListWidget(panel);
    files->setFixedHeight(168);
    files->setAccessibleName(tr("Available wallpapers"));
    auto* apply = new QPushButton(tr("Use Wallpaper"), panel);
    apply->setProperty("stellarPrimary", true);
    layout->addWidget(collections);
    layout->addWidget(files);
    auto* buttons = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("Fetch Collection"), panel);
    buttons->addWidget(refresh);
    buttons->addStretch();
    if (!instance.isEmpty()) {
        auto* reset = new QPushButton(tr("Reset"), panel);
        buttons->addWidget(reset);
        connect(reset, &QPushButton::clicked, panel, [this, instance] {
            auto covers = APPLICATION->settings()->get("StellarWallpaperCovers").toMap();
            covers.remove(instance);
            APPLICATION->settings()->set("StellarWallpaperCovers", covers);
            updateArtwork();
            dismissPanel();
        });
    }
    buttons->addWidget(apply);
    layout->addLayout(buttons);
    const auto populate = [this, collections, files, apply, refresh] {
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
        refresh->setEnabled(!m_fetching.contains(collection.id));
        apply->setEnabled(files->currentItem() != nullptr);
    };
    connect(collections, &QComboBox::currentIndexChanged, panel, [files, populate] { files->clear(); populate(); });
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
    connect(apply, &QPushButton::clicked, panel, [this, files, instance] {
        if (auto* item = files->currentItem()) {
            const auto path = item->data(Qt::UserRole).toString();
            if (instance.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            } else {
                auto covers = APPLICATION->settings()->get("StellarWallpaperCovers").toMap();
                covers.insert(instance, path);
                APPLICATION->settings()->set("StellarWallpaperCovers", covers);
                updateArtwork();
                dismissPanel();
            }
        }
    });
    auto* refreshTimer = new QTimer(panel);
    connect(refreshTimer, &QTimer::timeout, panel, populate);
    refreshTimer->start(350);
    populate();
    placePanel();
}

void StellarGallery::updateLaunch()
{
    if (!m_launchCover) {
        m_launchTimer->stop();
        return;
    }
    auto* cover = static_cast<LaunchCover*>(m_launchCover);
    auto* instance = APPLICATION->instances()->getInstanceById(m_launching);
    if (instance && instance->launchController() && m_launchStarted.secsTo(QDateTime::currentDateTime()) < 900) {
        const auto state = instance->property("stellarLaunchState").toString();
        const auto fields = state.split(' ');
        if (fields.value(0) == "download") {
            cover->advance(0.16 + 0.5 * fields.value(1).toDouble());
        } else if (fields.value(0) == "java") {
            cover->advance(0.74);
            QFile log(QDir(instance->gameRoot()).filePath("logs/latest.log"));
            const auto modified = QFileInfo(log).lastModified();
            if (modified >= m_launchStarted && log.open(QIODevice::ReadOnly)) {
                log.seek(std::max<qint64>(0, log.size() - 96 * 1024));
                const auto output = log.readAll();
                if (output.contains("minecraft:textures/atlas/gui.png") ||
                    (output.contains("Reloading ResourceManager") && output.contains("OpenAL initialized"))) {
                    cover->advance(0.91);
                    if (modified.msecsTo(QDateTime::currentDateTime()) >= 700) {
                        cover->advance(1.0);
                        m_launchTimer->stop();
                        QTimer::singleShot(240, this, [this, cover] {
                            if (m_launchCover == cover) {
                                cover->deleteLater();
                                m_launchCover = nullptr;
                            }
                        });
                    }
                }
            }
        } else {
            cover->advance(0.08);
        }
        return;
    }
    cover->deleteLater();
    m_launchCover = nullptr;
    m_launchTimer->stop();
}
