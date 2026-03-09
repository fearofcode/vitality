#pragma once

#include <string_view>

#include <QString>

namespace vitality {

[[nodiscard]] QString utf8_to_qstring(std::string_view utf8_text);

}  // namespace vitality
