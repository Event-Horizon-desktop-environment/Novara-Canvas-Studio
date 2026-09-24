#include "UX/MainWindow.hpp"

#include "UX/ActionSearch.hpp"
#include "UX/KeyboardCustomizationDialog.hpp"
#include "UX/SettingsDialog.hpp"
#include "ui_MainWindow.h"

#include <QApplication>
#include <QKeyEvent>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace canvas::gui {

void MainWindow::open_keyboard_customization() {
    auto* dialog = new KeyboardCustomizationDialog(ui->menubar, &shortcuts_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

bool MainWindow::dispatch_shortcut_event(QKeyEvent* event) {
    const std::string id = shortcuts_.matchEvent(event);
    if (id.empty()) return false;

    if (id == "app.keyboard_customization") open_keyboard_customization();
    else if (id == "app.preferences") {
        auto* dialog =
            new SettingsDialog(this, [this](bool on) { controller_.set_scrub_audio_enabled(on); });
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    } else if (id == "app.quit")
        QApplication::quit();
    else if (id == "file.new_project")
        on_new_project();
    else if (id == "file.open_project")
        on_open_project();
    else if (id == "file.project_manager")
        enter_project_manager();
    else if (id == "file.save_project")
        on_save_project();
    else if (id == "file.save_as")
        on_save_project_as();
    else if (id == "file.archive_project")
        on_archive_project();
    else if (id == "file.import_media")
        on_import_media();
    else if (id == "file.export_edl")
        export_edl();
    else if (id == "edit.undo")
        on_undo();
    else if (id == "edit.redo")
        on_redo();
    else if (id == "edit.find_action") {
        auto* search = new ActionSearch(ui->menubar, this);
        search->setAttribute(Qt::WA_DeleteOnClose);
        search->collect_actions();
        search->open();
    } else if (id == "clip.toggle_enable")
        toggle_disable_selected_clip();
    else if (id == "clip.add_transition")
        toggle_transition_on_selected();
    else if (id == "clip.add_title")
        add_title_clip();
    else if (id == "clip.toggle_link")
        toggle_clip_link();
    else if (id == "trim.ripple_delete")
        delete_selected_clip(true);
    else if (id == "trim.lift")
        delete_selected_clip(false);
    else if (id == "trim.remove_all_transitions")
        remove_all_transitions();
    else if (id == "timeline.split_at_playhead")
        split_selected_clips_at_playhead();
    else if (id == "timeline.toggle_bookmark")
        toggle_bookmark_at_playhead();
    else if (id == "timeline.select_tool" && timeline_ != nullptr)
        timeline_->set_tool(TimelineWidget::Tool::Select);
    else if (id == "timeline.blade_tool" && timeline_ != nullptr)
        timeline_->set_tool(TimelineWidget::Tool::Blade);
    else if (id == "timeline.zoom_fit" && timeline_ != nullptr)
        timeline_->zoom_fit();
    else if (id == "timeline.collapse_all" || id == "timeline.expand_all") {
        if (project_ != nullptr) {
            auto cmd = canvas::core::set_all_tracks_collapsed(project_->sequence,
                                                              id == "timeline.collapse_all");
            if (cmd != nullptr) {
                undo_.record(std::move(cmd));
                has_unsaved_changes_ = true;
                refresh_timeline();
                push_snapshot();
            }
        }
    } else if (id == "timeline.generate_subtitles")
        open_subtitle_dialog();
    else if (id == "mark.in")
        mark_in();
    else if (id == "mark.out")
        mark_out();
    else if (id == "mark.clear")
        clear_in_out();
    else if (id == "mark.create_range")
        create_range_from_marks();
    else if (id == "mark.insert")
        three_point_place(canvas::core::Placement::Insert);
    else if (id == "mark.overwrite")
        three_point_place(canvas::core::Placement::Overwrite);
    else if (id == "mark.append")
        three_point_place(canvas::core::Placement::AppendAtEnd);
    else if (id == "mark.place_on_top")
        three_point_place(canvas::core::Placement::PlaceOnTop);
    else if (id == "playback.play_pause")
        controller_.toggle_play_pause();
    else if (id == "playback.pause")
        controller_.pause();
    else if (id == "playback.step_back") {
        controller_.pause();
        controller_.step(-1);
    } else if (id == "playback.play_forward")
        controller_.play();
    else if (id == "playback.previous_frame") {
        controller_.pause();
        controller_.step(-1);
    } else if (id == "playback.next_frame") {
        controller_.pause();
        controller_.step(1);
    } else if (id == "playback.step_back_second") {
        controller_.pause();
        controller_.step(-static_cast<int64_t>(fps_));
    } else if (id == "playback.step_forward_second") {
        controller_.pause();
        controller_.step(static_cast<int64_t>(fps_));
    } else if (id == "playback.go_to_start") {
        controller_.pause();
        controller_.seek(0);
    } else if (id == "playback.go_to_end") {
        controller_.pause();
        controller_.seek(total_frames_ - 1);
    } else if (id == "view.inspector") {
        if (inspector_dock_ != nullptr) inspector_dock_->setVisible(!inspector_dock_->isVisible());
        if (inspector_toggle_action_ != nullptr)
            inspector_toggle_action_->setChecked(inspector_dock_ != nullptr &&
                                                 inspector_dock_->isVisible());
    } else if (id == "view.fullscreen") {
        isFullScreen() ? showNormal() : showFullScreen();
    } else {
        return false;
    }
    return true;
}

void MainWindow::split_selected_clips_at_playhead() {
    if (project_ == nullptr || timeline_ == nullptr) return;
    std::vector<canvas::core::ClipId> ids = timeline_->selected_clip_ids();
    if (ids.empty() && selected_clip_ != 0) ids.push_back(selected_clip_);
    if (ids.empty()) return;

    const int64_t pos = current_frame_ < 0 ? 0 : current_frame_;
    std::vector<canvas::core::ClipId> done;
    std::vector<std::unique_ptr<canvas::core::ICommand>> parts;
    const auto selected = [&](canvas::core::ClipId id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    for (const canvas::core::ClipId id : ids) {
        if (std::find(done.begin(), done.end(), id) != done.end()) continue;
        bool split = false;
        for (int kind_index = 0; kind_index < 2 && !split; ++kind_index) {
            const auto kind = kind_index == 0 ? canvas::core::Track::Kind::Video
                                              : canvas::core::Track::Kind::Audio;
            auto& tracks = kind == canvas::core::Track::Kind::Video
                               ? project_->sequence.video_tracks
                               : project_->sequence.audio_tracks;
            for (std::size_t track = 0; track < tracks.size() && !split; ++track) {
                const canvas::core::Clip* clip = tracks[track].clip_with_id(id);
                if (clip == nullptr || pos <= clip->tl_in || pos >= clip->tl_out) continue;
                auto cmd = canvas::core::blade_linked_at(project_->sequence, kind, track, pos);
                if (cmd == nullptr) continue;
                parts.push_back(std::move(cmd));
                done.push_back(id);
                if (clip->linked_id != 0 && selected(clip->linked_id))
                    done.push_back(clip->linked_id);
                split = true;
            }
        }
        done.push_back(id);
    }
    if (parts.empty()) return;
    if (parts.size() == 1) undo_.record(std::move(parts.front()));
    else
        undo_.record(std::make_unique<canvas::core::GroupCommand>("Split clips at playhead",
                                                                  std::move(parts)));
    has_unsaved_changes_ = true;
    refresh_timeline();
    push_snapshot();
}

void MainWindow::toggle_clip_link() {
    if (project_ == nullptr) return;
    canvas::core::ClipId id = selected_clip_;
    if (timeline_ != nullptr && !timeline_->selected_clip_ids().empty())
        id = timeline_->selected_clip_ids().front();
    if (id == 0) return;

    for (int kind_index = 0; kind_index < 2; ++kind_index) {
        const auto kind =
            kind_index == 0 ? canvas::core::Track::Kind::Video : canvas::core::Track::Kind::Audio;
        auto& tracks = kind == canvas::core::Track::Kind::Video ? project_->sequence.video_tracks
                                                                : project_->sequence.audio_tracks;
        for (std::size_t track = 0; track < tracks.size(); ++track) {
            const canvas::core::Clip* clip = tracks[track].clip_with_id(id);
            if (clip == nullptr) continue;
            auto cmd = clip->is_linked()
                           ? canvas::core::unlink_clip(project_->sequence, kind, track, id)
                           : canvas::core::link_clip(project_->sequence, kind, track, id);
            if (cmd == nullptr) return;
            undo_.record(std::move(cmd));
            has_unsaved_changes_ = true;
            refresh_timeline();
            push_snapshot();
            return;
        }
    }
}

}
