#include "UX/KeyboardCustomizationDialog.hpp"

#include "UX/theme.hpp"
#include "Widgets/keyboard_map_widget.hpp"
#include "features/shortcuts/shortcut_catalog.hpp"
#include "features/shortcuts/shortcut_manager.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <unordered_map>
#include <unordered_set>

namespace canvas::gui {

namespace {

QString to_text(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString command_shortcuts(const std::vector<canvas::core::actions::KeyBinding>& bindings,
                          const QString& command) {
    QStringList out;
    for (const auto& binding : bindings) {
        if (binding.command != command.toStdString()) continue;
        const std::string shortcut = canvas::core::actions::canonical_shortcut(binding.shortcut);
        if (!shortcut.empty()) out.append(QString::fromStdString(shortcut));
    }
    out.removeDuplicates();
    return out.join(QStringLiteral(" / "));
}

bool command_matches(const shortcuts::CommandBinding& command, const QString& shortcuts,
                     const QString& query) {
    if (query.isEmpty()) return true;
    const QString text = QString::fromStdString(command.title) + QLatin1Char(' ') +
                         QString::fromStdString(command.category) + QLatin1Char(' ') +
                         QString::fromStdString(command.id) + QLatin1Char(' ') + shortcuts;
    return text.toLower().contains(query);
}

std::unordered_set<std::string>
conflicted_shortcuts(const std::vector<canvas::core::actions::KeyBinding>& bindings) {
    std::unordered_set<std::string> out;
    for (const auto& conflict : canvas::core::actions::find_shortcut_conflicts(bindings))
        out.insert(conflict.shortcut);
    return out;
}

}

KeyboardCustomizationDialog::KeyboardCustomizationDialog(QMenuBar* menubar,
                                                         shortcuts::ShortcutManager* manager,
                                                         QWidget* parent)
    : QDialog(parent), menubar_(menubar), manager_(manager) {
    setObjectName(QStringLiteral("KeyboardCustomizationDialog"));
    setWindowTitle(tr("Keyboard Customization"));
    setModal(true);
    resize(1280, 820);
    setMinimumSize(1080, 700);

    preset_ = QStringLiteral("resolve");
    if (manager_ != nullptr) {
        preset_ = QString::fromStdString(manager_->preset());
        custom_ = manager_->custom();
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(12);

    auto* title_row = new QHBoxLayout();
    title_row->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("Keyboard Customization"), this);
    QFont title_font = title->font();
    title_font.setPointSize(16);
    title_font.setBold(true);
    title->setFont(title_font);
    title_row->addWidget(title);
    title_row->addStretch(1);
    preset_combo_ = new QComboBox(this);
    preset_combo_->setObjectName(QStringLiteral("KeymapPresetCombo"));
    for (const shortcuts::Preset preset : shortcuts::presets())
        preset_combo_->addItem(to_text(shortcuts::preset_title(preset)),
                               to_text(shortcuts::preset_id(preset)));
    preset_combo_->setCurrentIndex(preset_combo_->findData(preset_));
    title_row->addWidget(preset_combo_);
    root->addLayout(title_row);

    auto* keyboard_row = new QHBoxLayout();
    keyboard_row->setContentsMargins(0, 0, 0, 0);
    keyboard_row->setSpacing(12);
    auto* modifiers = new QVBoxLayout();
    modifiers->setContentsMargins(0, 0, 0, 0);
    modifiers->setSpacing(10);
    modifiers->addStretch(1);
    shift_button_ = new QToolButton(this);
    ctrl_button_ = new QToolButton(this);
    alt_button_ = new QToolButton(this);
    for (QToolButton* button : {shift_button_, ctrl_button_, alt_button_}) {
        button->setCheckable(true);
        button->setFixedWidth(72);
        modifiers->addWidget(button);
    }
    shift_button_->setText(tr("Shift"));
    ctrl_button_->setText(tr("Ctrl"));
    alt_button_->setText(tr("Alt"));
    modifiers->addStretch(1);
    keyboard_row->addLayout(modifiers);

    keyboard_ = new KeyboardMapWidget(this);
    keyboard_->setObjectName(QStringLiteral("KeyboardMap"));
    auto* keyboard_scroll = new QScrollArea(this);
    keyboard_scroll->setObjectName(QStringLiteral("KeyboardScroll"));
    keyboard_scroll->setWidgetResizable(false);
    keyboard_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    keyboard_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    keyboard_scroll->setFixedHeight(keyboard_->sizeHint().height() + 18);
    keyboard_scroll->setWidget(keyboard_);
    keyboard_row->addWidget(keyboard_scroll, 1);
    root->addLayout(keyboard_row);

    auto* panes = new QSplitter(Qt::Horizontal, this);
    panes->setObjectName(QStringLiteral("KeymapPanes"));

    auto* active_pane = new QWidget(panes);
    auto* active_layout = new QVBoxLayout(active_pane);
    active_layout->setContentsMargins(0, 0, 0, 0);
    active_layout->setSpacing(8);
    auto* active_title = new QLabel(tr("Active Key"), active_pane);
    QFont section_font = active_title->font();
    section_font.setBold(true);
    active_title->setFont(section_font);
    active_layout->addWidget(active_title);
    active_readout_ = new QLineEdit(active_pane);
    active_readout_->setObjectName(QStringLiteral("ActiveKeyReadout"));
    active_readout_->setReadOnly(true);
    active_layout->addWidget(active_readout_);
    active_table_ = new QTableWidget(active_pane);
    active_table_->setObjectName(QStringLiteral("ActiveKeyTable"));
    active_table_->setColumnCount(2);
    active_table_->setHorizontalHeaderLabels({tr("Panel"), tr("Commands")});
    active_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    active_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    active_table_->verticalHeader()->setVisible(false);
    active_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    active_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    active_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    active_layout->addWidget(active_table_, 1);
    panes->addWidget(active_pane);

    auto* commands_pane = new QWidget(panes);
    auto* commands_layout = new QVBoxLayout(commands_pane);
    commands_layout->setContentsMargins(0, 0, 0, 0);
    commands_layout->setSpacing(8);
    auto* commands_header = new QHBoxLayout();
    commands_header->setContentsMargins(0, 0, 0, 0);
    auto* commands_title = new QLabel(tr("Commands"), commands_pane);
    commands_title->setFont(section_font);
    commands_header->addWidget(commands_title);
    commands_header->addStretch(1);
    filter_combo_ = new QComboBox(commands_pane);
    filter_combo_->setObjectName(QStringLiteral("CommandFilterCombo"));
    filter_combo_->addItem(tr("Show All"), QStringLiteral("all"));
    filter_combo_->addItem(tr("Assigned"), QStringLiteral("assigned"));
    filter_combo_->addItem(tr("Unassigned"), QStringLiteral("unassigned"));
    filter_combo_->addItem(tr("Conflicts"), QStringLiteral("conflicts"));
    commands_header->addWidget(filter_combo_);
    search_ = new QLineEdit(commands_pane);
    search_->setObjectName(QStringLiteral("CommandSearch"));
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);
    commands_header->addWidget(search_);
    commands_layout->addLayout(commands_header);
    commands_ = new QTreeWidget(commands_pane);
    commands_->setObjectName(QStringLiteral("CommandTree"));
    commands_->setColumnCount(2);
    commands_->setHeaderLabels({tr("Command"), tr("Keystroke")});
    commands_->setSelectionMode(QAbstractItemView::SingleSelection);
    commands_->setContextMenuPolicy(Qt::CustomContextMenu);
    commands_layout->addWidget(commands_, 1);
    conflict_label_ = new QLabel(commands_pane);
    conflict_label_->setObjectName(QStringLiteral("ConflictLabel"));
    conflict_label_->setWordWrap(true);
    commands_layout->addWidget(conflict_label_);
    hint_label_ = new QLabel(
        tr("Select a key above, then double-click a command and press its keys."), commands_pane);
    hint_label_->setObjectName(QStringLiteral("CaptureHint"));
    commands_layout->addWidget(hint_label_);
    panes->addWidget(commands_pane);
    panes->setStretchFactor(0, 0);
    panes->setStretchFactor(1, 1);
    panes->setSizes({320, 900});
    root->addWidget(panes, 1);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::Save, this);
    buttons_->setObjectName(QStringLiteral("KeymapButtons"));
    root->addWidget(buttons_);

