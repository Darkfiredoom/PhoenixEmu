// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phoenix/util/icon_search.h"
#include "qt_common/game_list/case_art.h"

#include <cstdlib>
#include <iterator>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <QBuffer>
#include <QByteArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "common/httplib.h"
#include "common/scm_rev.h"
#include "common/logging.h"
#include "common/string_util.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_pictures.h"
#ifdef PHOENIX_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

namespace {

constexpr int IconTargetSize = 256;
constexpr int MinCandidateScore = 300;
constexpr std::size_t MaxIconDownloads = 3;
constexpr std::size_t MaxSgdbChoices = 8;

std::vector<u8> EncodeSquareIcon(const QImage& source) {
    if (source.isNull()) {
        return {};
    }
    QImage img = source.convertToFormat(QImage::Format_RGBA8888);
    if (img.width() != IconTargetSize || img.height() != IconTargetSize) {
        img = img.scaled(IconTargetSize, IconTargetSize, Qt::KeepAspectRatioByExpanding,
                         Qt::SmoothTransformation);
        if (img.width() > IconTargetSize || img.height() > IconTargetSize) {
            const int x = (img.width() - IconTargetSize) / 2;
            const int y = img.height() > img.width() ? (img.height() - IconTargetSize) / 3
                                                     : (img.height() - IconTargetSize) / 2;
            img = img.copy(x, y, IconTargetSize, IconTargetSize);
        }
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return std::vector<u8>(encoded.begin(), encoded.end());
}

std::string NormalizeGameSlug(std::string title) {
    title = Common::ToLower(title);
    boost::replace_all(title, "é", "e");
    boost::replace_all(title, "è", "e");
    boost::replace_all(title, "ê", "e");
    boost::replace_all(title, "ō", "o");
    std::replace(title.begin(), title.end(), ' ', '-');
    std::replace(title.begin(), title.end(), '_', '-');
    std::erase_if(title, [](unsigned char c) { return !std::isalnum(c) && c != '-'; });
    title.erase(title.begin(),
                std::find_if(title.begin(), title.end(), [](char ch) { return ch != '-'; }));
    title.erase(std::find_if(title.rbegin(), title.rend(), [](char ch) { return ch != '-'; }).base(),
                title.end());
    title.erase(std::unique(title.begin(), title.end(),
                            [](char a, char b) { return a == '-' && b == '-'; }),
                title.end());
    return title;
}

std::string ResolveGameNameAlias(const std::string& raw_name, u64 title_id) {
    static const std::unordered_map<u64, std::string> by_title_id{
        {0x0400C3F00006E000ULL, "Mario Kart World"},
        {0x0100000000010000ULL, "Super Mario Odyssey"},
        {0x0100152000022000ULL, "Mario Kart 8 Deluxe"},
        {0x01007EF00011E000ULL, "The Legend of Zelda: Breath of the Wild"},
        {0x0100F2C0115B6000ULL, "The Legend of Zelda: Tears of the Kingdom"},
        {0x01006A800016E000ULL, "Super Smash Bros. Ultimate"},
        {0x01006F8002326000ULL, "Animal Crossing: New Horizons"},
    };
    static const std::unordered_map<std::string, std::string> by_alias{
        {"mkw", "Mario Kart World"},
        {"mariokartworld", "Mario Kart World"},
        {"mario-kart-world", "Mario Kart World"},
        {"dkb", "Donkey Kong Bananza"},
        {"donkeykongbananza", "Donkey Kong Bananza"},
        {"kar", "Kirby Air Riders"},
        {"kirbyairriders", "Kirby Air Riders"},
        {"mp4", "Metroid Prime 4: Beyond"},
        {"metroidprime4", "Metroid Prime 4: Beyond"},
        {"plza", "Pokemon Legends: Z-A"},
        {"pokemonlegendsza", "Pokemon Legends: Z-A"},
        {"mk8", "Mario Kart 8 Deluxe"},
        {"mk8d", "Mario Kart 8 Deluxe"},
        {"smo", "Super Mario Odyssey"},
        {"botw", "The Legend of Zelda: Breath of the Wild"},
        {"totk", "The Legend of Zelda: Tears of the Kingdom"},
        {"ssbu", "Super Smash Bros. Ultimate"},
        {"acnh", "Animal Crossing: New Horizons"},
        {"smbw", "Super Mario Bros. Wonder"},
        {"splatoon3", "Splatoon 3"},
        {"xc3", "Xenoblade Chronicles 3"},
        {"lm3", "Luigi's Mansion 3"},
        {"pikmin4", "Pikmin 4"},
    };
    const std::string slug = NormalizeGameSlug(raw_name);
    if (const auto it = by_alias.find(slug); it != by_alias.end()) {
        return it->second;
    }
    if (slug.empty()) {
        if (const auto it = by_title_id.find(title_id); it != by_title_id.end()) {
            return it->second;
        }
    }
    return raw_name;
}

std::string UrlEncode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

constexpr std::chrono::seconds SearchDeadline{10};
constexpr std::size_t MaxHttpBodySize = 4 * 1024 * 1024;
constexpr std::chrono::seconds PrimarySourceDeadline{5};
constexpr std::string_view IconSiteHost = "https://switchiconshowdown.com";
constexpr std::string_view SgdbApiBase = "https://www.steamgriddb.com/api/v2";

bool IsAllowedIconHost(std::string_view host) {
    // cdn.switch-images-julio.com: Switch Cabinet's case scans (DownloadSwitchCabinetArt). Left
    // off this list, every Switch 1 cover request was refused here and no case ever came.
    return host == "switchiconshowdown.com" || host == "steamgriddb.com" ||
           host.ends_with(".steamgriddb.com") || host == "raw.githubusercontent.com" ||
           host == "cdn.switch-images-julio.com";
}

enum class HttpStatus { Ok, NetworkError, Timeout, Cancelled, Unauthorized };

struct HttpResult {
    HttpStatus status{HttpStatus::NetworkError};
    std::string body;
    std::string detail;
};

// Every request shares one deadline and cancel flag, so a whole search can never run past it.
HttpResult HttpGet(const std::string& full_url, std::chrono::steady_clock::time_point deadline,
                   const std::atomic_bool& cancel, const std::string& bearer_token = {}) {
    using namespace std::chrono;
    HttpResult result;
    if (cancel) {
        result.status = HttpStatus::Cancelled;
        return result;
    }
    constexpr std::string_view scheme = "https://";
    if (!full_url.starts_with(scheme)) {
        result.detail = "unsupported URL";
        return result;
    }
    const auto path_start = full_url.find('/', scheme.size());
    const std::string origin =
        path_start == std::string::npos ? full_url : full_url.substr(0, path_start);
    const std::string path =
        path_start == std::string::npos ? std::string{"/"} : full_url.substr(path_start);
    if (!IsAllowedIconHost(std::string_view{origin}.substr(scheme.size()))) {
        result.detail = "unexpected host";
        return result;
    }
    const auto remaining = duration_cast<milliseconds>(deadline - steady_clock::now());
    if (remaining.count() <= 0) {
        result.status = HttpStatus::Timeout;
        return result;
    }
    try {
        httplib::Client client(origin);
        client.set_connection_timeout(remaining);
        client.set_read_timeout(remaining);
        client.set_write_timeout(remaining);
        client.set_max_timeout(remaining);
        client.set_follow_location(true);
#ifdef PHOENIX_BUNDLED_OPENSSL
        client.load_ca_cert_store(kCert, sizeof(kCert));
#elif !defined(_WIN32)
        // As in Common::Net::Fetch: an AppImage's OpenSSL looks for certificates where the build
        // machine kept them, so SteamOS failed with "SSL server verification failed".
        for (const char* const bundle : {"/etc/ssl/certs/ca-certificates.crt",
                                         "/etc/pki/tls/certs/ca-bundle.crt",
                                         "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem",
                                         "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"}) {
            std::error_code error;
            if (std::filesystem::exists(bundle, error)) {
                client.set_ca_cert_path(bundle);
                break;
            }
        }
#endif
        httplib::Headers headers{{"User-Agent", "PhoenixEmu"}};
        if (!bearer_token.empty()) {
            headers.emplace("Authorization", "Bearer " + bearer_token);
        }
        auto res = client.Get(path, headers);
        if (!res) {
            result.status = res.error() == httplib::Error::Canceled || steady_clock::now() >= deadline
                                ? HttpStatus::Timeout
                                : HttpStatus::NetworkError;
            result.detail = httplib::to_string(res.error());
            return result;
        }
        if (res->status != 200) {
            result.status = res->status == 401 || res->status == 403 ? HttpStatus::Unauthorized
                                                                      : HttpStatus::NetworkError;
            result.detail = fmt::format("HTTP {}", res->status);
            return result;
        }
        if (res->body.empty() || res->body.size() > MaxHttpBodySize) {
            result.detail = "unexpected response size";
            return result;
        }
        result.status = HttpStatus::Ok;
        result.body = std::move(res->body);
    } catch (const std::exception& e) {
        result.detail = e.what();
    } catch (...) {
        result.detail = "unknown error";
    }
    return result;
}

struct IconCandidate {
    int id{};
    std::string title;
};

std::string DecodeHtmlEntities(std::string text) {
    static constexpr std::pair<std::string_view, std::string_view> entities[]{
        {"&amp;", "&"},  {"&lt;", "<"},     {"&gt;", ">"},     {"&quot;", "\""},
        {"&#039;", "'"}, {"&#39;", "'"},    {"&apos;", "'"},   {"&eacute;", "\xC3\xA9"},
    };
    for (const auto& [from, to] : entities) {
        boost::replace_all(text, from, to);
    }
    return text;
}

// Rows look like: <a href='./detail.php?id=N'><img ...></a></td><td><center>Title</center>
std::vector<IconCandidate> ParseSearchResults(const std::string& html) {
    constexpr std::string_view id_marker = "detail.php?id=";
    constexpr std::string_view title_marker = "<td><center>";
    std::vector<IconCandidate> out;
    std::size_t pos = 0;
    while (out.size() < 16) {
        const auto id_pos = html.find(id_marker, pos);
        if (id_pos == std::string::npos) {
            break;
        }
        pos = id_pos + id_marker.size();
        const auto id_end = html.find_first_not_of("0123456789", pos);
        const auto digits = html.substr(pos, id_end == std::string::npos ? id_end : id_end - pos);
        if (digits.empty() || digits.size() > 9) {
            continue;
        }
        const auto title_pos = html.find(title_marker, pos);
        if (title_pos == std::string::npos) {
            break;
        }
        const auto title_begin = title_pos + title_marker.size();
        const auto title_end = html.find("</center>", title_begin);
        if (title_end == std::string::npos) {
            break;
        }
        auto title = DecodeHtmlEntities(html.substr(title_begin, title_end - title_begin));
        if (title.find('<') == std::string::npos && !title.empty()) {
            out.push_back({std::stoi(digits), std::move(title)});
        }
        pos = title_end;
    }
    return out;
}

std::string UrlEncodePathSegment(std::string value) {
    std::ranges::replace(value, '/', ' ');
    std::ranges::replace(value, '\\', ' ');
    auto encoded = UrlEncode(value);
    boost::replace_all(encoded, "+", "%20");
    return encoded;
}

// Icon image URLs from a SteamGridDB response, best size first.
std::vector<std::string> ParseSgdbIconUrls(const std::string& body,
                                           std::size_t max_choices = MaxSgdbChoices) {
    std::vector<std::string> urls;
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("data") ||
        !json["data"].is_array()) {
        return urls;
    }
    for (const auto& item : json["data"]) {
        if (urls.size() >= max_choices) {
            break;
        }
        if (item.is_object() && item.contains("url") && item["url"].is_string()) {
            urls.push_back(item["url"].get<std::string>());
        }
    }
    return urls;
}

int ScoreCandidateTitle(const std::string& candidate, const std::string& query) {
    const std::string c_slug = NormalizeGameSlug(candidate);
    const std::string q_slug = NormalizeGameSlug(query);
    if (c_slug.empty() || q_slug.empty()) {
        return -1000;
    }
    if (c_slug == q_slug) {
        return 1000;
    }
    int score = 0;
    if (c_slug.starts_with(q_slug + "-") || q_slug.starts_with(c_slug + "-")) {
        score += 750 - static_cast<int>(std::abs(static_cast<long>(c_slug.size()) -
                                                 static_cast<long>(q_slug.size())));
    } else if (c_slug.find(q_slug) != std::string::npos) {
        score += 600;
    }
    const auto split_words = [](const std::string& s) {
        std::set<std::string> words;
        std::size_t start = 0;
        while (start < s.size()) {
            const auto dash = s.find('-', start);
            const auto word = s.substr(start, dash - start);
            if (!word.empty()) {
                words.insert(word);
            }
            if (dash == std::string::npos) {
                break;
            }
            start = dash + 1;
        }
        return words;
    };
    const auto q_words = split_words(q_slug);
    const auto c_words = split_words(c_slug);
    int overlap = 0;
    for (const auto& w : q_words) {
        if (c_words.contains(w)) {
            ++overlap;
        }
    }
    score += overlap * 120 - static_cast<int>(c_words.size()) * 10;
    const std::string lower_cand = Common::ToLower(candidate);
    if (lower_cand.find("(series)") != std::string::npos ||
        lower_cand.find("(character)") != std::string::npos ||
        lower_cand.find("soundtrack") != std::string::npos) {
        score -= 400;
    }
    return score;
}


// With sgdb_only, switchiconshowdown.com is skipped and SteamGridDB (which needs api_key) is the
// only source. max_sgdb_choices limits how many SteamGridDB icons are downloaded.
IconSearchResult RunIconSearch(const std::string& raw_query, u64 tid, const std::string& api_key,
                               const std::atomic_bool& cancel, bool sgdb_only = false,
                               std::size_t max_sgdb_choices = MaxSgdbChoices,
                               int min_sgdb_score = MinCandidateScore) {
    using namespace std::chrono;
    IconSearchResult out;
    try {
        const auto start = steady_clock::now();
        const auto deadline = start + SearchDeadline;
        const std::string search_name = ResolveGameNameAlias(raw_query, tid);
        if (search_name != raw_query) {
            out.resolved_title = search_name;
        }
        if (search_name.empty()) {
            out.status = IconSearchStatus::NotFound;
            return out;
        }

        const auto to_status = [](const HttpResult& res) {
            switch (res.status) {
            case HttpStatus::Timeout:
                return IconSearchStatus::Timeout;
            case HttpStatus::Cancelled:
                return IconSearchStatus::Cancelled;
            case HttpStatus::Unauthorized:
                return IconSearchStatus::BadApiKey;
            default:
                return IconSearchStatus::NetworkError;
            }
        };
        const auto decode_icon = [&out](const std::string& body) {
            QImage img;
            if (img.loadFromData(reinterpret_cast<const uchar*>(body.data()),
                                 static_cast<int>(body.size()))) {
                out.icon_bytes = EncodeSquareIcon(img);
            }
            return !out.icon_bytes.empty();
        };

        // Primary: switchiconshowdown.com, limited so the fallback keeps some of the time.
        IconSearchStatus primary = IconSearchStatus::NotFound;
        std::string primary_error;
        const auto primary_deadline = std::min(deadline, start + PrimarySourceDeadline);
        do {
            if (sgdb_only) {
                break;
            }
            std::vector<std::string> queries{search_name};
            if (const auto colon = search_name.find(':');
                colon != std::string::npos && colon > 0) {
                queries.push_back(search_name.substr(0, colon));
            }
            std::vector<IconCandidate> candidates;
            for (const auto& query : queries) {
                const auto res =
                    HttpGet(std::string{IconSiteHost} + "/search.php?query=" + UrlEncode(query),
                            primary_deadline, cancel);
                if (res.status != HttpStatus::Ok) {
                    primary = to_status(res);
                    primary_error = res.detail;
                    break;
                }
                candidates = ParseSearchResults(res.body);
                if (!candidates.empty()) {
                    break;
                }
            }
            if (primary != IconSearchStatus::NotFound) {
                break;
            }

            // The site can list one game several times; keep the closest title first.
            std::vector<std::pair<int, IconCandidate>> scored;
            for (auto& cand : candidates) {
                const int score = ScoreCandidateTitle(cand.title, search_name);
                if (score >= MinCandidateScore) {
                    scored.emplace_back(score, std::move(cand));
                }
            }
            std::ranges::stable_sort(
                scored, [](const auto& a, const auto& b) { return a.first > b.first; });
            for (std::size_t i = 0; i < scored.size() && i < MaxIconDownloads; ++i) {
                const auto res = HttpGet(fmt::format("{}/images/100/{}.png", IconSiteHost,
                                                     scored[i].second.id),
                                         primary_deadline, cancel);
                if (res.status != HttpStatus::Ok) {
                    primary = to_status(res);
                    primary_error = res.detail;
                    break;
                }
                if (decode_icon(res.body)) {
                    out.status = IconSearchStatus::Found;
                    return out;
                }
            }
        } while (false);

        if (primary == IconSearchStatus::Cancelled) {
            out.status = primary;
            return out;
        }

        // Fallback: SteamGridDB, which needs the user's API key.
        if (api_key.empty()) {
            out.status = primary == IconSearchStatus::NotFound ? IconSearchStatus::NeedsApiKey
                                                               : primary;
            out.error = primary_error;
            return out;
        }
        const auto sgdb_get = [&](const std::string& path) {
            return HttpGet(std::string{SgdbApiBase} + path, deadline, cancel, api_key);
        };
        const auto search = sgdb_get("/search/autocomplete/" + UrlEncodePathSegment(search_name));
        if (search.status != HttpStatus::Ok) {
            out.status = to_status(search);
            out.error = search.detail;
            return out;
        }
        const auto parsed = nlohmann::json::parse(search.body, nullptr, false);
        // The closest name wins; between equally close names the newest release, so a Switch 2
        // game is picked over an older game of the same name.
        int game_id = 0;
        int best_score = min_sgdb_score - 1;
        s64 best_release = -1;
        if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("data") &&
            parsed["data"].is_array()) {
            for (const auto& item : parsed["data"]) {
                if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
                    !item.contains("name") || !item["name"].is_string()) {
                    continue;
                }
                const int score = ScoreCandidateTitle(item["name"].get<std::string>(), search_name);
                const s64 release = item.contains("release_date") &&
                                            item["release_date"].is_number_integer()
                                        ? item["release_date"].get<s64>()
                                        : 0;
                if (score > best_score || (score == best_score && release > best_release)) {
                    best_score = score;
                    best_release = release;
                    game_id = item["id"].get<int>();
                }
            }
        }
        if (game_id <= 0) {
            out.status = IconSearchStatus::NotFound;
            return out;
        }
        const auto icons =
            sgdb_get(fmt::format("/icons/game/{}?dimensions=256,512&mimes=image/png", game_id));
        if (icons.status != HttpStatus::Ok) {
            out.status = to_status(icons);
            out.error = icons.detail;
            return out;
        }
        // Collect every icon that arrives in time; the user picks one.
        for (const auto& url : ParseSgdbIconUrls(icons.body, max_sgdb_choices)) {
            const auto img = HttpGet(url, deadline, cancel);
            if (img.status != HttpStatus::Ok) {
                if (img.status == HttpStatus::Cancelled || img.status == HttpStatus::Timeout) {
                    if (out.choices.empty()) {
                        out.status = to_status(img);
                        out.error = img.detail;
                        return out;
                    }
                    break;
                }
                continue;
            }
            if (decode_icon(img.body)) {
                out.choices.push_back(std::move(out.icon_bytes));
                out.icon_bytes.clear();
            }
        }
        if (!out.choices.empty()) {
            out.status = IconSearchStatus::Choices;
            return out;
        }
        out.status = IconSearchStatus::NotFound;
    } catch (const std::exception& e) {
        out.status = IconSearchStatus::InternalError;
        out.error = e.what();
    } catch (...) {
        out.status = IconSearchStatus::InternalError;
        out.error = "unknown error";
    }
    return out;
}

