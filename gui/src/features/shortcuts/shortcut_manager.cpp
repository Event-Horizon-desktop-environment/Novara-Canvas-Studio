#include "features/shortcuts/shortcut_manager.hpp"

#include "features/shortcuts/shortcut_catalog.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>

#include <functional>

namespace canvas::gui::shortcuts {

namespace {

QString settings_preset_key() {
    return QStringLiteral("shortcuts/preset");
}

QString settings_custom_group() {
    return QStringLiteral("shortcuts/custom");
}

std::vector<canvas::core::actions::KeyBinding>
sanitized_custom(std::vector<canvas::core::actions::KeyBinding> custom) {
    std::vector<canvas::core::actions::KeyBinding> out;
    for (auto& binding : custom) {
        if (find_command(binding.command) == nullptr) continue;
        const std::string shortcut = canvas::core::actions::canonical_shortcut(binding.shortcut);
        if (!binding.shortcut.empty() && shortcut.empty()) continue;
        binding.shortcut = shortcut;
        out.push_back(std::move(binding));
    }
    return canvas::core::actions::apply_shortcut_overrides({}, out);
}

}

ShortcutManager::ShortcutManager() {
    QSettings settings;
    loadFrom(settings);
    rebuild();
}

bool ShortcutManager::setPreset(std::string preset) {
    applyState(std::move(preset), {});
    return true;
}

bool ShortcutManager::setCustom(std::string command, std::string shortcut) {
    if (find_command(command) == nullptr) return false;
    const std::string canonical = canvas::core::actions::canonical_shortcut(shortcut);
    if (!shortcut.empty() && canonical.empty()) return false;
    std::vector<canvas::core::actions::KeyBinding> custom = custom_;
    bool replaced = false;
    for (auto& binding : custom) {
        if (binding.command == command) {
            binding.shortcut = canonical;
            replaced = true;
        }
    }
    if (!replaced) custom.push_back({std::move(command), canonical});
    applyState(preset_, std::move(custom));
    return true;
}

void ShortcutManager::clearCustom() {
    applyState(preset_, {});
}

void ShortcutManager::applyState(std::string preset,
                                 std::vector<canvas::core::actions::KeyBinding> custom) {
    preset_ = std::string(preset_id(preset_from_id(preset)));
    custom_ = sanitized_custom(std::move(custom));
    rebuild();
    QSettings settings;
    saveTo(settings);
}

void ShortcutManager::loadFrom(QSettings& settings) {
    preset_ = std::string(
        preset_id(preset_from_id(settings.value(settings_preset_key(), QStringLiteral("resolve"))
                                     .toString()
                                     .toStdString())));
    custom_.clear();
    settings.beginGroup(settings_custom_group());
    const auto keys = settings.childKeys();
    for (const QString& key : keys) {
        const std::string command = key.toStdString();
        if (find_command(command) == nullptr) continue;
        const std::string shortcut = settings.value(key).toString().toStdString();
        const std::string canonical = canvas::core::actions::canonical_shortcut(shortcut);
        if (!shortcut.empty() && canonical.empty()) continue;
        custom_.push_back({command, canonical});
    }
    settings.endGroup();
    custom_ = sanitized_custom(std::move(custom_));
    rebuild();
}

void ShortcutManager::setPreviewState(std::string preset,
                                      std::vector<canvas::core::actions::KeyBinding> custom) {
    preset_ = std::string(preset_id(preset_from_id(preset)));
    custom_ = sanitized_custom(std::move(custom));
    rebuild();
}

void ShortcutManager::applyToMenus(QMenuBar* bar) {
    if (bar == nullptr) return;
    std::function<void(QMenu*)> walk = [&](QMenu* menu) {
        if (menu == nullptr) return;
        for (QAction* action : menu->actions()) {
            if (action->isSeparator()) continue;
            if (QMenu* sub = action->menu()) {
                walk(sub);
                continue;
            }
            const std::string id = action->objectName().toStdString();
            if (id.empty() || find_command(id) == nullptr) continue;
            QList<QKeySequence> sequences;
            for (const auto& binding : bindings_) {
                if (binding.command != id) continue;
                const std::string shortcut =
                    canvas::core::actions::canonical_shortcut(binding.shortcut);
                if (shortcut.empty()) continue;
                const QKeySequence sequence(QString::fromStdString(shortcut));
                if (!sequence.isEmpty()) sequences.append(sequence);
            }
            action->setShortcuts(sequences);
        }
    };
    for (QAction* top : bar->actions())
        if (QMenu* menu = top->menu()) walk(menu);
}

std::string ShortcutManager::matchEvent(const QKeyEvent* event) const {
    const QString shortcut = eventShortcut(event);
    if (shortcut.isEmpty()) return {};
    const std::string canonical = canvas::core::actions::canonical_shortcut(shortcut.toStdString());
    if (canonical.empty()) return {};
    const auto it = lookup_.find(canonical);
    return it == lookup_.end() ? std::string{} : it->second;
}

QString ShortcutManager::eventShortcut(const QKeyEvent* event) {
    if (event == nullptr) return {};
    const int key = event->key();
    if (key == Qt::Key_unknown) return {};
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return {};
    constexpr Qt::KeyboardModifiers kept =
        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    const QKeySequence sequence(static_cast<int>(event->modifiers() & kept) | key);
    return sequence.toString(QKeySequence::PortableText);
}

void ShortcutManager::rebuild() {
    bindings_ = active_bindings(preset_, custom_);
    lookup_.clear();
    for (const auto& binding : bindings_) {
        const std::string shortcut = canvas::core::actions::canonical_shortcut(binding.shortcut);
        if (shortcut.empty()) continue;
        lookup_.emplace(shortcut, binding.command);
    }
}

void ShortcutManager::saveTo(QSettings& settings) const {
    settings.setValue(settings_preset_key(), QString::fromStdString(preset_));
    settings.remove(settings_custom_group());
    settings.beginGroup(settings_custom_group());
    for (const auto& binding : custom_)
        settings.setValue(QString::fromStdString(binding.command),
                          QString::fromStdString(binding.shortcut));
    settings.endGroup();
}

}
