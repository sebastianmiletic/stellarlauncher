// SPDX-License-Identifier: GPL-3.0-only

#import <AppKit/AppKit.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

void setStellarHorizonGameBranding(const QString& version)
{
    @autoreleasepool {
        NSBundle* bundle = NSBundle.mainBundle;
        NSString* iconName = [bundle objectForInfoDictionaryKey:@"CFBundleIconFile"];
        NSString* iconPath = [bundle pathForResource:iconName ofType:iconName.pathExtension.length ? nil : @"icns"];
        if (!iconPath) {
            return;
        }

        const QFileInfo source(QString::fromNSString(iconPath));
        const auto overrideRoot = qEnvironmentVariable("HORIZON_LAUNCHER_HOME");
        const QDir data(overrideRoot.isEmpty()
                            ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("horizon-launcher")
                            : overrideRoot);
        const QDir versions(data.filePath("versions"));
        const auto installed = versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        NSImage* icon = nil;
        for (const auto& build : installed) {
            if (!version.isEmpty() && build.fileName() != version) {
                continue;
            }
            const QDir directory(build.absoluteFilePath());
            if (!QFileInfo(directory.filePath("horizon-client")).isExecutable()) {
                continue;
            }
            const QFileInfo game(directory.filePath("Horizon.app"));
            if (game.isSymLink()) {
                continue;
            }
            const QDir gameDirectory(game.absoluteFilePath());
            const QString localization = QStringLiteral("Contents/Resources/en.lproj");
            if (!gameDirectory.mkpath(localization)) {
                continue;
            }
            const auto stringsPath = gameDirectory.filePath(localization + QStringLiteral("/InfoPlist.strings"));
            const QByteArray names = "\"CFBundleDisplayName\" = \"Stellar Minecraft\";\n"
                                     "\"CFBundleName\" = \"Stellar Minecraft\";\n";
            QFile existing(stringsPath);
            const bool hasNames = existing.open(QIODevice::ReadOnly) && existing.readAll() == names;
            existing.close();
            if (!hasNames) {
                QSaveFile localizedNames(stringsPath);
                if (localizedNames.open(QIODevice::WriteOnly) && localizedNames.write(names) == names.size()) {
                    localizedNames.commit();
                }
            }
            const QFileInfo customIcon(gameDirectory.filePath(QStringLiteral("Icon\r")));
            if (customIcon.exists() && customIcon.lastModified() >= source.lastModified()) {
                continue;
            }
            if (!icon) {
                icon = [[[NSImage alloc] initWithContentsOfFile:iconPath] autorelease];
                if (!icon) {
                    return;
                }
            }
            // Set a Finder custom icon. Horizon refreshes its own bundled artwork
            // before launch, while Launch Services keeps using the custom icon.
            [NSWorkspace.sharedWorkspace setIcon:icon forFile:game.absoluteFilePath().toNSString() options:0];
        }
    }
}
