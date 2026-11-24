// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <map>
#include <numeric>

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "input_common/drivers/virtual_amiibo.h"
#include "input_common/main.h"
#include "phoenix/amiibo_library.h"
#include "phoenix/util/controller_navigation.h"
#include "qt_common/config/uisettings.h"

namespace {

using State = InputCommon::VirtualAmiibo::State;
using Info = InputCommon::VirtualAmiibo::Info;

// The sizes VirtualAmiibo::LoadAmiibo accepts: NTAG215 dumps with and without the password
// pages or a signature, and MIFARE Classic 1K.
constexpr qint64 AmiiboSize = 0x21C;
constexpr qint64 MifareSize = 0x400;
bool IsTagSize(qint64 size) {
    return size == AmiiboSize || size == AmiiboSize - 0x8 || size == AmiiboSize + 0x20 ||
           size == MifareSize;
}

QString CanonicalPath(const QString& path) {
    const QFileInfo info{path};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

// The figure's model info sits unencrypted on pages 0x15-0x16 of an NTAG215 dump.
QString AmiiboId(const QString& path) {
    static QHash<QString, QString> cache;
    if (const auto it = cache.constFind(path); it != cache.constEnd()) {
        return *it;
    }
    QString id;
    QFile file{path};
    if (file.open(QIODevice::ReadOnly) && file.size() != MifareSize && IsTagSize(file.size()) &&
        file.seek(0x54)) {
        id = QString::fromLatin1(file.read(8).toHex());
    }
    if (file.isOpen()) {
        cache.insert(path, id);
    }
    return id;
}

struct AmiiboInfo {
    QString name;
    QString image;
    QString series;
};

// dist/amiibo_table.json, made by tools/amiibo/make_amiibo_table.py: names and series from
// AmiiboAPI, pictures from amiipedia.
const QHash<QString, AmiiboInfo>& Table() {
    static const QHash<QString, AmiiboInfo> table = [] {
        QHash<QString, AmiiboInfo> result;
        QFile file{QStringLiteral(":/amiibo/amiibo_table.json")};
        if (!file.open(QIODevice::ReadOnly)) {
            return result;
        }
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QJsonArray entry = it.value().toArray();
            result.insert(it.key(), {entry.at(0).toString(), entry.at(1).toString(),
                                     entry.at(2).toString()});
        }
        return result;
    }();
    return table;
}

AmiiboInfo Lookup(const QString& path) {
    return Table().value(AmiiboId(path));
}

QString ImagePath(const QString& image) {
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "amiibo_images";
    return QString::fromStdString(Common::FS::PathToUTF8String(dir)) + QLatin1Char('/') + image;
}

UISettings::AmiiboEntry* FindEntry(const QString& path) {
    auto& library = UISettings::values.amiibo_library;
    const auto it = std::find_if(library.begin(), library.end(),
                                 [&](const UISettings::AmiiboEntry& e) { return e.path == path; });
    return it == library.end() ? nullptr : &*it;
}

QString ViewKey(const QString& kind, const QString& name) {
    return kind + QLatin1Char(':') + name;
}

constexpr int IconSize = 96;
constexpr int PathRole = Qt::UserRole;
constexpr int ImageRole = Qt::UserRole + 1;
constexpr int NameRole = Qt::UserRole + 2;
constexpr int SeriesRole = Qt::UserRole + 3;
constexpr int FavoriteRole = Qt::UserRole + 4;
constexpr int ExistsRole = Qt::UserRole + 5;
constexpr int OnReaderRole = Qt::UserRole + 6;
constexpr int HasPictureRole = Qt::UserRole + 7;
const QString FavoriteMark = QStringLiteral("★ ");
const QSize CardSize{176, 214};

// The kiln palette (dist/qt_themes/phoenix/style.qss).
const QColor CardColor{0x1A, 0x15, 0x12};
const QColor CardHover{0x22, 0x1B, 0x16};
const QColor LineColor{0x3A, 0x2E, 0x25};
const QColor TextColor{0xF6, 0xEB, 0xDD};
const QColor MutedColor{0xA8, 0x99, 0x8A};
const QColor EmberColor{0xFF, 0x5A, 0x1F};
const QColor GoldColor{0xFF, 0xC2, 0x3D};
const QColor GoodColor{0x7F, 0xD4, 0x8A};

/// A figure's outline, for one whose picture has not arrived (or has none).
void PaintSilhouette(QPainter& p, const QRectF& area) {
    const qreal s = std::min(area.width(), area.height());
    const QPointF c = area.center();
    p.setPen(Qt::NoPen);
    p.setBrush(LineColor);
    p.drawEllipse(QPointF(c.x(), c.y() - s * 0.2), s * 0.13, s * 0.13);
    p.drawRoundedRect(QRectF(c.x() - s * 0.17, c.y() - s * 0.04, s * 0.34, s * 0.3), s * 0.08,
                      s * 0.08);
    p.drawRoundedRect(QRectF(c.x() - s * 0.24, c.y() + s * 0.27, s * 0.48, s * 0.07), s * 0.03,
                      s * 0.03);
}

/// A badge in a card's corner.
void PaintBadge(QPainter& p, const QPointF& top_right, const QString& text, const QColor& color) {
    QFont font = p.font();
    font.setPixelSize(11);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    const qreal w = QFontMetricsF(font).horizontalAdvance(text) + 14;
    const QRectF pill{top_right.x() - w, top_right.y(), w, 20};
    QColor fill = color;
    fill.setAlpha(46);
    p.setPen(QPen(color, 1));
    p.setBrush(fill);
    p.drawRoundedRect(pill, 10, 10);
    p.setPen(color.lighter(115));
    p.drawText(pill, Qt::AlignCenter, text);
}

/// A figure on its card: its picture on a little glow, its name and series, and badges for a
/// favorite, the one on the reader and a file that has gone.
class FigureCard final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return CardSize;
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->setRenderHint(QPainter::TextAntialiasing);
        const QRectF card = QRectF(option.rect).adjusted(6, 6, -6, -6);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool on_reader = index.data(OnReaderRole).toBool();
        const bool exists = index.data(ExistsRole).toBool();
        p->setPen(selected    ? QPen(EmberColor, 2)
                  : on_reader ? QPen(GoodColor, 2)
                              : QPen(QColor(0x2A, 0x21, 0x1B), 1));
        p->setBrush(hovered || selected ? CardHover : CardColor);
        p->drawRoundedRect(card, 16, 16);

