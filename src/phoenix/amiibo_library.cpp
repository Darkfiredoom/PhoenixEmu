// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <map>
#include <numeric>

#include <QAction>
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
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
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
const QString FavoriteMark = QStringLiteral("★ ");

} // namespace

AmiiboLibraryDialog::AmiiboLibraryDialog(QWidget* parent,
                                         InputCommon::InputSubsystem* input_subsystem_,
                                         Core::HID::HIDCore& hid_core)
    : QDialog(parent), input_subsystem{input_subsystem_} {
    setWindowTitle(tr("Amiibo Library"));
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    setAcceptDrops(true);
    resize(680, 580);

    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);
    view = new QComboBox(this);
    view->setToolTip(tr("Show all amiibo, your favorites, one of your groups or one series (L/R)"));
    view->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    sort = new QComboBox(this);
    sort->addItems({tr("Recently used"), tr("Name"), tr("Series")});
    sort->setCurrentIndex(std::clamp(UISettings::values.amiibo_sort, 0, 2));

    list = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(IconSize, IconSize));
    list->setGridSize(QSize(IconSize + 40, IconSize + 48));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setContextMenuPolicy(Qt::CustomContextMenu);

    network = new QNetworkAccessManager(this);

    QPixmap blank{IconSize, IconSize};
    blank.fill(Qt::transparent);
    placeholder = QIcon{blank};

    status = new QLabel(this);
    status->setWordWrap(true);

    tap_button = new QPushButton(tr("Tap"), this);
    tap_button->setDefault(true);
    remove_tag_button = new QPushButton(tr("Remove from Reader"), this);
    auto* add_files_button = new QPushButton(tr("Add Files..."), this);
    auto* add_folder_button = new QPushButton(tr("Add Folder..."), this);
    forget_button = new QPushButton(tr("Remove from Library"), this);

    auto* filter_row = new QHBoxLayout;
    filter_row->addWidget(search, 1);
    filter_row->addWidget(view);
    filter_row->addWidget(sort);
    auto* tag_row = new QHBoxLayout;
    tag_row->addWidget(tap_button);
    tag_row->addWidget(remove_tag_button);
    auto* library_row = new QHBoxLayout;
    library_row->addWidget(add_files_button);
    library_row->addWidget(add_folder_button);
    library_row->addWidget(forget_button);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(filter_row);
    layout->addWidget(list, 1);
    layout->addWidget(status);
    layout->addLayout(tag_row);
    layout->addLayout(library_row);

    connect(search, &QLineEdit::textChanged, this, &AmiiboLibraryDialog::ApplyView);
    connect(view, &QComboBox::currentIndexChanged, this, [this] {
        UISettings::values.amiibo_view = view->currentData().toString();
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
    connect(add_files_button, &QPushButton::clicked, this, &AmiiboLibraryDialog::AddFiles);
    connect(add_folder_button, &QPushButton::clicked, this, &AmiiboLibraryDialog::AddFolder);
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
            auto* item = new QListWidgetItem(IconFor(row.image), row.label, list);
            item->setData(PathRole, row.path);
            item->setData(ImageRole, row.image);
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
    const QString image_path = ImagePath(image);
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
            }
        }
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
                QFont font = item->font();
                font.setBold(path == on_reader);
                item->setFont(font);
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
    tap_button->setText(current_on_reader ? tr("Lift") : tr("Tap"));
    tap_button->setEnabled(current != nullptr);
    remove_tag_button->setEnabled(state == State::TagNearby || !pending_path.isEmpty());
    forget_button->setEnabled(current != nullptr);
}
