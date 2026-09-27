#include "UX/MainWindow.hpp"
#include "features/color/mini_timeline_strip.hpp"

#include <QInputDialog>
#include <QLineEdit>
#include <QStatusBar>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace canvas::gui {

namespace {

std::size_t clamped_active(const canvas::core::Project& project) {
    if (project.timelines.empty()) return 0;
    return std::min(project.active_timeline, project.timelines.size() - 1);
}

uint64_t max_clip_id(const canvas::core::Project& project) {
    uint64_t max_id = 0;
    for (const auto& tl : project.timelines)
        for (const auto* tracks : {&tl.video_tracks, &tl.audio_tracks})
            for (const auto& track : *tracks)
                for (const auto& clip : track.clips) max_id = std::max(max_id, clip.id);
    return max_id;
}

uint64_t max_bookmark_id(const canvas::core::Project& project) {
    uint64_t max_id = 0;
    for (const auto& tl : project.timelines)
        for (const auto& b : tl.bookmarks) max_id = std::max(max_id, b.id);
    return max_id;
}

std::string next_timeline_name(const canvas::core::Project& project) {
    for (std::size_t n = 1;; ++n) {
        const std::string candidate = "Timeline " + std::to_string(n);
        bool taken = false;
        for (const auto& tl : project.timelines)
            if (tl.name == candidate) {
                taken = true;
                break;
            }
        if (!taken) return candidate;
    }
}

canvas::core::Sequence make_empty_timeline(const std::string& name, const double fps,
                                           const uint64_t first_clip_id) {
    canvas::core::Sequence seq;
    seq.name = name;
    seq.fps = fps;
    seq.next_clip_id = first_clip_id;
    seq.next_bookmark_id = 1;
    canvas::core::Track v1;
    v1.kind = canvas::core::Track::Kind::Video;
    v1.name = "V1";
    canvas::core::Track a1;
    a1.kind = canvas::core::Track::Kind::Audio;
    a1.name = "A1";
    seq.video_tracks.push_back(std::move(v1));
    seq.audio_tracks.push_back(std::move(a1));
    return seq;
}

void remap_copy_ids(canvas::core::Sequence& seq, uint64_t& next_clip, uint64_t& next_bookmark) {
    std::vector<std::pair<uint64_t, uint64_t>> clip_map;
    for (auto* tracks : {&seq.video_tracks, &seq.audio_tracks})
        for (auto& track : *tracks)
            for (auto& clip : track.clips) {
                clip_map.emplace_back(clip.id, next_clip);
                clip.id = next_clip++;
            }
    for (auto* tracks : {&seq.video_tracks, &seq.audio_tracks})
        for (auto& track : *tracks)
            for (auto& clip : track.clips) {
                if (clip.linked_id == 0) continue;
                for (const auto& [old_id, new_id] : clip_map)
                    if (clip.linked_id == old_id) {
                        clip.linked_id = new_id;
                        break;
                    }
            }
    seq.next_clip_id = next_clip;
    for (auto& b : seq.bookmarks) b.id = next_bookmark++;
    seq.next_bookmark_id = next_bookmark;
}

}  // namespace

canvas::core::UndoStack& MainWindow::active_undo() {
    const std::size_t want =
        project_ && !project_->timelines.empty() ? clamped_active(*project_) + 1 : 1;
    while (undo_stacks_.size() < want) undo_stacks_.emplace_back();
    return undo_stacks_[want - 1];
}

void MainWindow::rebuild_timeline_tabs() {
    if (!timeline_tabs_ || !project_) return;
    timeline_tabs_->blockSignals(true);
    while (timeline_tabs_->count() > 0) timeline_tabs_->removeTab(0);
    for (const auto& tl : project_->timelines)
        timeline_tabs_->addTab(QString::fromStdString(tl.name));
    timeline_tabs_->setCurrentIndex(static_cast<int>(clamped_active(*project_)));
    timeline_tabs_->blockSignals(false);
}

void MainWindow::activate_timeline(const std::size_t index, const int64_t frame) {
    if (!project_ || project_->timelines.empty()) return;
    project_->active_timeline = std::min(index, project_->timelines.size() - 1);
    selected_clip_ = 0;
    selected_clip_ids_.clear();
    refresh_timeline();
    if (color_mini_strip_) color_mini_strip_->set_sequence(&project_->active_sequence());
    rebuild_timeline_tabs();
    update_inspector_audio_full(*this);
    update_inspector_file(*this);
    update_inspector_visual(*this);
    const int64_t last = std::max<int64_t>(total_frames_ - 1, 0);
    push_snapshot(std::max<int64_t>(0, std::min(frame, last)));
}

void MainWindow::switch_timeline(const std::size_t index) {
    if (!project_ || project_->timelines.empty() || index >= project_->timelines.size()) return;
    if (index == clamped_active(*project_)) {
        rebuild_timeline_tabs();
        return;
    }
    if (controller_.is_playing()) controller_.pause();
    const int64_t pos = controller_.current_frame();
    const std::size_t cur = clamped_active(*project_);
    if (cur < timeline_playheads_.size() && pos >= 0) timeline_playheads_[cur] = pos;
    const std::size_t target = index < timeline_playheads_.size() ? index : cur;
    const int64_t frame = target < timeline_playheads_.size() ? timeline_playheads_[target] : 0;
    activate_timeline(index, frame);
}

