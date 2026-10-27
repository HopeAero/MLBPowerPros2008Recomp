// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
// the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The shader machinery follows SwitchWakerHD (https://github.com/centollOS/SwitchWakerHD) at df8fbde:
// runtime/src/gfx/deko/shaders_dk.cpp (one uam worker with a large stack at priority 0x3B, the queue
// order, the per-frame load budget, skipped draws until a shader is ready, the local cache appended
// as shaders compile, the failure dumps), by way of SwitchWaker (switch/deko/shaders.cpp at 56c0758).
// Modified for MLB Power Pros 2008 Recomp (tools/switch/deko, 2026-10-26): one cache file on the SD
// card (no offline cache next to the NRO), modules keyed by the XXH3 of their WGSL and translated on
// the uam worker itself (Tint and uam on one thread), fixed shaders compiled from GLSL at start,
// WIICOMPILED_DK_* settings, the runtime's thread placement.
// The deko3d renderer's shaders at run time (dk.h):
//   - the registry: every DKSH by (stage, GLSL hash), filled from dksh_cache.bin at start and by the
//     uam worker; GX modules map the hash of their WGSL to their two stages;
//   - the uam worker: one thread (uam is not thread-safe, and Tint runs there too), 8 MiB of stack,
//     priority 0x3B on core 2 (WIICOMPILED_DK_COMPILE_CORE); jobs come from Aurora's pipeline
//     compile threads (aurora/dk_pipeline.cpp: one per new module, background) and from the encoder
//     (a draw that waits: foreground), foreground first, then the module that skipped most draws;
//   - dksh_cache.bin: every DKSH and module compiled here, appended as a record with its CRC
//     (dksh_file.h), read back at the next start, replaced as a whole after a compiler change;
//   - the load budget: the render thread copies at most WIICOMPILED_DK_SHADER_BUDGET compiled
//     shaders a frame into code memory (default 64; 0 = no limit); a draw whose shader is not loaded
//     yet is skipped (a brief pop-in) and counted, unless Aurora's policy says to wait.
#include "dk.h"

#include <pthread.h>
#include <sys/stat.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "dksh_file.h"
#include "uam_api.h"

#ifndef MLB_DK_CACHE_DIR
#define MLB_DK_CACHE_DIR "sdmc:/switch/mlbpowerpros2008/UserData/Cache"
#endif