        // The picture, standing on a warm glow.
        const QRectF art = card.adjusted(16, 16, -16, -64);
        QRadialGradient glow{QPointF(art.center().x(), art.bottom() - 6), art.width() * 0.5};
        glow.setColorAt(0.0, QColor(0xFF, 0x9A, 0x3D, 58));
        glow.setColorAt(1.0, QColor(0xFF, 0x9A, 0x3D, 0));
        p->setPen(Qt::NoPen);
        p->setBrush(glow);
        p->drawEllipse(QPointF(art.center().x(), art.bottom() - 6), art.width() * 0.5,
                       art.width() * 0.12);
        if (index.data(HasPictureRole).toBool()) {
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            const qreal dpr = p->device()->devicePixelRatioF();
            const QSize px = (art.size() * dpr).toSize();
            const QPixmap picture =
                icon.pixmap(px).scaled(px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QSizeF shown = QSizeF(picture.size()) / dpr;
            p->setOpacity(exists ? 1.0 : 0.4);
            p->drawPixmap(QRectF(art.center().x() - shown.width() / 2, art.bottom() - shown.height(),
                                 shown.width(), shown.height()),
                          picture, QRectF(picture.rect()));
            p->setOpacity(1.0);
        } else {
            PaintSilhouette(*p, art);
        }

        // Its name and series. "Young Link - Ocarina of Time" shows as the name, and the part
        // after the dash where the series would go.
        QString name = index.data(NameRole).toString();
        QString subtitle = index.data(SeriesRole).toString();
        if (const qsizetype dash = name.indexOf(QStringLiteral(" - ")); dash > 0) {
            subtitle = name.mid(dash + 3);
            name = name.left(dash);
        }
        QFont font = option.font;
        font.setPixelSize(14);
        font.setWeight(QFont::Black);
        p->setFont(font);
        p->setPen(exists ? TextColor : MutedColor);
        const QRectF name_rect{card.left() + 10, card.bottom() - 54, card.width() - 20, 20};
        p->drawText(name_rect, Qt::AlignCenter,
                    QFontMetrics(font).elidedText(name, Qt::ElideRight,
                                                  static_cast<int>(name_rect.width())));
        font.setPixelSize(12);
        font.setWeight(QFont::Medium);
        p->setFont(font);
        p->setPen(MutedColor);
        const QRectF series_rect{card.left() + 10, card.bottom() - 32, card.width() - 20, 18};
        p->drawText(series_rect, Qt::AlignCenter,
                    QFontMetrics(font).elidedText(subtitle, Qt::ElideRight,
                                                  static_cast<int>(series_rect.width())));

        // Badges.
        const QPointF corner{card.right() - 10, card.top() + 10};
        if (!exists) {
            PaintBadge(*p, corner, QCoreApplication::translate("AmiiboLibraryDialog", "Missing"),
                       EmberColor);
        } else if (on_reader) {
            PaintBadge(*p, corner, QCoreApplication::translate("AmiiboLibraryDialog", "On reader"),
                       GoodColor);
        }
        if (index.data(FavoriteRole).toBool()) {
            QFont star = option.font;
            star.setPixelSize(17);
            p->setFont(star);
            p->setPen(GoldColor);
            p->drawText(QRectF(card.left() + 12, card.top() + 8, 22, 22), Qt::AlignCenter,
                        QStringLiteral("★"));
        }
        p->restore();
    }
};

