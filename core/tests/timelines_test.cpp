#include "canvas/core/project/project.hpp"
#include "canvas/core/timeline/edit_ops.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace canvas::core;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

const char* kOutDir = "/tmp/canvas_timelines";

Project make_two_timeline_project() {
    Project p;
    p.name = "Tabs";
    Sequence first;
    first.name = "Timeline 1";
    first.fps = 30.0;
    Track v1;
    v1.kind = Track::Kind::Video;
    v1.name = "V1";
    first.video_tracks.push_back(std::move(v1));
    Clip a;
    a.media = -1;
    a.name = "A";
    a.tl_in = 0;
    a.src_in = 0;
    a.src_out = 30;
    place_clip(first, Track::Kind::Video, 0, a, Placement::Overwrite);
    Sequence second;
    second.name = "Cutdown";
    second.fps = 25.0;
    Track w1;
    w1.kind = Track::Kind::Video;
    w1.name = "V1";
    second.video_tracks.push_back(std::move(w1));
    Track au;
    au.kind = Track::Kind::Audio;
    au.name = "A1";
    second.audio_tracks.push_back(std::move(au));
    (void)second.toggle_bookmark(10, "Beat");
    p.timelines.push_back(std::move(first));
    p.timelines.push_back(std::move(second));
    p.active_timeline = 1;
    uint64_t max_id = 0;
    for (const auto& tl : p.timelines)
        for (const auto* tracks : {&tl.video_tracks, &tl.audio_tracks})
            for (const auto& track : *tracks)
                for (const auto& clip : track.clips) max_id = std::max(max_id, clip.id);
    for (auto& tl : p.timelines) tl.next_clip_id = std::max(tl.next_clip_id, max_id + 1);
    return p;
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

}  // namespace

int main() {
    std::filesystem::remove_all(kOutDir);
    std::filesystem::create_directories(kOutDir);

    const std::string path = std::string(kOutDir) + "/two.ncs";
    Project p = make_two_timeline_project();
    std::string error;
    check(save_project(p, path, &error), "save two-timeline project");
    if (!error.empty()) std::printf("      error: %s\n", error.c_str());

    Project loaded;
    check(load_project(loaded, path, &error), "load two-timeline project");
    check(loaded.timelines.size() == 2, "two timelines survive round-trip");
    if (loaded.timelines.size() == 2) {
        check(loaded.timelines[0].name == "Timeline 1", "first timeline name survives");
        check(loaded.timelines[1].name == "Cutdown", "second timeline name survives");
        check(loaded.timelines[0].video_tracks.size() == 1 &&
                  loaded.timelines[0].video_tracks[0].clips.size() == 1,
              "first timeline clip survives");
        check(loaded.timelines[1].fps == 25.0, "second timeline fps survives");
        check(loaded.timelines[1].audio_tracks.size() == 1, "second timeline audio track survives");
        check(loaded.timelines[1].bookmarks.size() == 1 &&
                  loaded.timelines[1].bookmarks[0].label == "Beat",
              "second timeline bookmark survives");
        check(loaded.timelines[0].video_tracks[0].clips[0].id != 0, "clip id survives");
    }
    check(loaded.active_timeline == 1, "active timeline index survives");
    check(loaded.active_sequence().name == "Cutdown", "active_sequence follows index");

    UndoStack undo;
    Clip extra;
    extra.media = -1;
    extra.name = "B";
    extra.tl_in = 0;
    extra.src_in = 0;
    extra.src_out = 10;
    auto cmd = place_clip(loaded.active_sequence(), Track::Kind::Video, 0, extra,
                          Placement::AppendAtEnd);
    check(cmd != nullptr, "edit targets the active timeline");
    if (cmd) undo.record(std::move(cmd));
    check(loaded.timelines[1].video_tracks[0].clips.size() == 1, "append lands on active");
    check(loaded.timelines[0].video_tracks[0].clips.size() == 1, "edit on active leaves other intact");
    check(undo.undo(loaded.active_sequence()), "per-timeline undo works");
    check(loaded.timelines[1].video_tracks[0].clips.empty(), "undo removes the append");

    const std::string legacy_path = std::string(kOutDir) + "/legacy.ehproj";
    write_text(legacy_path,
               "{\"event_horizon_project\": 3, \"name\": \"Old\", \"fps\": 24.0, "
               "\"video_tracks\": [{\"name\": \"V1\", \"clips\": [{\"id\": 7, \"media\": -1, "
               "\"name\": \"X\", \"tl_in\": 0, \"tl_out\": 12, \"src_in\": 0, \"src_out\": 12}]}], "
               "\"audio_tracks\": [], \"bookmarks\": [], \"media\": [], \"bins\": []}");
    Project legacy;
    check(load_project(legacy, legacy_path, &error), "legacy flat file loads");
    check(legacy.timelines.size() == 1, "legacy flat file becomes one timeline");
    if (!legacy.timelines.empty()) {
        check(legacy.timelines[0].name == "Timeline 1", "legacy timeline gets default name");
        check(legacy.timelines[0].fps == 24.0, "legacy fps survives");
        check(legacy.timelines[0].video_tracks.size() == 1 &&
                  legacy.timelines[0].video_tracks[0].clips.size() == 1 &&
                  legacy.timelines[0].video_tracks[0].clips[0].id == 7,
              "legacy clip survives");
        check(legacy.timelines[0].next_clip_id >= 8, "legacy counter bumped past max id");
    }
    check(legacy.active_timeline == 0, "legacy active index is zero");

    const std::string clamp_path = std::string(kOutDir) + "/clamp.ncs";
    Project q = make_two_timeline_project();
    q.active_timeline = 9;
    check(save_project(q, clamp_path, &error), "save out-of-range active index");
    Project clamped;
    check(load_project(clamped, clamp_path, &error), "load out-of-range active index");
    check(clamped.active_timeline == 0, "out-of-range active index clamps to zero");

    Project empty;
    check(empty.active_sequence().name == "Timeline 1", "empty project heals one timeline");
    check(empty.timelines.size() == 1, "healed project holds one timeline");

    if (failures == 0) {
        std::printf("ALL TIMELINE TAB TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