namespace swdk {
// (defined below with the switches it follows)
void rewrite_module(std::string* wgsl, uint32_t* fast, uint32_t* unit);
namespace {

constexpr const char* kCacheDir = MLB_DK_CACHE_DIR;
constexpr const char* kCachePath = MLB_DK_CACHE_DIR "/dksh_cache.bin";
constexpr size_t kWorkerStack = 8u << 20;  // Mesa's GLSL parser, nv50_ir and Tint recurse deeply
constexpr int kFailDumps = 16;

int stage_index(ShaderStage s) { return s == ShaderStage::Vertex ? 0 : 1; }
const char* stage_name(ShaderStage s) { return s == ShaderStage::Vertex ? "vertex" : "fragment"; }

// ---- the registry (g_mutex)
std::mutex g_mutex;
std::unordered_map<uint64_t, std::unique_ptr<ShaderEntry>> g_entries[2];
std::unordered_map<uint64_t, std::unique_ptr<ShaderModule>> g_modules;

// ---- the worker (g_jobMutex)
struct Job {
    ShaderModule* module = nullptr;  // a module to translate and compile
    bool dumpOnly = false;           // (or only to write its WGSL and GLSL: WIICOMPILED_DK_DUMP_MODULES)
    WgslSource source;
    ShaderEntry* entry = nullptr;  // or one stage's GLSL (the fixed shaders)
    std::string glsl;
    uint32_t skipped() const {
        return module ? module->skippedDraws.load(std::memory_order_relaxed) : 0u;
    }
};
std::mutex g_jobMutex;
std::condition_variable g_jobCv;   // a job queued
std::condition_variable g_doneCv;  // a job finished
std::deque<Job> g_foreground, g_background;
bool g_workerUp = false;
pthread_t g_workerThread{};
uint64_t g_compilerId = 0;

// the session's figures
uint64_t g_compiled = 0, g_failed = 0, g_compileNs = 0, g_compileMaxNs = 0, g_pending = 0;  // g_jobMutex
uint64_t g_translateFailures = 0, g_modulesCached = 0;                                     // g_jobMutex
std::atomic<uint64_t> g_loaded{0}, g_skipped{0};
int g_failDumps = 0;

// ---- the load budget (render thread)
uint32_t g_budget = 64;
uint32_t g_loadsThisFrame = 0;

// ---- the cache file (worker thread, or init before it starts)
FILE* g_cacheFile = nullptr;

void mkdirs(const char* path) {
    std::string p = path;
    for (size_t i = 1; i < p.size(); i++) {
        if (p[i] != '/') continue;
        if (i > 0 && p[i - 1] == ':') continue;  // "sdmc:/"
        mkdir(p.substr(0, i).c_str(), 0777);
    }
    mkdir(p.c_str(), 0777);
}

void cache_append(const std::vector<uint8_t>& bytes) {
    if (!g_cacheFile) return;
    if (fwrite(bytes.data(), 1, bytes.size(), g_cacheFile) != bytes.size()) {
        dklog("shaders: cannot append to %s; the cache stops here for this session", kCachePath);
        fclose(g_cacheFile);
        g_cacheFile = nullptr;
        return;
    }
    fflush(g_cacheFile);
}

void append_shader(const ShaderEntry& e, const std::vector<uint8_t>& dksh) {
    std::vector<uint8_t> bytes;
    DkshShaderRecord r;
    r.stage = e.stage;
    r.glslHash = e.glslHash;
    r.dksh = dksh;
    append_record(&bytes, r);
    cache_append(bytes);
}

void append_module(uint64_t key, uint64_t vh, uint64_t fh) {
    std::vector<uint8_t> bytes;
    append_record(&bytes, DkshModuleRecord{key, vh, fh});
    cache_append(bytes);
}

void dump_failure(uint64_t hash, const char* tag, const std::string& source, const std::string& log) {
    if (g_failDumps >= kFailDumps) return;
    g_failDumps++;
    char path[256];
    snprintf(path, sizeof path, "%s/shaderfail_%016llx_%s.txt", kCacheDir, (unsigned long long)hash, tag);
    if (FILE* f = fopen(path, "wb")) {
        fwrite(source.data(), 1, source.size(), f);
        fprintf(f, "\n/* %s\n*/\n", log.c_str());
        fclose(f);
    }
}

// the next job: foreground first, then the module whose draws were skipped most, then background
bool take_job(Job* out) {
    for (std::deque<Job>* q : {&g_foreground, &g_background}) {
        if (q->empty()) continue;
        auto best = q->begin();
        if (q == &g_background) {
            for (auto it = q->begin(); it != q->end(); ++it)
                if (it->skipped() > best->skipped()) best = it;
        }
        *out = std::move(*best);
        q->erase(best);
        return true;
    }
    return false;
}

// Compiles a Queued entry (worker thread); false if uam refused it.
bool compile_entry(ShaderEntry* e, const std::string& glsl) {
    const uint64_t c0 = now_ns();
    uam::Result res =
        uam::compile(e->stage == ShaderStage::Vertex ? uam::Stage::Vertex : uam::Stage::Fragment, glsl.c_str());
    const uint64_t ns = now_ns() - c0;
    const bool ok = res.ok && !res.dksh.empty();
    if (ok) {
        append_shader(*e, res.dksh);
    } else {
        dklog("shaders: uam refused %s shader %016llx (%.0f ms): %s", stage_name(e->stage),
              (unsigned long long)e->glslHash, double(ns) / 1e6, res.log.substr(0, 400).c_str());
        dump_failure(e->glslHash, e->stage == ShaderStage::Vertex ? "vs.glsl" : "fs.glsl", glsl, res.log);
        append_shader(*e, {});  // (an empty record: do not retry with this compiler)
    }
    std::lock_guard<std::mutex> lock(g_jobMutex);
    if (ok) {
        e->dksh = std::move(res.dksh);
        g_compiled++;
    } else {
        g_failed++;
    }
    g_compileNs += ns;
    g_compileMaxNs = std::max(g_compileMaxNs, ns);
    e->state.store(ok ? ShaderState::Compiled : ShaderState::Failed, std::memory_order_release);
    return ok;
}

// One stage of a module (worker thread): its entry, compiled now if this job is the first to need it.
ShaderEntry* module_stage(ShaderStage stage, const std::string& glsl) {
    ShaderEntry* e = shader_entry(stage, glsl_hash(glsl));
    ShaderState expected = ShaderState::Missing;
    if (e->state.compare_exchange_strong(expected, ShaderState::Queued, std::memory_order_acq_rel)) {
        compile_entry(e, glsl);
    }
    return e;
}

void write_text(const char* path, const std::string& text) {
    if (FILE* f = fopen(path, "wb")) {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
}

// WIICOMPILED_DK_DUMP_MODULES: a module's WGSL as Aurora generated it, and its two stages' GLSL as
// translated now (with WIICOMPILED_DK_TEV_FAST as set), next to the cache as
// mod_<key>.wgsl / mod_<key>_{vs,fs}.glsl. Nothing is compiled or recorded.
void run_dump_job(Job& job) {
    const uint64_t key = job.module->key;
    std::string wgsl = job.source ? job.source() : std::string();
    if (wgsl.empty()) return;
    char path[256];
    snprintf(path, sizeof path, "%s/mod_%016llx.wgsl", kCacheDir, (unsigned long long)key);
    write_text(path, wgsl);
    uint32_t rewritten = 0, unitSites = 0;
    rewrite_module(&wgsl, &rewritten, &unitSites);
    const TranslateResult tv = wgsl_to_glsl(wgsl, "vs_main", ShaderStage::Vertex, BindingScheme::GX);
    const TranslateResult tf = wgsl_to_glsl(wgsl, "fs_main", ShaderStage::Fragment, BindingScheme::GX);
    for (int st = 0; st < 2; st++) {
        const TranslateResult& r = st == 0 ? tv : tf;
        snprintf(path, sizeof path, "%s/mod_%016llx_%s.glsl", kCacheDir, (unsigned long long)key, st == 0 ? "vs" : "fs");
        write_text(path, r.ok ? r.glsl : "/* translation failed: " + r.error + " */\n");
    }
    dklog("shaders: module %016llx dumped to %s/mod_%016llx{.wgsl,_vs.glsl,_fs.glsl} (WIICOMPILED_DK_DUMP_MODULES; "
          "TEV fast rewrite: %u sites, TEV unit rewrite: %u overflows left out; vs %016llx fs %016llx)",
          (unsigned long long)key, kCacheDir, (unsigned long long)key, rewritten, unitSites,
          (unsigned long long)(tv.ok ? glsl_hash(tv.glsl) : 0), (unsigned long long)(tf.ok ? glsl_hash(tf.glsl) : 0));
}

void run_module_job(Job& job) {
    ShaderModule* m = job.module;
    const uint64_t t0 = now_ns();
    std::string wgsl = job.source ? job.source() : std::string();
    std::string original;  // (the WGSL as generated, when the TEV rewrite changed it)
    // the TEV byte conversions without the conversion unit, the TEV overflow left out where its operand
    // is in [0, 1] (shader_translate.h; same pixels)
    if (!wgsl.empty() && (tev_fast_enabled() || tev_unit_mode())) {
        original = wgsl;
        uint32_t sites = 0, unitSites = 0;
        rewrite_module(&wgsl, &sites, &unitSites);
        if (!sites && !unitSites) original.clear();
        static uint64_t rewritten = 0, unchanged = 0, unitModules = 0, unitTotal = 0;
        std::lock_guard<std::mutex> lock(g_jobMutex);
        (sites ? rewritten : unchanged)++;
        if (unitSites) unitModules++, unitTotal += unitSites;
        if (tev_fast_enabled() && !sites && unchanged <= 4)
            dklog("shaders: module %016llx: the TEV helpers are not as gx/shader.cpp writes them; translated as they "
                  "are (WIICOMPILED_DK_TEV_FAST; %llu rewritten, %llu unchanged so far)",
                  (unsigned long long)m->key, (unsigned long long)rewritten, (unsigned long long)unchanged);
        if (unitSites && (unitModules <= 8 || unitModules % 64 == 0))
            dklog("shaders: module %016llx: %u TEV overflows left out (WIICOMPILED_DK_TEV_UNIT; %llu modules, %llu "
                  "overflows so far)",
                  (unsigned long long)m->key, unitSites, (unsigned long long)unitModules,
                  (unsigned long long)unitTotal);
    }
    TranslateResult tv, tf;
    if (!wgsl.empty()) {
        tv = wgsl_to_glsl(wgsl, "vs_main", ShaderStage::Vertex, BindingScheme::GX);
        tf = wgsl_to_glsl(wgsl, "fs_main", ShaderStage::Fragment, BindingScheme::GX);
    }
    if (!original.empty() && (!tv.ok || !tf.ok)) {
        // (Tint refused the rewritten text: the module as generated, rather than no module)
        dklog("shaders: module %016llx: Tint refused the TEV rewrites (%s); translated as generated",
              (unsigned long long)m->key, (!tv.ok ? tv.error : tf.error).substr(0, 300).c_str());
        wgsl = std::move(original);
        tv = wgsl_to_glsl(wgsl, "vs_main", ShaderStage::Vertex, BindingScheme::GX);
        tf = wgsl_to_glsl(wgsl, "fs_main", ShaderStage::Fragment, BindingScheme::GX);
    }
    if (wgsl.empty() || !tv.ok || !tf.ok) {
        const std::string why = wgsl.empty() ? std::string("no WGSL") : (!tv.ok ? tv.error : tf.error);
        {
            std::lock_guard<std::mutex> lock(g_jobMutex);
            g_translateFailures++;
            if (g_translateFailures <= 20)
                dklog("shaders: module %016llx does not translate: %s", (unsigned long long)m->key,
                      why.substr(0, 300).c_str());
        }
        if (!wgsl.empty()) dump_failure(m->key, "module.wgsl", wgsl, why);
        append_module(m->key, 0, 0);
        m->state.store(ModuleState::Failed, std::memory_order_release);
        return;
    }
    const uint64_t t1 = now_ns();
    // WIICOMPILED_DK_DUMP_GLSL=n: the GLSL of the session's first n translated modules on the SD card
    // (glsl_<module>_{vs,fs}.glsl next to the cache), to check what uam compiles
    static const long dumpGlsl = env_long("WIICOMPILED_DK_DUMP_GLSL", 0);
    static long dumped = 0;
    if (dumped < dumpGlsl) {
        dumped++;
        for (int st = 0; st < 2; st++) {
            char path[256];
            snprintf(path, sizeof path, "%s/glsl_%016llx_%s.glsl", kCacheDir, (unsigned long long)m->key,
                     st == 0 ? "vs" : "fs");
            const std::string& text = st == 0 ? tv.glsl : tf.glsl;
            if (FILE* f = fopen(path, "wb")) {
                fwrite(text.data(), 1, text.size(), f);
                fclose(f);
            }
        }
        dklog("shaders: module %016llx GLSL written to %s/glsl_%016llx_{vs,fs}.glsl (WIICOMPILED_DK_DUMP_GLSL)",
              (unsigned long long)m->key, kCacheDir, (unsigned long long)m->key);
    }
    ShaderEntry* vs = module_stage(ShaderStage::Vertex, tv.glsl);
    ShaderEntry* fs = module_stage(ShaderStage::Fragment, tf.glsl);
    m->vs.store(vs, std::memory_order_release);
    m->fs.store(fs, std::memory_order_release);
    append_module(m->key, vs->glslHash, fs->glslHash);
    m->state.store(ModuleState::Resolved, std::memory_order_release);
    static int logged = 0;
    if (logged++ < 32) {
        dklog("shaders: module %016llx compiled: Tint %.1f ms, uam %.0f ms (%u draws skipped meanwhile); std140 "
              "scalar arrays rewritten vs %u fs %u",
              (unsigned long long)m->key, double(t1 - t0) / 1e6, double(now_ns() - t1) / 1e6,
              m->skippedDraws.load(std::memory_order_relaxed), tv.std140Rewrites, tf.std140Rewrites);
    }
}

void* worker_main(void*) {
    pin_current_thread(int(env_long("WIICOMPILED_DK_COMPILE_CORE", 2)), kMaxThreadPriority);
    const uint64_t t0 = now_ns();
    uam::init(true);
    translate_init();
    static const char kTest[] = "#version 460\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const uam::Result test = uam::compile(uam::Stage::Vertex, kTest);
    dklog("shaders: uam worker started (priority 0x%X, %zu MiB stack); self-test %s (%zu bytes) in %.1f ms",
          kMaxThreadPriority, kWorkerStack >> 20, test.ok ? "compiled" : "FAILED", test.dksh.size(),
          double(now_ns() - t0) / 1e6);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(g_jobMutex);
            g_jobCv.wait(lock, [] { return !g_foreground.empty() || !g_background.empty(); });
            take_job(&job);
        }
        if (job.module && job.dumpOnly) {
            run_dump_job(job);
        } else if (job.module) {
            run_module_job(job);
        } else if (job.entry) {
            compile_entry(job.entry, job.glsl);
        }
        {
            std::lock_guard<std::mutex> lock(g_jobMutex);
            g_pending--;
        }
        g_doneCv.notify_all();
    }
    return nullptr;
}

void start_worker() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWorkerStack);
    const int rc = pthread_create(&g_workerThread, &attr, worker_main, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        dklog("shaders: cannot start the uam worker (%d): shaders the cache lacks will never draw", rc);
        return;
    }
    pthread_detach(g_workerThread);
    g_workerUp = true;
}