// SteamGridDB only: it has Switch 2 games. switchiconshowdown.com knows only Switch 1 games and
// matched them by name (Ocarina of Time got Breath of the Wild's icon).
IconSearchResult RunPreferredSearch(const std::string& name, u64 tid, const std::string& api_key,
                                    const std::atomic_bool& cancel, std::size_t max_sgdb_choices,
                                    int min_score) {
    if (api_key.empty()) {
        IconSearchResult result;
        result.status = IconSearchStatus::NeedsApiKey;
        return result;
    }
    return RunIconSearch(name, tid, api_key, cancel, true, max_sgdb_choices, min_score);
}

} // namespace

std::unordered_map<u64, GamePictureListEntry> LoadGamePictureList(const std::atomic_bool& cancel) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    const fs::path cached = QtCommon::GamePictures::Folder() / "list.json";
    std::string body;
    std::error_code ec;
    const auto written = fs::last_write_time(cached, ec);
    const bool fresh = !ec && fs::file_time_type::clock::now() - written < hours{24};
    const auto read_cache = [&] {
        std::ifstream file(cached, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    if (fresh) {
        body = read_cache();
    }
    if (body.empty()) {
        // The list lives in the repository the updates come from.
        const auto res = HttpGet(fmt::format("https://raw.githubusercontent.com/{}/HEAD/dist/"
                                             "game_pictures.json",
                                             Common::g_build_auto_update_repo),
                                 steady_clock::now() + seconds{20}, cancel);
        if (res.status == HttpStatus::Ok) {
            body = res.body;
            fs::create_directories(cached.parent_path(), ec);
            std::ofstream(cached, std::ios::binary | std::ios::trunc) << body;
        } else {
            LOG_INFO(Frontend, "Game pictures: the picture list could not be downloaded ({})",
                     res.detail);
            body = read_cache(); // an old list beats none
        }
    }
    std::unordered_map<u64, GamePictureListEntry> list;
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("pictures") ||
        !json["pictures"].is_object()) {
        return list;
    }
    for (const auto& [id, entry] : json["pictures"].items()) {
        if (!entry.is_object() || !entry.contains("url") || !entry["url"].is_string()) {
            continue;
        }
        const u64 title_id = std::strtoull(id.c_str(), nullptr, 16);
        if (title_id != 0) {
            GamePictureListEntry pictures{entry["url"].get<std::string>(), {}};
            if (entry.contains("hero") && entry["hero"].is_string()) {
                pictures.hero = entry["hero"].get<std::string>();
            }
            list.emplace(title_id, std::move(pictures));
        }
    }
    return list;
}

