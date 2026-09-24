#pragma once

#include "canvas/core/export/exporter.hpp"
#include "canvas/core/project/project.hpp"

#include <string>
#include <vector>

namespace canvas::core {

bool export_parallel_chunks(const Project& project, const ExportSettings& base, int chunks,
                            ExportControl* control, std::string* error);

bool concat_chunk_files(const std::vector<std::string>& inputs, const std::string& output,
                        const std::string& format, std::string* error);

}
