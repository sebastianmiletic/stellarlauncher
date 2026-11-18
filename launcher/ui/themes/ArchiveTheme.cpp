// SPDX-License-Identifier: GPL-3.0-only
#include "ArchiveTheme.h"

QPalette ArchiveTheme::colorScheme()
{
    QPalette p = DarkTheme::colorScheme();
    p.setColor(QPalette::Window, QColor("#1b1b20"));
    p.setColor(QPalette::WindowText, QColor("#f0eff4"));
    p.setColor(QPalette::Base, QColor("#111114"));
    p.setColor(QPalette::AlternateBase, QColor("#232329"));
    p.setColor(QPalette::Text, QColor("#f0eff4"));
    p.setColor(QPalette::Button, QColor("#292930"));
    p.setColor(QPalette::ButtonText, QColor("#f0eff4"));
    p.setColor(QPalette::Highlight, QColor("#c5acff"));
    p.setColor(QPalette::HighlightedText, QColor("#211633"));
    p.setColor(QPalette::Link, QColor("#9ecbff"));
    p.setColor(QPalette::ToolTipBase, QColor("#303039"));
    p.setColor(QPalette::ToolTipText, QColor("#f0eff4"));
    p.setColor(QPalette::PlaceholderText, QColor("#aaa8b3"));
    return fadeInactive(p, 0.45, QColor("#1b1b20"));
}

QString ArchiveTheme::appStyleSheet()
{
    return QStringLiteral(R"(
        QToolBar { border: none; spacing: 3px; padding: 5px; }
        QToolBar#mainToolBar { background: #1b1b20; border-bottom: 1px solid #35343d; }
        QToolBar#instanceToolBar { background: #1b1b20; border-left: 1px solid #35343d; padding: 8px 5px; }
        QToolBar#instanceToolBar QToolButton { padding: 3px 6px; }
        QToolBar::separator { background: #35343d; width: 1px; height: 1px; margin: 8px; }
        QToolButton { border: 1px solid transparent; border-radius: 9px; padding: 4px 7px; }
        QToolButton:hover { background: #303039; }
        QToolButton:pressed, QToolButton:checked { background: #3b304f; }
        QToolButton:focus, QPushButton:focus { border: 1px solid #9ecbff; }
        QToolButton:disabled, QPushButton:disabled { color: #9b99a5; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance { background: #c5acff; color: #211633; font-weight: 600; padding: 7px 12px; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:hover { background: #d7c4ff; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:pressed { background: #ad8ae6; }
        QToolBar#instanceToolBar QToolButton#actionLaunchInstance:disabled { background: #303039; color: #9b99a5; }
        QPushButton { background: #292930; border: 1px solid #52515c; border-radius: 9px; padding: 5px 10px; }
        QPushButton:hover { background: #383742; }
        QPushButton:pressed { background: #454450; }
        QPushButton:default { border-color: #c5acff; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { border: 1px solid #52515c; border-radius: 8px; padding: 4px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #c5acff; }
        QMenu { border: 1px solid #52515c; padding: 5px; }
        QMenu::item { padding: 7px 22px; border-radius: 6px; }
        QMenu::item:selected { background: #3b304f; }
        QStatusBar { border-top: 1px solid #35343d; color: #aaa8b3; padding: 2px 6px; }
        QToolBar#mainToolBar::handle, QToolBar#instanceToolBar::handle { background: transparent; }
        QStatusBar::item { border: none; }
        QLabel#archiveWordmark { font-size: 13px; font-weight: 600; padding-right: 4px; }
        QWidget#archivePage { background: #111114; }
    )");
}
