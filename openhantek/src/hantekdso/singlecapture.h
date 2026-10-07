// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

// A capture belongs to the arm that existed BEFORE sampling started.
// Settings refreshes and in-flight captures from a previous arm cannot satisfy it.
class SingleCapture {
public:
    std::uint64_t arm() { return ++generation; }
    std::uint64_t current() const { return generation.load(); }
    bool accepts(std::uint64_t capturedGeneration) const {
        return capturedGeneration != 0 && capturedGeneration == generation.load();
    }
private:
    std::atomic<std::uint64_t> generation{0};
};
