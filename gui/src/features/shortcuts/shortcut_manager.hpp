#pragma once

#include <QString>

#include <string>
#include <unordered_map>
#include <vector>

#include "canvas/core/actions/keymap.hpp"

class QKeyEvent;
class QMenuBar;
class QSettings;

namespace canvas::gui::shortcuts {

class ShortcutManager {
public:
    ShortcutManager();

    [[nodiscard]] std::string preset() const { return preset_; }
    [[nodiscard]] const std::vector<canvas::core::actions::KeyBinding>& custom() const {
        return custom_;
    }
    [[nodiscard]] const std::vector<canvas::core::actions::KeyBinding>& bindings() const {
        return bindings_;
    }

    bool setPreset(std::string preset);
    bool setCustom(std::string command, std::string shortcut);
    void clearCustom();
    void applyState(std::string preset, std::vector<canvas::core::actions::KeyBinding> custom);
    void loadFrom(QSettings& settings);
    void setPreviewState(std::string preset, std::vector<canvas::core::actions::KeyBinding> custom);
    void applyToMenus(QMenuBar* bar);
    [[nodiscard]] std::string matchEvent(const QKeyEvent* event) const;

    [[nodiscard]] static QString eventShortcut(const QKeyEvent* event);
private:
    void rebuild();
    void saveTo(QSettings& settings) const;

    std::string preset_ = "resolve";
    std::vector<canvas::core::actions::KeyBinding> custom_;
    std::vector<canvas::core::actions::KeyBinding> bindings_;
    std::unordered_map<std::string, std::string> lookup_;
};

}
