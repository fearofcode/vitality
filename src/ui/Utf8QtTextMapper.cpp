#include "ui/Utf8QtTextMapper.h"

namespace vitality {

QString utf8_to_qstring(const std::string_view utf8_text) {
    return QString::fromUtf8(
        utf8_text.data(),  // NOLINT(bugprone-suspicious-stringview-data-usage)
        static_cast<qsizetype>(utf8_text.size()));
}

}  // namespace vitality
