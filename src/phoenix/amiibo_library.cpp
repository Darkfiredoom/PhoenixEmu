// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
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
};

// dist/amiibo_table.json, made by tools/amiibo/make_amiibo_table.py: names from AmiiboAPI,
// pictures from amiipedia.
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
            result.insert(it.key(), {entry.at(0).toString(), entry.at(1).toString()});
        }
        return result;
    }();
    return table;
}

AmiiboInfo Lookup(const QString& path) {
    return Table().value(AmiiboId(path));
}

QString DisplayName(const QString& path) {
    const QString name = Lookup(path).name;
    return name.isEmpty() ? QFileInfo{path}.completeBaseName() : name;
}

QString ImagePath(const QString& image) {
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "amiibo_images";
    return QString::fromStdString(Common::FS::PathToUTF8String(dir)) + QLatin1Char('/') + image;
}

constexpr int IconSize = 96;

} // namespace

AmiiboLibraryDialog::AmiiboLibraryDialog(QWidget* parent,
                                         InputCommon::InputSubsystem* input_subsystem_,
                                         Core::HID::HIDCore& hid_core)
    : QDialog(parent), input_subsystem{input_subsystem_} {
    setWindowTitle(tr("Amiibo Library"));
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    setAcceptDrops(true);
    resize(640, 560);

    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);

    list = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(IconSize, IconSize));
    list->setGridSize(QSize(IconSize + 40, IconSize + 48));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);

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

    auto* tag_row = new QHBoxLayout;
    tag_row->addWidget(tap_button);
    tag_row->addWidget(remove_tag_button);
    auto* library_row = new QHBoxLayout;
    library_row->addWidget(add_files_button);
    library_row->addWidget(add_folder_button);
    library_row->addWidget(forget_button);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addWidget(status);
    layout->addLayout(tag_row);
    layout->addLayout(library_row);

    connect(search, &QLineEdit::textChanged, this, &AmiiboLibraryDialog::Populate);
    connect(list, &QListWidget::itemActivated, this, &AmiiboLibraryDialog::TapSelected);
    connect(list, &QListWidget::itemSelectionChanged, this, &AmiiboLibraryDialog::UpdateState);
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

    Populate();
    if (list->count() > 0) {
        list->setCurrentRow(0);
    }
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
    default:
        QDialog::keyPressEvent(event);
    }
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

void AmiiboLibraryDialog::Populate() {
    const QString selected =
        list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QString filter = search->text().trimmed();

    // Several dumps of one figure (separate save data) are told apart by their file names.
    QHash<QString, int> name_counts;
    for (const QString& path : UISettings::values.amiibo_library) {
        ++name_counts[DisplayName(path)];
    }

    list->clear();
    for (const QString& path : UISettings::values.amiibo_library) {
        const QString file_name = QFileInfo{path}.completeBaseName();
        QString name = DisplayName(path);
        if (name_counts.value(name) > 1 && name != file_name) {
            name = tr("%1 (%2)").arg(name, file_name);
        }
        if (!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) &&
            !file_name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(name, list);
        item->setData(Qt::UserRole, path);
        // Sized up front: pictures arrive later and must not change the grid.
        item->setSizeHint(list->gridSize());
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setIcon(placeholder);
        const QString id = AmiiboId(path);
        item->setToolTip(id.isEmpty() ? path : tr("%1\nID: %2").arg(path, id));
        const QString image = Lookup(path).image;
        item->setData(Qt::UserRole + 1, image);
        if (!image.isEmpty()) {
            const QString image_path = ImagePath(image);
            if (QFileInfo::exists(image_path)) {
                item->setIcon(QIcon{image_path});
            } else {
                RequestImage(image);
            }
        }
        if (!QFileInfo::exists(path)) {
            item->setText(tr("%1 (missing)").arg(name));
            item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
        }
        if (path == selected) {
            list->setCurrentItem(item);
        }
    }
    if (list->currentItem() == nullptr && list->count() > 0) {
        list->setCurrentRow(0);
    }
    UpdateState();
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
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole + 1).toString() == image) {
                list->item(i)->setIcon(icon);
            }
        }
    });
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
    int added = 0;
    int skipped = 0;
    QString last_added;
    for (const QString& path : paths) {
        if (!IsTagSize(QFileInfo{path}.size())) {
            ++skipped;
            continue;
        }
        const QString canonical = CanonicalPath(path);
        const bool known = std::any_of(library.begin(), library.end(), [&](const QString& entry) {
            return CanonicalPath(entry) == canonical;
        });
        if (known) {
            continue;
        }
        library.append(canonical);
        last_added = canonical;
        ++added;
    }
    if (added > 0) {
        library.sort(Qt::CaseInsensitive);
        emit LibraryChanged();
    }
    Populate();
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).toString() == last_added) {
            list->setCurrentRow(i);
            break;
        }
    }
    status->setText(skipped == 0
                        ? tr("Added %n amiibo.", nullptr, added)
                        : tr("Added %1 amiibo. %2 file(s) were not amiibo dumps and were skipped.")
                              .arg(added)
                              .arg(skipped));
}

