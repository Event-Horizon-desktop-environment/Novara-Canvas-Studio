#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace canvas::core::actions {

struct KeyBinding {
    std::string command;
    std::string shortcut;

    bool operator==(const KeyBinding&) const = default;
};

struct ShortcutConflict {
    std::string shortcut;
    std::vector<std::string> commands;
};

[[nodiscard]] std::string canonical_shortcut(std::string_view shortcut);
[[nodiscard]] bool valid_shortcut(std::string_view shortcut);
[[nodiscard]] std::vector<KeyBinding>
apply_shortcut_overrides(const std::vector<KeyBinding>& base,
                         const std::vector<KeyBinding>& overrides);
[[nodiscard]] std::string shortcut_for_command(const std::vector<KeyBinding>& bindings,
                                               std::string_view command);
[[nodiscard]] std::string command_for_shortcut(const std::vector<KeyBinding>& bindings,
                                               std::string_view shortcut);
[[nodiscard]] std::vector<ShortcutConflict>
find_shortcut_conflicts(const std::vector<KeyBinding>& bindings);

}
