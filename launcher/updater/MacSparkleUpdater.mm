// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Kenneth Chew <kenneth.c0@protonmail.com>
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

#include "MacSparkleUpdater.h"
#include "Application.h"
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

#include <Sparkle/SPUUpdatePermissionRequest.h>
#include <Sparkle/SPUUpdater.h>
#include <Sparkle/SPUUpdaterDelegate.h>
#include <Sparkle/SPUUserDriver.h>
#include <Sparkle/SUAppcastItem.h>
#include <Sparkle/SUUpdatePermissionResponse.h>

namespace {
QString updateText(const char* text)
{
    return QCoreApplication::translate("StellarUpdater", text);
}

class UpdateDialog final : public QDialog {
public:
    explicit UpdateDialog(QWidget* parent) : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        setWindowTitle(updateText("Stellar Launcher Update"));
        setFixedWidth(368);
        setObjectName("stellarUpdate");
        setStyleSheet(
            "QDialog#stellarUpdate { background: #1c1c20; border: 1px solid #37363d; border-radius: 14px; }"
            "QLabel { background: transparent; }"
            "QLabel#updateTitle { font-size: 17px; font-weight: 600; }"
            "QLabel#updateDetail { color: #bcb9c5; }"
            "QPushButton#updatePrimary:enabled { background: palette(highlight); color: palette(highlighted-text); }"
            "QPushButton#updatePrimary:hover { border-color: palette(highlighted-text); }"
            "QProgressBar { border: 0; border-radius: 2px; background: #34323a; }"
            "QProgressBar::chunk { border-radius: 2px; background: palette(highlight); }");

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 17, 20, 17);
        layout->setSpacing(10);
        m_title = new QLabel(this);
        m_title->setObjectName("updateTitle");
        layout->addWidget(m_title);
        m_detail = new QLabel(this);
        m_detail->setObjectName("updateDetail");
        m_detail->setWordWrap(true);
        m_detail->setTextFormat(Qt::PlainText);
        layout->addWidget(m_detail);
        m_progress = new QProgressBar(this);
        m_progress->setFixedHeight(5);
        m_progress->setTextVisible(false);
        layout->addWidget(m_progress);

        auto* actions = new QHBoxLayout;
        actions->addStretch();
        m_secondary = new QPushButton(this);
        m_primary = new QPushButton(this);
        m_primary->setObjectName("updatePrimary");
        for (auto* button : {m_secondary, m_primary}) {
            button->setCursor(Qt::PointingHandCursor);
            button->setAutoDefault(false);
            actions->addWidget(button);
        }
        connect(m_secondary, &QPushButton::clicked, this, &UpdateDialog::reject);
        connect(m_primary, &QPushButton::clicked, this, [this] {
            auto action = std::move(m_confirm);
            if (action)
                action();
        });
        layout->addLayout(actions);
    }

    void present(const QString& title, const QString& detail, bool progress, const QString& primary = {},
                 std::function<void()> confirm = {}, const QString& secondary = {}, std::function<void()> cancel = {})
    {
        m_waitingToRestart = false;
        m_title->setText(title);
        m_detail->setText(detail);
        m_detail->setVisible(!detail.isEmpty());
        m_confirm = std::move(confirm);
        m_cancel = std::move(cancel);
        m_primary->setText(primary);
        m_primary->setVisible(!primary.isEmpty());
        m_primary->setEnabled(true);
        m_primary->setDefault(!primary.isEmpty());
        m_secondary->setText(secondary);
        m_secondary->setVisible(!secondary.isEmpty());
        m_progress->setAccessibleName(title);
        m_progress->setVisible(progress);
        setProgress(-1);
        adjustSize();
        QPainterPath outline;
        outline.addRoundedRect(rect(), 14, 14);
        setMask(QRegion(outline.toFillPolygon().toPolygon()));
        if (auto* anchor = parentWidget())
            move(anchor->frameGeometry().center() - rect().center());
        if (!isVisible())
            show();
        if (m_primary->isVisible())
            m_primary->setFocus(Qt::OtherFocusReason);
        else if (m_secondary->isVisible())
            m_secondary->setFocus(Qt::OtherFocusReason);
    }

    void setProgress(int percentage)
    {
        m_progress->setRange(0, percentage < 0 ? 0 : 100);
        if (percentage >= 0)
            m_progress->setValue(percentage);
    }

    void waitForRunningGames()
    {
        m_waitingToRestart = true;
        gameStateChanged(APPLICATION->updatesAreAllowed());
    }

    void gameStateChanged(bool allowed)
    {
        if (!m_waitingToRestart)
            return;
        m_primary->setEnabled(allowed);
        m_detail->setText(allowed ? updateText("Restart Stellar to finish installing.")
                                  : updateText("Quit Minecraft to finish installing."));
    }

    void reject() override
    {
        auto action = std::move(m_cancel);
        if (action)
            action();
    }

