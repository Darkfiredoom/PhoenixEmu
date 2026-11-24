// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <system_error>
#include <string>
#include <unordered_map>
#include <vector>

#include <QImageReader>
#include <QRegularExpression>
#include <QStringList>
#include <QTransform>

#include <fmt/format.h>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/case_art.h"

namespace CaseArt {
namespace {

namespace fs = std::filesystem;

/// The parts of the wrap-around cover (back | spine | front) as fractions of its width: a Switch
/// case is about 105 mm wide with a 10 mm spine (measured on the Donkey Kong Bananza cover).
constexpr double WrapSpineStart = 0.4775;
constexpr double WrapSpineEnd = 0.5225;

/// Images are read at most this tall (case art scans run to 4000+ px; the case is never drawn
/// near that big).
constexpr int MaxCoverHeight = 1400;
constexpr int MaxInsideWidth = 1800;

std::mutex cache_mutex;
// Keyed by title ID and name: several dumps of one ID can be named apart.
std::unordered_map<std::string, std::shared_ptr<const Art>> art_cache; // null: none
std::unordered_map<std::string, std::vector<u8>> banner_cache;         // empty: none

std::string CacheKey(const QString& title, u64 program_id, bool switch2) {
    return fmt::format("{:016X}|{}|{}", program_id, switch2 ? 2 : 1, title.toStdString());
}

QString Utf8(const fs::path& path) {
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

/// A name for matching: lower case, accents and punctuation gone, edition words dropped.
/// Windows file names use "꞉" (U+A789) for a colon.
QString Normalize(const QString& name) {
    QString text = name.normalized(QString::NormalizationForm_KD).toLower();
    QString kept;
    kept.reserve(text.size());
    for (const QChar c : text) {
        if (c.isLetterOrNumber() && c.unicode() < 0x80) {
            kept += c;
        } else if (c.isSpace() || c.isPunct() || c.unicode() == 0xA789 || c == QLatin1Char('-')) {
            kept += QLatin1Char(' ');
        }
    }
    QStringList words = kept.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // "Nintendo Switch 2 Edition", "Switch 2 edition" and a leading "the" say nothing about which
    // game it is.
    QString joined = words.join(QLatin1Char(' '));
    for (const char* const noise : {"nintendo switch 2 edition", "switch 2 edition",
                                    "nintendo switch 2", "nintendo switch"}) {
        joined.replace(QString::fromLatin1(noise), QString{});
    }
    words = joined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!words.isEmpty() && words.front() == QStringLiteral("the")) {
        words.removeFirst();
    }
    return words.join(QLatin1Char(' '));
}

/// How well a folder or file name fits the game's name (0: not at all).
int Score(const QString& candidate_raw, const QString& title_raw, u64 program_id) {
    const QString id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
    if (program_id != 0 && candidate_raw.compare(id, Qt::CaseInsensitive) == 0) {
        return 1000;
    }
    const QString candidate = Normalize(candidate_raw);
    const QString title = Normalize(title_raw);
    if (candidate.isEmpty() || title.isEmpty()) {
        return 0;
    }
    if (candidate == title) {
        return 900;
    }
    const QString c = QString(candidate).remove(QLatin1Char(' '));
    const QString t = QString(title).remove(QLatin1Char(' '));
    if (c == t) {
        return 880;
    }
    if (std::min(c.size(), t.size()) >= 5 && (t.contains(c) || c.contains(t))) {
        return 800 - static_cast<int>(std::abs(c.size() - t.size()));
    }
    // Initials of a run of the title's words: "OOT" for "... Ocarina of Time".
    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (c.size() >= 3 && c.size() <= 6 && !c.contains(QLatin1Char(' '))) {
        for (qsizetype start = 0; start + c.size() <= words.size(); ++start) {
            QString initials;
            for (qsizetype k = 0; k < c.size(); ++k) {
                initials += words[start + k].front();
            }
            if (initials == c) {
                return 600;
            }
        }
    }
    // Most of the words in common: "Hyrule Warriors Age of Inprisonment" (a typo) still finds
    // "Hyrule Warriors: Age of Imprisonment".
    const QStringList candidate_words = candidate.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    qsizetype shared = 0;
    for (const QString& word : candidate_words) {
        if (word.size() >= 2 && words.contains(word)) {
            ++shared;
        }
    }
    if (shared >= 2 && shared * 10 >= candidate_words.size() * 6) {
        return 400 + static_cast<int>(shared) * 10;
    }
    return 0;
}

/// The score once the edition is counted: a "Switch 2 Edition" entry (its name says Switch 2)
/// is never a Switch 1 game's, so a Switch 1 Pokémon Legends: Z-A gets its Switch 1 case; a
/// Switch 2 game ranks one above an entry without the words, and still takes that one when it is
/// all there is (Switch 2-only games have no edition in their names).
int EditionScore(int score, const QString& candidate, bool switch2) {
    if (score <= 0) {
        return score;
    }
    const bool edition = IsSwitch2EditionName(candidate);
    if (edition && !switch2) {
        return 0;
    }
    return edition ? score + 60 : score;
}

/// The best entry of a folder for the game, or empty; `score_out` gets its score. Switch 1 games
/// skip "Switch 2 Edition" entries and Switch 2 games prefer them (EditionScore).
fs::path BestMatch(const fs::path& folder, bool directories, const QString& title, u64 program_id,
                   bool switch2, int* score_out = nullptr) {
    std::error_code ec;
    fs::path best;
    int best_score = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_directory(ec) != directories) {
            continue;
        }
        const QString name = directories ? Utf8(entry.path().filename())
                                         : Utf8(entry.path().stem());
        const int score = EditionScore(Score(name, title, program_id), name, switch2);
        if (score > best_score) {
            best_score = score;
            best = entry.path();
        }
    }
    if (score_out != nullptr) {
        *score_out = best_score;
    }
    return best;
}

