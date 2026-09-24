#include "canvas/core/actions/keymap.hpp"

#include <cstdio>

using namespace canvas::core::actions;

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

void test_canonical_form() {
    check(canonical_shortcut("Ctrl+K") == "Ctrl+K", "portable shortcut is stable");
    check(canonical_shortcut("shift+ctrl+z") == "Ctrl+Shift+Z", "modifiers order and case");
    check(canonical_shortcut("Cmd+Option+k") == "Alt+Meta+K", "mac aliases map to portable names");
    check(canonical_shortcut("  alt + x ") == "Alt+X", "whitespace is ignored");
    check(canonical_shortcut("Ctrl+,") == "Ctrl+Comma", "comma key has one name");
    check(canonical_shortcut("ctrl+backslash") == "Ctrl+Backslash", "backslash key parses");
    check(canonical_shortcut("Shift++") == "Shift+Plus", "plus key survives separator parsing");
    check(canonical_shortcut("del") == "Delete", "delete alias");
    check(canonical_shortcut("Ctrl") == "", "modifier alone is not a shortcut");
    check(canonical_shortcut("Ctrl+K+L") == "", "two main keys are rejected");
    check(canonical_shortcut("Ctrl+F13") == "", "unknown keys are rejected");
    check(valid_shortcut("F9"), "function key is valid");
    check(!valid_shortcut(""), "empty means unbound");
}

void test_overrides_and_lookup() {
    const std::vector<KeyBinding> base = {{"save", "Ctrl+S"}, {"insert", "F9"}};
    const std::vector<KeyBinding> changed =
        apply_shortcut_overrides(base, {{"insert", "Comma"}, {"zoom", ""}});
    check(changed.size() == 3, "override replaces and unmapped commands append");
    check(shortcut_for_command(changed, "insert") == "Comma", "override wins");
    check(shortcut_for_command(changed, "missing") == "", "unknown command is empty");
    check(command_for_shortcut(changed, "comma") == "insert", "lookup is canonical");
    check(command_for_shortcut(changed, "") == "", "unbound lookup finds nothing");
}

void test_conflicts() {
    const std::vector<KeyBinding> bindings = {
        {"insert", "F9"}, {"overwrite", "F10"}, {"duplicate", "f9"}, {"unbound", ""}};
    const auto conflicts = find_shortcut_conflicts(bindings);
    check(conflicts.size() == 1, "one canonical collision reported");
    check(conflicts[0].shortcut == "F9", "collision names canonical shortcut");
    check(conflicts[0].commands.size() == 2 && conflicts[0].commands[0] == "insert" &&
              conflicts[0].commands[1] == "duplicate",
          "collision lists both commands in order");
    check(find_shortcut_conflicts({{"insert", "F9"}, {"overwrite", "F10"}}).empty(),
          "distinct shortcuts do not conflict");
}

}

int main() {
    test_canonical_form();
    test_overrides_and_lookup();
    test_conflicts();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