private:
    QLabel* m_title;
    QLabel* m_detail;
    QProgressBar* m_progress;
    QPushButton* m_primary;
    QPushButton* m_secondary;
    std::function<void()> m_confirm;
    std::function<void()> m_cancel;
    bool m_waitingToRestart = false;
};
}

@interface StellarUpdateDriver : NSObject <SPUUserDriver> {
    QPointer<QWidget> _parent;
    QPointer<UpdateDialog> _dialog;
    uint64_t _expectedBytes;
    uint64_t _receivedBytes;
}
- (instancetype)initWithParent:(QWidget*)parent;
- (UpdateDialog*)dialog;
@end

@implementation StellarUpdateDriver

- (instancetype)initWithParent:(QWidget*)parent
{
    self = [super init];
    if (self)
        _parent = parent;
    return self;
}

- (UpdateDialog*)dialog
{
    if (!_dialog) {
        _dialog = new UpdateDialog(_parent);
        QObject::connect(APPLICATION, &Application::updateAllowedChanged, _dialog, &UpdateDialog::gameStateChanged);
    }
    return _dialog;
}

- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request reply:(void (^)(SUUpdatePermissionResponse*))reply
{
    reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:YES
                                                 automaticUpdateDownloading:@NO
                                                           sendSystemProfile:NO]);
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancel
{
    [self dialog]->present(updateText("Checking for updates"), {}, true, {}, {}, updateText("Cancel"), [cancel] { cancel(); });
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item state:(SPUUserUpdateState*)state reply:(void (^)(SPUUserUpdateChoice))reply
{
    auto version = QString::fromNSString(item.displayVersionString);
    const auto detail = updateText("Stellar Launcher %1 is available.").arg(version);
    const auto later = [reply] { reply(SPUUserUpdateChoiceDismiss); };
    if (item.informationOnlyUpdate) {
        const auto url = QUrl(QString::fromNSString(item.infoURL.absoluteString));
        [self dialog]->present(updateText("Update available"), detail, false, updateText("View release"), [url, later] {
            QDesktopServices::openUrl(url);
            later();
        }, updateText("Later"), later);
        return;
    }

    if (state.stage == SPUUserUpdateStageInstalling) {
        [self showReadyToInstallAndRelaunch:reply];
        return;
    }
    [self dialog]->present(updateText("Update available"), detail, false, updateText("Download"),
                           [reply] { reply(SPUUserUpdateChoiceInstall); }, updateText("Later"), later);
}

- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData {}
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {}

- (void)showUpdateNotFoundWithError:(NSError*)error acknowledgement:(void (^)(void))acknowledge
{
    [self dialog]->present(updateText("No update available"), QString::fromNSString(error.localizedDescription), false,
                           updateText("Done"), [acknowledge] { acknowledge(); }, {}, [acknowledge] { acknowledge(); });
}

- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))acknowledge
{
    [self dialog]->present(updateText("Update couldn’t finish"), QString::fromNSString(error.localizedDescription), false,
                           updateText("Done"), [acknowledge] { acknowledge(); }, {}, [acknowledge] { acknowledge(); });
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancel
{
    _expectedBytes = 0;
    _receivedBytes = 0;
    [self dialog]->present(updateText("Downloading update"), updateText("You can cancel before the download finishes."),
                           true, {}, {}, updateText("Cancel"), [cancel] { cancel(); });
}

- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length
{
    _expectedBytes = length;
}

- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length
{
    _receivedBytes += length;
    if (_dialog && _expectedBytes)
        _dialog->setProgress(int(qMin(1.0, double(_receivedBytes) / double(_expectedBytes)) * 100));
}

- (void)showDownloadDidStartExtractingUpdate
{
    [self dialog]->present(updateText("Preparing update"), updateText("Verifying and unpacking the download."), true);
}

- (void)showExtractionReceivedProgress:(double)progress
{
    if (_dialog)
        _dialog->setProgress(qRound(qBound(0.0, progress, 1.0) * 100));
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply
{
    auto* dialog = [self dialog];
    dialog->present(updateText("Ready to install"), updateText("Restart Stellar to finish installing."), false,
                    updateText("Update and Restart"), [reply] {
        APPLICATION->updateIsRunning(true);
        reply(SPUUserUpdateChoiceInstall);
    }, updateText("Cancel"), [reply] { reply(SPUUserUpdateChoiceSkip); });
    dialog->waitForRunningGames();
}

- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated retryTerminatingApplication:(void (^)(void))retry
{
    if (terminated || !_dialog)
        return;

    _dialog->present(updateText("Installing update"), updateText("Stellar will reopen when the update is ready."), true);
    // Sparkle's system quit request can be blocked by the Qt modal window.
    // Close from Qt after painting the installation state and after Sparkle has armed its installer.
    const QPointer<UpdateDialog> dialog = _dialog;
    QTimer::singleShot(350, APPLICATION, [dialog] {
        if (!dialog || !APPLICATION->updatesAreAllowed())
            return;
        dialog->hide();
        QApplication::closeAllWindows();
    });
}

- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))acknowledge
{
    acknowledge();
}