/// Where the game folders are: the player's folder (<folder>/Cases, or the folder itself when
/// it holds the game folders directly), then the downloaded pack's Cases. The player's own art
/// wins: it is looked through first and kept on a tie.
std::vector<fs::path> CasesFolders() {
    std::vector<fs::path> folders;
    std::error_code ec;
    const fs::path root = Folder();
    folders.push_back(fs::is_directory(root / "Cases", ec) ? root / "Cases" : root);
    folders.push_back(PackFolder() / "Cases");
    return folders;
}

std::vector<fs::path> BannerFolders() {
    std::vector<fs::path> folders;
    std::error_code ec;
    const fs::path root = Folder();
    if (fs::is_directory(root / "Banner", ec)) {
        folders.push_back(root / "Banner");
    } else if (fs::is_directory(root / "Banners", ec)) {
        folders.push_back(root / "Banners");
    }
    folders.push_back(PackFolder() / "Banner");
    return folders;
}

/// The best entry for the game across folders (the earlier folder on a tie).
fs::path BestMatchIn(const std::vector<fs::path>& folders, bool directories, const QString& title,
                     u64 program_id, bool switch2, int* score_out = nullptr) {
    fs::path best;
    int best_score = 0;
    for (const fs::path& folder : folders) {
        int score = 0;
        const fs::path found = BestMatch(folder, directories, title, program_id, switch2, &score);
        if (!found.empty() && score > best_score) {
            best = found;
            best_score = score;
        }
    }
    if (score_out != nullptr) {
        *score_out = best_score;
    }
    return best;
}

bool IsImage(const fs::path& path) {
    const QString ext = Utf8(path.extension()).toLower();
    return ext == QStringLiteral(".jpg") || ext == QStringLiteral(".jpeg") ||
           ext == QStringLiteral(".png") || ext == QStringLiteral(".webp");
}

