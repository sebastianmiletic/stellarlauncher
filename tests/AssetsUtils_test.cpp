// SPDX-License-Identifier: GPL-3.0-only
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include "minecraft/AssetsUtils.h"

class AssetsUtilsTest : public QObject {
    Q_OBJECT
private slots:
    void verifiedCache()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("index.json");
        const QByteArray data = R"({"objects":{}})";
        const auto digest = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
        QVERIFY(!AssetsUtils::isAssetIndexCurrent(path, digest));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
        file.close();
        QVERIFY(AssetsUtils::isAssetIndexCurrent(path, digest));
        QVERIFY(AssetsUtils::isAssetIndexCurrent(path, digest.toUpper()));
        QVERIFY(!AssetsUtils::isAssetIndexCurrent(path, ""));
        QVERIFY(!AssetsUtils::isAssetIndexCurrent(path, QString(40, '0')));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("corrupted");
        file.close();
        QVERIFY(!AssetsUtils::isAssetIndexCurrent(path, digest));
    }
    void parsingAndReuse()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("index.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"virtual":true,"map_to_resources":true,"objects":{"sound/a.ogg":{"hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":5000000000},"sound/b.ogg":{"hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":5000000000}}})");
        file.close();
        AssetsIndex index;
        QVERIFY(AssetsUtils::loadAssetsIndexJson("first", path, index));
        QCOMPARE(index.objects.size(), 2);
        QCOMPARE(index.objects.value("sound/a.ogg").size, qint64(5000000000));
        QVERIFY(index.isVirtual);
        QVERIFY(index.mapToResources);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(R"({"objects":{}})");
        file.close();
        QVERIFY(AssetsUtils::loadAssetsIndexJson("second", path, index));
        QCOMPARE(index.id, QString("second"));
        QVERIFY(index.objects.isEmpty());
        QVERIFY(!index.isVirtual);
        QVERIFY(!index.mapToResources);
    }
    void malformedIndex()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("bad.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");
        file.close();
        AssetsIndex index;
        QVERIFY(!AssetsUtils::loadAssetsIndexJson("bad", path, index));
        QVERIFY(!AssetsUtils::loadAssetsIndexJson("missing", dir.filePath("missing"), index));
    }
};
QTEST_GUILESS_MAIN(AssetsUtilsTest)
#include "AssetsUtils_test.moc"
