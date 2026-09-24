#include "features/shortcuts/shortcut_catalog.hpp"

namespace canvas::gui::shortcuts {

namespace {

const std::vector<CommandBinding>& command_table() {
    static const std::vector<CommandBinding> commands = {
        {"app.keyboard_customization", "Keyboard Customization...", "Edit", "Ctrl+Alt+K",
         "Ctrl+Alt+K", "Ctrl+Alt+K", "Ctrl+Alt+K", "Ctrl+Alt+K"},
        {"app.preferences", "Preferences...", "Novara Canvas", "Ctrl+,", "Ctrl+,", "Ctrl+,",
         "Ctrl+,", "Ctrl+,"},
        {"app.quit", "Quit Novara Canvas Studio", "Novara Canvas", "Ctrl+Q", "Ctrl+Q", "Ctrl+Q",
         "Ctrl+Q", "Ctrl+Q"},
        {"file.new_project", "New Project", "File", "Ctrl+N", "Ctrl+Alt+N", "Ctrl+N", "Ctrl+N",
         "Ctrl+N"},
        {"file.open_project", "Open Project...", "File", "Ctrl+O", "Ctrl+O", "Ctrl+O", "Ctrl+O",
         "Ctrl+O"},
        {"file.project_manager", "Project Manager", "File", "Ctrl+Shift+M", "Ctrl+Shift+M",
         "Ctrl+Shift+M", "Ctrl+Shift+M", "Ctrl+Shift+M"},
        {"file.save_project", "Save Project", "File", "Ctrl+S", "Ctrl+S", "Ctrl+S", "Ctrl+S",
         "Ctrl+S"},
        {"file.save_as", "Save Project As...", "File", "Ctrl+Shift+S", "Ctrl+Shift+S",
         "Ctrl+Shift+S", "Ctrl+Shift+S", "Ctrl+Shift+S"},
        {"file.archive_project", "Archive Project...", "File", "", "", "", "", ""},
        {"file.import_media", "Import Media...", "File", "Ctrl+I", "Ctrl+I", "Ctrl+I", "",
         "Ctrl+Shift+I"},
        {"file.export_edl", "Export EDL...", "File", "", "", "", "", ""},
        {"edit.undo", "Undo", "Edit", "Ctrl+Z", "Ctrl+Z", "Ctrl+Z", "Ctrl+Z", "Ctrl+Z"},
        {"edit.redo", "Redo", "Edit", "Ctrl+Shift+Z", "Ctrl+Shift+Z", "Ctrl+Shift+Z", "Ctrl+R",
         "Shift+Z"},
        {"edit.find_action", "Find Action...", "Edit", "Ctrl+K", "Ctrl+Shift+K", "Ctrl+K", "Ctrl+K",
         "Ctrl+K"},
        {"clip.toggle_enable", "Enable/Disable Clip", "Clip", "D", "Ctrl+Shift+E", "V", "", ""},
        {"clip.add_transition", "Add Transition", "Clip", "Ctrl+T", "Ctrl+D", "Ctrl+T", "Backslash",
         "F"},
        {"clip.add_title", "Add Title", "Timeline", "Ctrl+Alt+T", "Ctrl+Alt+T", "Ctrl+Alt+T",
         "Ctrl+Alt+T", "Ctrl+Alt+T"},
        {"clip.toggle_link", "Link/Unlink", "Clip", "Ctrl+Alt+L", "Ctrl+L", "Ctrl+Alt+L",
         "Ctrl+Alt+L", "Ctrl+Alt+L"},
        {"trim.ripple_delete", "Ripple Delete", "Trim", "Shift+Backspace", "Shift+Delete",
         "Shift+Delete", "X", ""},
        {"trim.lift", "Lift", "Trim", "Backspace", "Semicolon", "Delete", "Z", ""},
        {"trim.remove_all_transitions", "Remove All Transitions", "Trim", "", "", "", "", ""},
        {"timeline.split_at_playhead", "Add Edit", "Timeline", "Ctrl+Backslash", "Ctrl+K",
         "Ctrl+Backslash", "", ""},
        {"timeline.toggle_bookmark", "Add Marker", "Timeline", "M", "M", "M", "M", "M"},
        {"timeline.select_tool", "Select Tool", "Timeline", "A", "V", "A", "", "F7"},
        {"timeline.blade_tool", "Blade Tool", "Timeline", "B", "C", "B", "", ""},
        {"timeline.zoom_fit", "Zoom to Fit", "Timeline", "Shift+Z", "Backslash", "Shift+Z",
         "Ctrl+Slash", "Alt+A"},
        {"timeline.collapse_all", "Collapse All Tracks", "Timeline", "Ctrl+Shift+C", "Ctrl+Shift+C",
         "Ctrl+Shift+C", "Ctrl+Shift+C", "Ctrl+Shift+C"},
        {"timeline.expand_all", "Expand All Tracks", "Timeline", "Ctrl+Shift+E", "Ctrl+Alt+E",
         "Ctrl+Shift+E", "Ctrl+Shift+E", "Ctrl+Shift+E"},
        {"timeline.generate_subtitles", "Generate Subtitles From Audio...", "Timeline", "", "", "",
         "", ""},
        {"mark.in", "Mark In", "Mark", "I", "I", "I", "I", ""},
        {"mark.out", "Mark Out", "Mark", "O", "O", "O", "O", ""},
        {"mark.clear", "Clear In/Out", "Mark", "Alt+X", "Ctrl+Shift+X", "Alt+X", "G", "G"},
        {"mark.create_range", "Create Range from In/Out", "Mark", "Alt+R", "Alt+R", "Alt+R",
         "Alt+R", "Alt+R"},
        {"mark.insert", "Insert from Source", "Mark", "F9;Comma", "Comma", "W", "V", ""},
        {"mark.overwrite", "Overwrite from Source", "Mark", "F10;Period", "Period", "D", "B", ""},
        {"mark.append", "Append at End", "Mark", "Shift+F12", "", "E", "", ""},
        {"mark.place_on_top", "Place on Top", "Mark", "F12", "", "Q", "", ""},
        {"playback.play_pause", "Play/Pause", "Playback", "Space", "Space", "Space", "Space",
         "Space"},
        {"playback.pause", "Pause", "Playback", "K", "K", "K", "K", "Ctrl+Space"},
        {"playback.step_back", "Step Back", "Playback", "J", "J", "", "", ""},
        {"playback.play_forward", "Play", "Playback", "L", "L", "L", "L", ""},
        {"playback.previous_frame", "Previous Frame", "Playback", "Left", "Left", "Left", "Left",
         "Left"},
        {"playback.next_frame", "Next Frame", "Playback", "Right", "Right", "Right", "Right",
         "Right"},
        {"playback.step_back_second", "Step Back One Second", "Playback", "Shift+Left",
         "Shift+Left", "Shift+Left", "Shift+Left", "Shift+Left"},
        {"playback.step_forward_second", "Step Forward One Second", "Playback", "Shift+Right",
         "Shift+Right", "Shift+Right", "Shift+Right", "Shift+Right"},
        {"playback.go_to_start", "Go to Start", "Playback", "Home", "Home", "Home", "Home", "Home"},
        {"playback.go_to_end", "Go to End", "Playback", "End", "End", "End", "End", "End"},
        {"view.inspector", "Inspector", "View", "Alt+I", "Alt+I", "Alt+I", "Alt+I", "Alt+I"},
        {"view.fullscreen", "Toggle Full Screen", "View", "F11", "F11", "F11", "F11", "F11"},
    };
    return commands;
}

std::string_view preset_shortcut(const CommandBinding& command, Preset preset) {
    switch (preset) {
    case Preset::Resolve:
        return command.resolve;
    case Preset::Premiere:
        return command.premiere;
    case Preset::Avid:
        return command.avid;
    case Preset::FinalCut:
        return command.final_cut;
    case Preset::ProTools:
        return command.pro_tools;
    }
    return command.resolve;
}

void append_bindings(std::vector<canvas::core::actions::KeyBinding>& out,
                     const CommandBinding& command, Preset preset) {
    std::string_view shortcuts = preset_shortcut(command, preset);
    std::size_t start = 0;
    while (start <= shortcuts.size()) {
        const std::size_t end = shortcuts.find(';', start);
        const std::string_view shortcut =
            shortcuts.substr(start, end == std::string_view::npos ? end : end - start);
        if (!shortcut.empty()) out.push_back({command.id, std::string(shortcut)});
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
}

}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {Preset::Resolve, Preset::Premiere, Preset::Avid,
                                             Preset::FinalCut, Preset::ProTools};
    return list;
}

std::string_view preset_id(Preset preset) {
    switch (preset) {
    case Preset::Resolve:
        return "resolve";
    case Preset::Premiere:
        return "premiere";
    case Preset::Avid:
        return "avid";
    case Preset::FinalCut:
        return "final_cut";
    case Preset::ProTools:
        return "pro_tools";
    }
    return "resolve";
}

std::string_view preset_title(Preset preset) {
    switch (preset) {
    case Preset::Resolve:
        return "DaVinci Resolve";
    case Preset::Premiere:
        return "Adobe Premiere Pro";
    case Preset::Avid:
        return "Avid Media Composer";
    case Preset::FinalCut:
        return "Final Cut Pro";
    case Preset::ProTools:
        return "Pro Tools";
    }
    return "DaVinci Resolve";
}

Preset preset_from_id(std::string_view id) {
    for (const Preset preset : presets())
        if (preset_id(preset) == id) return preset;
    return Preset::Resolve;
}

const std::vector<CommandBinding>& commands() {
    return command_table();
}

const CommandBinding* find_command(std::string_view id) {
    for (const auto& command : command_table())
        if (command.id == id) return &command;
    return nullptr;
}

std::vector<canvas::core::actions::KeyBinding> preset_bindings(Preset preset) {
    std::vector<canvas::core::actions::KeyBinding> bindings;
    for (const auto& command : command_table()) append_bindings(bindings, command, preset);
    return bindings;
}

std::vector<canvas::core::actions::KeyBinding>
active_bindings(std::string_view preset_id,
                const std::vector<canvas::core::actions::KeyBinding>& custom) {
    return canvas::core::actions::apply_shortcut_overrides(
        preset_bindings(preset_from_id(preset_id)), custom);
}

}