QImage Read(const fs::path& path, std::optional<int> max_height, std::optional<int> max_width) {
    QImageReader reader(Utf8(path));
    reader.setAutoTransform(true);
    reader.setAllocationLimit(1024); // MiB: the largest scans are 12 MB PNGs of 2473x1902
    QImage image = reader.read();
    if (image.isNull()) {
        LOG_WARNING(Frontend, "Case art: {} could not be read ({})", Common::FS::PathToUTF8String(path),
                    reader.errorString().toStdString());
        return {};
    }
    if (max_height && image.height() > *max_height) {
        image = image.scaledToHeight(*max_height, Qt::SmoothTransformation);
    }
    if (max_width && image.width() > *max_width) {
        image = image.scaledToWidth(*max_width, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

std::shared_ptr<const Art> ReadArt(const QString& title, u64 program_id, bool switch2) {
    const fs::path game = BestMatchIn(CasesFolders(), true, title, program_id, switch2);
    if (game.empty()) {
        return nullptr;
    }
    auto art = std::make_shared<Art>();
    fs::path wrap;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(game, ec)) {
        if (!entry.is_regular_file(ec) || !IsImage(entry.path())) {
            continue;
        }
        const QString stem = Utf8(entry.path().stem()).toLower();
        if (stem.endsWith(QStringLiteral("-front"))) {
            art->front = Read(entry.path(), MaxCoverHeight, std::nullopt);
        } else if (stem.endsWith(QStringLiteral("-back"))) {
            art->back = Read(entry.path(), MaxCoverHeight, std::nullopt);
        } else if (stem.endsWith(QStringLiteral("-side")) || stem.endsWith(QStringLiteral("-spine"))) {
            // Scanned lying down (text left to right): stood up, reading top to bottom.
            QImage side = Read(entry.path(), std::nullopt, std::nullopt);
            if (!side.isNull() && side.width() > side.height()) {
                side = side.transformed(QTransform().rotate(90), Qt::SmoothTransformation);
            }
            if (!side.isNull() && side.height() > MaxCoverHeight) {
                side = side.scaledToHeight(MaxCoverHeight, Qt::SmoothTransformation);
            }
            art->spine = side;
        } else if (stem.endsWith(QStringLiteral("-inside"))) {
            art->inside = Read(entry.path(), std::nullopt, MaxInsideWidth);
        } else if (wrap.empty()) {
            wrap = entry.path();
        }
    }
    if (art->front.isNull() && !wrap.empty()) {
        // The whole cover: back | spine | front.
        const QImage whole = Read(wrap, MaxCoverHeight, std::nullopt);
        if (!whole.isNull() && whole.width() > whole.height()) {
            const int w = whole.width();
            const int spine_left = static_cast<int>(w * WrapSpineStart);
            const int spine_right = static_cast<int>(w * WrapSpineEnd);
            art->back = whole.copy(0, 0, spine_left, whole.height());
            art->spine = whole.copy(spine_left, 0, spine_right - spine_left, whole.height());
            art->front = whole.copy(spine_right, 0, w - spine_right, whole.height());
        } else if (!whole.isNull()) {
            art->front = whole; // just a front
        }
    }
    if (art->front.isNull()) {
        return nullptr;
    }
    LOG_INFO(Frontend, "Case art for {:016X} ({}): {}{}{}", program_id, title.toStdString(),
             Common::FS::PathToUTF8String(game.filename()), art->inside.isNull() ? "" : ", inside",
             art->spine.isNull() ? "" : ", spine");
    return art;
}

} // Anonymous namespace

fs::path Folder() {
    const std::string chosen = UISettings::values.case_art_folder.GetValue();
    if (!chosen.empty()) {
        return fs::path{chosen};
    }
    return Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "case_art";
}

std::shared_ptr<const Art> Load(const QString& title, u64 program_id, bool switch2) {
    const std::string key = CacheKey(title, program_id, switch2);
    {
        std::scoped_lock lock{cache_mutex};
        if (const auto it = art_cache.find(key); it != art_cache.end()) {
            return it->second;
        }
    }
    auto art = ReadArt(title, program_id, switch2);
    std::scoped_lock lock{cache_mutex};
    art_cache[key] = art;
    return art;
}

std::vector<u8> BannerBytes(const QString& title, u64 program_id, bool switch2) {
    const std::string key = CacheKey(title, program_id, switch2);
    {
        std::scoped_lock lock{cache_mutex};
        if (const auto it = banner_cache.find(key); it != banner_cache.end()) {
            return it->second;
        }
    }
    std::vector<u8> bytes;
    const fs::path file = BestMatchIn(BannerFolders(), false, title, program_id, switch2);
    if (!file.empty() && IsImage(file)) {
        std::ifstream stream(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    std::scoped_lock lock{cache_mutex};
    banner_cache[key] = bytes;
    return bytes;
}

QString DisplayName(const QString& title, u64 program_id) {
    // A name taken from a file ("Pokopia_v2.0.1a (update)": underscores, a version, brackets)
    // that holds a case folder's name ("Pokopia") is shown as that folder's name. A game's own
    // title ("Pokémon Pokopia") is kept.
    static const QRegularExpression file_like{
        QStringLiteral(R"(_|\(|\[|\bv\d|\d+\.\d+|update|\bbase\b|\bdlc\b)"),
        QRegularExpression::CaseInsensitiveOption};
    if (!file_like.match(title).hasMatch()) {
        return title;
    }
    int score = 0;
    const fs::path game = BestMatchIn(CasesFolders(), true, title, program_id, true, &score);
    // Only a name found inside the title (not initials like "OOT", not a title ID folder).
    if (game.empty() || score < 700 || score > 880) {
        return title;
    }
    QString name = Utf8(game.filename());
    name.replace(QChar(0xA789), QLatin1Char(':'));
    return name;
}

fs::path PackFolder() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "case_art_pack";
}

int MatchScore(const QString& candidate, const QString& title, u64 program_id) {
    return Score(candidate, title, program_id);
}

bool IsSwitch2EditionName(const QString& name) {
    static const QRegularExpression edition{QStringLiteral(R"(switch\s*2|\bns2\b)"),
                                            QRegularExpression::CaseInsensitiveOption};
    return edition.match(name).hasMatch();
}

namespace {
fs::path PackMissPath(u64 program_id, bool switch2) {
    // "missing_v2": earlier builds marked Switch 1 games missing after asking only the pack (and
    // Switch Cabinet's host was refused), which would hold off their first real look for a day.
    // A game marked missing as Switch 1 looks again once its Switch 2 Edition is installed.
    return PackFolder() / "missing_v2" /
           fmt::format("{:016X}{}", program_id, switch2 ? ".switch2" : "");
}
} // Anonymous namespace

bool PackRecentlyMissing(u64 program_id, bool switch2) {
    std::error_code ec;
    const auto written = fs::last_write_time(PackMissPath(program_id, switch2), ec);
    return !ec && fs::file_time_type::clock::now() - written < std::chrono::hours{24};
}

void MarkPackMissing(u64 program_id, bool switch2) {
    std::error_code ec;
    fs::create_directories(PackMissPath(program_id, switch2).parent_path(), ec);
    std::ofstream file(PackMissPath(program_id, switch2), std::ios::trunc);
}

void Reset() {
    std::scoped_lock lock{cache_mutex};
    art_cache.clear();
    banner_cache.clear();
}

} // namespace CaseArt