/// A figure's picture large, or its outline.
QPixmap LargePicture(const QIcon& icon, bool has_picture, int size, qreal dpr) {
    QPixmap canvas(QSize(size, size) * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF area{0, 0, static_cast<qreal>(size), static_cast<qreal>(size)};
    if (has_picture) {
        const QSize px = QSize(size, size) * dpr;
        const QPixmap picture =
            icon.pixmap(px).scaled(px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QSizeF shown = QSizeF(picture.size()) / dpr;
        p.drawPixmap(QRectF(area.center().x() - shown.width() / 2, area.bottom() - shown.height(),
                            shown.width(), shown.height()),
                     picture, QRectF(picture.rect()));
    } else {
        PaintSilhouette(p, area);
    }
    return canvas;
}

} // namespace

AmiiboLibraryDialog::AmiiboLibraryDialog(QWidget* parent,
                                         InputCommon::InputSubsystem* input_subsystem_,
                                         Core::HID::HIDCore& hid_core)
    : QDialog(parent), input_subsystem{input_subsystem_} {
    setWindowTitle(tr("Amiibo Library"));
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    setAcceptDrops(true);
    resize(1180, 760);
    setObjectName(QStringLiteral("AmiiboLibrary"));
    setStyleSheet(QStringLiteral(R"(
        QDialog#AmiiboLibrary { background: #0E0B09; }
        QDialog#AmiiboLibrary QLabel { background: transparent; color: #F6EBDD; }
        QLabel#AmiiboTitle { font-size: 26px; font-weight: 900; }
        QLabel#AmiiboCount, QLabel#AmiiboMuted { color: #A8998A; font-size: 13px; }
        QLabel#AmiiboHeading { color: #A8998A; font-size: 11px; font-weight: 800; letter-spacing: 1px; }
        QLabel#AmiiboName { font-size: 24px; font-weight: 900; }
        QLabel#AmiiboKey { color: #A8998A; font-size: 13px; }
        QLabel#AmiiboValue { font-size: 13px; font-weight: 700; }
        QLabel#AmiiboPicture { border-radius: 16px;
            background: qradialgradient(cx:0.5, cy:0.85, radius:0.75, fx:0.5, fy:0.85,
                                        stop:0 #3A1E10, stop:1 #120E0B); }
        QLineEdit#AmiiboSearch { background: #1A1512; border: 1px solid #3A2E25; border-radius: 18px;
            padding: 7px 14px; color: #F6EBDD; min-width: 240px; }
        QLineEdit#AmiiboSearch:focus { border: 1px solid #FF5A1F; }
        QPushButton#AmiiboPill { background: transparent; border: none; border-radius: 15px; padding: 6px 14px;
            color: #D9CBBB; font-weight: 700; min-width: 0px; min-height: 0px; }
        QPushButton#AmiiboPill:hover { color: #FFFFFF; }
        QPushButton#AmiiboPill:checked { background: #F6EBDD; color: #1A1512; }
        QFrame#AmiiboPills { background: #1A1512; border: 1px solid #2A211B; border-radius: 19px; }
        QFrame#AmiiboPanel { background: #15110E; border: 1px solid #2A211B; border-radius: 18px; }
        QFrame#AmiiboFacts { background: #0E0B09; border: 1px solid #2A211B; border-radius: 12px; }
        QFrame#AmiiboFactLine { background: #2A211B; max-height: 1px; min-height: 1px; border: none; }
        QPushButton#AmiiboButton { background: #2A211B; border: 1px solid #3A2E25; border-radius: 12px;
            padding: 9px 16px; color: #F6EBDD; font-weight: 800; min-width: 0px; }
        QPushButton#AmiiboButton:hover { border: 1px solid #FF5A1F; }
        QPushButton#AmiiboButton:disabled { color: #6B5F55; border: 1px solid #2A211B; }
        QPushButton#AmiiboPrimary { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #FF5A1F, stop:1 #FF8A3D);
            border: none; border-radius: 12px; padding: 9px 20px; color: #FFF6EC; font-weight: 900; min-width: 0px; }
        QPushButton#AmiiboPrimary:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #FF6B33, stop:1 #FFC23D); }
        QPushButton#AmiiboPrimary:disabled { background: #3A2E25; color: #8C7F74; }
        QPushButton#AmiiboRound { background: #1A1512; border: 1px solid #3A2E25; border-radius: 18px;
            min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; padding: 0px;
            color: #F6EBDD; font-size: 18px; font-weight: 800; }
        QPushButton#AmiiboRound:hover { border: 1px solid #FF5A1F; }
        QPushButton#AmiiboRound::menu-indicator { image: none; width: 0px; }
        QComboBox#AmiiboSort { background: #1A1512; border: 1px solid #3A2E25; border-radius: 16px;
            padding: 6px 14px; color: #F6EBDD; font-weight: 700; }
        QListWidget#AmiiboGrid { background: transparent; border: none; outline: none; }
        QListWidget#AmiiboGrid::item { background: transparent; border: none; }
    )"));

    search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("AmiiboSearch"));
    search->setPlaceholderText(tr("Find a figure"));
    search->setClearButtonEnabled(true);
    // The views (all, favorites, groups, series): kept as the list behind the pills, and stepped
    // through with L and R.
    view = new QComboBox(this);
    view->hide();
    sort = new QComboBox(this);
    sort->setObjectName(QStringLiteral("AmiiboSort"));
    sort->addItems({tr("Recently used"), tr("Name"), tr("Series")});
    sort->setCurrentIndex(std::clamp(UISettings::values.amiibo_sort, 0, 2));

    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("AmiiboGrid"));
    list->setItemDelegate(new FigureCard(list));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(IconSize, IconSize));
    list->setGridSize(CardSize);
    list->setUniformItemSizes(true);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setMouseTracking(true);
    list->viewport()->setAttribute(Qt::WA_Hover);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    network = new QNetworkAccessManager(this);

    QPixmap blank{IconSize, IconSize};
    blank.fill(Qt::transparent);
    placeholder = QIcon{blank};

    status = new QLabel(this);
    status->setObjectName(QStringLiteral("AmiiboMuted"));
    status->setWordWrap(true);

    const auto make_button = [this](const QString& text, const char* style) {
        auto* button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(style));
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    tap_button = make_button(tr("Tap in"), "AmiiboPrimary");
    tap_button->setDefault(true);
    remove_tag_button = make_button(tr("Remove from reader"), "AmiiboButton");
    rename_button = make_button(tr("Rename..."), "AmiiboButton");
    backup_button = make_button(tr("Back up..."), "AmiiboButton");
    favorite_button = make_button(tr("Favorite"), "AmiiboButton");
    group_button = make_button(tr("Group..."), "AmiiboButton");
    forget_button = make_button(tr("Remove from library"), "AmiiboButton");

    // The top: the name and how many, the search, and adding figures.
    auto* title = new QLabel(tr("Amiibo"), this);
    title->setObjectName(QStringLiteral("AmiiboTitle"));
    count_label = new QLabel(this);
    count_label->setObjectName(QStringLiteral("AmiiboCount"));
    auto* add_button = make_button(QStringLiteral("+"), "AmiiboRound");
    add_button->setToolTip(tr("Add amiibo files or a folder of them (or drop them here)"));
    auto* add_menu = new QMenu(add_button);
    add_menu->addAction(tr("Add Files..."), this, &AmiiboLibraryDialog::AddFiles);
    add_menu->addAction(tr("Add Folder..."), this, &AmiiboLibraryDialog::AddFolder);
    add_button->setMenu(add_menu);
    auto* top_row = new QHBoxLayout;
    top_row->setSpacing(14);
    top_row->addWidget(title);
    top_row->addWidget(count_label, 0, Qt::AlignVCenter);
    top_row->addStretch(1);
    top_row->addWidget(search);
    top_row->addWidget(add_button);

    // The pills (all, favorites, groups, series), scrolling sideways when there are many, and the
    // sort.
    auto* pills = new QFrame(this);
    pills->setObjectName(QStringLiteral("AmiiboPills"));
    pill_row = new QHBoxLayout(pills);
    pill_row->setContentsMargins(4, 3, 4, 3);
    pill_row->setSpacing(2);
    pill_group = new QButtonGroup(this);
    pill_group->setExclusive(true);
    auto* pill_scroll = new QScrollArea(this);
    pill_scroll->setWidget(pills);
    pill_scroll->setWidgetResizable(false);
    pill_scroll->setFrameShape(QFrame::NoFrame);
    pill_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pill_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pill_scroll->setFixedHeight(48);
    pill_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"
                                              "QScrollArea > QWidget > QWidget { background: transparent; }"));
    auto* filter_row = new QHBoxLayout;
    filter_row->addWidget(pill_scroll, 1);
    filter_row->addWidget(sort, 0, Qt::AlignTop);

    // The left: pills, the grid of figures, what is happening.
    auto* left = new QVBoxLayout;
    left->setSpacing(10);
    left->addLayout(filter_row);
    left->addWidget(list, 1);
    left->addWidget(status);

    // The right: the picked figure, large, with what is known and what can be done.
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("AmiiboPanel"));
    panel->setFixedWidth(420);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(20, 18, 20, 20);
    panel_layout->setSpacing(10);
    detail_heading = new QLabel(panel);
    detail_heading->setObjectName(QStringLiteral("AmiiboHeading"));
    listening_label = new QLabel(panel);
    listening_label->setObjectName(QStringLiteral("AmiiboMuted"));
    auto* heading_row = new QHBoxLayout;
    heading_row->addWidget(detail_heading);
    heading_row->addStretch(1);
    heading_row->addWidget(listening_label);
    panel_layout->addLayout(heading_row);
    detail_picture = new QLabel(panel);
    detail_picture->setObjectName(QStringLiteral("AmiiboPicture"));
    detail_picture->setFixedHeight(250);
    detail_picture->setAlignment(Qt::AlignCenter);
    panel_layout->addWidget(detail_picture);
    detail_name = new QLabel(panel);
    detail_name->setObjectName(QStringLiteral("AmiiboName"));
    detail_series = new QLabel(panel);
    detail_series->setObjectName(QStringLiteral("AmiiboMuted"));
    panel_layout->addSpacing(4);
    panel_layout->addWidget(detail_name);
    panel_layout->addWidget(detail_series);
    auto* facts = new QFrame(panel);
    facts->setObjectName(QStringLiteral("AmiiboFacts"));
    auto* facts_layout = new QVBoxLayout(facts);
    facts_layout->setContentsMargins(14, 4, 14, 4);
    facts_layout->setSpacing(0);
    const auto add_fact = [&](const QString& key, bool line_before) {
        if (line_before) {
            auto* line = new QFrame(facts);
            line->setObjectName(QStringLiteral("AmiiboFactLine"));
            facts_layout->addWidget(line);
        }
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 9, 0, 9);
        auto* name = new QLabel(key, facts);
        name->setObjectName(QStringLiteral("AmiiboKey"));
        auto* value = new QLabel(facts);
        value->setObjectName(QStringLiteral("AmiiboValue"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(name);
        row->addWidget(value, 1);
        facts_layout->addLayout(row);
        return value;
    };
    detail_group = add_fact(tr("Group"), false);
    detail_file = add_fact(tr("File"), true);
    detail_id = add_fact(tr("Figure ID"), true);
    panel_layout->addSpacing(4);
    panel_layout->addWidget(facts);
    panel_layout->addStretch(1);
    auto* first_buttons = new QHBoxLayout;
    first_buttons->setSpacing(8);
    first_buttons->addWidget(tap_button);
    first_buttons->addWidget(rename_button);
    first_buttons->addWidget(backup_button);
    first_buttons->addStretch(1);
    auto* second_buttons = new QHBoxLayout;
    second_buttons->setSpacing(8);
    second_buttons->addWidget(favorite_button);
    second_buttons->addWidget(group_button);
    second_buttons->addWidget(remove_tag_button);
    second_buttons->addStretch(1);
    auto* third_buttons = new QHBoxLayout;
    third_buttons->addStretch(1);
    third_buttons->addWidget(forget_button);
    panel_layout->addLayout(first_buttons);
    panel_layout->addLayout(second_buttons);
    panel_layout->addLayout(third_buttons);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    body->addLayout(left, 1);
    body->addWidget(panel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 22);
    layout->setSpacing(14);
    layout->addLayout(top_row);
    layout->addLayout(body, 1);

    connect(rename_button, &QPushButton::clicked, this, [this] {
        if (!details_path.isEmpty()) {
            Rename(details_path);
        }
    });
    connect(backup_button, &QPushButton::clicked, this, [this] {
        if (!details_path.isEmpty()) {
            BackUp(details_path);
        }
    });
    connect(favorite_button, &QPushButton::clicked, this, [this] {
        const QStringList paths = SelectedPaths();
        const UISettings::AmiiboEntry* first = paths.isEmpty() ? nullptr : FindEntry(paths[0]);
        if (first != nullptr) {
            SetFavorite(paths, !first->favorite);
        }
    });
    connect(group_button, &QPushButton::clicked, this, [this] { SetGroup(SelectedPaths()); });
    connect(pill_group, &QButtonGroup::idClicked, this, [this](int id) {
        if (id >= 0 && id < view->count()) {
            view->setCurrentIndex(id);
        }
    });
    connect(search, &QLineEdit::textChanged, this, &AmiiboLibraryDialog::ApplyView);
    connect(view, &QComboBox::currentIndexChanged, this, [this](int index) {
        UISettings::values.amiibo_view = view->currentData().toString();
        if (QAbstractButton* pill = pill_group->button(index)) {
            pill->setChecked(true);
        }
        ApplyView();
    });
    connect(sort, &QComboBox::currentIndexChanged, this, [this](int index) {
        UISettings::values.amiibo_sort = index;
        ApplyView();
    });
    connect(list, &QListWidget::itemActivated, this, &AmiiboLibraryDialog::TapSelected);
    connect(list, &QListWidget::itemSelectionChanged, this, &AmiiboLibraryDialog::UpdateState);
    connect(list, &QListWidget::customContextMenuRequested, this,
            &AmiiboLibraryDialog::ShowContextMenu);
    connect(tap_button, &QPushButton::clicked, this, &AmiiboLibraryDialog::TapSelected);
    connect(remove_tag_button, &QPushButton::clicked, this, &AmiiboLibraryDialog::RemoveFromReader);
    connect(forget_button, &QPushButton::clicked, this, &AmiiboLibraryDialog::RemoveSelected);

    controller_navigation = new ControllerNavigation(hid_core, this);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent, this,
            [this](Qt::Key key) {
                if (!isVisible() || !isActiveWindow()) {
                    return;
                }
                QWidget* target = this;
                if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
                    key == Qt::Key_Right) {
                    target = list;
                }
                QCoreApplication::postEvent(target,
                                            new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
            });

    // The game starts and stops scanning on its own; follow it so a waiting tap lands.
    state_timer.setInterval(200);
    connect(&state_timer, &QTimer::timeout, this, &AmiiboLibraryDialog::UpdateState);
    state_timer.start();

    Rebuild();
    list->setFocus();
}