    connect(preset_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        stop_capture();
        preset_ = preset_combo_->itemData(index).toString();
        custom_.clear();
        capture_command_.clear();
        rebuild_all();
    });
    const auto modifier_changed = [this](const QString& modifier, bool on) {
        keyboard_->setModifier(modifier, on);
        active_key_ = keyboard_->activeShortcut();
        rebuild_active_key();
    };
    connect(shift_button_, &QToolButton::toggled, this,
            [modifier_changed](bool on) { modifier_changed(QStringLiteral("Shift"), on); });
    connect(ctrl_button_, &QToolButton::toggled, this,
            [modifier_changed](bool on) { modifier_changed(QStringLiteral("Ctrl"), on); });
    connect(alt_button_, &QToolButton::toggled, this,
            [modifier_changed](bool on) { modifier_changed(QStringLiteral("Alt"), on); });
    connect(keyboard_, &KeyboardMapWidget::shortcutSelected, this, [this](const QString& shortcut) {
        if (!capture_command_.isEmpty()) {
            if (!shortcut.isEmpty()) assign_capture(shortcut);
            return;
        }
        active_key_ = shortcut;
        keyboard_->setActiveShortcut(shortcut);
        rebuild_active_key();
    });
    connect(active_table_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (QTableWidgetItem* item = active_table_->item(row, 0)) {
            selected_command_ = item->data(Qt::UserRole).toString();
            rebuild_commands();
        }
    });
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuild_commands(); });
    connect(filter_combo_, &QComboBox::currentIndexChanged, this, [this] { rebuild_commands(); });
    connect(commands_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto selected = commands_->selectedItems();
        if (!selected.isEmpty() && selected.constFirst()->data(0, Qt::UserRole).isValid())
            selected_command_ = selected.constFirst()->data(0, Qt::UserRole).toString();
    });
    connect(commands_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item != nullptr && item->data(0, Qt::UserRole).isValid())
            start_capture(item->data(0, Qt::UserRole).toString());
    });
    connect(commands_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = commands_->itemAt(pos);
        if (item == nullptr || !item->data(0, Qt::UserRole).isValid()) return;
        const QString command = item->data(0, Qt::UserRole).toString();
        QMenu menu(this);
        QAction* assign = menu.addAction(tr("Assign shortcut..."));
        QAction* clear = menu.addAction(tr("Clear shortcut"));
        if (QAction* chosen = menu.exec(commands_->viewport()->mapToGlobal(pos))) {
            if (chosen == assign) start_capture(command);
            else if (chosen == clear)
                clear_command(command);
        }
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_->button(QDialogButtonBox::Save), &QPushButton::clicked, this,
            [this] { save(); });

    rebuild_all();
}

