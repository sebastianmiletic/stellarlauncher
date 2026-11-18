// Parser microbenchmark, not an end-to-end Minecraft performance measurement.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QVariant>
#include <algorithm>
#include <iostream>
struct Asset { QString hash; qint64 size = 0; };
using Objects = QMap<QString, Asset>;
Objects baseline(const QJsonObject& root) {
    Objects out;
    const auto map = root.value("objects").toVariant().toMap();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const auto nested = it.value().toMap();
        Asset asset;
        for (auto n = nested.begin(); n != nested.end(); ++n) {
            if (n.key() == "hash") asset.hash = n.value().toString();
            else if (n.key() == "size") asset.size = n.value().toLongLong();
        }
        out.insert(it.key(), asset);
    }
    return out;
}
Objects archive(const QJsonObject& root) {
    Objects out;
    const auto objects = root.value("objects").toObject();
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const auto object = it.value().toObject();
        out.insert(it.key(), { object.value("hash").toString(), object.value("size").toInteger() });
    }
    return out;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) return 3;
    const auto json = file.readAll();
    const auto before = baseline(QJsonDocument::fromJson(json).object());
    const auto after = archive(QJsonDocument::fromJson(json).object());
    if (before.size() != after.size()) return 4;
    for (auto it = before.begin(); it != before.end(); ++it) {
        if (it.value().hash != after.value(it.key()).hash || it.value().size != after.value(it.key()).size) return 5;
    }
    QVector<double> oldTimes, newTimes;
    for (int round = 0; round < 30; ++round) {
        auto run = [&](auto parse, QVector<double>& samples) {
            QElapsedTimer t; t.start();
            const auto result = parse(QJsonDocument::fromJson(json).object());
            samples.push_back(t.nsecsElapsed() / 1e6);
            if (result.size() != before.size()) std::abort();
        };
        if (round % 2) { run(archive, newTimes); run(baseline, oldTimes); }
        else { run(baseline, oldTimes); run(archive, newTimes); }
    }
    std::sort(oldTimes.begin(), oldTimes.end());
    std::sort(newTimes.begin(), newTimes.end());
    std::cout << "objects=" << before.size() << " samples=30 baseline_median_ms=" << oldTimes[15]
              << " archive_median_ms=" << newTimes[15] << " speedup=" << oldTimes[15]/newTimes[15] << '\n';
}
