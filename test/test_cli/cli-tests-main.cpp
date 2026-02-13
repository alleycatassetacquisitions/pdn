//
// CLI Test Entry Point
// GoogleTest discovers TEST_F macros across all .cpp files in this directory.
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>

int main(int argc, char **argv) {
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
