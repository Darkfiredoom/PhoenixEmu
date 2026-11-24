// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "phoenix/album_dialog.h"

#include <algorithm>
#include <filesystem>
#include <map>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "phoenix/game/game_list.h"

namespace {

QString FormatBytes(u64 bytes) {
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString PathString(const std::filesystem::path& path) {
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

/// A capture in the grid: its picture with rounded corners, an ember edge when picked.
class Thumb final : public QAbstractButton {
public:
    Thumb(const QPixmap& picture_, QWidget* parent) : QAbstractButton(parent), picture{picture_} {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        QPainterPath shape;
        shape.addRoundedRect(r, 10, 10);
        p.fillPath(shape, QColor(0x1A, 0x15, 0x12));
        if (!picture.isNull()) {
            p.save();
            p.setClipPath(shape);
            // Cropped to fill, centred.
            const QSizeF source = picture.size();
            const qreal scale = std::max(r.width() / source.width(), r.height() / source.height());
            const QSizeF shown = source * scale;
            p.drawPixmap(QRectF(r.center().x() - shown.width() / 2, r.center().y() - shown.height() / 2,
                                shown.width(), shown.height()),
                         picture, QRectF(QPointF(0, 0), source));
            p.restore();
        }
        if (isChecked()) {
            p.setPen(QPen(QColor(0xFF, 0x5A, 0x1F), 3));
        } else if (underMouse()) {
            p.setPen(QPen(QColor(0xFF, 0xC2, 0x3D, 160), 2));
        } else {
            p.setPen(QPen(QColor(0x2A, 0x21, 0x1B), 1));
        }
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 10, 10);
    }

private:
    QPixmap picture;
};

/// The day group a capture belongs to.
QString DayGroup(const QDate& day) {
    const QDate today = QDate::currentDate();
    const qint64 days = day.daysTo(today);
    if (days <= 0) {
        return QCoreApplication::translate("AlbumDialog", "Today");
    }
    if (days == 1) {
        return QCoreApplication::translate("AlbumDialog", "Yesterday");
    }
    if (days < 7) {
        return QCoreApplication::translate("AlbumDialog", "This week");
    }
    return QLocale().toString(day, QStringLiteral("MMMM yyyy"));
}

QString WhenText(const QDateTime& taken) {
    const QString time = QLocale().toString(taken.time(), QLocale::ShortFormat);
    const qint64 days = taken.date().daysTo(QDate::currentDate());
    if (days <= 0) {
        return QCoreApplication::translate("AlbumDialog", "Today, %1").arg(time);
    }
    if (days == 1) {
        return QCoreApplication::translate("AlbumDialog", "Yesterday, %1").arg(time);
    }
    return QLocale().toString(taken.date(), QLocale::ShortFormat) + QStringLiteral(", ") + time;
}

} // namespace

AlbumDialog::AlbumDialog(QWidget* parent, GameList* game_list)
    : QDialog(parent), m_game_list{game_list} {
    setWindowTitle(tr("Album"));
    resize(1240, 780);
    setObjectName(QStringLiteral("Album"));
    setStyleSheet(QStringLiteral(R"(
        QDialog#Album { background: #0E0B09; }
        QDialog#Album QLabel { background: transparent; color: #F6EBDD; }
        QLabel#AlbumTitle { font-size: 26px; font-weight: 900; }
        QLabel#AlbumMuted { color: #A8998A; font-size: 13px; }
        QLabel#AlbumGroup { font-size: 16px; font-weight: 900; }
        QLabel#AlbumGame { font-size: 17px; font-weight: 900; }
        QLabel#AlbumKey { color: #A8998A; font-size: 13px; }
        QLabel#AlbumValue { font-size: 13px; font-weight: 700; }
        QLabel#AlbumNote { color: #FFC23D; font-size: 12px; }
        QLabel#AlbumPreview { background: #120E0B; border: 1px solid #2A211B; border-radius: 14px; }
        QPushButton#AlbumPill { background: #1A1512; border: 1px solid #2A211B; border-radius: 16px; padding: 6px 14px;
            color: #D9CBBB; font-weight: 800; min-width: 0px; min-height: 0px; }
        QPushButton#AlbumPill:hover { border: 1px solid #FF5A1F; }
        QPushButton#AlbumPill:checked { background: #F6EBDD; color: #1A1512; border: 1px solid #F6EBDD; }
        QFrame#AlbumPanel { background: #15110E; border: 1px solid #2A211B; border-radius: 18px; }
        QFrame#AlbumFacts { background: #0E0B09; border: 1px solid #2A211B; border-radius: 12px; }
        QFrame#AlbumFactLine { background: #2A211B; max-height: 1px; min-height: 1px; border: none; }
        QPushButton#AlbumButton { background: #2A211B; border: 1px solid #3A2E25; border-radius: 12px;
            padding: 9px 16px; color: #F6EBDD; font-weight: 800; min-width: 0px; }
        QPushButton#AlbumButton:hover { border: 1px solid #FF5A1F; }
        QPushButton#AlbumButton:disabled { color: #6B5F55; border: 1px solid #2A211B; }
        QPushButton#AlbumDanger { background: #2A211B; border: 1px solid #5A2A1C; border-radius: 12px;
            padding: 9px 16px; color: #FFB59A; font-weight: 800; min-width: 0px; }
        QPushButton#AlbumDanger:hover { background: #5A2A1C; color: #FFFFFF; }
        QPushButton#AlbumDanger:disabled { color: #6B5F55; border: 1px solid #2A211B; }
        QComboBox#AlbumOrder { background: #1A1512; border: 1px solid #3A2E25; border-radius: 16px;
            padding: 6px 14px; color: #F6EBDD; font-weight: 700; }
        QScrollArea#AlbumScroll { background: transparent; border: none; }
        QWidget#AlbumGrid { background: transparent; }
    )"));

    const auto make_button = [this](const QString& text, const char* style) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(style));
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };

