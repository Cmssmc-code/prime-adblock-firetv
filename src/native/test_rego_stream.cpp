// test_rego_stream.cpp — unit tests for rego_stream.h (+ Layer 1 from hooks.cpp
// reproduced through rego_filter.h). Built for armeabi-v7a with the NDK and run
// on the Fire TV (adb push + shell), or with any host C++17 compiler.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rego_filter.h"
#include "rego_stream.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static const std::string kBody =
    "{\"description\":{\"adDeliverySessionId\":\"x_PBP_EXPL_1\",\"adMarkerId\":\"b4777783\"},"
    "\"playlist\":["
    "{\"id\":\"a1\",\"title\":\"Pub ] avec \\\"crochet\\\" }\",\"media\":{\"urls\":[\"https://a/1.mpd\"]}},"
    "{\"id\":\"a2\",\"title\":\"deux\",\"media\":{\"urls\":[\"https://a/2.mpd\",\"https://a/2b.mpd\"]}},"
    "{\"id\":\"a3\",\"title\":\"trois [x]\",\"media\":{\"urls\":[\"https://a/3.mpd\"]}}"
    "],\"measurement\":{\"k\":[1,2,{\"z\":\"]\"}]}}";

// Layer 1 as hooks.cpp runs it on a truncated chunk: blank the complete ad
// objects (with their trailing comma), then complete media.urls arrays.
static void layer1(char* buf, size_t n, size_t open) {
    size_t i = open + 1;
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (i < n) {
        while (i < n && ws(buf[i])) ++i;
        if (i >= n || buf[i] != '{') break;
        size_t cl = pvfilter::json_match_bracket(buf, n, i);
        if (cl == static_cast<size_t>(-1)) break;
        size_t j = cl + 1;
        while (j < n && ws(buf[j])) ++j;
        bool comma = j < n && buf[j] == ',';
        size_t e = comma ? j + 1 : cl + 1;
        for (size_t k = i; k < e; ++k) buf[k] = ' ';
        if (!comma) break;
        i = j + 1;
    }
    pvfilter::blank_complete_media_urls(buf, n, open);
}

// Expected final body: playlist interior all spaces, rest unchanged.
static std::string expected() {
    std::string e = kBody;
    size_t open = e.find("\"playlist\":[") + 11;
    size_t close = pvfilter::json_match_bracket(e.data(), e.size(), open);
    for (size_t i = open + 1; i < close; ++i) e[i] = ' ';
    return e;
}

// Deliver kBody in pieces of the given sizes. mode: 0 = buffer never moves,
// 1 = realloc before each follow-up chunk, 2 = new buffer + full copy (std::string growth).
static std::string run(const std::vector<size_t>& cuts, int mode, pvstream::Stream& st, int& done) {
    size_t open = kBody.find("\"playlist\":[") + 11;
    std::vector<std::vector<char>*> owned;
    auto* buf = new std::vector<char>(kBody.size() + 1, 0);
    owned.push_back(buf);
    char* base = buf->data();
    size_t len = 0, k = 0;
    done = 0;
    for (size_t c : cuts) {
        if (k > 0 && mode != 0) {
            auto* nb = new std::vector<char>(kBody.size() + 1, 0);
            owned.push_back(nb);
            std::memcpy(nb->data(), base, len);
            if (mode == 1) pvstream::on_realloc(st, base, nb->data());
            else pvstream::on_copy(st, nb->data(), base, len, true);
            base = nb->data();
        }
        std::memcpy(base + len, kBody.data() + len, c);
        if (k == 0) {
            layer1(base, c, open);
            pvstream::on_chunk1(st, base, c, open);
        } else if (pvstream::on_copy(st, base + len, kBody.data() + len, c, true) == pvstream::Event::Done) {
            ++done;
        }
        len += c;
        ++k;
    }
    std::string out(base, len);
    for (auto* v : owned) delete v;
    return out;
}

int main() {
    const std::string exp = expected();
    const size_t total = kBody.size();
    const size_t open = kBody.find("\"playlist\":[") + 11;
    const size_t close = pvfilter::json_match_bracket(kBody.data(), total, open);

    // Every cut point inside the array, 2 and 3 chunks, three buffer behaviours.
    int cases = 0;
    for (int mode = 0; mode < 3; ++mode) {
        for (size_t a = open + 1; a < close; ++a) {
            pvstream::Stream st; int done;
            std::string out = run({a, total - a}, mode, st, done);
            CHECK(out == exp); CHECK(done == 1); CHECK(!st.active);
            ++cases;
            size_t b = a + (close - a) / 2 + 1;
            if (b > a && b < total) {
                pvstream::Stream st3;
                out = run({a, b - a, total - b}, mode, st3, done);
                CHECK(out == exp); CHECK(done == 1);
                ++cases;
            }
        }
    }

    // A copy elsewhere (not base + len, not a full copy) is ignored.
    {
        std::string b = kBody.substr(0, open + 40);
        std::vector<char> v(b.begin(), b.end()); v.resize(total);
        pvstream::Stream st;
        pvstream::on_chunk1(st, v.data(), b.size(), open);
        CHECK(st.active);
        char other[64]; std::memcpy(other, "zzzz", 4);
        CHECK(pvstream::on_copy(st, other, "zzzz", 4, true) == pvstream::Event::None);
        CHECK(pvstream::on_copy(st, v.data() + 5, v.data(), 3, true) == pvstream::Event::None);
        CHECK(std::string(v.data(), b.size()) == b);           // untouched
        // free() of the buffer stops the stream for good.
        CHECK(pvstream::on_free(st, v.data()));
        CHECK(pvstream::on_copy(st, v.data() + b.size(), "x", 1, true) == pvstream::Event::None);
    }

    // Lost track (no follow-up seen): the chunk stays exactly Layer 1's valid output.
    {
        size_t a = open + 60;
        std::vector<char> v(kBody.begin(), kBody.begin() + a);
        std::vector<char> l1 = v;
        layer1(l1.data(), a, open);
        layer1(v.data(), a, open);
        pvstream::Stream st;
        pvstream::on_chunk1(st, v.data(), a, open);
        CHECK(v == l1);
    }

    // A chunk where the array already closes does not arm.
    {
        std::vector<char> v(kBody.begin(), kBody.end());
        pvstream::Stream st;
        pvstream::on_chunk1(st, v.data(), v.size(), open);
        CHECK(!st.active);
    }

    // Failed realloc (nullptr) keeps the old base.
    {
        std::vector<char> v(kBody.begin(), kBody.begin() + open + 30); v.resize(total);
        pvstream::Stream st;
        pvstream::on_chunk1(st, v.data(), open + 30, open);
        CHECK(pvstream::on_realloc(st, v.data(), nullptr) == pvstream::Event::None);
        CHECK(st.base == v.data());
    }

    std::printf("%s: %d split cases, %d failure(s)\n", g_fail ? "ECHEC" : "OK", cases, g_fail);
    return g_fail ? 1 : 0;
}