AmiiboLibraryDialog::~AmiiboLibraryDialog() {
    controller_navigation->UnloadController();
}

void AmiiboLibraryDialog::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (!search->hasFocus() || list->currentItem() != nullptr) {
            TapSelected();
        }
        return;
    case Qt::Key_Delete:
        if (list->hasFocus()) {
            RemoveSelected();
        }
        return;
    case Qt::Key_F:
        if (list->hasFocus() && event->modifiers() == Qt::NoModifier) {
            const QStringList paths = SelectedPaths();
            const UISettings::AmiiboEntry* first = paths.isEmpty() ? nullptr : FindEntry(paths[0]);
            if (first != nullptr) {
                SetFavorite(paths, !first->favorite);
            }
            return;
        }
        break;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        // L and R on a controller step through the views.
        if (view->count() > 0) {
            const int step = event->key() == Qt::Key_PageDown ? 1 : -1;
            view->setCurrentIndex((view->currentIndex() + step + view->count()) % view->count());
        }
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(event);
}

void AmiiboLibraryDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AmiiboLibraryDialog::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QFileInfo{path}.isDir()) {
            QDirIterator it{path, {QStringLiteral("*.bin")}, QDir::Files,
                            QDirIterator::Subdirectories};
            while (it.hasNext()) {
                paths.append(it.next());
            }
        } else if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    AddPaths(paths);
    event->acceptProposedAction();
}

