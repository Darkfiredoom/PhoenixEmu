// SPDX-FileCopyrightText: 2026 PhoenixEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <numbers>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "common/scm_rev.h"
#include "phoenix/configuration/settings_shell.h"

namespace SettingsShell {
namespace {

QColor ColourOf(Icon icon) {
    switch (icon) {
    case Icon::General:
        return QColor(0xFF, 0x8A, 0x3D);
    case Icon::Library:
        return QColor(0x7E, 0xE0, 0x8A);
    case Icon::Graphics:
        return QColor(0xFF, 0xC8, 0x3D);
    case Icon::Audio:
        return QColor(0x7F, 0xB2, 0xFF);
    case Icon::Controls:
        return QColor(0xC7, 0x92, 0xFF);
    case Icon::System:
        return QColor(0xFF, 0x6B, 0x6B);
    case Icon::Multiplayer:
        return QColor(0x5E, 0xD6, 0xC8);
    case Icon::Shortcuts:
        return QColor(0xF5, 0xD7, 0x6E);
    case Icon::Cpu:
        return QColor(0xFF, 0x9F, 0x6B);
    case Icon::Debug:
        return QColor(0xB8, 0xB0, 0xA8);
    case Icon::Applets:
        return QColor(0x8A, 0xD0, 0xFF);
    case Icon::Overview:
        return QColor(0xFF, 0x7A, 0xB6);
    case Icon::FrameRate:
        return QColor(0xFF, 0xD2, 0x3F);
    case Icon::Mods:
        return QColor(0xB4, 0x8C, 0xFF);
    case Icon::Artwork:
        return QColor(0xFF, 0xB3, 0x47);
    case Icon::Network:
        return QColor(0x6F, 0xD3, 0xFF);
    }
    return QColor(0xFF, 0xC8, 0x3D);
}

/// Draws `icon` in a 20 x 20 box.
void Draw(QPainter& p, Icon icon) {
    switch (icon) {
    case Icon::General:
    case Icon::Network: {
        if (icon == Icon::Network) {
            // Wireless: three arcs and a dot.
            for (int i = 0; i < 3; ++i) {
                const qreal r = 3.5 + i * 3.4;
                p.drawArc(QRectF(10 - r, 14 - r, 2 * r, 2 * r), 45 * 16, 90 * 16);
            }
            p.drawPoint(QPointF(10, 14.5));
            break;
        }
        p.drawEllipse(QRectF(2.5, 2.5, 15, 15));
        p.drawEllipse(QRectF(6.5, 2.5, 7, 15));
        p.drawLine(QPointF(2.5, 10), QPointF(17.5, 10));
        break;
    }
    case Icon::Library: {
        QPainterPath folder;
        folder.moveTo(2.5, 5.5);
        folder.lineTo(7.5, 5.5);
        folder.lineTo(9, 7.5);
        folder.lineTo(17.5, 7.5);
        folder.lineTo(17.5, 15.5);
        folder.lineTo(2.5, 15.5);
        folder.closeSubpath();
        p.drawPath(folder);
        break;
    }
    case Icon::Graphics:
        p.drawRoundedRect(QRectF(2.5, 3.5, 15, 10), 1.8, 1.8);
        p.drawLine(QPointF(7, 17), QPointF(13, 17));
        p.drawLine(QPointF(10, 13.5), QPointF(10, 17));
        break;
    case Icon::Audio: {
        QPainterPath speaker;
        speaker.moveTo(3, 8);
        speaker.lineTo(6, 8);
        speaker.lineTo(10, 4.5);
        speaker.lineTo(10, 15.5);
        speaker.lineTo(6, 12);
        speaker.lineTo(3, 12);
        speaker.closeSubpath();
        p.drawPath(speaker);
        p.drawArc(QRectF(10, 7, 4, 6), -60 * 16, 120 * 16);
        p.drawArc(QRectF(10, 4.5, 7, 11), -60 * 16, 120 * 16);
        break;
    }
    case Icon::Controls: {
        QPainterPath pad;
        pad.addRoundedRect(QRectF(2, 5.5, 16, 10), 5, 5);
        p.drawPath(pad);
        p.drawLine(QPointF(5, 10.5), QPointF(8, 10.5));
        p.drawLine(QPointF(6.5, 9), QPointF(6.5, 12));
        p.drawPoint(QPointF(13, 9.5));
        p.drawPoint(QPointF(15, 11.5));
        break;
    }
    case Icon::System:
    case Icon::Cpu:
        p.drawRoundedRect(QRectF(5, 5, 10, 10), 1.5, 1.5);
        p.drawRect(QRectF(8, 8, 4, 4));
        for (const qreal at : {7.5, 10.0, 12.5}) {
            p.drawLine(QPointF(at, 2.5), QPointF(at, 5));
            p.drawLine(QPointF(at, 15), QPointF(at, 17.5));
            p.drawLine(QPointF(2.5, at), QPointF(5, at));
            p.drawLine(QPointF(15, at), QPointF(17.5, at));
        }
        break;
    case Icon::Multiplayer:
        p.drawEllipse(QRectF(4, 3.5, 5, 5));
        p.drawArc(QRectF(1.5, 10, 10, 9), 0, 180 * 16);
        p.drawEllipse(QRectF(11.5, 4.5, 4.5, 4.5));
        p.drawArc(QRectF(10.5, 10.5, 8, 8), 0, 180 * 16);
        break;
    case Icon::Shortcuts:
        p.drawRoundedRect(QRectF(2, 5, 16, 10), 2, 2);
        for (const qreal x : {5.0, 8.0, 11.0, 14.0}) {
            p.drawPoint(QPointF(x, 8.5));
        }
        p.drawLine(QPointF(6.5, 12), QPointF(13.5, 12));
        break;
    case Icon::Debug:
        p.drawEllipse(QRectF(6.5, 6, 7, 10));
        p.drawLine(QPointF(10, 6), QPointF(10, 16));
        for (const qreal y : {8.5, 11.5, 14.5}) {
            p.drawLine(QPointF(6.5, y), QPointF(3.5, y - 1));
            p.drawLine(QPointF(13.5, y), QPointF(16.5, y - 1));
        }
        p.drawArc(QRectF(7.5, 2.5, 5, 5), 0, 180 * 16);
        break;
    case Icon::Applets:
        for (const qreal x : {3.0, 11.0}) {
            for (const qreal y : {3.0, 11.0}) {
                p.drawRoundedRect(QRectF(x, y, 6, 6), 1.6, 1.6);
            }
        }
        break;
    case Icon::Overview: {
        QPainterPath star;
        for (int i = 0; i < 10; ++i) {
            const qreal angle = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
            const qreal r = i % 2 == 0 ? 8.0 : 3.6;
            const QPointF at{10 + r * std::cos(angle), 10.5 + r * std::sin(angle)};
            i == 0 ? star.moveTo(at) : star.lineTo(at);
        }
        star.closeSubpath();
        p.drawPath(star);
        break;
    }
    case Icon::FrameRate: {
        QPainterPath bolt;
        bolt.moveTo(11.5, 2);
        bolt.lineTo(4.5, 11);
        bolt.lineTo(9.5, 11);
        bolt.lineTo(8.5, 18);
        bolt.lineTo(15.5, 9);
        bolt.lineTo(10.5, 9);
        bolt.closeSubpath();
        p.drawPath(bolt);
        break;
    }
    case Icon::Mods: {
        QPainterPath piece;
        piece.addRoundedRect(QRectF(3, 6, 11, 11), 1.8, 1.8);
        p.drawPath(piece);
        p.drawEllipse(QRectF(6.5, 2.5, 4, 4));
        p.drawEllipse(QRectF(13, 9.5, 4, 4));
        break;
    }
    case Icon::Artwork:
        p.drawRoundedRect(QRectF(2.5, 4, 15, 12), 1.8, 1.8);
        p.drawEllipse(QRectF(5, 6.5, 3, 3));
        p.drawPolyline(QPolygonF{QPointF(3, 15), QPointF(8, 10.5), QPointF(11, 13),
                                 QPointF(13.5, 10.5), QPointF(17, 14)});
        break;
    }
}

} // Anonymous namespace

QIcon MakeIcon(Icon icon) {
    QIcon result;
    for (const qreal scale : {1.0, 2.0}) {
        QPixmap pixmap(QSize(20, 20) * scale);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(scale);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(ColourOf(icon), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        Draw(p, icon);
        p.end();
        result.addPixmap(pixmap);
    }
    return result;
}

Sidebar BuildSidebar(QWidget* parent, QListWidget* list, const QString& title,
                     const QString& search_hint) {
    Sidebar sidebar;
    sidebar.frame = new QFrame(parent);
    sidebar.frame->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar.frame->setFixedWidth(224);
    auto* const column = new QVBoxLayout(sidebar.frame);
    column->setContentsMargins(14, 16, 14, 14);
    column->setSpacing(10);
    sidebar.top = new QVBoxLayout;
    sidebar.top->setSpacing(8);
    column->addLayout(sidebar.top);
    sidebar.title = new QLabel(title, sidebar.frame);
    sidebar.title->setObjectName(QStringLiteral("SettingsTitle"));
    column->addWidget(sidebar.title);
    sidebar.search = new QLineEdit(sidebar.frame);
    sidebar.search->setObjectName(QStringLiteral("SettingsSearch"));
    sidebar.search->setPlaceholderText(search_hint);
    sidebar.search->setClearButtonEnabled(true);
    column->addWidget(sidebar.search);
    list->setParent(sidebar.frame);
    list->setObjectName(QStringLiteral("SettingsPages"));
    list->setMinimumWidth(0);
    list->setMaximumWidth(QWIDGETSIZE_MAX);
    list->setIconSize(QSize(20, 20));
    list->setFrameShape(QFrame::NoFrame);
    list->setSpacing(1);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    column->addWidget(list, 1);
    sidebar.footer = new QLabel(
        QStringLiteral("ⓘ  PhoenixEmu · %1").arg(QString::fromUtf8(Common::g_build_version)),
        sidebar.frame);
    sidebar.footer->setObjectName(QStringLiteral("SettingsFooter"));
    column->addWidget(sidebar.footer);
    return sidebar;
}

Header BuildHeader(QWidget* parent) {
    Header header;
    header.widget = new QWidget(parent);
    header.widget->setObjectName(QStringLiteral("SettingsHeader"));
    auto* const row = new QHBoxLayout(header.widget);
    row->setContentsMargins(4, 6, 4, 10);
    auto* const text = new QVBoxLayout;
    text->setSpacing(2);
    header.title = new QLabel(header.widget);
    header.title->setObjectName(QStringLiteral("SettingsPageTitle"));
    header.subtitle = new QLabel(header.widget);
    header.subtitle->setObjectName(QStringLiteral("SettingsPageSubtitle"));
    header.subtitle->setWordWrap(true);
    text->addWidget(header.title);
    text->addWidget(header.subtitle);
    row->addLayout(text, 1);
    header.right = new QHBoxLayout;
    header.right->setSpacing(8);
    row->addLayout(header.right);
    return header;
}

QButtonGroup* BuildModeSwitch(QWidget* parent, QHBoxLayout* into, bool advanced) {
    auto* const frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("SettingsModeSwitch"));
    auto* const row = new QHBoxLayout(frame);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(2);
    auto* const group = new QButtonGroup(frame);
    group->setExclusive(true);
    const QStringList names{QObject::tr("Everyday"), QObject::tr("Advanced")};
    for (int i = 0; i < 2; ++i) {
        auto* const button = new QPushButton(names[i], frame);
        button->setObjectName(QStringLiteral("SettingsModeButton"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked((i == 1) == advanced);
        group->addButton(button, i);
        row->addWidget(button);
    }
    into->addWidget(frame, 0, Qt::AlignTop);
    return group;
}

bool ContainsText(const QWidget* root, const QString& query) {
    if (query.isEmpty()) {
        return true;
    }
    const auto has = [&](const QString& text) {
        return text.contains(query, Qt::CaseInsensitive);
    };
    for (const auto* label : root->findChildren<QLabel*>()) {
        if (has(label->text())) {
            return true;
        }
    }
    for (const auto* button : root->findChildren<QAbstractButton*>()) {
        if (has(button->text())) {
            return true;
        }
    }
    for (const auto* group : root->findChildren<QGroupBox*>()) {
        if (has(group->title())) {
            return true;
        }
    }
    for (const auto* combo : root->findChildren<QComboBox*>()) {
        for (int i = 0; i < combo->count(); ++i) {
            if (has(combo->itemText(i))) {
                return true;
            }
        }
    }
    return false;
}

} // namespace SettingsShell