void KeyboardCustomizationDialog::keyPressEvent(QKeyEvent* event) {
    if (capture_command_.isEmpty()) {
        QDialog::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        stop_capture();
        event->accept();
        return;
    }
    if (event->isAutoRepeat()) return;
    const QString shortcut = shortcuts::ShortcutManager::eventShortcut(event);
    if (shortcut.isEmpty()) return;
    assign_capture(shortcut);
    event->accept();
}

void KeyboardCustomizationDialog::reject() {
    stop_capture();
    QDialog::reject();
}

void KeyboardCustomizationDialog::rebuild_all() {
    bindings_ = shortcuts::active_bindings(preset_.toStdString(), custom_);
    rebuild_keyboard();
    rebuild_active_key();
    rebuild_commands();
    refresh_dirty();
}

void KeyboardCustomizationDialog::rebuild_keyboard() {
    keyboard_->setBindings(bindings_);
    keyboard_->setActiveShortcut(active_key_);
    const bool block_shift = shift_button_->blockSignals(true);
    const bool block_ctrl = ctrl_button_->blockSignals(true);
    const bool block_alt = alt_button_->blockSignals(true);
    shift_button_->setChecked(active_key_.contains(QStringLiteral("Shift+")));
    ctrl_button_->setChecked(active_key_.contains(QStringLiteral("Ctrl+")));
    alt_button_->setChecked(active_key_.contains(QStringLiteral("Alt+")));
    shift_button_->blockSignals(block_shift);
    ctrl_button_->blockSignals(block_ctrl);
    alt_button_->blockSignals(block_alt);
}

