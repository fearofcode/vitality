#include "file/FilePath.h"

#include <utility>

namespace vitality {

FilePath FilePath::from_command_line_arg(const char *arg) {
    return FilePath(std::filesystem::path(arg));
}

const std::filesystem::path &FilePath::native_path() const {
    return path_;
}

std::string FilePath::display_name() const {
    const std::filesystem::path filename = path_.filename();
    if (!filename.empty()) {
        return filename.string();
    }

    return path_.string();
}

FilePath::FilePath(std::filesystem::path path)
    : path_(std::move(path)) {
}

}  // namespace vitality
