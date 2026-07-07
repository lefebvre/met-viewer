#pragma once

#include <filesystem>
#include <memory>

#include "viewer/readers/ireader.h"

namespace met::readers {

// Opens a file by probing all registered format readers and using the
// highest-confidence match. Throws ReadError if no reader claims the file or the
// winning reader fails to open it.
[[nodiscard]] std::unique_ptr<IDataset> openDataset(const std::filesystem::path& path);

}  // namespace met::readers