void queue_job(Job&& job, bool foreground) {
    std::lock_guard<std::mutex> lock(g_jobMutex);
    (foreground ? g_foreground : g_background).push_back(std::move(job));
    g_pending++;
    g_jobCv.notify_one();
}

// dksh_cache.bin: its records into the registry; a file of another compiler is started again
void read_cache(uint32_t gxConfigVersion) {
    mkdirs(kCacheDir);
    const uint64_t expected = dksh_compiler_id(gxConfigVersion);
    const uint64_t t0 = now_ns();
    std::vector<uint8_t> data;
    if (FILE* f = fopen(kCachePath, "rb")) {
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize(size > 0 ? size_t(size) : 0);
        if (size <= 0 || fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
    }
    DkshFile file;
    std::string error;
    const bool readable = !data.empty() && read_dksh_file(data, &file, &error);
    if (readable && file.compilerId == expected) {
        const DkshFileStats stats = dksh_file_stats(file, expected);
        size_t loaded = 0, deferred = 0, modules = 0;
        for (DkshShaderRecord& r : file.shaders) {
            ShaderEntry* e = shader_entry(r.stage, r.glslHash);
            if (e->state.load() != ShaderState::Missing) continue;
            if (r.dksh.empty()) {
                e->state.store(ShaderState::Failed);
                continue;
            }
            if (code_load(e->shader, r.dksh.data(), uint32_t(r.dksh.size()), "cached DKSH")) {
                e->state.store(ShaderState::Ready);
                loaded++;
            } else {
                e->dksh = std::move(r.dksh);  // (code memory full: loaded on use, if ever room)
                e->state.store(ShaderState::Compiled);
                deferred++;
            }
        }
        for (const DkshModuleRecord& r : file.modules) {
            ShaderModule* m = shader_module(r.moduleHash);
            if (r.vertexGlslHash == 0 || r.fragmentGlslHash == 0) {
                m->state.store(ModuleState::Failed);
                continue;
            }
            ShaderEntry* vs = shader_entry(ShaderStage::Vertex, r.vertexGlslHash);
            ShaderEntry* fs = shader_entry(ShaderStage::Fragment, r.fragmentGlslHash);
            if (vs->state.load() == ShaderState::Missing || fs->state.load() == ShaderState::Missing) continue;
            m->vs.store(vs);
            m->fs.store(fs);
            m->state.store(ModuleState::Resolved);
            modules++;
        }
        g_loaded = loaded;
        g_modulesCached = modules;
        dklog("shaders: %s: %.2f MiB in %.0f ms; %s; %zu loaded into code memory (%u KiB), %zu left for later "
              "(code memory full), %zu modules",
              kCachePath, double(data.size()) / 1048576.0, double(now_ns() - t0) / 1e6,
              dksh_file_summary(stats).c_str(), loaded, code_used() >> 10, deferred, modules);
        g_cacheFile = fopen(kCachePath, "ab");
    } else {
        dklog("shaders: %s %s: started again", kCachePath,
              data.empty() ? "is missing or empty"
              : readable   ? "is from another compiler (uam, Tint options, translation or GX config version)"
                           : ("is unreadable (" + error + ")").c_str());
        g_cacheFile = fopen(kCachePath, "wb");
        if (g_cacheFile) cache_append(dksh_file_header(expected));
    }
    if (!g_cacheFile) dklog("shaders: cannot write %s: compiled shaders are not kept for the next session", kCachePath);
}

}  // namespace

