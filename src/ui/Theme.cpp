#include "ui/Theme.h"

QColor Theme::accent()
{
    return QColor(QStringLiteral("#3D8B7A"));
}

QString Theme::stylesheet(bool /*dark*/)
{
    // Encre sombre + selection miel : lisible meme si l'OS est en theme sombre.
    return QStringLiteral(
               "QWidget { background: #F4F1EA; color: #1A1208; font-size: 13px; }"
               "QMainWindow, QDialog { background: #F4F1EA; color: #1A1208; }"
               "QGroupBox, QTabWidget::pane { background: #FFFEFA; color: #1A1208;"
               "  border: 1px solid #D4CBB8; border-radius: 8px; }"
               "QTabBar::tab { background: #E8E0D2; color: #1A1208; padding: 8px 14px; }"
               "QTabBar::tab:selected { background: #FFFEFA; color: #1A1208; font-weight: 600; }"
               "QHeaderView::section { background: #EFE8DA; color: #1A1208; border: none; padding: 6px; }"
               "QTableView, QTreeView, QTableWidget, QTreeWidget, QListView {"
               "  background: #FFFEFA; color: #1A1208; gridline-color: #E4DCCB;"
               "  alternate-background-color: #F6F1E6;"
               "  selection-background-color: #F0C94A; selection-color: #1A1208; }"
               "QTableView::item:selected, QTreeView::item:selected,"
               "QTableWidget::item:selected, QTreeWidget::item:selected,"
               "QListView::item:selected {"
               "  background: #F0C94A; color: #1A1208; }"
               "QTableView::item:hover, QTreeView::item:hover {"
               "  background: #F7E7A8; color: #1A1208; }"
               "QPlainTextEdit, QTextEdit {"
               "  background: #FFFEFA; color: #1A1208;"
               "  selection-background-color: #F0C94A; selection-color: #1A1208; }"
               "QLineEdit, QSpinBox, QComboBox {"
               "  background: #FFFEFA; color: #1A1208; border: 1px solid #D4CBB8;"
               "  border-radius: 4px; padding: 5px;"
               "  selection-background-color: #F0C94A; selection-color: #1A1208; }"
               "QComboBox QAbstractItemView { background: #FFFEFA; color: #1A1208;"
               "  selection-background-color: #F0C94A; selection-color: #1A1208; }"
               "QPushButton { background: #2F6F62; color: #FFFEFA; border: none;"
               "  border-radius: 6px; padding: 8px 14px; }"
               "QPushButton:hover { background: #255A50; color: #FFFEFA; }"
               "QPushButton:disabled { background: #D4CBB8; color: #5C5348; }"
               "QMenu { background: #FFFEFA; color: #1A1208; }"
               "QMenu::item:selected { background: #F0C94A; color: #1A1208; }"
               "QStatusBar { background: #EFE8DA; color: #1A1208; }"
               "QToolBar { background: #EFE8DA; color: #1A1208; border: none; }"
               "QLabel { color: #1A1208; }"
               "QCheckBox { color: #1A1208; spacing: 8px; }"
               "QProgressBar { border: 1px solid #B8A878; background: #FFF8E8; color: #1A1208;"
               "  text-align: center; font-weight: 700; font-size: 13px; min-height: 22px; }"
               "QProgressBar::chunk { background: #F0C94A; }"
               "QScrollBar:vertical { background: #EFE8DA; width: 12px; }"
               "QScrollBar::handle:vertical { background: #C4B89A; min-height: 24px; }"
               );
}
