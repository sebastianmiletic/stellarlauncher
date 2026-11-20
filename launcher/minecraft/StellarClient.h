// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"

namespace StellarClient {
inline const QString toolsJar = QStringLiteral("stellar-client.jar");

inline QHash<QString, QString> versions(const MinecraftInstance* instance)
{
    // A fresh gallery item has not run the component resolver yet.
    QFile manifest(QDir(instance->instanceRoot()).filePath("mmc-pack.json"));
    QHash<QString, QString> result;
    if (manifest.open(QIODevice::ReadOnly)) {
        const auto components = QJsonDocument::fromJson(manifest.readAll()).object().value("components").toArray();
        for (const auto& entry : components) {
            const auto component = entry.toObject();
            if (!component.value("disabled").toBool())
                result.insert(component.value("uid").toString(), component.value("version").toString());
        }
    }
    return result;
}

inline bool hasSupportedTitle(const QString& version)
{
    qsizetype suffix = 0;
    const auto release = QVersionNumber::fromString(version, &suffix);
    return suffix == version.size() && release >= QVersionNumber(1, 21, 9) && release <= QVersionNumber(26, 3);
}

inline Result<> updateBundledFile(const QString& target, const QString& resource, const QList<QByteArray>& previousHashes)
{
    if (QFile::exists(target)) {
        TRY_INTO(const auto current, FS::read(target))
        if (!previousHashes.contains(QCryptographicHash::hash(current, QCryptographicHash::Sha256).toHex()))
            return {};
    }
    TRY_INTO(const auto bundled, FS::read(resource))
    return FS::write(target, bundled);
}

inline Result<> prepareTitle(MinecraftInstance* instance, bool manual = false,
                             const QString& titleTint = {}, const QString& diskTint = {})
{
    const auto version = versions(instance).value("net.minecraft");
    if (!hasSupportedTitle(version))
        return {};

    const QDir game(instance->gameRoot());
    const auto marker = game.filePath(".stellar-title-installed");
    const auto target = game.filePath("resourcepacks/Stellar Look.zip");
    if (manual || QFile::exists(target) || !QFile::exists(marker)) {
        TRY(updateBundledFile(target, ":/archive-look.zip",
            { "e136389352707b6605e5e2c701a7a10976122aa22d18418f42f6b9d7c5888b6a",
              "3965a851732038c028bc04526a29a389221e6528f63688511d2d5cbc6275661e" }))
    }
    const bool firstLaunch = !QFile::exists(marker);
    const auto options = game.filePath("options.txt");
    QByteArray contents;
    if (QFile::exists(options)) {
        TRY_INTO(contents, FS::read(options))
    }

    auto lines = contents.split('\n');
    auto entry = std::find_if(lines.begin(), lines.end(), [](const auto& line) {
        return line.startsWith("resourcePacks:");
    });
    QJsonArray packs{QStringLiteral("vanilla")};
    if (entry != lines.end()) {
        QJsonParseError error;
        const auto selected = QJsonDocument::fromJson(entry->mid(14).trimmed(), &error);
        if (error.error != QJsonParseError::NoError || !selected.isArray())
            return std::unexpected(QStringLiteral("Minecraft resource-pack options could not be read."));
        packs = selected.array();
    }

    const auto original = packs;
    const QString stellar = QStringLiteral("file/Stellar Look.zip");
    if ((manual || firstLaunch) && !packs.contains(stellar))
        packs.append(stellar);

    const auto applyColor = [&](const QString& layer, const QString& packName, const QString& color, bool enabled) -> Result<> {
        const QString packEntry = "file/" + packName;
        for (qsizetype i = packs.size(); i > 0; --i) {
            if (packs.at(i - 1) == packEntry)
                packs.removeAt(i - 1);
        }
        if (!enabled || !packs.contains(stellar) || !QStringList{"violet", "ice", "amber"}.contains(color))
            return {};
        const auto path = game.filePath("resourcepacks/" + packName);
        TRY_INTO(const auto bundled, FS::read(":/stellar-" + layer + "-" + color + ".zip"))
        const auto saved = FS::read(path);
        if (!saved || *saved != bundled) {
            TRY(FS::write(path, bundled))
        }
        packs.append(packEntry);
        return {};
    };
    TRY(applyColor("title", "Stellar Title Color.zip", titleTint, true))
    TRY(applyColor("disk", "Stellar Disk Color.zip", diskTint, version == "26.3"))
    if (packs != original) {
        const auto line = "resourcePacks:" + QJsonDocument(packs).toJson(QJsonDocument::Compact);
        if (entry == lines.end())
            lines.append(line);
        else
            *entry = line + (entry->endsWith('\r') ? "\r" : "");
        if (lines.empty() || !lines.last().isEmpty())
            lines.append(QByteArray());
        TRY(FS::write(options, lines.join('\n')))
    }
    return firstLaunch ? FS::write(marker, "1\n") : Result<>{};
}

inline Result<> installTools(MinecraftInstance* instance)
{
    const QDir game(instance->gameRoot());
    const auto target = game.filePath("mods/" + toolsJar);
    const auto disabled = target + ".disabled";
    if (QFile::exists(disabled) && !QFile::exists(target) && !QFile::rename(disabled, target))
        return std::unexpected(QStringLiteral("Could not enable the installed Stellar Client mod."));
    return updateBundledFile(target, ":/stellar-client.jar",
        { "e3a44ad86c710e1c2c0b42912f6c9febca327d3f7927218a57c8dadef0de4d59",
          "b0838ec0274b5ec94b4ad6f021077c9b6cf60b977a4803b55921da32334e96cb" });
}
}