ShaderEntry* shader_entry(ShaderStage stage, uint64_t glslHash) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& slot = g_entries[stage_index(stage)][glslHash];
    if (!slot) {
        slot = std::make_unique<ShaderEntry>();
        slot->stage = stage;
        slot->glslHash = glslHash;
    }
    return slot.get();
}

ShaderModule* shader_module(uint64_t wgslHash) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& slot = g_modules[wgslHash];
    if (!slot) {
        slot = std::make_unique<ShaderModule>();
        slot->key = wgslHash;
    }
    return slot.get();
}

void module_compile(ShaderModule* m, WgslSource source, bool foreground) {
    if (!g_workerUp) return;
    ModuleState expected = ModuleState::Unresolved;
    if (source && m->state.compare_exchange_strong(expected, ModuleState::Queued, std::memory_order_acq_rel)) {
        Job job;
        job.module = m;
        job.source = std::move(source);
        queue_job(std::move(job), foreground);
        return;
    }
    if (!foreground || m->state.load(std::memory_order_acquire) != ModuleState::Queued) return;
    // already queued as background: a draw needs it now
    std::lock_guard<std::mutex> lock(g_jobMutex);
    for (auto it = g_background.begin(); it != g_background.end(); ++it) {
        if (it->module == m && !it->dumpOnly) {
            g_foreground.push_back(std::move(*it));
            g_background.erase(it);
            break;
        }
    }
}