- (void)dismissUpdateInstallation
{
    APPLICATION->updateIsRunning(false);
    if (_dialog) {
        _dialog->hide();
        _dialog->deleteLater();
        _dialog.clear();
    }
}

- (void)showUpdateInFocus
{
    if (_dialog) {
        _dialog->raise();
        _dialog->activateWindow();
    }
}

@end

@interface UpdaterObserver : NSObject

@property(nonatomic, readonly) SPUUpdater* updater;

/// A callback to run when the state of `canCheckForUpdates` for the `updater` changes.
@property(nonatomic, copy) void (^callback)(bool);

- (id)initWithUpdater:(SPUUpdater*)updater;

@end

@implementation UpdaterObserver

- (id)initWithUpdater:(SPUUpdater*)updater {
    self = [super init];
    _updater = updater;
    [self addObserver:self forKeyPath:@"updater.canCheckForUpdates" options:NSKeyValueObservingOptionNew context:nil];

    return self;
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
    if (self.callback && [keyPath isEqualToString:@"updater.canCheckForUpdates"]) {
        bool canCheck = [change[NSKeyValueChangeNewKey] boolValue];
        self.callback(canCheck);
    }
}

@end

@interface UpdaterDelegate : NSObject <SPUUpdaterDelegate>

@property(nonatomic, copy) NSSet<NSString*>* allowedChannels;

@end

@implementation UpdaterDelegate

- (BOOL)updater:(SPUUpdater*)updater mayPerformUpdateCheck:(SPUUpdateCheck)check error:(NSError**)error {
    if (APPLICATION->updatesAreAllowed()) {
        return YES;
    }
    if (error) {
        *error = [NSError errorWithDomain:@"app.archivelauncher.update"
                                    code:1
                                userInfo:@{NSLocalizedDescriptionKey: @"Close Minecraft before checking for an update."}];
    }
    return NO;
}

- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
    return _allowedChannels ? _allowedChannels : [NSSet set];
}

@end

