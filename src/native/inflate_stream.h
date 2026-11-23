// inflate_stream.h — allocation-free playlist scanner for inflate output.
// The caller decides which z_stream belongs to an ad response and owns its
// state; this header only scans and edits the output chunks in place.
#pragma once

#include <cstddef>

#include "rego_stream.h"

namespace pvinflstream {

constexpr char kPlaylist[] = "\"playlist\":[";
constexpr size_t kPlaylistLength = sizeof(kPlaylist) - 1;

struct State {
    size_t match = 0;
    bool blanking = false;
    pvstream::Scan scan;
};

struct Result {
    size_t blanked = 0;
    bool scanned_array = false;
    bool complete = false;
};

// Blanks the array interior, preserving its closing ']' and all later bytes.
// State carries both a partial marker and JSON string/bracket state between
// chunks, including a backslash at the very end of a chunk.
inline Result on_chunk(State& state, char* bytes, size_t length) {
    Result result;
    size_t position = 0;
    if (!state.blanking) {
        for (; position < length; ++position) {
            if (bytes[position] == kPlaylist[state.match]) {
                if (++state.match == kPlaylistLength) {
                    state.blanking = true;
                    state.scan = pvstream::Scan{};
                    ++position; // leave the opening '[' intact
                    break;
                }
            } else {
                state.match = bytes[position] == kPlaylist[0] ? 1 : 0;
            }
        }
    }
    if (state.blanking) {
        result.scanned_array = true;
        result.blanked = pvstream::scan_blank(bytes + position, length - position,
                                              state.scan, true);
        result.complete = result.blanked < length - position;
    }
    return result;
}

} // namespace pvinflstream
