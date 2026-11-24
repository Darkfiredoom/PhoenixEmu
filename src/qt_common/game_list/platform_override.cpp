// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <mutex>

#include <QSettings>
#include <QString>

#include "common/fs/path_util.h"
#include "qt_common/game_list/platform_override.h"

namespace PlatformOverride {
namespace {

std::mutex mutex;

QSettings& Store() {
    static QSettings settings{
        QString::fromStdString(Common::FS::PathToUTF8String(
            Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "platforms.ini")),
        QSettings::IniFormat};
    return settings;
}

QString Key(u64 program_id) {
    return QStringLiteral("platforms/") +
           QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper();
}

} // Anonymous namespace

Choice Get(u64 program_id) {
    std::scoped_lock lock{mutex};
    const int value = Store().value(Key(program_id), 0).toInt();
    return value == 1 ? Choice::Switch1 : value == 2 ? Choice::Switch2 : Choice::Automatic;
}

void Set(u64 program_id, Choice choice) {
    std::scoped_lock lock{mutex};
    QSettings& settings = Store();
    if (choice == Choice::Automatic) {
        settings.remove(Key(program_id));
    } else {
        settings.setValue(Key(program_id), choice == Choice::Switch1 ? 1 : 2);
    }
    settings.sync();
}

} // namespace PlatformOverride