QString AmiiboLibraryDialog::LabelFor(const QString& path) const {
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&](const Row& row) { return row.path == path; });
    if (it != rows.end()) {
        return it->name;
    }
    const QString name = Lookup(path).name;
    return name.isEmpty() ? QFileInfo{path}.completeBaseName() : name;
}

void AmiiboLibraryDialog::Rebuild() {
    const auto& library = UISettings::values.amiibo_library;
    rows.clear();
    rows.reserve(library.size());

    const auto base_name = [](const UISettings::AmiiboEntry& entry, const AmiiboInfo& info) {
        if (!entry.name.isEmpty()) {
            return entry.name;
        }
        return info.name.isEmpty() ? QFileInfo{entry.path}.completeBaseName() : info.name;
    };
    // Several dumps of one figure (separate save data) are told apart by their file names.
    QHash<QString, int> name_counts;
    for (const auto& entry : library) {
        ++name_counts[base_name(entry, Lookup(entry.path))];
    }

    for (int i = 0; i < library.size(); ++i) {
        const auto& entry = library[i];
        const AmiiboInfo info = Lookup(entry.path);
        Row row;
        row.path = entry.path;
        row.file_name = QFileInfo{entry.path}.completeBaseName();
        row.name = base_name(entry, info);
        if (entry.name.isEmpty() && name_counts.value(row.name) > 1 && row.name != row.file_name) {
            row.name = tr("%1 (%2)").arg(row.name, row.file_name);
        }
        row.series = info.series.isEmpty() ? tr("Other") : info.series;
        row.image = info.image;
        row.recent = i;
        row.exists = QFileInfo::exists(entry.path);
        row.label = (entry.favorite ? FavoriteMark : QString{}) + row.name;
        if (!row.exists) {
            row.label = tr("%1 (missing)").arg(row.label);
        }
        const QString id = AmiiboId(entry.path);
        QStringList tip{entry.path};
        if (!id.isEmpty()) {
            tip << tr("ID: %1").arg(id);
        }
        if (!info.series.isEmpty()) {
            tip << tr("Series: %1").arg(info.series);
        }
        if (!entry.group.isEmpty()) {
            tip << tr("Group: %1").arg(entry.group);
        }
        row.tooltip = tip.join(QLatin1Char('\n'));
        rows.push_back(std::move(row));
    }
    RefreshFilters();
    ApplyView();
}

void AmiiboLibraryDialog::RefreshFilters() {
    std::map<QString, int> groups;
    std::map<QString, int> series;
    int favorites = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& entry = UISettings::values.amiibo_library[static_cast<int>(i)];
        favorites += entry.favorite;
        if (!entry.group.isEmpty()) {
            ++groups[entry.group];
        }
        ++series[rows[i].series];
    }

    const QString wanted = UISettings::values.amiibo_view;
    const QSignalBlocker blocker{view};
    view->clear();
    view->addItem(tr("All amiibo (%1)").arg(rows.size()), QString{});
    view->addItem(tr("Favorites (%1)").arg(favorites), QStringLiteral("favorites"));
    for (const auto& [name, count] : groups) {
        view->addItem(tr("Group: %1 (%2)").arg(name).arg(count),
                      ViewKey(QStringLiteral("group"), name));
    }
    for (const auto& [name, count] : series) {
        view->addItem(tr("%1 (%2)").arg(name).arg(count), ViewKey(QStringLiteral("series"), name));
    }
    const int index = view->findData(wanted);
    view->setCurrentIndex(index < 0 ? 0 : index);
    UISettings::values.amiibo_view = view->currentData().toString();
    RebuildPills();
    count_label->setText(tr("%n figure(s)", nullptr, static_cast<int>(rows.size())) +
                         (favorites > 0 ? tr(" · %n favorite(s)", nullptr, favorites) : QString{}));
}

void AmiiboLibraryDialog::RebuildPills() {
    for (QAbstractButton* pill : pill_group->buttons()) {
        pill_group->removeButton(pill);
        pill->deleteLater();
    }
    for (int i = 0; i < view->count(); ++i) {
        const QString key = view->itemData(i).toString();
        QString text;
        if (key.isEmpty()) {
            text = tr("All");
        } else if (key == QStringLiteral("favorites")) {
            text = tr("★ Favorites");
        } else {
            text = key.section(QLatin1Char(':'), 1);
            if (key.startsWith(QStringLiteral("group:"))) {
                text = tr("▣ %1").arg(text);
            }
        }
        auto* pill = new QPushButton(text, pill_row->parentWidget());
        pill->setObjectName(QStringLiteral("AmiiboPill"));
        pill->setCheckable(true);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setToolTip(view->itemText(i));
        pill_group->addButton(pill, i);
        pill_row->addWidget(pill);
        pill->show();
    }
    if (QAbstractButton* pill = pill_group->button(view->currentIndex())) {
        pill->setChecked(true);
    }
    pill_row->parentWidget()->adjustSize();
}

