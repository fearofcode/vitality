#pragma once

#include <filesystem>
#include <string>

namespace vitality {

class FilePath {
public:
    [[nodiscard]] static FilePath from_command_line_arg(const char *arg);

    [[nodiscard]] const std::filesystem::path &native_path() const;
    [[nodiscard]] std::string display_name() const;

private:
    explicit FilePath(std::filesystem::path path);

    std::filesystem::path path_;
};

}  // namespace vitality