    // The top: the name, how many and how big, the order, a refresh.
    auto* title = new QLabel(tr("Album"), this);
    title->setObjectName(QStringLiteral("AlbumTitle"));
    m_count_label = new QLabel(this);
    m_count_label->setObjectName(QStringLiteral("AlbumMuted"));
    m_order = new QComboBox(this);
    m_order->setObjectName(QStringLiteral("AlbumOrder"));
    m_order->addItems({tr("Newest first"), tr("Oldest first")});
    auto* refresh = make_button(tr("Refresh"), "AlbumButton");
    auto* top_row = new QHBoxLayout;
    top_row->setSpacing(14);
    top_row->addWidget(title);
    top_row->addWidget(m_count_label, 0, Qt::AlignVCenter);
    top_row->addStretch(1);
    top_row->addWidget(m_order);
    top_row->addWidget(refresh);

    // The game pills.
    m_pill_row = new QHBoxLayout;
    m_pill_row->setSpacing(8);
    m_pill_group = new QButtonGroup(this);
    m_pill_group->setExclusive(true);

    // The grid, grouped by day.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("AlbumScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_grid = new QWidget;
    m_grid->setObjectName(QStringLiteral("AlbumGrid"));
    m_grid_layout = new QVBoxLayout(m_grid);
    m_grid_layout->setContentsMargins(0, 0, 8, 0);
    m_grid_layout->setSpacing(10);
    m_scroll->setWidget(m_grid);
    m_scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* left = new QVBoxLayout;
    left->setSpacing(12);
    left->addLayout(m_pill_row);
    left->addWidget(m_scroll, 1);

