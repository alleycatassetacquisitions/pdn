//
// Created by Elli Furedy on 8/28/2024.
//
//
// Created by Elli Furedy on 8/27/2024.
//
#include <unity.h>

bool success = false;

void setUp(void) {
    success = true;
}

void tearDown(void) {
    // clean stuff up here
}

void testFunction(void) {
    TEST_ASSERT_TRUE(success);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(testFunction);
    UNITY_END();
}