// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QPainter>
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "phoenix/steam_shortcuts.h"

namespace SteamShortcuts {
namespace {

namespace fs = std::filesystem;

QString Tr(const char* text) {
    return QCoreApplication::translate("SteamShortcuts", text);
}

// ---- Steam's binary key-value format (shortcuts.vdf) ----

enum Type : unsigned char {
    Map = 0x00,
    String = 0x01,
    Int32 = 0x02,
    Float = 0x03,
    Pointer = 0x04,
    WideString = 0x05,
    Color = 0x06,
    UInt64 = 0x07,
    End = 0x08,
    Int64 = 0x0A,
};

/// One key and its value. Numbers keep their bytes as read, so what this code does not touch is
/// written back exactly.
struct Node {
    unsigned char type = Map;
    std::string key;
    std::string value; ///< A string's text, or a number's bytes
    std::vector<Node> children;

    Node* Find(std::string_view name) {
        for (Node& child : children) {
            // Steam itself has written both "appname" and "AppName" over the years.
            if (child.key.size() == name.size() &&
                std::equal(child.key.begin(), child.key.end(), name.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                })) {
                return &child;
            }
        }
        return nullptr;
    }
};

class Reader {
public:
    explicit Reader(const std::vector<char>& data_) : data{data_} {}

    /// The nodes up to the map's end marker.
    std::optional<std::vector<Node>> Children() {
        std::vector<Node> nodes;
        while (pos < data.size()) {
            const auto type = static_cast<unsigned char>(data[pos++]);
            if (type == End) {
                return nodes;
            }
            Node node;
            node.type = type;
            const auto key = CString();
            if (!key) {
                return std::nullopt;
            }
            node.key = *key;
            switch (type) {
            case Map: {
                auto children = Children();
                if (!children) {
                    return std::nullopt;
                }
                node.children = std::move(*children);
                break;
            }
            case String: {
                const auto text = CString();
                if (!text) {
                    return std::nullopt;
                }
                node.value = *text;
                break;
            }
            case Int32:
            case Float:
            case Pointer:
            case Color:
                if (!Bytes(4, node.value)) {
                    return std::nullopt;
                }
                break;
            case UInt64:
            case Int64:
                if (!Bytes(8, node.value)) {
                    return std::nullopt;
                }
                break;
            default:
                return std::nullopt; // a kind this code doesn't know: leave the file alone
            }
            nodes.push_back(std::move(node));
        }
        return std::nullopt; // no end marker
    }

private:
    std::optional<std::string> CString() {
        const auto start = data.begin() + static_cast<std::ptrdiff_t>(pos);
        const auto end = std::find(start, data.end(), '\0');
        if (end == data.end()) {
            return std::nullopt;
        }
        std::string text(start, end);
        pos += text.size() + 1;
        return text;
    }

    bool Bytes(std::size_t count, std::string& out) {
        if (pos + count > data.size()) {
            return false;
        }
        out.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                   data.begin() + static_cast<std::ptrdiff_t>(pos + count));
        pos += count;
        return true;
    }

    const std::vector<char>& data;
    std::size_t pos = 0;
};

void Write(std::string& out, const std::vector<Node>& nodes) {
    for (const Node& node : nodes) {
        out.push_back(static_cast<char>(node.type));
        out += node.key;
        out.push_back('\0');
        if (node.type == Map) {
            Write(out, node.children);
        } else {
            out += node.value;
            if (node.type == String) {
                out.push_back('\0');
            }
        }
    }
    out.push_back(static_cast<char>(End));
}

Node StringNode(std::string key, std::string text) {
    return Node{String, std::move(key), std::move(text), {}};
}

Node IntNode(std::string key, u32 number) {
    std::string bytes(4, '\0');
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<char>((number >> (8 * i)) & 0xFF);
    }
    return Node{Int32, std::move(key), std::move(bytes), {}};
}

u32 IntValue(const Node* node) {
    if (node == nullptr || node->type != Int32 || node->value.size() != 4) {
        return 0;
    }
    u32 number = 0;
    for (int i = 0; i < 4; ++i) {
        number |= static_cast<u32>(static_cast<unsigned char>(node->value[i])) << (8 * i);
    }
    return number;
}