void AmiiboLibraryDialog::ApplyView() {
    QSet<QString> selected;
    for (const QString& path : SelectedPaths()) {
        selected.insert(path);
    }
    const QString current =
        list->currentItem() ? list->currentItem()->data(PathRole).toString() : QString{};

    const QString key = view->currentData().toString();
    const QString filter = search->text().trimmed();
    const auto& library = UISettings::values.amiibo_library;
    std::vector<int> shown;
    shown.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const auto& entry = library[static_cast<int>(i)];
        if (key == QStringLiteral("favorites") && !entry.favorite) {
            continue;
        }
        if (key.startsWith(QStringLiteral("group:")) && ViewKey(QStringLiteral("group"), entry.group) != key) {
            continue;
        }
        if (key.startsWith(QStringLiteral("series:")) && ViewKey(QStringLiteral("series"), row.series) != key) {
            continue;
        }
        if (!filter.isEmpty() && !row.name.contains(filter, Qt::CaseInsensitive) &&
            !row.file_name.contains(filter, Qt::CaseInsensitive) &&
            !row.series.contains(filter, Qt::CaseInsensitive) &&
            !entry.group.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        shown.push_back(static_cast<int>(i));
    }
    const int mode = sort->currentIndex();
    std::stable_sort(shown.begin(), shown.end(), [&](int a, int b) {
        const Row& x = rows[a];
        const Row& y = rows[b];
        if (mode == 2 && x.series != y.series) {
            return QString::localeAwareCompare(x.series, y.series) < 0;
        }
        if (mode >= 1) {
            const int by_name = QString::localeAwareCompare(x.name, y.name);
            if (by_name != 0) {
                return by_name < 0;
            }
        }
        return x.recent < y.recent;
    });

    list->setUpdatesEnabled(false);
    {
        const QSignalBlocker blocker{list};
        list->clear();
        for (const int i : shown) {
            const Row& row = rows[i];
            const QIcon icon = IconFor(row.image);
            auto* item = new QListWidgetItem(icon, row.label, list);
            item->setData(PathRole, row.path);
            item->setData(ImageRole, row.image);
            item->setData(NameRole, row.name);
            item->setData(SeriesRole, row.series);
            item->setData(FavoriteRole, library[i].favorite);
            item->setData(ExistsRole, row.exists);
            item->setData(HasPictureRole, icon.cacheKey() != placeholder.cacheKey());
            // Sized up front: pictures arrive later and must not change the grid.
            item->setSizeHint(list->gridSize());
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setToolTip(row.tooltip);
            if (!row.exists) {
                item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            }
            if (selected.contains(row.path)) {
                item->setSelected(true);
            }
            if (row.path == current) {
                list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
            }
        }
        if (list->currentItem() == nullptr && list->count() > 0) {
            list->setCurrentRow(0);
        }
    }
    list->setUpdatesEnabled(true);
    bold_path.clear(); // the new items start without the mark
    UpdateState();
}

QIcon AmiiboLibraryDialog::IconFor(const QString& image) {
    if (image.isEmpty()) {
        return placeholder;
    }
    if (const auto it = icons.constFind(image); it != icons.constEnd()) {
        return *it;
    }
    // Figures newer than amiipedia's pictures ship with PhoenixEmu (dist/amiibo_images).
    const QString bundled = QStringLiteral(":/amiibo/images/") + image;
    const QString image_path = QFileInfo::exists(bundled) ? bundled : ImagePath(image);
    if (QFileInfo::exists(image_path)) {
        const QIcon icon{QPixmap{image_path}};
        icons.insert(image, icon);
        return icon;
    }
    RequestImage(image);
    return placeholder;
}

void AmiiboLibraryDialog::RequestImage(const QString& image) {
    if (!UISettings::values.auto_game_pictures.GetValue() || downloading.contains(image)) {
        return;
    }
    downloading.insert(image);
    QNetworkRequest request{
        QUrl{QStringLiteral("https://media.publit.io/file/w_%1/AMIIPEDIA/%2")
                 .arg(IconSize * 2)
                 .arg(image)}};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    QNetworkReply* reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, image] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        QPixmap pixmap;
        if (reply->error() != QNetworkReply::NoError || !pixmap.loadFromData(data)) {
            // Left in the set so a failing picture is not asked for again until the next start.
            return;
        }
        const QString image_path = ImagePath(image);
        QDir{}.mkpath(QFileInfo{image_path}.absolutePath());
        QSaveFile file{image_path};
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) {
            file.commit();
        }
        downloading.remove(image);
        const QIcon icon{pixmap};
        icons.insert(image, icon);
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(ImageRole).toString() == image) {
                list->item(i)->setIcon(icon);
                list->item(i)->setData(HasPictureRole, true);
            }
        }
        details_path.clear(); // the panel shows it large too
        UpdateState();
    });
}

QStringList AmiiboLibraryDialog::SelectedPaths() const {
    QStringList paths;
    for (const QListWidgetItem* item : list->selectedItems()) {
        paths.append(item->data(PathRole).toString());
    }
    if (paths.isEmpty() && list->currentItem() != nullptr) {
        paths.append(list->currentItem()->data(PathRole).toString());
    }
    return paths;
}

void AmiiboLibraryDialog::SaveLibrary() {
    emit LibraryChanged();
    Rebuild();
}

void AmiiboLibraryDialog::SetFavorite(const QStringList& paths, bool favorite) {
    for (const QString& path : paths) {
        if (auto* entry = FindEntry(path)) {
            entry->favorite = favorite;
        }
    }
    SaveLibrary();
}

void AmiiboLibraryDialog::SetGroup(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    QStringList groups;
    for (const auto& entry : UISettings::values.amiibo_library) {
        if (!entry.group.isEmpty() && !groups.contains(entry.group)) {
            groups.append(entry.group);
        }
    }
    groups.sort(Qt::CaseInsensitive);
    const UISettings::AmiiboEntry* first = FindEntry(paths[0]);
    groups.prepend(QString{});
    bool ok = false;
    const QString group =
        QInputDialog::getItem(this, tr("Group"),
                              tr("Put %n amiibo in a group (type a new name, or leave it empty for "
                                 "none):",
                                 nullptr, static_cast<int>(paths.size())),
                              groups, first ? std::max<int>(0, groups.indexOf(first->group)) : 0,
                              true, &ok)
            .trimmed();
    if (!ok) {
        return;
    }
    for (const QString& path : paths) {
        if (auto* entry = FindEntry(path)) {
            entry->group = group;
        }
    }
    SaveLibrary();
}