std::vector<u8> DownloadGamePicture(const std::string& url, const std::atomic_bool& cancel) {
    const auto res = HttpGet(url, std::chrono::steady_clock::now() + SearchDeadline, cancel);
    if (res.status != HttpStatus::Ok) {
        return {};
    }
    QImage image;
    if (!image.loadFromData(reinterpret_cast<const uchar*>(res.body.data()),
                            static_cast<int>(res.body.size()))) {
        return {};
    }
    return EncodeSquareIcon(image);
}

CaseArtFetch DownloadCaseArt(u64 title_id, const std::string& name, bool switch2,
                             const std::atomic_bool& cancel) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    const std::string base = fmt::format("https://raw.githubusercontent.com/{}/case-art/",
                                         Common::g_build_auto_update_repo);
    const fs::path pack = CaseArt::PackFolder();
    std::error_code ec;
    fs::create_directories(pack, ec);
    // The manifest, kept for a day.
    const fs::path cached = pack / "manifest.json";
    std::string body;
    const auto written = fs::last_write_time(cached, ec);
    const bool fresh = !ec && fs::file_time_type::clock::now() - written < hours{24};
    const auto read_cache = [&] {
        std::ifstream file(cached, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    if (fresh) {
        body = read_cache();
    }
    if (body.empty()) {
        const auto res = HttpGet(base + "manifest.json", steady_clock::now() + seconds{20}, cancel);
        if (res.status == HttpStatus::Ok) {
            body = res.body;
            std::ofstream(cached, std::ios::binary | std::ios::trunc) << body;
        } else {
            body = read_cache(); // an old manifest beats none
            if (body.empty()) {
                LOG_INFO(Frontend, "Case art: the pack's manifest could not be downloaded ({})",
                         res.detail);
                return CaseArtFetch::Failed;
            }
        }
    }
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.contains("games") || !json["games"].is_array()) {
        return CaseArtFetch::Failed;
    }
    // The game: its title ID (or its base game's), else the best name. A Switch 2 Edition shares
    // its Switch 1 game's title ID, so the names tell them apart: a Switch 1 game skips entries
    // named "... Switch 2 Edition" (Switch Cabinet then has its Switch 1 case), a Switch 2 game
    // prefers them.
    const std::string id = fmt::format("{:016X}", title_id);
    const std::string base_id = fmt::format("{:016X}", title_id & ~u64{0xFFF});
    const QString title = QString::fromStdString(name);
    const nlohmann::json* best = nullptr;
    int best_score = 0;
    for (const auto& game : json["games"]) {
        if (!game.is_object() || !game.contains("files") || !game["files"].is_array()) {
            continue;
        }
        bool edition = false;
        if (game.contains("folder") && game["folder"].is_string()) {
            edition = CaseArt::IsSwitch2EditionName(
                QString::fromStdString(game["folder"].get<std::string>()));
        }
        if (game.contains("names") && game["names"].is_array()) {
            for (const auto& n : game["names"]) {
                edition = edition || (n.is_string() && CaseArt::IsSwitch2EditionName(
                                                           QString::fromStdString(n.get<std::string>())));
            }
        }
        if (edition && !switch2) {
            continue;
        }
        int score = 0;
        if (game.contains("ids") && game["ids"].is_array()) {
            for (const auto& game_id : game["ids"]) {
                if (game_id.is_string() &&
                    (game_id.get<std::string>() == id || game_id.get<std::string>() == base_id)) {
                    score = 1000;
                }
            }
        }
        if (score == 0) {
            std::vector<std::string> names;
            if (game.contains("names") && game["names"].is_array()) {
                for (const auto& n : game["names"]) {
                    if (n.is_string()) {
                        names.push_back(n.get<std::string>());
                    }
                }
            }
            if (game.contains("folder") && game["folder"].is_string()) {
                names.push_back(game["folder"].get<std::string>());
            }
            for (const std::string& candidate : names) {
                score = std::max(score, CaseArt::MatchScore(QString::fromStdString(candidate),
                                                            title, title_id));
            }
        }
        if (edition && score > 0) {
            score += 60;
        }
        if (score >= 600 && score > best_score) {
            best = &game;
            best_score = score;
        }
    }
    if (best == nullptr) {
        return CaseArtFetch::NotInPack;
    }
    // Its files, each saved where the pack has it (Cases/<game>/..., Banner/...).
    std::vector<std::string> files;
    for (const auto& file : (*best)["files"]) {
        if (file.is_string()) {
            files.push_back(file.get<std::string>());
        }
    }
    if (best->contains("banner") && (*best)["banner"].is_string()) {
        files.push_back((*best)["banner"].get<std::string>());
    }
    bool downloaded = false;
    for (const std::string& file : files) {
        if (file.find("..") != std::string::npos || file.empty() || file.front() == '/') {
            continue; // only paths inside the pack
        }
        const fs::path target = pack / fs::path{std::u8string{file.begin(), file.end()}};
        if (fs::exists(target, ec)) {
            continue;
        }
        std::string url = base;
        size_t start = 0;
        while (start <= file.size()) {
            const size_t slash = file.find('/', start);
            const std::string part = file.substr(start, slash == std::string::npos ? std::string::npos
                                                                                   : slash - start);
            url += UrlEncodePathSegment(part);
            if (slash == std::string::npos) {
                break;
            }
            url += '/';
            start = slash + 1;
        }
        const auto res = HttpGet(url, steady_clock::now() + seconds{60}, cancel);
        if (res.status != HttpStatus::Ok || res.body.empty()) {
            LOG_WARNING(Frontend, "Case art: {} could not be downloaded ({})", file, res.detail);
            return downloaded ? CaseArtFetch::Downloaded : CaseArtFetch::Failed;
        }
        fs::create_directories(target.parent_path(), ec);
        const fs::path temporary = fs::path{target}.concat(".part");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
        }
        fs::rename(temporary, target, ec);
        downloaded = !ec || downloaded;
    }
    if (downloaded) {
        LOG_INFO(Frontend, "Case art for {:016X} ({}) downloaded: {}", title_id, name,
                 (*best)["folder"].is_string() ? (*best)["folder"].get<std::string>() : "");
    }
    return downloaded ? CaseArtFetch::Downloaded : CaseArtFetch::AlreadyHere;
}