    // The picked capture, large, with its game, details and what can be done with it.
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("AlbumPanel"));
    panel->setFixedWidth(500);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(18, 18, 18, 18);
    panel_layout->setSpacing(12);
    m_preview = new QLabel(panel);
    m_preview->setObjectName(QStringLiteral("AlbumPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFixedHeight(260);
    panel_layout->addWidget(m_preview);
    auto* game_row = new QHBoxLayout;
    game_row->setSpacing(10);
    m_game_icon = new QLabel(panel);
    m_game_icon->setFixedSize(40, 40);
    m_game_icon->setScaledContents(true);
    auto* game_text = new QVBoxLayout;
    game_text->setSpacing(0);
    m_game_name = new QLabel(panel);
    m_game_name->setObjectName(QStringLiteral("AlbumGame"));
    m_meta = new QLabel(panel);
    m_meta->setObjectName(QStringLiteral("AlbumMuted"));
    game_text->addWidget(m_game_name);
    game_text->addWidget(m_meta);
    game_row->addWidget(m_game_icon);
    game_row->addLayout(game_text, 1);
    panel_layout->addLayout(game_row);
    m_copy_btn = make_button(tr("Copy"), "AlbumButton");
    m_folder_btn = make_button(tr("Show in folder"), "AlbumButton");
    m_banner_btn = make_button(tr("Use as banner"), "AlbumButton");
    m_banner_btn->setToolTip(tr("Show this picture behind the game on the home screen"));
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(m_copy_btn);
    actions->addWidget(m_folder_btn);
    actions->addWidget(m_banner_btn);
    actions->addStretch(1);
    panel_layout->addLayout(actions);
    auto* facts = new QFrame(panel);
    facts->setObjectName(QStringLiteral("AlbumFacts"));
    auto* facts_layout = new QVBoxLayout(facts);
    facts_layout->setContentsMargins(14, 4, 14, 4);
    facts_layout->setSpacing(0);
    const auto add_fact = [&](const QString& key, bool line_before) {
        if (line_before) {
            auto* line = new QFrame(facts);
            line->setObjectName(QStringLiteral("AlbumFactLine"));
            facts_layout->addWidget(line);
        }
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 9, 0, 9);
        auto* name = new QLabel(key, facts);
        name->setObjectName(QStringLiteral("AlbumKey"));
        auto* value = new QLabel(facts);
        value->setObjectName(QStringLiteral("AlbumValue"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(name);
        row->addWidget(value, 1);
        facts_layout->addLayout(row);
        return value;
    };
    m_fact_game = add_fact(tr("Title ID"), false);
    m_fact_file = add_fact(tr("File"), true);
    m_fact_format = add_fact(tr("Format"), true);
    panel_layout->addWidget(facts);
    m_note = new QLabel(panel);
    m_note->setObjectName(QStringLiteral("AlbumNote"));
    m_note->setWordWrap(true);
    panel_layout->addWidget(m_note);
    panel_layout->addStretch(1);
    auto* bottom = new QHBoxLayout;
    auto* hint = new QLabel(tr("Capture in a game to add pictures here."), panel);
    hint->setObjectName(QStringLiteral("AlbumMuted"));
    m_delete_btn = make_button(tr("Delete"), "AlbumDanger");
    bottom->addWidget(hint, 1);
    bottom->addWidget(m_delete_btn);
    panel_layout->addLayout(bottom);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    body->addLayout(left, 1);
    body->addWidget(panel);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 24, 22);
    root->setSpacing(14);
    root->addLayout(top_row);
    root->addLayout(body, 1);

    connect(refresh, &QPushButton::clicked, this, &AlbumDialog::Reload);
    connect(m_order, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_newest_first = index == 0;
        RebuildGrid();
    });
    connect(m_pill_group, &QButtonGroup::idClicked, this, [this](int id) {
        m_filter = id <= 0 ? 0 : m_games[static_cast<size_t>(id - 1)].first;
        RebuildGrid();
    });
    connect(m_copy_btn, &QPushButton::clicked, this, &AlbumDialog::CopyToClipboard);
    connect(m_folder_btn, &QPushButton::clicked, this, &AlbumDialog::ShowInFolder);
    connect(m_banner_btn, &QPushButton::clicked, this, &AlbumDialog::UseAsBanner);
    connect(m_delete_btn, &QPushButton::clicked, this, &AlbumDialog::DeleteCurrent);

    Reload();
}

AlbumDialog::~AlbumDialog() = default;

AlbumDialog::Game AlbumDialog::GameOf(u64 program_id) {
    for (const auto& [id, game] : m_games) {
        if (id == program_id) {
            return game;
        }
    }
    Game game;
    if (m_game_list != nullptr && program_id != 0) {
        auto [name, picture] = m_game_list->GameNameAndPicture(program_id);
        game.name = name;
        game.picture = picture;
    }
    if (game.name.isEmpty()) {
        game.name = program_id != 0 ? QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper()
                                    : tr("Other pictures");
    }
    m_games.emplace_back(program_id, game);
    return game;
}

