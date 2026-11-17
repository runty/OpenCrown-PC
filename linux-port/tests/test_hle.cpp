// HLE pure-helper tests (no game data): aligned host allocation, direct-memory offset allocator, /app0 path mapping.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>


#include "hle/kernel/pure.h"
#include "hle/hle.h"
#include "loader/hle.h"
#include "loader/nid.h"
#include <chrono>
#include <thread>

using namespace dcp::hle;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Exercise the registered guest ABI, including the Darwin monotonic wait adapter.
static void test_runtime_threads() {
    dcp::hle::register_all({});
    dcp::loader::Binder binder;
    auto address = [&](const char* name) {
        return binder.resolve({dcp::loader::nid_from_name(name) + "#A#B", 0x12, 0, 0});
    };
    using Lock = DCP_SYSV int32_t (*)(void**);
    using Wait = DCP_SYSV int32_t (*)(void**, void**, uint32_t);
    using Destroy = DCP_SYSV int32_t (*)(void**);
    auto lock = reinterpret_cast<Lock>(address("scePthreadMutexLock"));
    auto unlock = reinterpret_cast<Lock>(address("scePthreadMutexUnlock"));
    auto wait = reinterpret_cast<Wait>(address("scePthreadCondTimedwait"));
    auto destroy_cond = reinterpret_cast<Destroy>(address("scePthreadCondDestroy"));
    auto destroy_mutex = reinterpret_cast<Destroy>(address("scePthreadMutexDestroy"));
    CHECK(binder.stats().stubs == 0);
    void* mutex = nullptr;
    void* condition = nullptr;
    CHECK(lock(&mutex) == 0);
    auto started = std::chrono::steady_clock::now();
    CHECK(uint32_t(wait(&condition, &mutex, 20000)) == 0x8002003c);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed >= std::chrono::milliseconds(10));
    CHECK(elapsed < std::chrono::seconds(5));
    CHECK(unlock(&mutex) == 0);
    CHECK(destroy_cond(&condition) == 0);
    CHECK(destroy_mutex(&mutex) == 0);

    struct TlsIndex { uint64_t module, offset; };
    using Tls = DCP_SYSV void* (*)(const TlsIndex*);
    auto tls = reinterpret_cast<Tls>(address("__tls_get_addr"));
    uint64_t initial = 123;
    TlsIndex index{dcp::hle::add_tls_module(&initial, sizeof initial, 32, 16), 0};
    uint64_t values[2]{};
    auto worker = [&](int slot) {
        auto* value = static_cast<uint64_t*>(tls(&index));
        if (!value || *value != initial) { values[slot] = 0; return; }
        *value = uint64_t(slot + 100);
        values[slot] = *static_cast<uint64_t*>(tls(&index));
    };
    std::thread first(worker, 0), second(worker, 1);
    first.join();
    second.join();
    CHECK(values[0] == 100 && values[1] == 101);
    CHECK(*static_cast<uint64_t*>(tls(&index)) == initial);
}