CaseArtFetch DownloadSwitchCabinetArt(u64 title_id, const std::string& name,
                                      const std::atomic_bool& cancel) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    // Switch Cabinet (cdn.switch-images-julio.com): scans of Switch 1 cases. list.csv names each
    // case's code; <code>/front.png, back.png, spine.png and inside.png are the scans, and
    // _resized/<code>/... the same at about 1130 px tall (the full scans run to 75 MB).
    static constexpr std::string_view Base = "https://cdn.switch-images-julio.com/file/switch-images-julio/";
    const fs::path pack = CaseArt::PackFolder();
    std::error_code ec;
    fs::create_directories(pack, ec);
    const fs::path cached = pack / "switch_cabinet_list.csv";
    std::string body;
    const auto written = fs::last_write_time(cached, ec);
    const bool fresh = !ec && fs::file_time_type::clock::now() - written < hours{24};
    const auto read_cache = [&] {
        std::ifstream file(cached, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    if (fresh) {
        body = read_cache();
    }
    if (body.empty()) {
        const auto res =
            HttpGet(std::string{Base} + "list.csv", steady_clock::now() + seconds{20}, cancel);
        if (res.status == HttpStatus::Ok && !res.body.empty()) {
            body = res.body;
            std::ofstream(cached, std::ios::binary | std::ios::trunc) << body;
        } else {
            body = read_cache();
            if (body.empty()) {
                LOG_INFO(Frontend, "Case art: the Switch Cabinet list could not be downloaded ({})",
                         res.detail);
                return CaseArtFetch::Failed;
            }
        }
    }
    // Rows: title,code,titleids (| between),inside_exist,region,publisher,limprint_type,
    // limprint_number. Titles are not quoted; anything before the last seven fields is the title.
    const std::string id = fmt::format("{:016X}", title_id);
    const std::string base_id = fmt::format("{:016X}", title_id & ~u64{0xFFF});
    const QString title = QString::fromStdString(name);
    const auto region_rank = [](const std::string& region) {
        return region == "NA" ? 3 : region == "EU" ? 2 : 1;
    };
    std::string best_code;
    bool best_inside = false;
    int best_score = 0;
    int best_region = 0;
    size_t start = 0;
    bool header = true;
    while (start < body.size()) {
        size_t end = body.find('\n', start);
        if (end == std::string::npos) {
            end = body.size();
        }
        std::string line = body.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (header) {
            header = false;
            continue;
        }
        std::vector<std::string> fields;
        size_t field_start = 0;
        while (true) {
            const size_t comma = line.find(',', field_start);
            fields.push_back(line.substr(field_start, comma == std::string::npos
                                                          ? std::string::npos
                                                          : comma - field_start));
            if (comma == std::string::npos) {
                break;
            }
            field_start = comma + 1;
        }
        if (fields.size() < 8) {
            continue;
        }
        const size_t n = fields.size();
        std::string row_title = fields[0];
        for (size_t i = 1; i + 7 < n; ++i) {
            row_title += ',' + fields[i];
        }
        const std::string& code = fields[n - 7];
        const std::string& ids = fields[n - 6];
        const bool inside = fields[n - 5] == "true";
        const std::string& region = fields[n - 4];
        if (code.empty() || code.find_first_not_of(
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos) {
            continue; // only plain codes go into a URL
        }
        int score = 0;
        size_t id_start = 0;
        while (id_start <= ids.size() && !ids.empty()) {
            const size_t bar = ids.find('|', id_start);
            std::string one = ids.substr(id_start, bar == std::string::npos ? std::string::npos
                                                                           : bar - id_start);
            std::transform(one.begin(), one.end(), one.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (one == id || one == base_id) {
                score = 1000;
            }
            if (bar == std::string::npos) {
                break;
            }
            id_start = bar + 1;
        }
        if (score == 0) {
            score = CaseArt::MatchScore(QString::fromStdString(row_title), title, title_id);
        }
        // A scan with its inside, then the North American case, wins a tie.
        const int rank = region_rank(region) + (inside ? 10 : 0);
        if (score >= 800 && (score > best_score || (score == best_score && rank > best_region))) {
            best_code = code;
            best_inside = inside;
            best_score = score;
            best_region = rank;
        }
    }
    if (best_code.empty()) {
        return CaseArtFetch::NotInPack;
    }
    // Saved as the player's own folders are read: Cases/<title ID>/<title ID>-Front.png, ...
    // (a folder named after the title ID always matches its game).
    const fs::path folder = pack / "Cases" / id;
    std::vector<std::pair<std::string, std::string>> files{
        {"front.png", "Front"}, {"back.png", "Back"}, {"spine.png", "Spine"}};
    if (best_inside) {
        files.emplace_back("inside.png", "Inside");
    }
    bool downloaded = false;
    for (const auto& [remote, part] : files) {
        const fs::path target = folder / fmt::format("{}-{}.png", id, part);
        if (fs::exists(target, ec)) {
            continue;
        }
        const auto res = HttpGet(fmt::format("{}_resized/{}/{}", Base, best_code, remote),
                                 steady_clock::now() + seconds{60}, cancel);
        if (res.status != HttpStatus::Ok || res.body.empty()) {
            LOG_WARNING(Frontend, "Case art: Switch Cabinet {} {} could not be downloaded ({})",
                        best_code, remote, res.detail);
            if (part == "Front") {
                return CaseArtFetch::Failed; // no cover, no case
            }
            continue;
        }
        fs::create_directories(folder, ec);
        const fs::path temporary = fs::path{target}.concat(".part");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(res.body.data(), static_cast<std::streamsize>(res.body.size()));
        }
        fs::rename(temporary, target, ec);
        downloaded = !ec || downloaded;
    }
    if (downloaded) {
        LOG_INFO(Frontend, "Case art for {:016X} ({}) downloaded from Switch Cabinet: {}", title_id,
                 name, best_code);
    }
    return downloaded ? CaseArtFetch::Downloaded : CaseArtFetch::AlreadyHere;
}

std::vector<u8> DownloadGameBanner(const std::string& url, const std::atomic_bool& cancel) {
    const auto res = HttpGet(url, std::chrono::steady_clock::now() + SearchDeadline, cancel);
    if (res.status != HttpStatus::Ok) {
        return {};
    }
    QImage image;
    if (!image.loadFromData(reinterpret_cast<const uchar*>(res.body.data()),
                            static_cast<int>(res.body.size())) ||
        image.width() < 2 * image.height()) {
        return {}; // not a banner's shape
    }
    if (image.width() > 1920) {
        image = image.scaledToWidth(1920, Qt::SmoothTransformation);
    }
    QByteArray bytes;
    QBuffer buffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    if (!image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPG", 88)) {
        return {};
    }
    return std::vector<u8>(bytes.begin(), bytes.end());
}

GamePictureLookup FindGamePictureAutomatically(const std::string& name, u64 title_id,
                                               const std::string& api_key,
                                               const std::atomic_bool& cancel) {
    GamePictureLookup lookup;
    if (api_key.empty()) {
        return lookup; // nothing to ask without a key; not a miss
    }
    constexpr int AutomaticMinScore = 750;
    auto result = RunPreferredSearch(name, title_id, api_key, cancel, 1, AutomaticMinScore);
    switch (result.status) {
    case IconSearchStatus::Found:
        lookup.png = std::move(result.icon_bytes);
        break;
    case IconSearchStatus::Choices:
        lookup.png = std::move(result.choices.front());
        break;
    case IconSearchStatus::NotFound:
        lookup.nothing_found = true;
        break;
    default:
        break;
    }
    return lookup;
}

IconSearch::IconSearch(QWidget* parent) : QObject(parent), parent_widget{parent} {
    connect(&watcher, &QFutureWatcher<IconSearchResult>::finished, this, &IconSearch::OnFinished);
}

IconSearch::~IconSearch() {
    // Never block the UI thread: the detached worker stops on its own once it sees the flag.
    cancel_flag->store(true);
}

void IconSearch::Start(const QString& name, u64 title_id) {
    if (watcher.isRunning()) {
        return;
    }
    const std::string query = name.trimmed().toStdString();
    if (query.empty() && title_id == 0) {
        QMessageBox::information(parent_widget, tr("Find Icon"),
                                 tr("Please enter a game name first so the icon can be found."));
        return;
    }
    last_name = name;
    last_title_id = title_id;
    cancel_flag->store(false);
    emit RunningChanged(true);
    // The worker captures no pointer to the dialog, so it may safely outlive it.
    watcher.setFuture(QtConcurrent::run(
        [query, title_id, api_key = UISettings::values.steamgriddb_api_key.GetValue(),
         cancel = cancel_flag] {
            return RunPreferredSearch(query, title_id, api_key, *cancel, MaxSgdbChoices,
                                      MinCandidateScore);
        }));
}

bool IconSearch::IsRunning() const {
    return watcher.isRunning();
}

int IconSearch::ChooseIcon(const std::vector<std::vector<u8>>& choices) {
    if (choices.size() == 1) {
        return 0;
    }
    QDialog dialog(parent_widget);
    dialog.setWindowTitle(tr("Choose an Icon"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(
        new QLabel(tr("SteamGridDB has several icons for this game. Pick one:"), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(128, 128));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (std::size_t i = 0; i < choices.size(); ++i) {
        QPixmap map;
        if (map.loadFromData(choices[i].data(), static_cast<uint>(choices[i].size()))) {
            auto* item = new QListWidgetItem(QIcon(map), QString::number(i + 1));
            item->setData(Qt::UserRole, static_cast<int>(i));
            list->addItem(item);
        }
    }
    if (list->count() == 0) {
        return -1;
    }
    list->setCurrentRow(0);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    layout->addWidget(list);
    layout->addWidget(buttons);
    dialog.resize(520, 420);
    if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr) {
        return -1;
    }
    return list->currentItem()->data(Qt::UserRole).toInt();
}

bool IconSearch::AskForApiKey(bool missing) {
    QString text = missing ? tr("PhoenixEmu finds game pictures on SteamGridDB, which needs a "
                                "free API key. With a key, pictures for your games also appear "
                                "on their own.")
                           : tr("SteamGridDB rejected the saved API key.");
    if (UISettings::values.steamgriddb_api_key.GetValue().empty()) {
        text += QStringLiteral("<br><br>") +
                tr("Create a free key on the <a href=\"%1\">SteamGridDB API preferences</a> page.")
                    .arg(QStringLiteral("https://www.steamgriddb.com/profile/preferences/api"));
    }
    QDialog dialog(parent_widget);
    dialog.setWindowTitle(tr("SteamGridDB API Key"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(text, &dialog);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    auto* key_edit = new QLineEdit(
        QString::fromStdString(UISettings::values.steamgriddb_api_key.GetValue()), &dialog);
    key_edit->setEchoMode(QLineEdit::Password);
    key_edit->setPlaceholderText(tr("API key"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(label);
    layout->addWidget(key_edit);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(420);
    if (dialog.exec() != QDialog::Accepted || key_edit->text().trimmed().isEmpty()) {
        return false;
    }
    UISettings::values.steamgriddb_api_key.SetValue(key_edit->text().trimmed().toStdString());
    // Titles nothing was found for without a key are worth another automatic search.
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(QtCommon::GamePictures::Folder(), ec)) {
        if (entry.path().extension() == ".none") {
            std::filesystem::remove(entry.path(), ec);
        }
    }
    return true;
}

void IconSearch::OnFinished() {
    emit RunningChanged(false);

    IconSearchResult result;
    try {
        result = watcher.result();
    } catch (const std::exception& e) {
        result.status = IconSearchStatus::InternalError;
        result.error = e.what();
    } catch (...) {
        result.status = IconSearchStatus::InternalError;
    }

    const auto select = [&](std::vector<u8> png) {
        emit IconSelected(QByteArray(reinterpret_cast<const char*>(png.data()),
                                     static_cast<qsizetype>(png.size())),
                          QString::fromStdString(result.resolved_title));
    };

    QString message;
    switch (result.status) {
    case IconSearchStatus::Found:
        select(std::move(result.icon_bytes));
        return;
    case IconSearchStatus::Choices: {
        const int index = ChooseIcon(result.choices);
        if (index >= 0) {
            select(std::move(result.choices[static_cast<std::size_t>(index)]));
        }
        return;
    }
    case IconSearchStatus::NotFound:
        message = tr("SteamGridDB has no picture for a game with this name. Try a different "
                     "name or choose a picture file.");
        break;
    case IconSearchStatus::NeedsApiKey:
    case IconSearchStatus::BadApiKey:
        if (AskForApiKey(result.status == IconSearchStatus::NeedsApiKey)) {
            Start(last_name, last_title_id);
        }
        return;
    case IconSearchStatus::Timeout:
        message = tr("Searching for an icon took longer than %1 seconds and was stopped. Check "
                     "your connection and try again.")
                      .arg(SearchDeadline.count());
        break;
    case IconSearchStatus::NetworkError:
        message = tr("Could not reach the icon sources (%1). Check your connection and try again.")
                      .arg(QString::fromStdString(result.error));
        break;
    case IconSearchStatus::Cancelled:
        return;
    case IconSearchStatus::InternalError:
        message = tr("Searching for an icon failed unexpectedly: %1")
                      .arg(QString::fromStdString(result.error));
        break;
    }
    LOG_WARNING(Frontend, "Icon search failed: {}", message.toStdString());
    QMessageBox::warning(parent_widget, tr("Find Icon"), message);
}