void AmiiboLibraryDialog::RemoveSelected() {
    const auto items = list->selectedItems();
    if (items.isEmpty()) {
        return;
    }
    for (const QListWidgetItem* item : items) {
        UISettings::values.amiibo_library.removeAll(item->data(Qt::UserRole).toString());
    }
    emit LibraryChanged();
    Populate();
}

void AmiiboLibraryDialog::TapSelected() {
    const QListWidgetItem* item = list->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    if (!QFileInfo::exists(path)) {
        status->setText(tr("%1 is no longer at %2.").arg(DisplayName(path), path));
        return;
    }

    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    switch (virtual_amiibo->GetCurrentState()) {
    case State::TagNearby:
        if (CanonicalPath(QString::fromStdString(virtual_amiibo->GetLastFilePath())) == path) {
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
    switch (input_subsystem->GetVirtualAmiibo()->LoadAmiibo(path.toStdString())) {
    case Info::Success:
        MoveToFront(path);
        status->setText(tr("%1 is on the reader.").arg(DisplayName(path)));
        break;
    case Info::NotAnAmiibo:
        status->setText(tr("%1 is not a valid amiibo.").arg(DisplayName(path)));
        break;
    case Info::UnableToLoad:
        status->setText(tr("%1 could not be opened.").arg(DisplayName(path)));
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
    const qsizetype index = library.indexOf(path);
    if (index <= 0) {
        return;
    }
    library.move(index, 0);
    emit LibraryChanged();
    Populate();
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

    const QString on_reader =
        state == State::TagNearby
            ? CanonicalPath(QString::fromStdString(virtual_amiibo->GetLastFilePath()))
            : QString{};
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        QFont font = item->font();
        const bool bold = !on_reader.isEmpty() && item->data(Qt::UserRole).toString() == on_reader;
        if (font.bold() != bold) {
            font.setBold(bold);
            item->setFont(font);
        }
    }

    if (!pending_path.isEmpty()) {
        status->setText(tr("%1 will be tapped when the game scans for amiibo.")
                            .arg(DisplayName(pending_path)));
    } else if (UISettings::values.amiibo_library.isEmpty()) {
        status->setText(tr("Add amiibo files or a folder of them, or drop them here."));
    } else if (status->text().isEmpty()) {
        switch (state) {
        case State::WaitingForAmiibo:
            status->setText(tr("The game is scanning for amiibo."));
            break;
        case State::TagNearby:
            status->setText(tr("%1 is on the reader.").arg(DisplayName(on_reader)));
            break;
        default:
            status->setText(tr("Pick an amiibo; it is tapped when the game scans."));
            break;
        }
    }

    const QListWidgetItem* current = list->currentItem();
    const bool current_on_reader =
        current != nullptr && current->data(Qt::UserRole).toString() == on_reader;
    tap_button->setText(current_on_reader ? tr("Lift") : tr("Tap"));
    tap_button->setEnabled(current != nullptr);
    remove_tag_button->setEnabled(state == State::TagNearby || !pending_path.isEmpty());
    forget_button->setEnabled(!list->selectedItems().isEmpty());
}
