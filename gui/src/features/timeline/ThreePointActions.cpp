#include "UX/MainWindow.hpp"

#include "Widgets/media_pool_widget.hpp"
#include "core/timecode.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QStatusBar>
#include <QWidget>

#include <algorithm>
#include <string>
#include <utility>

#include "canvas/core/timeline/three_point.hpp"

namespace canvas::gui {

bool MainWindow::source_monitor_focused() const {
    const QWidget* focus = QApplication::focusWidget();
    if (!focus || !source_panel_) return false;
    return focus == source_panel_ || source_panel_->isAncestorOf(focus);
}

void MainWindow::mark_in() {
    if (source_monitor_focused() && src_preview_.has_media()) {
        src_mark_in_ = std::max<int64_t>(0, src_preview_.current_frame());
    } else {
        tl_mark_in_ = std::max<int64_t>(0, current_frame_);
        if (timeline_) timeline_->set_marks(tl_mark_in_, tl_mark_out_);
    }
    update_mark_status();
}

void MainWindow::mark_out() {
    if (source_monitor_focused() && src_preview_.has_media()) {
        src_mark_out_ = std::max<int64_t>(0, src_preview_.current_frame());
    } else {
        tl_mark_out_ = std::max<int64_t>(0, current_frame_);
        if (timeline_) timeline_->set_marks(tl_mark_in_, tl_mark_out_);
    }
    update_mark_status();
}

void MainWindow::clear_in_out() {
    src_mark_in_ = -1;
    src_mark_out_ = -1;
    tl_mark_in_ = -1;
    tl_mark_out_ = -1;
    if (timeline_) timeline_->set_marks(-1, -1);
    update_mark_status();
}

void MainWindow::update_mark_status() {
    if (!status_) return;
    const double seq_fps = project_ && project_->sequence.fps > 0.0 ? project_->sequence.fps : 30.0;
    const double src_fps =
        src_preview_.has_media() && src_preview_.fps() > 0.0 ? src_preview_.fps() : seq_fps;
    const auto dash = QStringLiteral("—");
    QStringList parts;
    if (src_preview_.has_media()) {
        parts << tr("Source %1 → %2")
                     .arg(src_mark_in_ >= 0 ? timecode(src_mark_in_, src_fps) : dash)
                     .arg(src_mark_out_ >= 0 ? timecode(src_mark_out_, src_fps) : dash);
    }
    parts << tr("Timeline IN %1%2")
                 .arg(tl_mark_in_ >= 0 ? timecode(tl_mark_in_, seq_fps) : dash)
                 .arg(tl_mark_out_ >= 0 ? tr(" OUT %1").arg(timecode(tl_mark_out_, seq_fps))
                                        : QString());
    parts << tr("Playhead %1").arg(timecode(current_frame_, seq_fps));
    status_->showMessage(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::create_range_from_marks() {
    if (!project_) return;
    const int64_t tl_dur = project_->sequence.duration_frames();
    int64_t in = tl_mark_in_;
    int64_t out = tl_mark_out_;
    if (in >= 0 && in > out) std::swap(in, out);
    if (in < 0 || out <= in) {
        if (status_)
            status_->showMessage(
                tr("Set a timeline in/out range first (I and O on the timeline)."));
        return;
    }
    if (tl_dur > 0) {
        if (in >= tl_dur) in = tl_dur - 1;
        if (out > tl_dur) out = tl_dur;
        if (out <= in) out = in + 1;
    }

    const std::string fallback =
        "Range " + std::to_string(project_->sequence.bookmarks.size() + 1);
    bool ok = false;
    const QString entered = QInputDialog::getText(
        this, tr("Create Range"), tr("Name:"), QLineEdit::Normal,
        QString::fromStdString(fallback), &ok);
    if (!ok) return;
    const std::string label =
        entered.trimmed().isEmpty() ? fallback : entered.trimmed().toStdString();

    const uint64_t id = project_->sequence.add_range(in, out, label);
    if (id == 0) return;
    has_unsaved_changes_ = true;
    refresh_timeline();
    push_snapshot();
    if (status_)
        status_->showMessage(tr("Range %1 created: %2 → %3")
                                 .arg(QString::fromStdString(label))
                                 .arg(timecode(in, project_->sequence.fps > 0.0
                                                       ? project_->sequence.fps
                                                       : 30.0))
                                 .arg(timecode(out, project_->sequence.fps > 0.0
                                                        ? project_->sequence.fps
                                                        : 30.0)));
}

bool MainWindow::three_point_place(const canvas::core::Placement mode) {
    if (!project_) return false;

    const canvas::core::MediaEntry* found = nullptr;
    bool from_source = false;
    if (src_preview_.has_media()) {
        const auto it = std::find_if(project_->media.begin(), project_->media.end(),
                                     [this](const canvas::core::MediaEntry& m) {
                                         return m.path == src_preview_.media_path();
                                     });
        if (it != project_->media.end()) {
            found = &*it;
            from_source = true;
        }
    }
    if (!found && media_pool_ && media_pool_->currentRow() >= 0) {
        const QVariant v = media_pool_->currentItem()->data(kPoolMediaIndexRole);
        if (v.isValid()) {
            const auto idx = static_cast<std::size_t>(v.toLongLong());
            if (idx < project_->media.size()) found = &project_->media[idx];
        }
    }
    if (!found) {
        if (status_)
            status_->showMessage(
                tr("Nothing to place: open a source in the monitor or select media in the pool."));
        return false;
    }

    const double seq_fps = project_->sequence.fps > 0.0 ? project_->sequence.fps : 30.0;
    const double media_fps =
        found->fps > 0.0
            ? found->fps
            : (src_preview_.has_media() && src_preview_.fps() > 0.0 ? src_preview_.fps() : seq_fps);
    const int64_t media_frames = found->total_frames > 0
                                     ? found->total_frames
                                     : (from_source ? src_preview_.total_frames() : 0);

    const auto marks = canvas::core::three_point::resolve(canvas::core::three_point::ResolveInput{
        .src_in = from_source ? src_mark_in_ : -1,
        .src_out = from_source ? src_mark_out_ : -1,
        .tl_in = tl_mark_in_,
        .tl_out = tl_mark_out_,
        .playhead = current_frame_,
        .media_frames = media_frames,
        .seq_fps = seq_fps,
        .media_fps = media_fps,
    });

    if (!place_media_at(found->id, marks.tl_in, mode, std::nullopt, marks.src_in, marks.src_out))
        return false;

    const QString base = QFileInfo(QString::fromStdString(found->path)).completeBaseName();
    QString verb = tr("Place");
    switch (mode) {
    case canvas::core::Placement::Insert:
        verb = tr("Insert");
        break;
    case canvas::core::Placement::Overwrite:
        verb = tr("Overwrite");
        break;
    case canvas::core::Placement::AppendAtEnd:
        verb = tr("Append");
        break;
    case canvas::core::Placement::PlaceOnTop:
        verb = tr("Place on top");
        break;
    }
    if (status_)
        status_->showMessage(tr("%1: %2 %3 → %4 at %5")
                                 .arg(verb)
                                 .arg(base)
                                 .arg(timecode(marks.src_in, media_fps))
                                 .arg(timecode(marks.src_out, media_fps))
                                 .arg(timecode(marks.tl_in, seq_fps)));
    return true;
}

}
