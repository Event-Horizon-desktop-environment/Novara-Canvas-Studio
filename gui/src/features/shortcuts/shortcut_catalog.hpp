#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "canvas/core/actions/keymap.hpp"

namespace canvas::gui::shortcuts {

enum class Preset { Resolve, Premiere, Avid, FinalCut, ProTools };

struct CommandBinding {
    const char* id = nullptr;
    const char* title = nullptr;
    const char* category = nullptr;
    const char* resolve = "";
    const char* premiere = "";
    const char* final_cut = "";
    const char* avid = "";
    const char* pro_tools = "";
};

[[nodiscard]] const std::vector<Preset>& presets();
[[nodiscard]] std::string_view preset_id(Preset preset);
[[nodiscard]] std::string_view preset_title(Preset preset);
[[nodiscard]] Preset preset_from_id(std::string_view id);
[[nodiscard]] const std::vector<CommandBinding>& commands();
[[nodiscard]] const CommandBinding* find_command(std::string_view id);
[[nodiscard]] std::vector<canvas::core::actions::KeyBinding> preset_bindings(Preset preset);
[[nodiscard]] std::vector<canvas::core::actions::KeyBinding>
active_bindings(std::string_view preset_id,
                const std::vector<canvas::core::actions::KeyBinding>& custom);

}