bool tev_fast_enabled() {
    static const bool on = [] {
        const bool v = env_flag("WIICOMPILED_DK_TEV_FAST", true);
        dklog("shaders: TEV byte conversions %s (WIICOMPILED_DK_TEV_FAST; same pixels, GX modules keyed %s)",
              v ? "without the conversion unit" : "as generated", v ? "with the tev_fast seed" : "by their plain XXH3");
        return v;
    }();
    return on;
}

uint32_t tev_unit_mode() {
    static const uint32_t mode = [] {
        const long v = env_long("WIICOMPILED_DK_TEV_UNIT", 2);
        const uint32_t m = v <= 0 ? 0u : v == 1 ? 1u : 2u;
        dklog("shaders: TEV overflow %s (WIICOMPILED_DK_TEV_UNIT=%u; same pixels)",
              m == 2 ? "left out for registers, konst colours and textures known in [0, 1]"
              : m == 1 ? "left out for registers and konst colours known in [0, 1]"
                       : "as generated",
              m);
        return m;
    }();
    return mode;
}

uint64_t module_key_seed() {
    const uint32_t unit = tev_unit_mode();
    return (tev_fast_enabled() ? kTevFastSeed : 0) ^ (unit == 2 ? kTevUnitSeed : unit == 1 ? kTevUnitRegsSeed : 0);
}

