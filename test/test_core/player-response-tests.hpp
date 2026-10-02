#pragma once

#include <gtest/gtest.h>
#include "alleycat-server/player-response.hpp"

inline void playerResponseParsesValidPayload() {
    const char* json = R"({"data":{"id":"42","name":"Test","hunter":true,"allegiance":2,"faction":"Guild"}})";
    PlayerResponse response;
    ASSERT_TRUE(response.parseFromJson(json));
    EXPECT_EQ(response.id, "42");
    EXPECT_EQ(response.name, "Test");
    EXPECT_TRUE(response.isHunter);
    EXPECT_EQ(response.allegiance, 2);
    EXPECT_EQ(response.faction, "Guild");
}

inline void playerResponseRejectsErrorsArray() {
    const char* json = R"({"errors":["not found"],"data":{"id":"1","name":"x","hunter":false,"allegiance":0,"faction":""}})";
    PlayerResponse response;
    EXPECT_FALSE(response.parseFromJson(json));
    EXPECT_EQ(response.errors.size(), 1u);
}

inline void playerResponseRejectsMissingData() {
    PlayerResponse response;
    EXPECT_FALSE(response.parseFromJson(R"({"errors":[]})"));
}
