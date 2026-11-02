#pragma once

// Ours, not retail: the Benchmark run's state (world/benchmark.hxx).

#include <chrono>
#include <string>
#include <vector>

struct benchmark_t
{
    bool active = false;
    bool finishing = false;              // this level's end is ours: anything else that leaves the race is a Quit
    std::size_t index = 0;
    std::vector< std::string > tracks;
    value_t game_config, boat_config;    // the player's, put back at the end
    std::chrono::steady_clock::time_point window, last;
    int window_frames = 0;
    bool timing = false;
    std::chrono::steady_clock::time_point cut;   // the last camera change
    bool cut_set = false;
    std::vector< float > frames;         // this level's frame times, seconds
};