// the WGSL rewrites of the variant in use (tev_fast_rewrite, tev_unit_rewrite): sites rewritten by each
void rewrite_module(std::string* wgsl, uint32_t* fast, uint32_t* unit) {
    *fast = tev_fast_enabled() ? tev_fast_rewrite(wgsl) : 0;
    *unit = tev_unit_mode() ? tev_unit_rewrite(wgsl, tev_unit_mode() == 2) : 0;
}

bool module_dump_wanted(uint64_t key) {
    // WIICOMPILED_DK_DUMP_MODULES=<hex key>[,<hex key>...] (the "module" of the draw-times lines), or "all"
    static const std::vector<uint64_t> keys = [] {
        std::vector<uint64_t> v;
        const char* s = getenv("WIICOMPILED_DK_DUMP_MODULES");
        if (!s || !*s) return v;
        if (strcmp(s, "all") == 0) return std::vector<uint64_t>{0};
        while (*s) {
            char* end = nullptr;
            const unsigned long long k = strtoull(s, &end, 16);
            if (end == s) break;
            v.push_back(uint64_t(k));
            s = end;
            while (*s == ',' || *s == ' ') s++;
        }
        dklog("shaders: %zu modules to dump when their pipelines are made (WIICOMPILED_DK_DUMP_MODULES)", v.size());
        return v;
    }();
    for (uint64_t k : keys)
        if (k == 0 || k == key) return true;
    return false;
}