void MainWindow::new_timeline() {
    if (!project_) return;
    if (controller_.is_playing()) controller_.pause();
    const int64_t pos = controller_.current_frame();
    const std::size_t cur = clamped_active(*project_);
    if (cur < timeline_playheads_.size() && pos >= 0) timeline_playheads_[cur] = pos;
    const double fps = project_->active_sequence().fps;
    project_->timelines.push_back(
        make_empty_timeline(next_timeline_name(*project_), fps, max_clip_id(*project_) + 1));
    undo_stacks_.emplace_back();
    timeline_playheads_.push_back(0);
    has_unsaved_changes_ = true;
    activate_timeline(project_->timelines.size() - 1, 0);
}

void MainWindow::duplicate_timeline(const std::size_t index) {
    if (!project_ || project_->timelines.empty() || index >= project_->timelines.size()) return;
    if (controller_.is_playing()) controller_.pause();
    const int64_t pos = controller_.current_frame();
    const std::size_t cur = clamped_active(*project_);
    if (cur < timeline_playheads_.size() && pos >= 0) timeline_playheads_[cur] = pos;
    canvas::core::Sequence copy = project_->timelines[index];
    uint64_t next_clip = max_clip_id(*project_) + 1;
    uint64_t next_bookmark = max_bookmark_id(*project_) + 1;
    remap_copy_ids(copy, next_clip, next_bookmark);
    copy.name = next_timeline_name(*project_);
    const std::size_t at = index + 1;
    project_->timelines.insert(project_->timelines.begin() + static_cast<std::ptrdiff_t>(at),
                               std::move(copy));
    undo_stacks_.insert(undo_stacks_.begin() + static_cast<std::ptrdiff_t>(std::min(at, undo_stacks_.size())),
                        canvas::core::UndoStack{});
    timeline_playheads_.insert(
        timeline_playheads_.begin() + static_cast<std::ptrdiff_t>(std::min(at, timeline_playheads_.size())), 0);
    has_unsaved_changes_ = true;
    activate_timeline(at, pos >= 0 ? pos : 0);
}

void MainWindow::close_timeline(const std::size_t index) {
    if (!project_ || project_->timelines.size() <= 1 || index >= project_->timelines.size()) {
        if (status_) status_->showMessage(tr("Cannot close the last timeline."));
        return;
    }
    if (controller_.is_playing()) controller_.pause();
    const std::size_t cur = clamped_active(*project_);
    project_->timelines.erase(project_->timelines.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < undo_stacks_.size())
        undo_stacks_.erase(undo_stacks_.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < timeline_playheads_.size())
        timeline_playheads_.erase(timeline_playheads_.begin() + static_cast<std::ptrdiff_t>(index));
    std::size_t next = cur;
    if (index == cur)
        next = std::min(index, project_->timelines.size() - 1);
    else if (index < cur)
        next = cur - 1;
    has_unsaved_changes_ = true;
    const int64_t frame =
        next < timeline_playheads_.size() ? timeline_playheads_[next] : controller_.current_frame();
    activate_timeline(next, frame >= 0 ? frame : 0);
}

void MainWindow::rename_timeline(const std::size_t index) {
    if (!project_ || project_->timelines.empty() || index >= project_->timelines.size()) return;
    const QString current = QString::fromStdString(project_->timelines[index].name);
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename timeline"), tr("Name:"), QLineEdit::Normal, current, &ok);
    if (!ok) return;
    const std::string trimmed = name.trimmed().toStdString();
    if (trimmed.empty() || trimmed == project_->timelines[index].name) return;
    project_->timelines[index].name = trimmed;
    has_unsaved_changes_ = true;
    rebuild_timeline_tabs();
}

void MainWindow::move_timeline(const std::size_t from, const std::size_t to) {
    if (!project_ || project_->timelines.empty()) return;
    const std::size_t n = project_->timelines.size();
    if (from >= n || to >= n || from == to) {
        rebuild_timeline_tabs();
        return;
    }
    const std::size_t cur = clamped_active(*project_);
    auto rotate_all = [&](const std::size_t a, const std::size_t b) {
        if (a == b) return;
        const auto lo = static_cast<std::ptrdiff_t>(std::min(a, b));
        const auto mid = static_cast<std::ptrdiff_t>(a < b ? a + 1 : a);
        const auto hi = static_cast<std::ptrdiff_t>(std::max(a, b) + 1);
        std::rotate(project_->timelines.begin() + lo, project_->timelines.begin() + mid,
                    project_->timelines.begin() + hi);
        if (undo_stacks_.size() == n)
            std::rotate(undo_stacks_.begin() + lo, undo_stacks_.begin() + mid,
                        undo_stacks_.begin() + hi);
        if (timeline_playheads_.size() == n)
            std::rotate(timeline_playheads_.begin() + lo, timeline_playheads_.begin() + mid,
                        timeline_playheads_.begin() + hi);
    };
    rotate_all(from, to);
    std::size_t next = cur;
    if (cur == from)
        next = to;
    else if (from < cur && cur <= to)
        next = cur - 1;
    else if (to <= cur && cur < from)
        next = cur + 1;
    has_unsaved_changes_ = true;
    activate_timeline(next, controller_.current_frame() >= 0 ? controller_.current_frame() : 0);
}

}  // namespace canvas::gui
