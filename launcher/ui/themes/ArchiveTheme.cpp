// SPDX-License-Identifier: GPL-3.0-only
#include "ArchiveTheme.h"
#include "Application.h"
#include "settings/SettingsObject.h"
#include "ui/themes/StellarAccent.h"

QPalette ArchiveTheme::colorScheme()
{
    QPalette p = DarkTheme::colorScheme();
    p.setColor(QPalette::Window, QColor("#161616"));
    p.setColor(QPalette::WindowText, QColor("#eeeeee"));
    p.setColor(QPalette::Base, QColor("#101010"));
    p.setColor(QPalette::AlternateBase, QColor("#222222"));
    p.setColor(QPalette::Text, QColor("#eeeeee"));
    p.setColor(QPalette::Button, QColor("#242424"));
    p.setColor(QPalette::ButtonText, QColor("#eeeeee"));
    p.setColor(QPalette::Highlight, stellarAccent(APPLICATION->settings()->get("StellarAccent").toString()));
    p.setColor(QPalette::HighlightedText, QColor("#151515"));
    p.setColor(QPalette::Link, QColor("#e0e0e0"));
    p.setColor(QPalette::ToolTipBase, QColor("#292929"));
    p.setColor(QPalette::ToolTipText, QColor("#eeeeee"));
    p.setColor(QPalette::PlaceholderText, QColor("#adadad"));
    return fadeInactive(p, 0.45, QColor("#161616"));
}

QString ArchiveTheme::appStyleSheet()
{
    return QStringLiteral(R"(
        QToolBar { border: none; spacing: 3px; padding: 5px; }
        QToolBar#mainToolBar { background: #161616; border-bottom: 1px solid #303030; }
        QToolBar#instanceToolBar { background: #161616; border-left: 1px solid #303030; padding: 8px 5px; }
        QToolBar#instanceToolBar QToolButton { padding: 3px 6px; }
        QToolBar::separator { background: #303030; width: 1px; height: 1px; margin: 8px; }
        QToolButton { border: 1px solid transparent; border-radius: 9px; padding: 4px 7px; }
        QToolBar#mainToolBar QToolButton#archiveAccountsButton { padding: 2px 4px; }
        QToolButton:hover { background: #292929; }
        QToolButton:pressed, QToolButton:checked { background: #363636; }
        QToolButton:focus, QPushButton:focus { border: 1px solid %1; }
        QToolButton:disabled, QPushButton:disabled { color: #929292; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance { background: #303030; color: #f2f2f2; border-color: #777777; font-weight: 600; padding: 7px 12px; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:hover { background: #424242; border-color: #a0a0a0; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:pressed { background: #222222; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:disabled { background: #202020; border-color: #383838; color: #929292; }
        QPushButton { background: #242424; border: 1px solid #444444; border-radius: 9px; padding: 5px 10px; }
        QPushButton:hover { background: #323232; }
        QPushButton:pressed { background: #3c3c3c; }
        QPushButton:default { border-color: #eaeaea; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { border: 1px solid #444444; border-radius: 8px; padding: 4px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #eaeaea; }
        QComboBox { padding: 5px 25px 5px 9px; background: #242424; }
        QComboBox:hover:enabled { background: #303035; border-color: #686574; }
        QComboBox::drop-down:on { background: #383640; }
        QComboBox::drop-down { border: none; width: 23px; }
        QComboBox::down-arrow { image: url(:/stellar-gallery/chevron.svg); width: 12px; height: 8px; }
        QComboBox QAbstractItemView { background: #1d1d1d; color: #eeeeee; padding: 4px; border: 1px solid #424242; }
        QComboBox QAbstractItemView::item { padding: 5px 7px; }
        QLineEdit { background: #1b1b1b; padding: 6px 8px; }
        QMenu { background: #1d1d1d; color: #eeeeee; border: 1px solid #424242; padding: 4px; menu-scrollable: 1; }
        QMenu::item { padding: 5px 28px 5px 25px; }
        QMenu::item:selected { background: #383838; color: #f2f2f2; border-radius: 4px; }
        QMenu::item:disabled { color: #929292; }
        QMenu::separator { height: 1px; background: #383838; margin: 4px 6px; }
        QMenu::indicator { width: 12px; height: 12px; }
        QMenuBar { background: #161616; color: #eeeeee; }
        QMenuBar::item:selected { background: #363636; }
        QToolTip { background: #242424; color: #eeeeee; border: 1px solid #484848; padding: 4px; }
        QScrollBar:vertical { width: 0px; border: none; }
        QScrollBar:horizontal { height: 0px; border: none; }
        QTabBar::tab { padding: 6px 12px; }
        QStatusBar { border-top: 1px solid #303030; color: #adadad; padding: 2px 6px; }
        QToolBar#mainToolBar::handle, QToolBar#instanceToolBar::handle { background: transparent; }
        QStatusBar::item { border: none; }
        QLabel#archiveWordmark { font-size: 13px; font-weight: 600; padding-right: 4px; }
        QWidget#archivePage { background: #101010; }
    )").arg(stellarAccent(APPLICATION->settings()->get("StellarAccent").toString()).name());
}
