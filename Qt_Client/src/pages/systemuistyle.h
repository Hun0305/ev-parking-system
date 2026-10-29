#ifndef SYSTEMUISTYLE_H
#define SYSTEMUISTYLE_H

#include <QString>

namespace SystemUiStyle {

inline constexpr int ConnectionsMaxWidth = 960;
inline constexpr int DiagnosticsMaxWidth = 1080;
inline constexpr int LogsMaxWidth = 1080;
inline constexpr int TestToolsMaxWidth = 900;

inline QString pageStyleSheet()
{
    return QStringLiteral(
        "QTabWidget::pane { background:#f4f7f9;border:1px solid #d5dee3;"
        "border-radius:10px;top:-1px; }"
        "QTabBar::tab { background:#e8edf0;color:#526772;border:1px solid #d5dee3;"
        "border-bottom:none;min-width:96px;padding:10px 12px;font-weight:700; }"
        "QTabBar::tab:first { border-top-left-radius:7px; }"
        "QTabBar::tab:last { border-top-right-radius:7px; }"
        "QTabBar::tab:selected { background:#ffffff;color:#173b4d;"
        "border-top:3px solid #ef7d00;padding-top:8px; }"
        "QTabBar::tab:hover:!selected { background:#f2f5f7;color:#263238; }"
        "QScrollArea { background:#f4f7f9;border:none; }"
        "QWidget[systemTabContent=\"true\"] { background:#f4f7f9; }"
        "QGroupBox { background:#ffffff;color:#29434e;border:1px solid #d5dee3;"
        "border-radius:10px;margin-top:16px;padding:14px 12px 12px 12px;"
        "font-size:13px;font-weight:800; }"
        "QGroupBox::title { subcontrol-origin:margin;subcontrol-position:top left;"
        "left:12px;padding:0 6px;background:#ffffff;color:#29434e; }"
        "QLineEdit, QComboBox, QSpinBox { background:#ffffff;color:#263238;"
        "border:1px solid #b8c6cd;border-radius:6px;min-height:34px;"
        "padding:0 9px;selection-background-color:#406274; }"
        "QSpinBox[uiDurationInput=\"true\"] { padding:0 28px 0 8px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border:2px solid #ef7d00; }"
        "QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {"
        "background:#eef1f3;color:#8b979d;border-color:#d5dde1; }"
        "QPushButton { min-height:36px;border-radius:6px;padding:0 14px;font-weight:750; }"
        "QPushButton[uiActionRole=\"primary\"] { background:#ef7d00;color:#ffffff;"
        "border:1px solid #d86f00; }"
        "QPushButton[uiActionRole=\"primary\"]:hover { background:#ff8f1f; }"
        "QPushButton[uiActionRole=\"secondary\"] { background:#ffffff;color:#294b5a;"
        "border:1px solid #8ca7b3; }"
        "QPushButton[uiActionRole=\"secondary\"]:hover { background:#edf4f7;"
        "border-color:#5f8292; }"
        "QPushButton[uiActionRole=\"warning\"] { background:#fff8e1;color:#7a4f00;"
        "border:1px solid #e7bd58;text-align:left; }"
        "QPushButton[uiActionRole=\"warning\"]:hover { background:#fff1c2;"
        "border-color:#d79a16; }"
        "QPushButton[uiActionRole=\"danger\"] { background:#ffffff;color:#8a2f2f;"
        "border:1px solid #d9a4a4; }"
        "QPushButton[uiActionRole=\"danger\"]:hover { background:#fff1f1;"
        "border-color:#c96f6f; }"
        "QPushButton[uiWideAction=\"true\"] { text-align:left;padding-left:14px; }"
        "QPushButton:disabled { background:#dfe4e7;color:#8b979d;border:1px solid #d0d7db; }"
        "QFrame[uiCard=\"true\"], QFrame[uiPanel=\"true\"] { background:#ffffff;"
        "border:1px solid #d5dee3;border-radius:10px; }"
        "QLabel[uiBanner=\"neutral\"] { background:#f8fafb;color:#526772;"
        "border:1px solid #d5dee3;border-radius:8px;padding:10px 12px;font-weight:700; }"
        "QLabel[uiBanner=\"info\"] { background:#eaf3f7;color:#17475c;"
        "border:1px solid #a8c6d4;border-radius:8px;padding:10px 12px;font-weight:700; }"
        "QLabel[uiBanner=\"warning\"] { background:#fff7df;color:#7a4f00;"
        "border:1px solid #e7c56d;border-radius:8px;padding:10px 12px;font-weight:700; }"
        "QLabel[uiBanner=\"success\"] { background:#edf7ed;color:#285b2b;"
        "border:1px solid #acd3ae;border-radius:8px;padding:10px 12px;font-weight:700; }"
        "QLabel[uiBanner=\"danger\"] { background:#fff0f1;color:#8b2530;"
        "border:1px solid #e2a8ae;border-radius:8px;padding:10px 12px;font-weight:700; }"
        "QLabel[uiReadOnlyValue=\"true\"] { background:#f8fafb;color:#29434e;"
        "border:1px solid #d5dee3;border-radius:6px;padding:7px 9px;font-weight:750; }"
        "QLabel[uiCaption=\"true\"] { color:#607681;font-size:12px;font-weight:800; }"
        "QLabel[uiDetail=\"true\"] { color:#5f727b;font-size:11px; }"
        "QTableWidget { background:#ffffff;alternate-background-color:#f5f8f9;"
        "border:1px solid #d5dee3;border-radius:8px;gridline-color:#e1e7ea; }"
        "QTableWidget::item { padding:5px 7px;border-bottom:1px solid #edf1f3; }"
        "QTableWidget::item:selected { background:#dceaf0;color:#17313c; }"
        "QHeaderView::section { background:#eaf0f3;color:#3d5662;border:none;"
        "border-right:1px solid #d5dee3;border-bottom:1px solid #d5dee3;"
        "padding:7px 8px;font-weight:800; }"
        "QCheckBox, QRadioButton { spacing:7px;color:#455a64; }"
        "QRadioButton { min-height:32px;font-weight:750; }"
        "QRadioButton::indicator { width:17px;height:17px; }"
        "QRadioButton::indicator:unchecked { background:#ffffff;border:2px solid #8ca7b3;"
        "border-radius:9px; }"
        "QRadioButton::indicator:checked { background:#ffffff;border:5px solid #ef7d00;"
        "border-radius:9px; }"
    );
}

} // namespace SystemUiStyle

#endif
