#include <iostream>
#include <utility>

#include <QApplication>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"
#include "ui/EditorScrollArea.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    vitality::TextBuffer buffer = vitality::TextBuffer::make_empty();

    if (argc > 1) {
        const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(argv[1]);
        auto load_result = vitality::TextBuffer::load_from_path(file_path);
        if (load_result.success) {
            buffer = std::move(load_result.buffer);
        } else {
            std::cerr << "Failed to open file: " << file_path.display_name() << '\n';
        }
    }

    vitality::EditorScrollArea editor(std::move(buffer));
    editor.resize(900, 700);
    editor.show();

    return app.exec();
}
