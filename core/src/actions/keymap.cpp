#include "canvas/core/actions/keymap.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace canvas::core::actions {

namespace {

std::string trim_text(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0)
        ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) --last;
    return std::string(text.substr(first, last - first));
}

std::string lower_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

bool is_modifier_name(const std::string& name) {
    return name == "ctrl" || name == "control" || name == "shift" || name == "alt" ||
           name == "option" || name == "opt" || name == "meta" || name == "command" ||
           name == "cmd" || name == "super" || name == "win" || name == "windows";
}

std::string canonical_key(const std::string& name) {
    static const std::unordered_map<std::string, std::string> aliases = {
        {"escape", "Escape"},
        {"esc", "Escape"},
        {"tab", "Tab"},
        {"space", "Space"},
        {"spacebar", "Space"},
        {"return", "Return"},
        {"enter", "Enter"},
        {"insert", "Insert"},
        {"ins", "Insert"},
        {"delete", "Delete"},
        {"del", "Delete"},
        {"backspace", "Backspace"},
        {"home", "Home"},
        {"end", "End"},
        {"pageup", "PageUp"},
        {"pgup", "PageUp"},
        {"pagedown", "PageDown"},
        {"pgdn", "PageDown"},
        {"left", "Left"},
        {"right", "Right"},
        {"up", "Up"},
        {"down", "Down"},
        {"capslock", "CapsLock"},
        {"numlock", "NumLock"},
        {"scrolllock", "ScrollLock"},
        {"print", "Print"},
        {"printscreen", "Print"},
        {"pause", "Pause"},
        {"menu", "Menu"},
        {"comma", "Comma"},
        {",", "Comma"},
        {"period", "Period"},
        {"point", "Period"},
        {"dot", "Period"},
        {".", "Period"},
        {"slash", "Slash"},
        {"/", "Slash"},
        {"backslash", "Backslash"},
        {"\\", "Backslash"},
        {"asterisk", "Asterisk"},
        {"*", "Asterisk"},
        {"minus", "Minus"},
        {"hyphen", "Minus"},
        {"-", "Minus"},
        {"equal", "Equal"},
        {"equals", "Equal"},
        {"=", "Equal"},
        {"plus", "Plus"},
        {"+", "Plus"},
        {"semicolon", "Semicolon"},
        {";", "Semicolon"},
        {"apostrophe", "Apostrophe"},
        {"'", "Apostrophe"},
        {"bracketleft", "BracketLeft"},
        {"[", "BracketLeft"},
        {"bracketright", "BracketRight"},
        {"]", "BracketRight"},
        {"quoteleft", "QuoteLeft"},
        {"backquote", "QuoteLeft"},
        {"grave", "QuoteLeft"},
        {"`", "QuoteLeft"}};
    const auto it = aliases.find(name);
    if (it != aliases.end()) return it->second;
    if (name.size() == 1) {
        const char c = name[0];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        if (c >= '0' && c <= '9') return std::string(1, c);
        return {};
    }
    if (name.size() >= 2 && (name[0] == 'f' || name[0] == 'F')) {
        int number = 0;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') return {};
            number = number * 10 + (name[i] - '0');
        }
        if (number >= 1 && number <= 12) return "F" + std::to_string(number);
    }
    return {};
}

}

std::string canonical_shortcut(std::string_view shortcut) {
    const std::string text = trim_text(shortcut);
    if (text.empty()) return {};

    std::string body = text;
    std::string forced_key;
    if (!body.empty() && body.back() == '+') {
        body.pop_back();
        forced_key = "plus";
    }

    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
    std::string key;
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t end = body.find('+', start);
        if (end == std::string::npos) end = body.size();
        const std::string token =
            lower_text(trim_text(std::string_view(body).substr(start, end - start)));
        if (!token.empty()) {
            if (token == "ctrl" || token == "control") ctrl = true;
            else if (token == "alt" || token == "option" || token == "opt")
                alt = true;
            else if (token == "shift")
                shift = true;
            else if (token == "meta" || token == "command" || token == "cmd" || token == "super" ||
                     token == "win" || token == "windows")
                meta = true;
            else if (is_modifier_name(token))
                return {};
            else if (!key.empty())
                return {};
            else
                key = canonical_key(token);
        }
        if (end == body.size()) break;
        start = end + 1;
    }
    if (!forced_key.empty()) {
        if (!key.empty()) return {};
        key = canonical_key(forced_key);
    }
    if (key.empty()) return {};

    std::string out;
    if (ctrl) out += "Ctrl+";
    if (alt) out += "Alt+";
    if (shift) out += "Shift+";
    if (meta) out += "Meta+";
    out += key;
    return out;
}

bool valid_shortcut(std::string_view shortcut) {
    return !canonical_shortcut(shortcut).empty();
}

std::vector<KeyBinding> apply_shortcut_overrides(const std::vector<KeyBinding>& base,
                                                 const std::vector<KeyBinding>& overrides) {
    std::vector<KeyBinding> out = base;
    for (const auto& override_binding : overrides) {
        if (override_binding.command.empty()) continue;
        bool replaced = false;
        for (auto it = out.begin(); it != out.end();) {
            if (it->command != override_binding.command) {
                ++it;
                continue;
            }
            if (!replaced) {
                it->shortcut = override_binding.shortcut;
                replaced = true;
                ++it;
            } else {
                it = out.erase(it);
            }
        }
        if (!replaced) out.push_back(override_binding);
    }
    return out;
}

std::string shortcut_for_command(const std::vector<KeyBinding>& bindings,
                                 std::string_view command) {
    for (const auto& binding : bindings)
        if (binding.command == command) return binding.shortcut;
    return {};
}

std::string command_for_shortcut(const std::vector<KeyBinding>& bindings,
                                 std::string_view shortcut) {
    const std::string wanted = canonical_shortcut(shortcut);
    if (wanted.empty()) return {};
    for (const auto& binding : bindings)
        if (canonical_shortcut(binding.shortcut) == wanted) return binding.command;
    return {};
}

std::vector<ShortcutConflict> find_shortcut_conflicts(const std::vector<KeyBinding>& bindings) {
    std::vector<ShortcutConflict> conflicts;
    for (const auto& binding : bindings) {
        const std::string shortcut = canonical_shortcut(binding.shortcut);
        if (shortcut.empty() || binding.command.empty()) continue;
        auto it =
            std::find_if(conflicts.begin(), conflicts.end(), [&](const ShortcutConflict& conflict) {
                return conflict.shortcut == shortcut;
            });
        if (it == conflicts.end()) {
            ShortcutConflict conflict;
            conflict.shortcut = shortcut;
            conflict.commands.push_back(binding.command);
            conflicts.push_back(std::move(conflict));
            continue;
        }
        if (std::find(it->commands.begin(), it->commands.end(), binding.command) ==
            it->commands.end())
            it->commands.push_back(binding.command);
    }
    conflicts.erase(std::remove_if(conflicts.begin(), conflicts.end(),
                                   [](const ShortcutConflict& conflict) {
                                       return conflict.commands.size() < 2;
                                   }),
                    conflicts.end());
    return conflicts;
}

}
