#pragma once

#include <gtest/gtest.h>
#include <functional>
#include <vector>

#include "utils/callback-list.hpp"

// Two subscribers on one list: the case a single std::function slot gets wrong.
inline void callbackListKeepsEachOwnersCallback() {
    CallbackList<std::function<void(int)>> list;
    int first = 0;
    int second = 0;
    std::vector<int> order;

    list.set(
    [&](int value) {
        first = value;
        order.push_back(1);
    },
    &first);
    list.set(
    [&](int value) {
        second = value;
        order.push_back(2);
    },
    &second);
    list.fire(7);
    EXPECT_EQ(first, 7);
    EXPECT_EQ(second, 7) << "the second subscriber displaced the first, or was never called";
    EXPECT_EQ(order, (std::vector<int>{1, 2}));

    // One owner leaving must not empty the other's slot.
    list.set(nullptr, &first);
    list.fire(9);
    EXPECT_EQ(first, 7);
    EXPECT_EQ(second, 9);

    // Re-registering replaces that owner's callback rather than adding one.
    int replaced = 0;
    list.set([&](int value) { replaced = value; }, &second);
    list.fire(11);
    EXPECT_EQ(second, 9);
    EXPECT_EQ(replaced, 11);

    list.set(nullptr, &second);
    list.fire(12);
    EXPECT_EQ(replaced, 11) << "a cleared owner was still called";
}

// Clearing from inside a fire: the running callback survives, a subscriber
// cleared mid-fire is skipped, the rest still hear the event.
inline void callbackListToleratesClearingDuringFire() {
    CallbackList<std::function<void()>> list;
    int ownerA = 0;
    int ownerB = 0;
    int ownerC = 0;
    int callsA = 0;
    int callsB = 0;
    int callsC = 0;

    list.set(
    [&]() {
        callsA++;
        list.set(nullptr, &ownerA);
        list.set(nullptr, &ownerB);
    },
    &ownerA);
    list.set([&]() { callsB++; }, &ownerB);
    list.set([&]() { callsC++; }, &ownerC);

    list.fire();
    EXPECT_EQ(callsA, 1);
    EXPECT_EQ(callsB, 0) << "a subscriber cleared earlier in the same fire was still called";
    EXPECT_EQ(callsC, 1);

    list.fire();
    EXPECT_EQ(callsA, 1);
    EXPECT_EQ(callsB, 0);
    EXPECT_EQ(callsC, 2);
}

// A subscriber added from inside a handler hears the next event, not this one.
inline void callbackListDefersCallbacksAddedDuringFire() {
    CallbackList<std::function<void()>> list;
    int ownerA = 0;
    int ownerB = 0;
    int callsA = 0;
    int callsB = 0;

    list.set(
    [&]() {
        if (callsA++ == 0) list.set([&]() { callsB++; }, &ownerB);
    },
    &ownerA);
    list.fire();
    EXPECT_EQ(callsB, 0);
    list.fire();
    EXPECT_EQ(callsB, 1);
}