void AmiiboLibraryDialog::Rename(const QString& path) {
    auto* entry = FindEntry(path);
    if (entry == nullptr) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename"),
                              tr("Name shown in the library (leave it empty for the amiibo's own "
                                 "name):"),
                              QLineEdit::Normal, entry->name, &ok)
            .trimmed();
    if (!ok) {
        return;
    }
    entry->name = name;
    SaveLibrary();
}

void AmiiboLibraryDialog::ShowContextMenu(const QPoint& pos) {
    if (QListWidgetItem* item = list->itemAt(pos); item != nullptr && !item->isSelected()) {
        list->setCurrentItem(item);
    }
    const QStringList paths = SelectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    const UISettings::AmiiboEntry* first = FindEntry(paths[0]);
    const bool favorite = first != nullptr && first->favorite;

    QMenu menu{this};
    menu.addAction(tap_button->text(), this, &AmiiboLibraryDialog::TapSelected);
    menu.addSeparator();
    const QString favorite_text =
        (favorite ? tr("Remove from Favorites") : tr("Add to Favorites")) + QStringLiteral("\tF");
    menu.addAction(favorite_text, this, [this, paths, favorite] { SetFavorite(paths, !favorite); });
    menu.addAction(tr("Group..."), this, [this, paths] { SetGroup(paths); });
    if (paths.size() == 1) {
        menu.addAction(tr("Rename..."), this, [this, path = paths[0]] { Rename(path); });
        menu.addAction(tr("Show in Folder"), this, [path = paths[0]] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo{path}.absolutePath()));
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Remove from Library") + QStringLiteral("\tDel"), this,
                   &AmiiboLibraryDialog::RemoveSelected);
    menu.exec(list->viewport()->mapToGlobal(pos));
}

void AmiiboLibraryDialog::AddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Amiibo"), {},
        tr("Amiibo File (%1);; All Files (*.*)").arg(QStringLiteral("*.bin")));
    AddPaths(files);
}

void AmiiboLibraryDialog::AddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Amiibo Folder"));
    if (dir.isEmpty()) {
        return;
    }
    QStringList paths;
    QDirIterator it{dir, {QStringLiteral("*.bin")}, QDir::Files, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        paths.append(it.next());
    }
    AddPaths(paths);
}

void AmiiboLibraryDialog::AddPaths(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    auto& library = UISettings::values.amiibo_library;
    // Entries are stored canonical, so a set of them answers "already added" without asking the
    // file system again for every pair.
    QSet<QString> known;
    for (const auto& entry : library) {
        known.insert(entry.path);
    }
    int added = 0;
    int skipped = 0;
    QString last_added;
    for (const QString& path : paths) {
        if (!IsTagSize(QFileInfo{path}.size())) {
            ++skipped;
            continue;
        }
        const QString canonical = CanonicalPath(path);
        if (known.contains(canonical)) {
            continue;
        }
        known.insert(canonical);
        library.append({.path = canonical});
        last_added = canonical;
        ++added;
    }
    if (added > 0) {
        SaveLibrary();
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(PathRole).toString() == last_added) {
                list->setCurrentRow(i);
                list->scrollToItem(list->item(i));
                break;
            }
        }
    }
    status->setText(skipped == 0
                        ? tr("Added %n amiibo.", nullptr, added)
                        : tr("Added %1 amiibo. %2 file(s) were not amiibo dumps and were skipped.")
                              .arg(added)
                              .arg(skipped));
}

void AmiiboLibraryDialog::RemoveSelected() {
    const QStringList paths = SelectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    auto& library = UISettings::values.amiibo_library;
    library.removeIf(
        [&](const UISettings::AmiiboEntry& entry) { return paths.contains(entry.path); });
    SaveLibrary();
}

void AmiiboLibraryDialog::TapSelected() {
    const QListWidgetItem* item = list->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(PathRole).toString();
    if (!QFileInfo::exists(path)) {
        status->setText(tr("%1 is no longer at %2.").arg(LabelFor(path), path));
        return;
    }

    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    switch (virtual_amiibo->GetCurrentState()) {
    case State::TagNearby:
        if (OnReaderPath() == path) {
            RemoveFromReader();
            return;
        }
        // Lift the old figure first and give the game a moment to see it go.
        virtual_amiibo->CloseAmiibo();
        QTimer::singleShot(300, this, [this, path] {
            pending_path = path;
            UpdateState();
        });
        break;
    case State::WaitingForAmiibo:
        TapNow(path);
        break;
    default:
        pending_path = path;
        break;
    }
    UpdateState();
}

void AmiiboLibraryDialog::TapNow(const QString& path) {
    pending_path.clear();
    const QString name = LabelFor(path);
    switch (input_subsystem->GetVirtualAmiibo()->LoadAmiibo(path.toStdString())) {
    case Info::Success:
        MoveToFront(path);
        status->setText(tr("%1 is on the reader.").arg(name));
        break;
    case Info::NotAnAmiibo:
        status->setText(tr("%1 is not a valid amiibo.").arg(name));
        break;
    case Info::UnableToLoad:
        status->setText(tr("%1 could not be opened.").arg(name));
        break;
    case Info::WrongDeviceState:
        status->setText(tr("The game stopped looking for amiibo."));
        break;
    default:
        status->setText(tr("An unknown error occurred."));
        break;
    }
}

void AmiiboLibraryDialog::MoveToFront(const QString& path) {
    auto& library = UISettings::values.amiibo_library;
    const auto it = std::find_if(library.begin(), library.end(),
                                 [&](const UISettings::AmiiboEntry& e) { return e.path == path; });
    if (it == library.end() || it == library.begin()) {
        return;
    }
    library.move(static_cast<qsizetype>(it - library.begin()), 0);
    SaveLibrary();
}