void KeyboardCustomizationDialog::rebuild_active_key() {
    active_readout_->setText(active_key_.isEmpty() ? tr("Select a key") : active_key_);
    active_table_->clearContents();
    active_table_->setRowCount(0);
    int row = 0;
    for (const auto& binding : bindings_) {
        if (canvas::core::actions::canonical_shortcut(binding.shortcut).empty()) continue;
        if (QString::fromStdString(canvas::core::actions::canonical_shortcut(binding.shortcut)) !=
            active_key_)
            continue;
        const shortcuts::CommandBinding* command = shortcuts::find_command(binding.command);
        if (command == nullptr) continue;
        active_table_->insertRow(row);
        auto* panel = new QTableWidgetItem(QString::fromStdString(command->category));
        panel->setData(Qt::UserRole, QString::fromStdString(command->id));
        panel->setFlags(panel->flags() & ~Qt::ItemIsEditable);
        auto* name = new QTableWidgetItem(QString::fromStdString(command->title));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        active_table_->setItem(row, 0, panel);
        active_table_->setItem(row, 1, name);
        ++row;
    }
    if (row == 0) {
        active_table_->insertRow(0);
        auto* hint =
            new QTableWidgetItem(active_key_.isEmpty() ? tr("Click a key on the keyboard above.")
                                                       : tr("No commands use this keystroke."));
        hint->setFlags(Qt::NoItemFlags);
        active_table_->setItem(0, 0, hint);
        active_table_->setSpan(0, 0, 1, 2);
    }
}

void KeyboardCustomizationDialog::rebuild_commands() {
    const QString query = search_->text().trimmed().toLower();
    const QString mode = filter_combo_->currentData().toString();
    const auto conflicts = conflicted_shortcuts(bindings_);
    std::unordered_map<std::string, QTreeWidgetItem*> categories;
    QStringList expanded;
    for (int i = 0; i < commands_->topLevelItemCount(); ++i)
        if (commands_->topLevelItem(i)->isExpanded())
            expanded.append(commands_->topLevelItem(i)->text(0));

    commands_->clear();
    for (const auto& command : shortcuts::commands()) {
        const QString id = QString::fromStdString(command.id);
        const QString shortcuts = command_shortcuts(bindings_, id);
        if (!command_matches(command, shortcuts, query)) continue;
        const bool assigned = !shortcuts.isEmpty();
        bool conflicted = false;
        for (const QString& shortcut : shortcuts.split(QStringLiteral(" / "), Qt::SkipEmptyParts))
            conflicted = conflicted || conflicts.contains(shortcut.toStdString());
        if (mode == QStringLiteral("assigned") && !assigned) continue;
        if (mode == QStringLiteral("unassigned") && assigned) continue;
        if (mode == QStringLiteral("conflicts") && !conflicted) continue;

        const QString category = QString::fromStdString(command.category);
        QTreeWidgetItem* parent = nullptr;
        const auto found = categories.find(category.toStdString());
        if (found == categories.end()) {
            parent = new QTreeWidgetItem(commands_, {category});
            parent->setFirstColumnSpanned(false);
            categories.emplace(category.toStdString(), parent);
        } else {
            parent = found->second;
        }
        auto* item =
            new QTreeWidgetItem(parent, {QString::fromStdString(command.title), shortcuts});
        item->setData(0, Qt::UserRole, id);
        if (conflicted) {
            item->setForeground(0, tokens().warn);
            item->setForeground(1, tokens().warn);
            item->setToolTip(0, tr("Another command already uses this keystroke."));
        }
        if (id == selected_command_) item->setSelected(true);
    }

    for (int i = 0; i < commands_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = commands_->topLevelItem(i);
        top->setExpanded(!query.isEmpty() || expanded.isEmpty() || expanded.contains(top->text(0)));
    }
    commands_->resizeColumnToContents(0);

    if (!conflicts.empty()) {
        QStringList names;
        for (const auto& conflict : canvas::core::actions::find_shortcut_conflicts(bindings_))
            names.append(QString::fromStdString(conflict.shortcut));
        names.sort();
        conflict_label_->setText(tr("Resolve these duplicate keystrokes before saving: %1")
                                     .arg(names.join(QStringLiteral(", "))));
        conflict_label_->setStyleSheet(QStringLiteral("color: %1;").arg(css(tokens().warn)));
    } else {
        conflict_label_->clear();
    }
    refresh_dirty();
}

