#include "features/shortcuts/shortcut_catalog.hpp"

#include <cstdio>
#include <string>

using namespace canvas::gui::shortcuts;
using canvas::core::actions::find_shortcut_conflicts;
using canvas::core::actions::valid_shortcut;

namespace {

int failures = 0;

void check(const bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

std::string shortcut_in(const Preset preset, const char* id) {
    const auto bindings = preset_bindings(preset);
    for (const auto& binding : bindings)
        if (binding.command == id) return binding.shortcut;
    return {};
}

void test_catalog_shape() {
    check(presets().size() == 5, "five editor presets");
    check(preset_id(presets().front()) == "resolve", "DaVinci Resolve is the default preset");
    check(preset_from_id("premier") == Preset::Resolve, "unknown preset falls back to Resolve");
    check(find_command("mark.insert") != nullptr, "insert command exists");
    check(find_command("missing") == nullptr, "unknown command is null");
}

void test_preset_laws() {
    for (const Preset preset : presets()) {
        const auto bindings = preset_bindings(preset);
        for (const auto& binding : bindings)
            if (!valid_shortcut(binding.shortcut)) {
                std::fprintf(stderr, "FAIL: invalid %s %s\n", binding.command.c_str(),
                             binding.shortcut.c_str());
                ++failures;
                return;
            }
        check(find_shortcut_conflicts(bindings).empty(), "preset has no conflicts");
    }
    check(shortcut_in(Preset::Resolve, "clip.toggle_enable") == "D", "Resolve enables with D");
    check(shortcut_in(Preset::Resolve, "trim.ripple_delete") == "Shift+Backspace",
          "Resolve ripple closes the gap");
    check(shortcut_in(Preset::Resolve, "timeline.split_at_playhead") == "Ctrl+Backslash",
          "Resolve splits with Ctrl+Backslash");
    check(shortcut_in(Preset::Premiere, "mark.insert") == "Comma", "Premiere inserts with comma");
    check(shortcut_in(Preset::Premiere, "timeline.split_at_playhead") == "Ctrl+K",
          "Premiere adds edits with Ctrl+K");
    check(shortcut_in(Preset::Premiere, "clip.toggle_enable") == "Ctrl+Shift+E",
          "Premiere toggles with Ctrl+Shift+E");
    check(shortcut_in(Preset::Avid, "mark.insert") == "V", "Avid splices with V");
    check(shortcut_in(Preset::Avid, "mark.overwrite") == "B", "Avid overwrites with B");
    check(shortcut_in(Preset::Avid, "edit.redo") == "Ctrl+R", "Avid redoes with Ctrl+R");
    check(shortcut_in(Preset::FinalCut, "mark.overwrite") == "D", "Final Cut overwrites with D");
    check(shortcut_in(Preset::FinalCut, "clip.toggle_enable") == "V", "Final Cut toggles with V");
    check(shortcut_in(Preset::FinalCut, "mark.append") == "E", "Final Cut appends with E");
    check(shortcut_in(Preset::ProTools, "playback.pause") == "Ctrl+Space",
          "Pro Tools pauses with Ctrl+Space");
    check(shortcut_in(Preset::ProTools, "timeline.select_tool") == "F7",
          "Pro Tools selects with F7");
}

void test_custom_layer() {
    const auto custom = active_bindings("resolve", {{"mark.insert", "W"}});
    int matches = 0;
    for (const auto& binding : custom)
        if (binding.command == "mark.insert") {
            ++matches;
            check(binding.shortcut == "W", "custom shortcut replaces Resolve aliases");
        }
    check(matches == 1, "custom insert has one binding");
    check(active_bindings("premiere", {}).size() == preset_bindings(Preset::Premiere).size(),
          "empty custom layer preserves preset");
}

}

int main() {
    test_catalog_shape();
    test_preset_laws();
    test_custom_layer();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