void module_dump(ShaderModule* m, WgslSource source) {
    if (!g_workerUp || !source) return;
    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        static std::unordered_map<uint64_t, bool> dumped;
        if (dumped[m->key] || dumped.size() > 256) return;
        dumped[m->key] = true;
    }
    Job job;
    job.module = m;
    job.dumpOnly = true;
    job.source = std::move(source);
    queue_job(std::move(job), false);
}

bool module_wait(ShaderModule* m) {
    const auto queued = [m] {
        if (m->state.load(std::memory_order_acquire) == ModuleState::Queued) return true;
        for (ShaderEntry* e : {m->vs.load(std::memory_order_acquire), m->fs.load(std::memory_order_acquire)})
            if (e && e->state.load(std::memory_order_acquire) == ShaderState::Queued) return true;
        return false;
    };
    if (queued()) {
        module_compile(m, {}, true);
        std::unique_lock<std::mutex> lock(g_jobMutex);
        g_doneCv.wait(lock, [&] { return !queued(); });
    }
    if (m->state.load(std::memory_order_acquire) != ModuleState::Resolved) return false;
    return m->vs.load()->state.load() != ShaderState::Failed && m->fs.load()->state.load() != ShaderState::Failed;
}

bool shader_usable(ShaderEntry* e, bool force) {
    const ShaderState s = e->state.load(std::memory_order_acquire);
    if (s == ShaderState::Ready) return true;
    if (s == ShaderState::Compiled && (force || g_budget == 0 || g_loadsThisFrame < g_budget)) {
        std::vector<uint8_t> dksh;
        {
            std::lock_guard<std::mutex> lock(g_jobMutex);
            dksh.swap(e->dksh);
        }
        g_loadsThisFrame++;
        char name[48];
        snprintf(name, sizeof name, "%s %016llx", stage_name(e->stage), (unsigned long long)e->glslHash);
        if (!dksh.empty() && code_load(e->shader, dksh.data(), uint32_t(dksh.size()), name)) {
            e->state.store(ShaderState::Ready, std::memory_order_release);
            g_loaded++;
            return true;
        }
        // (code memory full, or invalid: the draws that need it stay skipped)
        e->state.store(ShaderState::Failed, std::memory_order_release);
    }
    return false;
}

bool module_usable(ShaderModule* m, bool force) {
    if (m->state.load(std::memory_order_acquire) == ModuleState::Resolved) {
        ShaderEntry* vs = m->vs.load(std::memory_order_acquire);
        ShaderEntry* fs = m->fs.load(std::memory_order_acquire);
        const bool v = shader_usable(vs, force);
        const bool f = shader_usable(fs, force);
        if (v && f) return true;
    }
    m->skippedDraws.fetch_add(1, std::memory_order_relaxed);
    g_skipped++;
    return false;
}