void AmiiboLibraryDialog::RemoveFromReader() {
    pending_path.clear();
    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    if (virtual_amiibo->GetCurrentState() == State::TagNearby) {
        virtual_amiibo->CloseAmiibo();
        status->setText(tr("The amiibo was removed from the reader."));
    }
    UpdateState();
}

void AmiiboLibraryDialog::CancelPendingTap() {
    pending_path.clear();
    UpdateState();
}

QString AmiiboLibraryDialog::OnReaderPath() {
    // Asked five times a second: the file system is only asked again when the file changes.
    const QString raw =
        QString::fromStdString(input_subsystem->GetVirtualAmiibo()->GetLastFilePath());
    if (raw != reader_raw_path) {
        reader_raw_path = raw;
        reader_path = raw.isEmpty() ? QString{} : CanonicalPath(raw);
    }
    return reader_path;
}

void AmiiboLibraryDialog::UpdateState() {
    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    State state = virtual_amiibo->GetCurrentState();
    if (!pending_path.isEmpty() && state == State::WaitingForAmiibo) {
        TapNow(pending_path);
        state = virtual_amiibo->GetCurrentState();
    }
    if (static_cast<int>(state) != last_state) {
        // A message about the last tap is stale once the game moves on.
        if (last_state != -1 && pending_path.isEmpty()) {
            status->clear();
        }
        last_state = static_cast<int>(state);
    }

    const QString on_reader = state == State::TagNearby ? OnReaderPath() : QString{};
    if (on_reader != bold_path) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            const QString path = item->data(PathRole).toString();
            if (path == bold_path || path == on_reader) {
                item->setData(OnReaderRole, path == on_reader);
            }
        }
        bold_path = on_reader;
    }

    if (!pending_path.isEmpty()) {
        status->setText(tr("%1 will be tapped when the game scans for amiibo.")
                            .arg(LabelFor(pending_path)));
    } else if (UISettings::values.amiibo_library.isEmpty()) {
        status->setText(tr("Add amiibo files or a folder of them, or drop them here."));
    } else if (status->text().isEmpty()) {
        switch (state) {
        case State::WaitingForAmiibo:
            status->setText(tr("The game is scanning for amiibo."));
            break;
        case State::TagNearby:
            status->setText(tr("%1 is on the reader.").arg(LabelFor(on_reader)));
            break;
        default:
            status->setText(tr("Pick an amiibo; it is tapped when the game scans. Right-click to "
                               "organise."));
            break;
        }
    }

    const QListWidgetItem* current = list->currentItem();
    const bool current_on_reader =
        current != nullptr && !on_reader.isEmpty() && current->data(PathRole).toString() == on_reader;
    tap_button->setText(current_on_reader ? tr("Lift") : tr("Tap in"));
    tap_button->setEnabled(current != nullptr);
    remove_tag_button->setEnabled(state == State::TagNearby || !pending_path.isEmpty());
    forget_button->setEnabled(current != nullptr);
    rename_button->setEnabled(current != nullptr);
    backup_button->setEnabled(current != nullptr);
    group_button->setEnabled(current != nullptr);
    favorite_button->setEnabled(current != nullptr);
    if (current != nullptr) {
        const UISettings::AmiiboEntry* entry = FindEntry(current->data(PathRole).toString());
        favorite_button->setText(entry != nullptr && entry->favorite ? tr("★ Favorite")
                                                                     : tr("☆ Favorite"));
    }
    UpdateDetails(on_reader, static_cast<int>(state));
}

void AmiiboLibraryDialog::UpdateDetails(const QString& on_reader, int state) {
    // Whether the game is listening for a figure.
    switch (static_cast<State>(state)) {
    case State::WaitingForAmiibo:
        listening_label->setText(tr("<span style='color:#7FD48A'>●</span> The game is listening"));
        break;
    case State::TagNearby:
        listening_label->setText(tr("<span style='color:#FFC23D'>●</span> A figure is on the reader"));
        break;
    default:
        listening_label->setText(tr("Not listening"));
        break;
    }
    const QListWidgetItem* current = list->currentItem();
    const QString path = current != nullptr ? current->data(PathRole).toString() : on_reader;
    if (path == details_path && state == details_state && !path.isEmpty()) {
        return;
    }
    details_path = path;
    details_state = state;
    detail_heading->setText(!path.isEmpty() && path == on_reader ? tr("ON THE READER")
                                                                 : tr("PICKED"));
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&](const Row& row) { return row.path == path; });
    if (path.isEmpty() || it == rows.end()) {
        detail_picture->setPixmap(LargePicture({}, false, 200, devicePixelRatioF()));
        detail_name->setText(UISettings::values.amiibo_library.isEmpty() ? tr("No figures yet")
                                                                         : tr("No figure picked"));
        detail_series->setText(tr("Add amiibo files with + or drop them here."));
        detail_group->clear();
        detail_file->clear();
        detail_id->clear();
        return;
    }
    const QIcon icon = IconFor(it->image);
    detail_picture->setPixmap(
        LargePicture(icon, icon.cacheKey() != placeholder.cacheKey(), 230, devicePixelRatioF()));
    detail_name->setText(it->name);
    detail_series->setText(it->series);
    const UISettings::AmiiboEntry* entry = FindEntry(path);
    detail_group->setText(entry != nullptr && !entry->group.isEmpty() ? entry->group : tr("None"));
    const QString file = QFileInfo{path}.fileName();
    detail_file->setText(it->exists ? file : tr("%1 (missing)").arg(file));
    detail_file->setToolTip(path);
    const QString id = AmiiboId(path);
    detail_id->setText(id.isEmpty() ? tr("Unknown") : id.toUpper());
}

void AmiiboLibraryDialog::BackUp(const QString& path) {
    const QString target = QFileDialog::getSaveFileName(
        this, tr("Back Up Amiibo"), QFileInfo{path}.fileName(),
        tr("Amiibo File (%1)").arg(QStringLiteral("*.bin")));
    if (target.isEmpty()) {
        return;
    }
    if (QFileInfo::exists(target)) {
        QFile::remove(target);
    }
    status->setText(QFile::copy(path, target)
                        ? tr("Backed up %1 to %2.").arg(LabelFor(path), target)
                        : tr("%1 could not be backed up.").arg(LabelFor(path)));
}