void AlbumDialog::Reload() {
    m_captures.clear();
    m_games.clear();
    m_current = -1;
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ScreenshotsDir);
    std::error_code ec;
    // <title id>_<yyyy-MM-dd_hh-mm-ss-zzz>.png, as MainWindow::OnCaptureScreenshot names them.
    static const QRegularExpression pattern{
        QStringLiteral("^([0-9a-fA-F]{16})_(\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2})")};
    u64 total = 0;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const QString ext = PathString(entry.path().extension()).toLower();
            if (ext != QStringLiteral(".png") && ext != QStringLiteral(".jpg") &&
                ext != QStringLiteral(".jpeg")) {
                continue;
            }
            Capture capture;
            capture.path = entry.path();
            capture.filename = PathString(entry.path().filename());
            capture.file_size = entry.file_size(ec);
            total += capture.file_size;
            const auto match = pattern.match(capture.filename);
            if (match.hasMatch()) {
                capture.program_id = match.captured(1).toULongLong(nullptr, 16);
                capture.taken = QDateTime::fromString(match.captured(2),
                                                      QStringLiteral("yyyy-MM-dd_hh-mm-ss"));
            }
            if (!capture.taken.isValid()) {
                capture.taken = QFileInfo{PathString(capture.path)}.lastModified();
            }
            QImageReader reader{PathString(capture.path)};
            capture.resolution = reader.size();
            if (capture.resolution.isValid()) {
                reader.setScaledSize(capture.resolution.scaled(QSize(400, 400), Qt::KeepAspectRatio));
            }
            capture.thumbnail = QPixmap::fromImageReader(&reader);
            GameOf(capture.program_id);
            m_captures.push_back(std::move(capture));
        }
    }
    m_count_label->setText(tr("%n capture(s)", nullptr, static_cast<int>(m_captures.size())) +
                           (total > 0 ? QStringLiteral(" · ") + FormatBytes(total) : QString{}));
    // Games named as the list names them, in name order.
    std::sort(m_games.begin(), m_games.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.second.name, b.second.name) < 0;
    });
    if (std::none_of(m_games.begin(), m_games.end(),
                     [this](const auto& game) { return game.first == m_filter; })) {
        m_filter = 0;
    }
    RebuildPills();
    RebuildGrid();
}

void AlbumDialog::RebuildPills() {
    for (QAbstractButton* pill : m_pill_group->buttons()) {
        m_pill_group->removeButton(pill);
        pill->deleteLater();
    }
    while (QLayoutItem* item = m_pill_row->takeAt(0)) {
        delete item;
    }
    const auto add_pill = [this](const QString& text, const QPixmap& icon, int id, bool on) {
        auto* pill = new QPushButton(text, this);
        pill->setObjectName(QStringLiteral("AlbumPill"));
        pill->setCheckable(true);
        pill->setChecked(on);
        pill->setCursor(Qt::PointingHandCursor);
        if (!icon.isNull()) {
            pill->setIcon(QIcon(icon));
            pill->setIconSize(QSize(20, 20));
        }
        m_pill_group->addButton(pill, id);
        m_pill_row->addWidget(pill);
    };
    add_pill(tr("All games"), {}, 0, m_filter == 0);
    for (size_t i = 0; i < m_games.size(); ++i) {
        add_pill(m_games[i].second.name, m_games[i].second.picture, static_cast<int>(i + 1),
                 m_filter != 0 && m_games[i].first == m_filter);
    }
    m_pill_row->addStretch(1);
}

std::vector<int> AlbumDialog::Shown() const {
    std::vector<int> shown;
    for (size_t i = 0; i < m_captures.size(); ++i) {
        if (m_filter == 0 || m_captures[i].program_id == m_filter) {
            shown.push_back(static_cast<int>(i));
        }
    }
    std::stable_sort(shown.begin(), shown.end(), [this](int a, int b) {
        return m_newest_first ? m_captures[a].taken > m_captures[b].taken
                              : m_captures[a].taken < m_captures[b].taken;
    });
    return shown;
}

