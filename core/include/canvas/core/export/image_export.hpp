#pragma once

#include "canvas/core/export/exporter.hpp"

#include <string>

namespace canvas::core {

struct Project;

bool export_image_frames(const Project& project, const ExportSettings& settings,
                         ExportControl* control, std::string* error = nullptr);

}