int main() {
    test_runtime_threads();
    // 2 MiB alignment as requested by the game's MapDirectMemory calls; memory is writable.
    for (uint64_t len : {uint64_t(0x4000), uint64_t(0x200000), uint64_t(0x1234000)}) {
        auto* p = static_cast<uint8_t*>(alloc_aligned(len, 0x200000));
        CHECK(p && (reinterpret_cast<uint64_t>(p) & 0x1FFFFF) == 0);
        if (p) {
            p[0] = 1, p[len - 1] = 2;
            CHECK(alloc_size(p) >= len && release_alloc(p) >= len && alloc_size(p) == 0);
        }
    }
    CHECK(alloc_aligned(0, 0x200000) == nullptr);
    for (uint64_t len : {uint64_t(0x4000), uint64_t(0x4200000)}) {  // direct memory: 40-bit GPU-visible addresses
        auto* p = static_cast<uint8_t*>(alloc_low(len, 0x200000));
        uint64_t a = reinterpret_cast<uint64_t>(p);
        CHECK(p && (a & 0x1FFFFF) == 0 && a + len <= (1ull << 40) && a >= 0x200000000ull);
        if (p) {
            p[len - 1] = 1;
            CHECK(release_alloc(p) >= len);
        }
    }

    uint64_t cur = 0;
    CHECK(dmem_alloc(&cur, 0, 0x1000000, 0x4000, 0x200000) == 0);
    CHECK(dmem_alloc(&cur, 0, 0x1000000, 0x4000, 0x200000) == 0x200000);
    CHECK(dmem_alloc(&cur, 0, 0x1000000, 0x10000, 0x4000) == 0x204000);
    CHECK(dmem_alloc(&cur, 0, 0x1000000, 0x1000000, 0x200000) == -1);  // does not fit
    CHECK(dmem_alloc(&cur, 0, 0x1000000, 0x4000, 0x3000) == -1);        // align not a power of two

    CHECK(map_app0("C:/dump", "/app0/eboot.bin") == "C:/dump/eboot.bin");
    CHECK(map_app0("C:/dump", "/app0//sce_sys/./param.sfo") == "C:/dump/sce_sys/param.sfo");
    CHECK(map_app0("C:/dump", "/app0") == "C:/dump");
    CHECK(!map_app0("C:/dump", "/app0/../secret"));
    CHECK(!map_app0("C:/dump", "/app0/a/..\\b"));
    CHECK(!map_app0("C:/dump", "/app0/c:/x"));
    CHECK(!map_app0("C:/dump", "/app0x/eboot.bin"));
    CHECK(!map_app0("C:/dump", "/savedata0/x"));

    // Save-data mount table: /savedataN -> host dir, writable paths only under a live mount.
    SaveMountTable mt;
    CHECK(!mt.resolve("/savedata0/SAVE0.DAT"));
    CHECK(mt.mount("SAVEDATA00", "R:/saves/SAVEDATA00") == "/savedata0");
    CHECK(mt.mount("SAVEDATA00", "R:/saves/SAVEDATA00").empty());  // already mounted
    CHECK(mt.mount("OTHER", "R:/saves/OTHER") == "/savedata1");
    CHECK(mt.resolve("/savedata0/SAVE0.DAT") == "R:/saves/SAVEDATA00/SAVE0.DAT");
    CHECK(mt.resolve("/savedata1/sce_sys/param.bin") == "R:/saves/OTHER/sce_sys/param.bin");
    CHECK(!mt.resolve("/savedata0/../SAVEDATA01/x"));
    CHECK(!mt.resolve("/savedata00/x"));
    CHECK(!mt.resolve("/savedata2/x"));
    CHECK(!mt.resolve("/app0/eboot.bin"));
    CHECK(mt.dir_name_of("/savedata1") && *mt.dir_name_of("/savedata1") == "OTHER");
    CHECK(mt.umount("/savedata0"));
    CHECK(!mt.umount("/savedata0"));
    CHECK(!mt.resolve("/savedata0/SAVE0.DAT"));
    CHECK(mt.mount("SAVEDATA00", "R:/x") == "/savedata0");  // freed slot is reused
    for (int i = 2; i < SaveMountTable::kMax; ++i) CHECK(!mt.mount("D" + std::to_string(i), "R:/d").empty());
    CHECK(mt.mount("FULL", "R:/d").empty());
    CHECK(mt.resolve("/savedata15/a") == "R:/d/a");

    CHECK(valid_savedata_dirname("SAVEDATA00") && valid_savedata_dirname("a-b_c"));
    CHECK(!valid_savedata_dirname("") && !valid_savedata_dirname("..") && !valid_savedata_dirname("a/b") &&
          !valid_savedata_dirname("C:") && !valid_savedata_dirname("0123456789012345678901234567890123"));
    CHECK(savedata_name_match(nullptr, "SAVEDATA00") && savedata_name_match("", "X"));
    CHECK(savedata_name_match("SAVEDATA00", "SAVEDATA00") && !savedata_name_match("SAVEDATA00", "SAVEDATA01"));
    CHECK(savedata_name_match("SAVE%", "SAVEDATA00") && savedata_name_match("%00", "SAVEDATA00"));
    CHECK(savedata_name_match("SAVEDATA0_", "SAVEDATA07") && !savedata_name_match("SAVEDATA0_", "SAVEDATA0"));

    // Input script.
    std::vector<InputEvent> ev;
    int bad = 0;
    CHECK(parse_input_script("# title\n300 CROSS\r\n120 OPTIONS  # comment\n130 0\n\n400 up+Circle\n500 0x4008\n", &ev, &bad));
    CHECK(ev.size() == 5 && ev[0].frame == 120 && ev[0].buttons == 0x8 && ev[2].buttons == 0x4000);
    CHECK(script_buttons_at(ev, 0) == 0 && script_buttons_at(ev, 120) == 0x8 && script_buttons_at(ev, 129) == 0x8);
    CHECK(script_buttons_at(ev, 130) == 0 && script_buttons_at(ev, 350) == 0x4000);
    CHECK(script_buttons_at(ev, 400) == 0x2010 && script_buttons_at(ev, 99999) == 0x4008);
    CHECK(!parse_input_script("10 CROSS\nabc CROSS\n", &ev, &bad) && bad == 2);
    CHECK(parse_input_script("0 RS_LEFT+RS_UP\n10 RS_RIGHT+RS_DOWN+CROSS\n", &ev, &bad));
    CHECK(script_buttons_at(ev, 0) == (kScriptRightStickLeft | kScriptRightStickUp));
    CHECK(script_buttons_at(ev, 10) == (kScriptRightStickRight | kScriptRightStickDown | 0x4000));
    CHECK(parse_input_script("10 MAP+SQUARE\n20 0\n30 TOUCHPAD\n", &ev, &bad));
    CHECK(!script_event_at(ev, 9));
    CHECK(script_event_at(ev, 10)->map_touch && script_buttons_at(ev, 10) == 0x108000);
    CHECK(!script_event_at(ev, 20)->map_touch && script_buttons_at(ev, 20) == 0);
    CHECK(!script_event_at(ev, 30)->map_touch && script_buttons_at(ev, 30) == 0x100000);
    CHECK(!parse_input_script("10 JUMP\n", &ev, &bad) && bad == 1);
    CHECK(!parse_input_script("10\n", &ev, &bad) && bad == 1);
    CHECK(parse_input_script("", &ev, &bad) && ev.empty());

    std::printf("hle: %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