void AlbumDialog::RebuildGrid() {
    m_thumbs.clear();
    while (QLayoutItem* item = m_grid_layout->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    // As many columns as fit, each picture 16:9.
    const int width = std::max(300, m_scroll->viewport()->width() - 12);
    m_columns = std::max(2, (width + 10) / 220);
    const int thumb_w = (width - (m_columns - 1) * 10) / m_columns;
    const QSize thumb_size{thumb_w, thumb_w * 9 / 16};

    const std::vector<int> shown = Shown();
    if (shown.empty()) {
        auto* empty = new QLabel(m_captures.empty()
                                     ? tr("No captures yet. Press Capture in a game (or its key) "
                                          "and the picture shows here.")
                                     : tr("No captures of this game."),
                                 m_grid);
        empty->setObjectName(QStringLiteral("AlbumMuted"));
        empty->setWordWrap(true);
        m_grid_layout->addWidget(empty);
    }
    size_t i = 0;
    while (i < shown.size()) {
        const QString group = DayGroup(m_captures[shown[i]].taken.date());
        size_t end = i;
        while (end < shown.size() && DayGroup(m_captures[shown[end]].taken.date()) == group) {
            ++end;
        }
        auto* heading = new QLabel(m_grid);
        heading->setObjectName(QStringLiteral("AlbumGroup"));
        heading->setText(QStringLiteral("%1 &nbsp;<span style='font-size:12px; font-weight:600; "
                                        "color:#A8998A'>%2</span>")
                             .arg(group.toHtmlEscaped(),
                                  tr("%n capture(s)", nullptr, static_cast<int>(end - i))));
        m_grid_layout->addWidget(heading);
        auto* cells = new QWidget(m_grid);
        auto* grid = new QGridLayout(cells);
        grid->setContentsMargins(0, 0, 0, 6);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);
        for (size_t k = i; k < end; ++k) {
            const int index = shown[k];
            auto* thumb = new Thumb(m_captures[index].thumbnail, cells);
            thumb->setFixedSize(thumb_size);
            thumb->setProperty("capture", index);
            thumb->setChecked(index == m_current);
            thumb->setToolTip(WhenText(m_captures[index].taken));
            connect(thumb, &QAbstractButton::clicked, this, [this, index] { Select(index); });
            const int n = static_cast<int>(k - i);
            grid->addWidget(thumb, n / m_columns, n % m_columns, Qt::AlignLeft | Qt::AlignTop);
            m_thumbs.push_back(thumb);
        }
        grid->setColumnStretch(m_columns, 1);
        m_grid_layout->addWidget(cells);
        i = end;
    }
    m_grid_layout->addStretch(1);
    const bool current_shown = std::find(shown.begin(), shown.end(), m_current) != shown.end();
    Select(current_shown ? m_current : (shown.empty() ? -1 : shown.front()));
}

void AlbumDialog::Select(int index) {
    m_current = index;
    for (QWidget* widget : m_thumbs) {
        auto* thumb = static_cast<QAbstractButton*>(widget);
        thumb->setChecked(thumb->property("capture").toInt() == index);
        if (thumb->isChecked()) {
            m_scroll->ensureWidgetVisible(thumb, 10, 10);
        }
    }
    m_note->clear();
    const bool valid = index >= 0 && index < static_cast<int>(m_captures.size());
    for (QPushButton* button : {m_copy_btn, m_folder_btn, m_banner_btn, m_delete_btn}) {
        button->setEnabled(valid);
    }
    if (!valid) {
        m_preview->clear();
        m_preview->setText(tr("Nothing picked"));
        m_game_icon->clear();
        m_game_name->clear();
        m_meta->clear();
        m_fact_game->clear();
        m_fact_file->clear();
        m_fact_format->clear();
        return;
    }
    const Capture& capture = m_captures[static_cast<size_t>(index)];
    QImageReader reader{PathString(capture.path)};
    const QSize box = m_preview->size() - QSize(4, 4);
    if (capture.resolution.isValid()) {
        reader.setScaledSize(capture.resolution.scaled(box * devicePixelRatioF(), Qt::KeepAspectRatio));
    }
    QPixmap large = QPixmap::fromImageReader(&reader);
    large.setDevicePixelRatio(devicePixelRatioF());
    m_preview->setPixmap(large);
    const Game game = GameOf(capture.program_id);
    m_game_icon->setPixmap(game.picture);
    m_game_icon->setVisible(!game.picture.isNull());
    m_game_name->setText(game.name);
    QStringList meta{WhenText(capture.taken)};
    if (capture.resolution.isValid()) {
        meta << QStringLiteral("%1 × %2").arg(capture.resolution.width()).arg(capture.resolution.height());
    }
    meta << FormatBytes(capture.file_size);
    m_meta->setText(meta.join(QStringLiteral(" · ")));
    m_fact_game->setText(capture.program_id != 0
                             ? QStringLiteral("%1").arg(capture.program_id, 16, 16, QLatin1Char('0')).toUpper()
                             : tr("Unknown"));
    m_fact_file->setText(
        m_fact_file->fontMetrics().elidedText(capture.filename, Qt::ElideMiddle, 300));
    m_fact_file->setToolTip(PathString(capture.path));
    m_fact_format->setText(PathString(capture.path.extension()).mid(1).toUpper());
    m_banner_btn->setEnabled(capture.program_id != 0 && m_game_list != nullptr);
}

