#include "UX/ActionSearch.hpp"
#include "UX/MainWindow.hpp"
#include "UX/SettingsDialog.hpp"
#include "UX/theme.hpp"
#include "ui_MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QObject>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

#include <functional>

namespace canvas::gui {

void build_app_menus(MainWindow& mw) {
    const auto tag = [](QAction* action, const char* id) {
        action->setObjectName(QString::fromUtf8(id));
        return action;
    };
    auto* canvas_menu = mw.ui->menubar->addMenu(MainWindow::tr("Novara Canvas"));
    canvas_menu->addAction(MainWindow::tr("&About Novara Canvas Studio"), &mw, [&mw] {
        QMessageBox::about(
            &mw, MainWindow::tr("About Novara Canvas Studio"),
            MainWindow::tr("Novara Canvas Studio\n\n"
                           "C++20 / Qt %1 / FFmpeg nonlinear video editor — "
                           "dark, editor-grade UI.\n\n"
                           "Ships an auto-detected \u201cHyprDark\u201d palette that "
                           "pre-compensates for Hyprland's native-Wayland "
                           "colour-management pass.")
                .arg(QString::fromUtf8(qVersion())));
    });
    canvas_menu->addSeparator();

    auto* appearance = canvas_menu->addMenu(MainWindow::tr("A&ppearance"));
    apply_rounded_menu(appearance);
    auto* appearance_group = new QActionGroup(appearance);
    appearance_group->setExclusive(true);
    const bool light_now = is_light();
    const bool hypr_now = is_hypr_dark();
    auto* dark_action = appearance->addAction(MainWindow::tr("&Dark"));
    dark_action->setCheckable(true);
    dark_action->setChecked(!light_now && !hypr_now);
    auto* hypr_action = appearance->addAction(MainWindow::tr("Dark (&Hyprland)"));
    hypr_action->setCheckable(true);
    hypr_action->setChecked(!light_now && hypr_now);
    hypr_action->setToolTip(MainWindow::tr(
        "Compensated for Hyprland's native-Wayland colour-management pass."));
    auto* light_action = appearance->addAction(MainWindow::tr("&Light"));
    light_action->setCheckable(true);
    light_action->setChecked(light_now);
    appearance_group->addAction(dark_action);
    appearance_group->addAction(hypr_action);
    appearance_group->addAction(light_action);
    const auto select_theme = [](bool light, bool hypr) {
        set_light(light);
        set_hypr_dark(hypr);
        QSettings settings;
        settings.setValue(QStringLiteral("appearance/theme"),
                          light ? QStringLiteral("light") : QStringLiteral("dark"));
        settings.setValue(QStringLiteral("appearance/hypr_dark"), hypr);
    };
    QObject::connect(dark_action, &QAction::triggered, &mw,
                     [select_theme] { select_theme(false, false); });
    QObject::connect(hypr_action, &QAction::triggered, &mw,
                     [select_theme] { select_theme(false, true); });
    QObject::connect(light_action, &QAction::triggered, &mw,
                     [select_theme] { select_theme(true, false); });

    const auto show_preferences = [&mw]() {
        auto* dialog = new SettingsDialog(
            &mw, [&mw](bool on) { mw.controller_.set_scrub_audio_enabled(on); });
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    };
    tag(canvas_menu->addAction(MainWindow::tr("&Preferences..."), QKeySequence::Preferences,
                                &mw, show_preferences),
        "app.preferences");
    canvas_menu->addSeparator();
    tag(canvas_menu->addAction(MainWindow::tr("&Quit Novara Canvas Studio"), QKeySequence::Quit,
                                qApp, &QApplication::quit),
        "app.quit");

    auto* file = mw.ui->menubar->addMenu(MainWindow::tr("&File"));
    tag(file->addAction(MainWindow::tr("&New Project"), QKeySequence::New, &mw,
                         &MainWindow::on_new_project),
        "file.new_project");
    tag(file->addAction(MainWindow::tr("&Open Project..."), QKeySequence::Open, &mw,
                         &MainWindow::on_open_project),
        "file.open_project");
    mw.open_recent_menu_ = file->addMenu(MainWindow::tr("Open &Recent"));
    mw.open_recent_menu_->setEnabled(false);
    QObject::connect(mw.open_recent_menu_, &QMenu::triggered, &mw, &MainWindow::on_open_recent_file);
    tag(file->addAction(MainWindow::tr("&Project Manager"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), &mw,
                         &MainWindow::enter_project_manager),
        "file.project_manager");
    tag(file->addAction(MainWindow::tr("&Save Project"), QKeySequence::Save, &mw,
                         &MainWindow::on_save_project),
        "file.save_project");
    tag(file->addAction(MainWindow::tr("Save Project &As..."), QKeySequence::SaveAs, &mw,
                         &MainWindow::on_save_project_as),
        "file.save_as");
    tag(file->addAction(MainWindow::tr("&Archive Project..."), &mw, &MainWindow::on_archive_project),
        "file.archive_project");
    file->addSeparator();
    tag(file->addAction(MainWindow::tr("&Import Media..."), QKeySequence(Qt::CTRL | Qt::Key_I),
                         &mw, &MainWindow::on_import_media),
        "file.import_media");
    file->addSeparator();
    tag(file->addAction(MainWindow::tr("Export &EDL..."), &mw, &MainWindow::export_edl),
        "file.export_edl");

    auto* edit = mw.ui->menubar->addMenu(MainWindow::tr("&Edit"));
    tag(edit->addAction(MainWindow::tr("&Undo"), QKeySequence::Undo, &mw, &MainWindow::on_undo),
        "edit.undo");
    tag(edit->addAction(MainWindow::tr("&Redo"), QKeySequence::Redo, &mw, &MainWindow::on_redo),
        "edit.redo");
    edit->addSeparator();
    tag(edit->addAction(MainWindow::tr("&Find Action..."), QKeySequence(Qt::CTRL | Qt::Key_K),
                         &mw,
                         [&mw] {
                             auto* search = new canvas::gui::ActionSearch(mw.ui->menubar, &mw);
                             search->setAttribute(Qt::WA_DeleteOnClose);
                             search->collect_actions();
                             search->open();
                         }),
        "edit.find_action");
    tag(edit->addAction(MainWindow::tr("&Keyboard Customization..."),
                         QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K), &mw,
                         [&mw] { mw.open_keyboard_customization(); }),
        "app.keyboard_customization");
    auto* trim = mw.ui->menubar->addMenu(MainWindow::tr("&Trim"));
    tag(trim->addAction(MainWindow::tr("Ripple Delete"),
                         QKeySequence(Qt::SHIFT | Qt::Key_Backspace), &mw,
                         [&mw] { mw.delete_selected_clip(true); }),
        "trim.ripple_delete");
    tag(trim->addAction(MainWindow::tr("Lift"), QKeySequence(Qt::Key_Backspace), &mw,
                         [&mw] { mw.delete_selected_clip(false); }),
        "trim.lift");
    trim->addAction(MainWindow::tr("Cycle Edit Point Side"), QKeySequence(Qt::Key_U));
    tag(trim->addAction(MainWindow::tr("Remove All Transitions"), &mw,
                         [&mw] { mw.remove_all_transitions(); }),
        "trim.remove_all_transitions");

