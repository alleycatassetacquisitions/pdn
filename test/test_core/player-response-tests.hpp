#pragma once

#include <gtest/gtest.h>
#include "game/quickdraw-requests.hpp"

inline void playerResponseParsesValidPayload() {
    PlayerResponse response;
    const std::string json =
        R"({"data":{"id":"0010","name":"TestPlayer","hunter":true,"allegiance":2,"faction":"Guild"}})";

    ASSERT_TRUE(response.parseFromJson(json));
    EXPECT_EQ(response.id, "0010");
    EXPECT_EQ(response.name, "TestPlayer");
    EXPECT_TRUE(response.isHunter);
    EXPECT_EQ(response.allegiance, 2);
    EXPECT_EQ(response.faction, "Guild");
    EXPECT_TRUE(response.errors.empty());
}

inline void playerResponseRejectsMalformedJson() {
    PlayerResponse response;
    ASSERT_FALSE(response.parseFromJson("{not json"));
}

inline void playerResponseRejectsErrorsArray() {
    PlayerResponse response;
    const std::string json = R"({"errors":["Player not found"]})";
    ASSERT_FALSE(response.parseFromJson(json));
    ASSERT_EQ(response.errors.size(), 1u);
    EXPECT_EQ(response.errors[0], "Player not found");
}