void AlbumDialog::Note(const QString& text) {
    m_note->setText(text);
}

void AlbumDialog::CopyToClipboard() {
    if (m_current < 0) {
        return;
    }
    const QImage image{PathString(m_captures[static_cast<size_t>(m_current)].path)};
    if (!image.isNull()) {
        QGuiApplication::clipboard()->setImage(image);
        Note(tr("Copied. Paste it anywhere."));
    }
}

void AlbumDialog::ShowInFolder() {
    if (m_current < 0) {
        return;
    }
    const QString path = PathString(m_captures[static_cast<size_t>(m_current)].path);
#ifdef _WIN32
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo{path}.absolutePath()));
#endif
}

void AlbumDialog::UseAsBanner() {
    if (m_current < 0 || m_game_list == nullptr) {
        return;
    }
    const Capture& capture = m_captures[static_cast<size_t>(m_current)];
    if (m_game_list->SetBannerImage(capture.program_id, QImage(PathString(capture.path)))) {
        Note(tr("It is %1's banner on the home screen now.").arg(GameOf(capture.program_id).name));
    } else {
        Note(tr("The banner could not be saved."));
    }
}

void AlbumDialog::DeleteCurrent() {
    if (m_current < 0) {
        return;
    }
    const Capture& capture = m_captures[static_cast<size_t>(m_current)];
    const auto answer = QMessageBox::question(
        this, tr("Delete Capture"),
        tr("Delete %1 from the album? It is removed from the screenshots folder.").arg(capture.filename),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    // The next one along is picked after it.
    const std::vector<int> shown = Shown();
    const auto it = std::find(shown.begin(), shown.end(), m_current);
    QString next_name;
    if (it != shown.end() && it + 1 != shown.end()) {
        next_name = m_captures[static_cast<size_t>(*(it + 1))].filename;
    } else if (it != shown.end() && it != shown.begin()) {
        next_name = m_captures[static_cast<size_t>(*(it - 1))].filename;
    }
    std::error_code ec;
    std::filesystem::remove(capture.path, ec);
    Reload();
    for (size_t i = 0; i < m_captures.size(); ++i) {
        if (m_captures[i].filename == next_name) {
            Select(static_cast<int>(i));
            break;
        }
    }
}

void AlbumDialog::keyPressEvent(QKeyEvent* event) {
    const std::vector<int> shown = Shown();
    const auto it = std::find(shown.begin(), shown.end(), m_current);
    const int position = it == shown.end() ? -1 : static_cast<int>(it - shown.begin());
    int step = 0;
    switch (event->key()) {
    case Qt::Key_Left:
        step = -1;
        break;
    case Qt::Key_Right:
        step = 1;
        break;
    case Qt::Key_Up:
        step = -m_columns;
        break;
    case Qt::Key_Down:
        step = m_columns;
        break;
    case Qt::Key_Delete:
        DeleteCurrent();
        return;
    default:
        QDialog::keyPressEvent(event);
        return;
    }
    if (!shown.empty()) {
        const int next = std::clamp(position + step, 0, static_cast<int>(shown.size()) - 1);
        Select(shown[static_cast<size_t>(next)]);
    }
}

void AlbumDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    // The columns follow the width (once the resize settles).
    const int width = std::max(300, m_scroll->viewport()->width() - 12);
    if (std::max(2, (width + 10) / 220) != m_columns || !m_thumbs.empty()) {
        QTimer::singleShot(0, this, [this] { RebuildGrid(); });
    }
}