const DkShader* fixed_shader(ShaderStage stage, const char* glsl, const char* name) {
    ShaderEntry* e = shader_entry(stage, glsl_hash(glsl));
    ShaderState expected = ShaderState::Missing;
    if (e->state.compare_exchange_strong(expected, ShaderState::Queued, std::memory_order_acq_rel)) {
        if (!g_workerUp) {
            e->state.store(ShaderState::Failed);
        } else {
            Job job;
            job.entry = e;
            job.glsl = glsl;
            queue_job(std::move(job), true);
        }
    }
    {
        std::unique_lock<std::mutex> lock(g_jobMutex);
        g_doneCv.wait(lock, [e] { return e->state.load(std::memory_order_acquire) != ShaderState::Queued; });
    }
    if (e->state.load() == ShaderState::Compiled) {
        std::vector<uint8_t> dksh;
        {
            std::lock_guard<std::mutex> lock(g_jobMutex);
            dksh.swap(e->dksh);
        }
        e->state.store(!dksh.empty() && code_load(e->shader, dksh.data(), uint32_t(dksh.size()), name)
                           ? ShaderState::Ready
                           : ShaderState::Failed);
    }
    if (e->state.load() != ShaderState::Ready) {
        dklog("shaders: the renderer's own shader %s did not compile", name);
        return nullptr;
    }
    return &e->shader;
}

void shaders_frame_start() { g_loadsThisFrame = 0; }

void shaders_init(uint32_t gxConfigVersion) {
    g_compilerId = dksh_compiler_id(gxConfigVersion);
    g_budget = uint32_t(std::max(0L, env_long("WIICOMPILED_DK_SHADER_BUDGET", 64)));
    read_cache(gxConfigVersion);
    start_worker();
    dklog("shaders: %llu loaded; misses translated and compiled by the uam worker and appended to %s; load budget "
          "%u per frame (WIICOMPILED_DK_SHADER_BUDGET, 0 = no limit)",
          (unsigned long long)g_loaded.load(), kCachePath, g_budget);
}

ShaderTotals shader_totals() {
    ShaderTotals t;
    std::lock_guard<std::mutex> lock(g_jobMutex);
    t.loaded = g_loaded.load();
    t.pending = g_pending;
    t.failed = g_failed;
    t.compiled = g_compiled;
    t.compileNs = g_compileNs;
    t.compileMaxNs = g_compileMaxNs;
    t.skippedDraws = g_skipped.load();
    t.modulesCached = g_modulesCached;
    t.translateFailures = g_translateFailures;
    {
        std::lock_guard<std::mutex> registry(g_mutex);
        t.modules = g_modules.size();
    }
    return t;
}

int shaders_report(char* out, size_t size) {
    static ShaderTotals last;
    const ShaderTotals t = shader_totals();
    const uint64_t compiled = t.compiled - last.compiled;
    const uint64_t ns = t.compileNs - last.compileNs;
    const int n = snprintf(out, size,
                           "shaders: %llu loaded, %llu jobs pending, %llu failed (+%llu modules Tint refused); "
                           "%llu modules (%llu from the cache); window: %llu compiled (mean %.0f ms, session max "
                           "%.0f ms), %llu draws skipped; code %u KiB of %u",
                           (unsigned long long)t.loaded, (unsigned long long)t.pending, (unsigned long long)t.failed,
                           (unsigned long long)t.translateFailures, (unsigned long long)t.modules,
                           (unsigned long long)t.modulesCached, (unsigned long long)compiled,
                           compiled ? double(ns) / double(compiled) / 1e6 : 0.0, double(t.compileMaxNs) / 1e6,
                           (unsigned long long)(t.skippedDraws - last.skippedDraws), code_used() >> 10,
                           kCodeSize >> 10);
    last = t;
    return n > 0 ? n : 0;
}

}  // namespace swdk
