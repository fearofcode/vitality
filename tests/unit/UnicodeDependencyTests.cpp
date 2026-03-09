#include <hb.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("HarfBuzz basic buffer APIs are available") {
    hb_buffer_t *buffer = hb_buffer_create();
    REQUIRE(buffer != nullptr);

    constexpr char text[] = "hello";
    hb_buffer_add_utf8(buffer, text, 5, 0, 5);
    hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
    hb_buffer_set_language(buffer, hb_language_from_string("en", -1));

    CHECK(hb_buffer_get_length(buffer) == 5);
    CHECK(hb_version_atleast(1, 0, 0));

    hb_buffer_destroy(buffer);
}