class MacSparkleUpdater::Private {
   public:
    SPUUpdater* updater;
    StellarUpdateDriver* userDriver;
    UpdaterObserver* updaterObserver;
    UpdaterDelegate* updaterDelegate;
};

MacSparkleUpdater::MacSparkleUpdater(QWidget* parent) : priv(new Private)
{
    priv->updaterDelegate = [[UpdaterDelegate alloc] init];
    priv->userDriver = [[StellarUpdateDriver alloc] initWithParent:parent];
    priv->updater = [[SPUUpdater alloc] initWithHostBundle:NSBundle.mainBundle
                                      applicationBundle:NSBundle.mainBundle
                                              userDriver:priv->userDriver
                                                delegate:priv->updaterDelegate];
    priv->updaterObserver = [[UpdaterObserver alloc] initWithUpdater:priv->updater];
    priv->updaterObserver.callback = ^(bool canCheck) {
        emit canCheckForUpdatesChanged(canCheck);
    };
}

MacSparkleUpdater::~MacSparkleUpdater()
{
    [priv->updaterObserver removeObserver:priv->updaterObserver forKeyPath:@"updater.canCheckForUpdates"];
    delete priv;
}

void MacSparkleUpdater::checkForUpdates()
{
    [priv->updater checkForUpdates];
}

void MacSparkleUpdater::checkOnStartup()
{
    NSError* error = nil;
    if (![priv->updater startUpdater:&error]) {
        qWarning() << "Could not start the Stellar updater:" << QString::fromNSString(error.localizedDescription);
        return;
    }

    if (priv->updater.canCheckForUpdates && APPLICATION->updatesAreAllowed()) {
        qInfo() << "Checking the Stellar update feed on launch.";
        [priv->updater checkForUpdatesInBackground];
    }
}

bool MacSparkleUpdater::getAutomaticallyChecksForUpdates() {
    return priv->updater.automaticallyChecksForUpdates;
}

double MacSparkleUpdater::getUpdateCheckInterval() {
    return priv->updater.updateCheckInterval;
}

QSet<QString> MacSparkleUpdater::getAllowedChannels() {
    // Convert NSSet<NSString> -> QSet<QString>
    __block QSet<QString> channels;
    [priv->updaterDelegate.allowedChannels enumerateObjectsUsingBlock:^(NSString* channel, BOOL* stop) {
      channels.insert(QString::fromNSString(channel));
    }];
    return channels;
}

bool MacSparkleUpdater::getBetaAllowed() {
    return getAllowedChannels().contains("beta");
}

void MacSparkleUpdater::setAutomaticallyChecksForUpdates(bool check) {
    priv->updater.automaticallyChecksForUpdates = check ? YES : NO;  // make clang-tidy happy
}

void MacSparkleUpdater::setUpdateCheckInterval(double seconds) {
    priv->updater.updateCheckInterval = seconds;
}

void MacSparkleUpdater::clearAllowedChannels() {
    priv->updaterDelegate.allowedChannels = [NSSet set];
}

void MacSparkleUpdater::setAllowedChannel(const QString& channel) {
    if (channel.isEmpty()) {
        clearAllowedChannels();
        return;
    }

    NSSet<NSString*>* nsChannels = [NSSet setWithObject:channel.toNSString()];
    priv->updaterDelegate.allowedChannels = nsChannels;
}

void MacSparkleUpdater::setAllowedChannels(const QSet<QString>& channels) {
    if (channels.isEmpty()) {
        clearAllowedChannels();
        return;
    }

    QString channelsConfig = "";
    // Convert QSet<QString> -> NSSet<NSString>
    NSMutableSet<NSString*>* nsChannels = [NSMutableSet setWithCapacity:channels.count()];
    for (const QString& channel : channels) {
        [nsChannels addObject:channel.toNSString()];
        channelsConfig += channel + " ";
    }

    priv->updaterDelegate.allowedChannels = nsChannels;
}

void MacSparkleUpdater::setBetaAllowed(bool allowed) {
    if (allowed) {
        setAllowedChannel("beta");
    } else {
        clearAllowedChannels();
    }
}