u32 Crc32(std::string_view text) {
    static const auto table = [] {
        std::array<u32, 256> t{};
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    u32 crc = 0xFFFFFFFFU;
    for (const char ch : text) {
        crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

std::string Quoted(const std::string& text) {
    return "\"" + text + "\"";
}

/// Whether a shortcut is this game's from PhoenixEmu: its launch options name the game's file.
bool IsOurs(Node& entry, const std::string& game_path) {
    const Node* const launch = entry.Find("LaunchOptions");
    return launch != nullptr && launch->type == String &&
           launch->value.find(Quoted(game_path)) != std::string::npos;
}

/// Each Steam user's config folder (userdata/<account>/config).
std::vector<fs::path> UserConfigs(const fs::path& steam) {
    std::vector<fs::path> configs;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(steam / "userdata", error)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_directory(error) || name.empty() || name == "0" ||
            !std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        configs.push_back(entry.path() / "config");
    }
    return configs;
}

/// A user's shortcuts (the list inside "shortcuts"), read; an absent file is an empty list.
std::optional<std::vector<Node>> ReadShortcuts(const fs::path& file) {
    std::error_code error;
    if (!fs::exists(file, error)) {
        return std::vector<Node>{Node{Map, "shortcuts", {}, {}}};
    }
    std::ifstream in{file, std::ios::binary};
    std::vector<char> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (data.empty()) {
        return std::vector<Node>{Node{Map, "shortcuts", {}, {}}};
    }
    Reader reader{data};
    auto top = reader.Children();
    if (!top) {
        LOG_WARNING(Frontend, "Steam: {} could not be read; left as it is", file.string());
        return std::nullopt;
    }
    if (std::none_of(top->begin(), top->end(),
                     [](const Node& node) { return node.type == Map && node.key == "shortcuts"; })) {
        top->push_back(Node{Map, "shortcuts", {}, {}});
    }
    return top;
}

Node& ShortcutList(std::vector<Node>& top) {
    for (Node& node : top) {
        if (node.type == Map && node.key == "shortcuts") {
            return node;
        }
    }
    return top.front(); // ReadShortcuts always adds it
}

bool WriteShortcuts(const fs::path& file, const std::vector<Node>& top) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (fs::exists(file, error)) {
        // The file as it was, before each change.
        fs::copy_file(file, fs::path{file}.concat(".phoenixemu-backup"),
                      fs::copy_options::overwrite_existing, error);
    }
    std::string out;
    Write(out, top);
    // Written beside it, then moved over it, so Steam never finds half a file.
    const fs::path temporary = fs::path{file}.concat(".phoenixemu-new");
    {
        std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
        stream.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!stream) {
            return false;
        }
    }
    fs::rename(temporary, file, error);
    return !error;
}

/// The library's pictures, made from the game's icon over a darkened, enlarged copy of itself.
QImage Picture(const QImage& icon, int width, int height, double icon_share) {
    QImage canvas(width, height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor(0x1C, 0x18, 0x11));
    if (icon.isNull()) {
        return canvas;
    }
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage cover =
        icon.scaled(width, height, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawImage((width - cover.width()) / 2, (height - cover.height()) / 2, cover);
    p.fillRect(canvas.rect(), QColor(0, 0, 0, 150));
    const int side = static_cast<int>(std::min(width, height) * icon_share);
    const QImage middle = icon.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.drawImage((width - middle.width()) / 2, (height - middle.height()) / 2, middle);
    p.end();
    return canvas;
}

constexpr std::array<const char*, 5> PictureSuffixes{"p.png", ".png", "_hero.png", "_logo.png",
                                                     "_icon.png"};

void RemovePictures(const fs::path& config, u32 appid) {
    std::error_code error;
    for (const char* const suffix : PictureSuffixes) {
        fs::remove(config / "grid" / (std::to_string(appid) + suffix), error);
    }
}

} // namespace

fs::path FindSteam() {
    std::vector<fs::path> candidates;
#ifdef _WIN32
    const QSettings registry{QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                             QSettings::NativeFormat};
    const QString path = registry.value(QStringLiteral("SteamPath")).toString();
    if (!path.isEmpty()) {
        candidates.emplace_back(QDir::toNativeSeparators(path).toStdWString());
    }
    candidates.emplace_back(L"C:\\Program Files (x86)\\Steam");
#else
    const QString home = QDir::homePath();
    for (const QString& relative :
         {QStringLiteral("/.local/share/Steam"), QStringLiteral("/.steam/steam"),
          QStringLiteral("/.steam/root"),
          QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")}) {
        candidates.emplace_back(QString{home + relative}.toStdString());
    }
#endif
    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (fs::is_directory(candidate / "userdata", error)) {
            return fs::canonical(candidate, error);
        }
    }
    return {};
}

bool SteamRunning() {
#ifdef _WIN32
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    bool running = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !running;
         more = Process32NextW(snapshot, &entry)) {
        running = _wcsicmp(entry.szExeFile, L"steam.exe") == 0;
    }
    CloseHandle(snapshot);
    return running;
#else
    std::error_code error;
    for (const auto& entry : fs::directory_iterator("/proc", error)) {
        std::ifstream comm{entry.path() / "comm"};
        std::string name;
        if (comm && std::getline(comm, name) && name == "steam") {
            return true;
        }
    }
    return false;
#endif
}

bool IsAdded(const std::string& game_path) {
    const fs::path steam = FindSteam();
    if (steam.empty()) {
        return false;
    }
    for (const fs::path& config : UserConfigs(steam)) {
        auto top = ReadShortcuts(config / "shortcuts.vdf");
        if (!top) {
            continue;
        }
        for (Node& entry : ShortcutList(*top).children) {
            if (IsOurs(entry, game_path)) {
                return true;
            }
        }
    }
    return false;
}