void KeyboardCustomizationDialog::refresh_dirty() {
    bool dirty = true;
    if (manager_ != nullptr) {
        dirty =
            preset_ != QString::fromStdString(manager_->preset()) || custom_ != manager_->custom();
    }
    const bool blocked = !conflicted_shortcuts(bindings_).empty();
    if (QPushButton* save = buttons_->button(QDialogButtonBox::Save))
        save->setEnabled(dirty && !blocked);
}

void KeyboardCustomizationDialog::start_capture(const QString& command) {
    if (shortcuts::find_command(command.toStdString()) == nullptr) return;
    selected_command_ = command;
    capture_command_ = command;
    const shortcuts::CommandBinding* meta = shortcuts::find_command(command.toStdString());
    hint_label_->setText(tr("Press the new keys for %1 (Esc cancels).")
                             .arg(meta != nullptr ? QString::fromStdString(meta->title) : command));
    grabKeyboard();
    rebuild_commands();
    setFocus();
}

void KeyboardCustomizationDialog::stop_capture() {
    capture_command_.clear();
    releaseKeyboard();
    hint_label_->setText(tr("Select a key above, then double-click a command and press its keys."));
}

void KeyboardCustomizationDialog::assign_capture(const QString& shortcut) {
    if (capture_command_.isEmpty()) return;
    const std::string canonical = canvas::core::actions::canonical_shortcut(shortcut.toStdString());
    if (canonical.empty()) return;
    std::vector<canvas::core::actions::KeyBinding> custom;
    for (const auto& binding : custom_)
        if (binding.command != capture_command_.toStdString()) custom.push_back(binding);
    custom.push_back({capture_command_.toStdString(), canonical});
    custom_ = std::move(custom);
    active_key_ = QString::fromStdString(canonical);
    stop_capture();
    rebuild_all();
}

void KeyboardCustomizationDialog::clear_command(const QString& command) {
    stop_capture();
    std::vector<canvas::core::actions::KeyBinding> custom;
    for (const auto& binding : custom_)
        if (binding.command != command.toStdString()) custom.push_back(binding);
    bool preset_binds = false;
    for (const auto& binding :
         shortcuts::preset_bindings(shortcuts::preset_from_id(preset_.toStdString())))
        preset_binds = preset_binds || binding.command == command.toStdString();
    if (preset_binds) custom.push_back({command.toStdString(), {}});
    custom_ = std::move(custom);
    rebuild_all();
}

void KeyboardCustomizationDialog::save() {
    stop_capture();
    if (manager_ == nullptr || menubar_ == nullptr) return;
    manager_->applyState(preset_.toStdString(), custom_);
    manager_->applyToMenus(menubar_);
    custom_ = manager_->custom();
    rebuild_all();
    accept();
}

}
