#pragma once

#include <QDialog>

#include <QString>
#include <vector>

#include "canvas/core/actions/keymap.hpp"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QMenuBar;
class QTableWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace canvas::gui {
class KeyboardMapWidget;
}

namespace canvas::gui::shortcuts {
class ShortcutManager;
}

namespace canvas::gui {

class KeyboardCustomizationDialog final : public QDialog {
    Q_OBJECT
public:
    explicit KeyboardCustomizationDialog(QMenuBar* menubar, shortcuts::ShortcutManager* manager,
                                         QWidget* parent = nullptr);
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void reject() override;
private:
    void rebuild_all();
    void rebuild_keyboard();
    void rebuild_active_key();
    void rebuild_commands();
    void refresh_dirty();
    void start_capture(const QString& command);
    void stop_capture();
    void assign_capture(const QString& shortcut);
    void clear_command(const QString& command);
    void save();

    QMenuBar* menubar_ = nullptr;
    shortcuts::ShortcutManager* manager_ = nullptr;
    QString preset_;
    std::vector<canvas::core::actions::KeyBinding> custom_;
    std::vector<canvas::core::actions::KeyBinding> bindings_;
    QString active_key_;
    QString selected_command_;
    QString capture_command_;

    QComboBox* preset_combo_ = nullptr;
    KeyboardMapWidget* keyboard_ = nullptr;
    QToolButton* shift_button_ = nullptr;
    QToolButton* ctrl_button_ = nullptr;
    QToolButton* alt_button_ = nullptr;
    QLineEdit* active_readout_ = nullptr;
    QTableWidget* active_table_ = nullptr;
    QComboBox* filter_combo_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* commands_ = nullptr;
    QLabel* conflict_label_ = nullptr;
    QLabel* hint_label_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

}