Result Add(const std::string& game_path, const QString& title, const fs::path& exe,
           const std::string& launch, const QImage& icon) {
    const fs::path steam = FindSteam();
    if (steam.empty()) {
        return {false, Tr("Steam was not found on this PC.")};
    }
    if (SteamRunning()) {
        return {false, Tr("Close Steam first: it writes its own list of games when it closes, "
                          "so nothing is changed while it runs.")};
    }
    const std::vector<fs::path> configs = UserConfigs(steam);
    if (configs.empty()) {
        return {false, Tr("Sign in to Steam once first: no Steam user was found on this PC.")};
    }
    const std::string name = title.toStdString();
    const std::string exe_text = Quoted(Common::FS::PathToUTF8String(exe));
    const std::string start_dir = Quoted(Common::FS::PathToUTF8String(exe.parent_path()));
    // Steam's own id for a non-Steam game: its pictures are named after it.
    const u32 appid = Crc32(exe_text + name) | 0x80000000U;

    int added = 0;
    for (const fs::path& config : configs) {
        const fs::path file = config / "shortcuts.vdf";
        auto top = ReadShortcuts(file);
        if (!top) {
            continue;
        }
        Node& list = ShortcutList(*top);
        if (std::any_of(list.children.begin(), list.children.end(),
                        [&](Node& entry) { return IsOurs(entry, game_path); })) {
            continue; // there already
        }
        const fs::path grid = config / "grid";
        std::error_code error;
        fs::create_directories(grid, error);
        const std::string id = std::to_string(appid);
        const fs::path icon_file = grid / (id + "_icon.png");
        if (!icon.isNull()) {
            const auto save = [](const QImage& image, const fs::path& path) {
                image.save(QString::fromStdString(Common::FS::PathToUTF8String(path)), "PNG");
            };
            save(icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation), icon_file);
            save(Picture(icon, 600, 900, 0.8), grid / (id + "p.png"));
            save(Picture(icon, 920, 430, 0.85), grid / (id + ".png"));
            save(Picture(icon, 1920, 620, 0.7), grid / (id + "_hero.png"));
        }

        Node entry{Map, std::to_string(list.children.size()), {}, {}};
        entry.children = {
            IntNode("appid", appid),
            StringNode("AppName", name),
            StringNode("Exe", exe_text),
            StringNode("StartDir", start_dir),
            StringNode("icon", icon.isNull() ? std::string{}
                                             : Common::FS::PathToUTF8String(icon_file)),
            StringNode("ShortcutPath", ""),
            StringNode("LaunchOptions", launch),
            IntNode("IsHidden", 0),
            IntNode("AllowDesktopConfig", 1),
            IntNode("AllowOverlay", 1),
            IntNode("OpenVR", 0),
            IntNode("Devkit", 0),
            StringNode("DevkitGameID", ""),
            IntNode("DevkitOverrideAppID", 0),
            IntNode("LastPlayTime", 0),
            StringNode("FlatpakAppID", ""),
            Node{Map, "tags", {}, {StringNode("0", "PhoenixEmu")}},
        };
        list.children.push_back(std::move(entry));
        if (!WriteShortcuts(file, *top)) {
            LOG_ERROR(Frontend, "Steam: {} could not be written", file.string());
            continue;
        }
        ++added;
    }
    if (added == 0) {
        return IsAdded(game_path)
                   ? Result{true, Tr("%1 is already in your Steam library.").arg(title)}
                   : Result{false, Tr("Steam's list of games could not be changed.")};
    }
    LOG_INFO(Frontend, "Steam: {} added for {} user(s)", name, added);
    return {true, Tr("%1 is in your Steam library now. Start Steam to find it there.").arg(title)};
}

Result Remove(const std::string& game_path) {
    const fs::path steam = FindSteam();
    if (steam.empty()) {
        return {false, Tr("Steam was not found on this PC.")};
    }
    if (SteamRunning()) {
        return {false, Tr("Close Steam first: it writes its own list of games when it closes, "
                          "so nothing is changed while it runs.")};
    }
    int removed = 0;
    for (const fs::path& config : UserConfigs(steam)) {
        const fs::path file = config / "shortcuts.vdf";
        auto top = ReadShortcuts(file);
        if (!top) {
            continue;
        }
        Node& list = ShortcutList(*top);
        const auto before = list.children.size();
        std::erase_if(list.children, [&](Node& entry) {
            if (!IsOurs(entry, game_path)) {
                return false;
            }
            RemovePictures(config, IntValue(entry.Find("appid")));
            return true;
        });
        if (list.children.size() == before) {
            continue;
        }
        // Steam numbers its shortcuts from 0 with no gaps.
        for (std::size_t i = 0; i < list.children.size(); ++i) {
            list.children[i].key = std::to_string(i);
        }
        if (WriteShortcuts(file, *top)) {
            ++removed;
        }
    }
    return removed > 0 ? Result{true, Tr("The game is out of your Steam library.")}
                       : Result{false, Tr("The game was not in your Steam library.")};
}

} // namespace SteamShortcuts
