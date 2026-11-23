// Host-side tests for the scanner used after inflate() produces output.
#include <cstdio>
#include <string>

#include "inflate_stream.h"

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
    ++failures; \
} } while (0)

static const std::string body =
    "{\"description\":{\"adDeliverySessionId\":\"session\"},"
    "\"playlist\":["
    "{\"title\":\"escaped \\\" quote and ] bracket\",\"media\":{\"urls\":[\"a\",\"b\"]}},"
    "{\"title\":\"second\"}"
    "],\"measurement\":{\"count\":2}}";

static std::string expected_body() {
    std::string expected = body;
    const size_t open = expected.find(pvinflstream::kPlaylist) +
                        pvinflstream::kPlaylistLength - 1;
    pvstream::Scan scan;
    const size_t interior = pvstream::scan_blank(expected.data() + open + 1,
                                                 expected.size() - open - 1,
                                                 scan, true);
    CHECK(interior < expected.size() - open - 1);
    return expected;
}

static void feed(pvinflstream::State& state, std::string& chunk,
                 bool& complete, size_t& blanked) {
    if (complete) return; // the hook releases its entry at the closing ']'
    const pvinflstream::Result result =
        pvinflstream::on_chunk(state, chunk.data(), chunk.size());
    blanked += result.blanked;
    complete = result.complete;
}

int main() {
    const std::string expected = expected_body();
    const size_t open = body.find(pvinflstream::kPlaylist) +
                        pvinflstream::kPlaylistLength - 1;
    const size_t close = body.find("],\"measurement\"", open);
    const size_t interior = close - open - 1;
    int cases = 0;

    // Every possible two-chunk boundary, including inside the marker,
    // an escaped JSON string, nested arrays and the closing bracket.
    for (size_t cut = 0; cut <= body.size(); ++cut) {
        std::string first = body.substr(0, cut);
        std::string second = body.substr(cut);
        pvinflstream::State state;
        bool complete = false;
        size_t blanked = 0;
        feed(state, first, complete, blanked);
        feed(state, second, complete, blanked);
        CHECK(first + second == expected);
        CHECK(complete);
        CHECK(blanked == interior);
        ++cases;
    }

    // The same response delivered one byte at a time.
    {
        pvinflstream::State state;
        bool complete = false;
        size_t blanked = 0;
        std::string output;
        for (char byte : body) {
            std::string chunk(1, byte);
            feed(state, chunk, complete, blanked);
            output += chunk;
        }
        CHECK(output == expected);
        CHECK(complete);
        CHECK(blanked == interior);
        ++cases;
    }

    // An unrelated response remains unchanged; an unfinished array stays open.
    {
        std::string unrelated = "{\"playlist2\":[1],\"other\":2}";
        pvinflstream::State state;
        const auto result = pvinflstream::on_chunk(state, unrelated.data(), unrelated.size());
        CHECK(unrelated == "{\"playlist2\":[1],\"other\":2}");
        CHECK(!result.scanned_array);
        CHECK(!result.complete);
        ++cases;
    }
    {
        std::string prefix = body.substr(0, close);
        pvinflstream::State state;
        const auto result = pvinflstream::on_chunk(state, prefix.data(), prefix.size());
        CHECK(result.scanned_array);
        CHECK(!result.complete);
        CHECK(state.blanking);
        ++cases;
    }

    std::printf("inflate stream: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
