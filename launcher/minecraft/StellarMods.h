// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVersionNumber>

#include "archive/ArchiveReader.h"

#ifdef Q_OS_MACOS
#include <cstdio>
#include <sys/clonefile.h>
#endif

namespace StellarMods {

inline QFileInfoList enabledMods(const QDir& mods)
{
    auto entries = mods.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    entries.removeIf([](const QFileInfo& file) { return file.suffix().compare("jar", Qt::CaseInsensitive) != 0; });
    return entries;
}

inline std::optional<bool> matches(const QString& installed, const QString& predicate)
{
    static const QRegularExpression number(R"(^\d+(?:\.\d+)*$)");
    static const QRegularExpression constraint(R"(^(>=|<=|>|<|=|\^|~)?(\d+(?:\.\d+)*)(?:\.([xX*]))?$)");
    static const QRegularExpression spaces(R"(\s+)");
    if (installed == predicate || predicate == "*")
        return true;
    if (!number.match(installed).hasMatch())
        return {};
    const auto version = QVersionNumber::fromString(installed).normalized();
    bool unknown = false;
    for (const auto& term : predicate.split(spaces, Qt::SkipEmptyParts)) {
        const auto match = constraint.match(term);
        if (!match.hasMatch()) {
            unknown = true;
            continue;
        }
        const auto op = match.captured(1);
        const auto target = QVersionNumber::fromString(match.captured(2));
        const auto comparison = QVersionNumber::compare(version, target.normalized());
        const auto samePrefix = [&](qsizetype components) {
            for (qsizetype i = 0; i < components; ++i) {
                if (version.segmentAt(i) != target.segmentAt(i))
                    return false;
            }
            return true;
        };
        bool valid;
        if (!match.captured(3).isEmpty())
            valid = samePrefix(target.segmentCount());
        else if (op == ">=")
            valid = comparison >= 0;
        else if (op == "<=")
            valid = comparison <= 0;
        else if (op == ">")
            valid = comparison > 0;
        else if (op == "<")
            valid = comparison < 0;
        else if (op == "~" || op == "^")
            valid = comparison >= 0 && samePrefix(op == "~" ? 2 : 1);
        else
            valid = comparison == 0;
        if (!valid)
            return false;
    }
    return unknown || predicate.trimmed().isEmpty() ? std::optional<bool>() : true;
}

inline QStringList incompatible(const QDir& mods, const QHash<QString, QString>& versions)
{
    QStringList issues;
    const auto game = versions.value("net.minecraft");
    const auto fabric = versions.value("net.fabricmc.fabric-loader");
    const auto quilt = versions.contains("org.quiltmc.quilt-loader");
    const auto forge = versions.contains("net.minecraftforge");
    const auto neo = versions.contains("net.neoforged");
    for (const auto& file : enabledMods(mods)) {
        MMCZip::ArchiveReader jar(file.absoluteFilePath());
        if (!jar.collectFiles())
            continue;
        const auto hasFabric = jar.exists("fabric.mod.json");
        const auto hasQuilt = jar.exists("quilt.mod.json");
        const auto hasForge = jar.exists("META-INF/mods.toml") || jar.exists("mcmod.info");
        const auto hasNeo = jar.exists("META-INF/neoforge.mods.toml");
        if ((hasFabric || hasQuilt || hasForge || hasNeo)
            && !(hasFabric && (!fabric.isEmpty() || quilt)) && !(hasQuilt && quilt)
            && !(hasForge && (forge || neo)) && !(hasNeo && neo)) {
            issues.append(QObject::tr("%1 — incompatible with this instance’s mod loader.").arg(file.fileName()));
            continue;
        }
        if (fabric.isEmpty() || !hasFabric)
            continue;
        const auto bytes = jar.readFile("fabric.mod.json");
        if (!bytes)
            continue;
        const auto deps = QJsonDocument::fromJson(*bytes).object().value("depends").toObject();
        for (const auto& key : {QStringLiteral("minecraft"), QStringLiteral("fabricloader")}) {
            const auto value = deps.value(key);
            const auto installed = key == "minecraft" ? game : fabric;
            if (installed.isEmpty() || (!value.isString() && !value.isArray()))
                continue;
            const auto predicates = value.isString() ? QJsonArray{value} : value.toArray();
            bool incompatible = !predicates.isEmpty();
            QStringList requirements;
            for (const auto& predicate : predicates) {
                requirements.append(predicate.toString());
                if (matches(installed, predicate.toString()) != false)
                    incompatible = false;
            }
            if (incompatible) {
                issues.append(QObject::tr("%1 — requires %2 %3 (installed %4).")
                    .arg(file.fileName(), key == "minecraft" ? "Minecraft" : "Fabric Loader",
                         requirements.join(" or "), installed));
            }
        }
    }
    return issues;
}

#ifdef Q_OS_MACOS
inline QByteArray digest(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return hash.result();
}

// Clone the blocks only after proving the bytes match. Each instance retains
// an independent file, so replacing or changing one mod never alters another.
inline qsizetype shareCopies(const QList<QDir>& referenceFolders, const QList<QDir>& targetFolders)
{
    QMultiHash<qint64, QString> candidates;
    QHash<QString, QByteArray> digests;
    for (const auto& folder : referenceFolders) {
        for (const auto& file : enabledMods(folder))
            candidates.insert(file.size(), file.absoluteFilePath());
    }
    qsizetype shared = 0;
    QSet<QByteArray> processed;
    for (const auto& folder : targetFolders) {
        for (const auto& file : enabledMods(folder)) {
            const auto target = file.absoluteFilePath();
            auto sameSize = candidates.values(file.size());
            sameSize.removeAll(target);
            if (sameSize.isEmpty())
                continue;
            const auto expected = digest(target);
            if (expected.isEmpty())
                continue;
            for (const auto& source : sameSize) {
                if (!digests.contains(source))
                    digests.insert(source, digest(source));
                if (digests.value(source) != expected || processed.contains(source.toUtf8() + '\0' + expected))
                    continue;
                QTemporaryDir temporary(folder.filePath(".stellar-mod-XXXXXX"));
                if (!temporary.isValid())
                    break;
                const auto clone = QDir(temporary.path()).filePath("mod.jar");
                if (::clonefile(QFile::encodeName(source).constData(), QFile::encodeName(clone).constData(), CLONE_NOFOLLOW) != 0)
                    continue;
                const QFileInfo current(target);
                if (current.isSymLink() || digest(clone) != expected || digest(target) != expected)
                    break;
                if (!QFile::setPermissions(clone, current.permissions()))
                    break;
                if (::rename(QFile::encodeName(clone).constData(), QFile::encodeName(target).constData()) == 0) {
                    ++shared;
                    processed.insert(target.toUtf8() + '\0' + expected);
                    break;
                }
            }
        }
    }
    return shared;
}
#endif
}