    auto* timeline_menu = mw.ui->menubar->addMenu(MainWindow::tr("&Timeline"));
    tag(timeline_menu->addAction(MainWindow::tr("Add Edit"),
                                 QKeySequence(Qt::CTRL | Qt::Key_Backslash), &mw,
                                 [&mw] { mw.split_selected_clips_at_playhead(); }),
        "timeline.split_at_playhead");
    tag(timeline_menu->addAction(MainWindow::tr("Add Marker"), QKeySequence(Qt::Key_M), &mw,
                                 [&mw] { mw.toggle_bookmark_at_playhead(); }),
        "timeline.toggle_bookmark");
    tag(timeline_menu->addAction(MainWindow::tr("Select Tool"), QKeySequence(Qt::Key_A), &mw,
                                 [&mw] {
                                     if (mw.timeline_ != nullptr)
                                         mw.timeline_->set_tool(TimelineWidget::Tool::Select);
                                 }),
        "timeline.select_tool");
    tag(timeline_menu->addAction(MainWindow::tr("Blade Tool"), QKeySequence(Qt::Key_B), &mw,
                                 [&mw] {
                                     if (mw.timeline_ != nullptr)
                                         mw.timeline_->set_tool(TimelineWidget::Tool::Blade);
                                 }),
        "timeline.blade_tool");
    tag(timeline_menu->addAction(MainWindow::tr("Add Title"),
                                 QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T), &mw,
                                 [&mw] { mw.add_title_clip(); }),
        "timeline.add_title");
    tag(timeline_menu->addAction(MainWindow::tr("Zoom to Fit"), QKeySequence(Qt::SHIFT | Qt::Key_Z),
                                 &mw, [&mw] { mw.timeline_->zoom_fit(); }),
        "timeline.zoom_fit");
    const auto collapse_all = [&mw](bool collapsed) {
        if (!mw.project_) return;
        auto cmd = canvas::core::set_all_tracks_collapsed(mw.project_->sequence, collapsed);
        if (!cmd) return;
        mw.has_unsaved_changes_ = true;
        mw.undo_.record(std::move(cmd));
        mw.refresh_timeline();
        mw.push_snapshot();
    };
    tag(timeline_menu->addAction(MainWindow::tr("Collapse All Tracks"),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), &mw,
                                  [collapse_all] { collapse_all(true); }),
        "timeline.collapse_all");
    tag(timeline_menu->addAction(MainWindow::tr("Expand All Tracks"),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), &mw,
                                  [collapse_all] { collapse_all(false); }),
        "timeline.expand_all");

    auto* ai_menu = timeline_menu->addMenu(MainWindow::tr("AI Tools"));
    apply_rounded_menu(ai_menu);
    tag(ai_menu->addAction(MainWindow::tr("Generate Subtitles From Audio…"), &mw,
                          [&mw] { mw.open_subtitle_dialog(); }),
        "timeline.generate_subtitles");
    ai_menu->addSeparator();
    auto* ai_hint = new QAction(MainWindow::tr("Transcription runs locally"), ai_menu);
    ai_hint->setEnabled(false);
    ai_menu->addAction(ai_hint);

    auto* clip_menu = mw.ui->menubar->addMenu(MainWindow::tr("&Clip"));
    tag(clip_menu->addAction(MainWindow::tr("Add Transition"), QKeySequence(Qt::CTRL | Qt::Key_T),
                             &mw, [&mw] { mw.toggle_transition_on_selected(); }),
        "clip.add_transition");
    tag(clip_menu->addAction(MainWindow::tr("Enable/Disable Clip"), QKeySequence(Qt::Key_D), &mw,
                             [&mw] { mw.toggle_disable_selected_clip(); }),
        "clip.toggle_enable");
    tag(clip_menu->addAction(MainWindow::tr("Link/Unlink"),
                             QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L), &mw,
                             [&mw] { mw.toggle_clip_link(); }),
        "clip.toggle_link");

    auto* clip_color_menu = clip_menu->addMenu(MainWindow::tr("Clip Colour") + QStringLiteral(" >"));
    apply_rounded_menu(clip_color_menu);
    const auto swatch_action = [&mw](uint8_t color) {
        QAction* act = new QAction(&mw);
        act->setData(color);
        QObject::connect(act, &QAction::triggered, &mw,
                         [&mw, color]() { mw.apply_clip_color(color); });
        return act;
    };
    const QColor* swatches = clip_color_swatches();
    for (int i = 0; i < 12; ++i) {
        QAction* act = swatch_action(static_cast<uint8_t>(i + 1));
        act->setText(QStringLiteral("#%1").arg(swatches[i].name()));
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QBrush(swatches[i]));
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 3, 3);
        act->setIcon(QIcon(pm));
        clip_color_menu->addAction(act);
    }
    clip_color_menu->addSeparator();
    QAction* no_color = swatch_action(0);
    no_color->setText(MainWindow::tr("&No Colour"));
    clip_color_menu->addAction(no_color);
    QObject::connect(clip_menu, &QMenu::aboutToShow, &mw, [clip_color_menu, &mw]() {
        clip_color_menu->setEnabled(mw.selected_clip_ != 0);
    });

    auto* mark_menu = mw.ui->menubar->addMenu(MainWindow::tr("&Mark"));
    tag(mark_menu->addAction(MainWindow::tr("Mark In"), QKeySequence(Qt::Key_I), &mw,
                             [&mw] { mw.mark_in(); }),
        "mark.in");
    tag(mark_menu->addAction(MainWindow::tr("Mark Out"), QKeySequence(Qt::Key_O), &mw,
                             [&mw] { mw.mark_out(); }),
        "mark.out");
    tag(mark_menu->addAction(MainWindow::tr("Clear In/Out"), QKeySequence(Qt::ALT | Qt::Key_X),
                             &mw, [&mw] { mw.clear_in_out(); }),
        "mark.clear");
    mark_menu->addSeparator();
    tag(mark_menu->addAction(MainWindow::tr("Create Range from In/Out"),
                             QKeySequence(Qt::ALT | Qt::Key_R), &mw,
                             [&mw] { mw.create_range_from_marks(); }),
        "mark.create_range");
    tag(mark_menu->addAction(MainWindow::tr("Insert from Source"), QKeySequence(Qt::Key_Comma),
                             &mw,
                             [&mw] { mw.three_point_place(canvas::core::Placement::Insert); }),
        "mark.insert");
    tag(mark_menu->addAction(MainWindow::tr("Overwrite from Source"),
                             QKeySequence(Qt::Key_Period), &mw,
                             [&mw] { mw.three_point_place(canvas::core::Placement::Overwrite); }),
        "mark.overwrite");
    tag(mark_menu->addAction(MainWindow::tr("Append at End"),
                             QKeySequence(Qt::SHIFT | Qt::Key_F12), &mw,
                             [&mw] { mw.three_point_place(canvas::core::Placement::AppendAtEnd); }),
        "mark.append");
    tag(mark_menu->addAction(MainWindow::tr("Place on Top"), QKeySequence(Qt::Key_F12), &mw,
                             [&mw] { mw.three_point_place(canvas::core::Placement::PlaceOnTop); }),
        "mark.place_on_top");

    auto* view = mw.ui->menubar->addMenu(MainWindow::tr("&View"));
    auto* inspector_toggle_action = tag(
        view->addAction(MainWindow::tr("&Inspector"), QKeySequence(Qt::ALT | Qt::Key_I), &mw,
                        [&mw] {
                            if (mw.inspector_dock_)
                                mw.inspector_dock_->setVisible(!mw.inspector_dock_->isVisible());
                        }),
        "view.inspector");
    inspector_toggle_action->setCheckable(true);
    mw.inspector_toggle_action_ = inspector_toggle_action;
    tag(view->addAction(MainWindow::tr("Toggle &Full Screen"), QKeySequence(Qt::Key_F11), &mw,
                        [&mw] { mw.isFullScreen() ? mw.showNormal() : mw.showFullScreen(); }),
        "view.fullscreen");

    auto* playback = mw.ui->menubar->addMenu(MainWindow::tr("Play&back"));
    tag(playback->addAction(MainWindow::tr("&Play/Pause"), QKeySequence(Qt::Key_Space), &mw,
                            [&mw] { mw.controller_.toggle_play_pause(); }),
        "playback.play_pause");
    tag(playback->addAction(MainWindow::tr("Pa&use"), QKeySequence(Qt::Key_K), &mw,
                            [&mw] { mw.controller_.pause(); }),
        "playback.pause");
    tag(playback->addAction(MainWindow::tr("Step &Back"), QKeySequence(Qt::Key_J), &mw,
                            [&mw] { mw.controller_.pause(); mw.controller_.step(-1); }),
        "playback.step_back");
    tag(playback->addAction(MainWindow::tr("P&lay"), QKeySequence(Qt::Key_L), &mw,
                            [&mw] { mw.controller_.play(); }),
        "playback.play_forward");
    tag(playback->addAction(MainWindow::tr("Previous &Frame"), QKeySequence(Qt::Key_Left), &mw,
                            [&mw] { mw.controller_.pause(); mw.controller_.step(-1); }),
        "playback.previous_frame");
    tag(playback->addAction(MainWindow::tr("&Next Frame"), QKeySequence(Qt::Key_Right), &mw,
                            [&mw] { mw.controller_.pause(); mw.controller_.step(1); }),
        "playback.next_frame");
    tag(playback->addAction(MainWindow::tr("Step Back One &Second"),
                            QKeySequence(Qt::SHIFT | Qt::Key_Left), &mw,
                            [&mw] {
                                mw.controller_.pause();
                                mw.controller_.step(-static_cast<int64_t>(mw.fps_));
                            }),
        "playback.step_back_second");
    tag(playback->addAction(MainWindow::tr("Step Forward One Secon&d"),
                            QKeySequence(Qt::SHIFT | Qt::Key_Right), &mw,
                            [&mw] {
                                mw.controller_.pause();
                                mw.controller_.step(static_cast<int64_t>(mw.fps_));
                            }),
        "playback.step_forward_second");
    tag(playback->addAction(MainWindow::tr("Go &to Start"), QKeySequence(Qt::Key_Home), &mw,
                            [&mw] { mw.controller_.seek(0); }),
        "playback.go_to_start");
    tag(playback->addAction(MainWindow::tr("Go &to End"), QKeySequence(Qt::Key_End), &mw,
                            [&mw] { mw.controller_.seek(mw.total_frames_ - 1); }),
        "playback.go_to_end");

    for (const char* name : {"Fusion", "Color", "Fairlight", "Workspace", "Help"}) {
        auto* m = mw.ui->menubar->addMenu(MainWindow::tr(name));
        if (qstrcmp(name, "Help") == 0) {
            QAction* help_item = m->addAction(MainWindow::tr("Novara Canvas Studio Help"));
            help_item->setEnabled(false);
        } else if (qstrcmp(name, "Workspace") == 0) {
            m->addAction(MainWindow::tr("Reset UI Layout"));
        } else {
            m->setEnabled(false);
        }
    }

    std::function<void(QMenu*)> round_menu_tree = [&](QMenu* menu) {
        if (!menu) return;
        apply_rounded_menu(menu);
        const auto actions = menu->actions();
        for (QAction* act : actions)
            if (QMenu* sub = act->menu()) round_menu_tree(sub);
    };
    const auto bar_actions = mw.ui->menubar->actions();
    for (QAction* act : bar_actions)
        round_menu_tree(act->menu());
    mw.shortcuts_.applyToMenus(mw.ui->menubar);
}

}
