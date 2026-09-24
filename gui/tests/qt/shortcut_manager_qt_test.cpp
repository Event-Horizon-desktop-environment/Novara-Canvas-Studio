#include "Widgets/keyboard_map_widget.hpp"
#include "UX/KeyboardCustomizationDialog.hpp"
#include "features/shortcuts/shortcut_catalog.hpp"
#include "features/shortcuts/shortcut_manager.hpp"

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cstdio>

using namespace canvas::gui;
using namespace canvas::gui::shortcuts;

namespace {

bool expect(bool cond, const char* label, bool& ok) {
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ok = false;
        return false;
    }
    return true;
}

}

static int run_checks() {
    bool ok = true;
    QApplication::setOrganizationName(QStringLiteral("ShortcutManagerTest"));
    QApplication::setApplicationName(QStringLiteral("shortcut-manager-qt-test"));

    {
        QMenuBar bar;
        QMenu* edit = bar.addMenu(QStringLiteral("Edit"));
        QAction* find = edit->addAction(QStringLiteral("Find Action..."));
        find->setObjectName(QStringLiteral("edit.find_action"));
        QMenu* mark = bar.addMenu(QStringLiteral("Mark"));
        QAction* insert = mark->addAction(QStringLiteral("Insert from Source"));
        insert->setObjectName(QStringLiteral("mark.insert"));
        QAction* split = mark->addAction(QStringLiteral("Add Edit"));
        split->setObjectName(QStringLiteral("timeline.split_at_playhead"));

        QTemporaryDir dir;
        expect(dir.isValid(), "temp settings dir", ok);
        QSettings settings(dir.filePath(QStringLiteral("shortcuts.ini")), QSettings::IniFormat);
        ShortcutManager manager;
        manager.loadFrom(settings);
        manager.setPreviewState("premiere", {});
        manager.applyToMenus(&bar);
        expect(insert->shortcuts().size() == 1 &&
                   insert->shortcuts().constFirst() == QKeySequence(QStringLiteral("Comma")),
               "premiere insert reaches menu actions", ok);
        expect(find->shortcuts().constFirst() == QKeySequence(QStringLiteral("Ctrl+Shift+K")),
               "premiere find moves off Ctrl+K", ok);

        manager.setPreviewState("resolve", {});
        manager.applyToMenus(&bar);
        expect(split->shortcuts().constFirst() == QKeySequence(QStringLiteral("Ctrl+Backslash")),
               "backslash shortcuts parse for menus", ok);
        const QKeyEvent split_press(QEvent::KeyPress, Qt::Key_Backslash, Qt::ControlModifier);
        expect(manager.matchEvent(&split_press) == "timeline.split_at_playhead",
               "key events resolve through the active preset", ok);
        const QKeyEvent find_press(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        expect(manager.matchEvent(&find_press) == "edit.find_action",
               "control keys match canonical bindings", ok);

        settings.setValue(QStringLiteral("shortcuts/preset"), QStringLiteral("avid"));
        settings.setValue(QStringLiteral("shortcuts/custom/mark.insert"), QStringLiteral("W"));
        settings.setValue(QStringLiteral("shortcuts/custom/mark.overwrite"),
                          QStringLiteral("bogus"));
        manager.loadFrom(settings);
        expect(manager.preset() == "avid", "preset persists", ok);
        manager.applyToMenus(&bar);
        expect(insert->shortcuts().constFirst() == QKeySequence(QStringLiteral("W")),
               "custom override persists", ok);
        const QKeyEvent overwrite_press(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
        expect(manager.matchEvent(&overwrite_press) == "mark.overwrite",
               "invalid custom values do not disturb presets", ok);
    }

    {
        bool all_parse = true;
        for (const Preset preset : presets()) {
            for (const auto& binding : preset_bindings(preset)) {
                const std::string canonical =
                    canvas::core::actions::canonical_shortcut(binding.shortcut);
                if (canonical.empty()) continue;
                if (QKeySequence(QString::fromStdString(canonical)).isEmpty()) {
                    std::printf("UNPARSED: %s %s\n", binding.command.c_str(), canonical.c_str());
                    all_parse = false;
                }
            }
        }
        expect(all_parse, "every preset shortcut parses as a QKeySequence", ok);
    }

    {
        QMenuBar bar;
        ShortcutManager manager;
        manager.setPreviewState("resolve", {});
        KeyboardCustomizationDialog dialog(&bar, &manager);
        const auto* presets = dialog.findChild<QComboBox*>(QStringLiteral("KeymapPresetCombo"));
        expect(presets != nullptr && presets->count() == 5, "dialog lists five presets", ok);
        expect(dialog.findChild<QTreeWidget*>() != nullptr, "dialog builds command tree", ok);
    }

    {
        KeyboardMapWidget keyboard;
        keyboard.setBindings(preset_bindings(Preset::Resolve));
        keyboard.setActiveShortcut(QStringLiteral("Ctrl+K"));
        expect(keyboard.activeShortcut() == QStringLiteral("Ctrl+K"),
               "keyboard keeps canonical selection", ok);
        keyboard.setModifier(QStringLiteral("Shift"), true);
        expect(keyboard.activeShortcut() == QStringLiteral("Ctrl+Shift+K"),
               "modifier buttons join the active key", ok);
    }

    std::printf(ok ? "ALL SHORTCUT MANAGER TESTS PASSED\n" : "SHORTCUT MANAGER TEST(S) FAILED\n");
    return ok ? 0 : 1;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    return run_checks();
}
