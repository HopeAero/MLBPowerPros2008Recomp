// See pgo_core.h. Built with the game (MLB_PGO_GEN) and by the standalone validation program.


#include "pgo_core.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zlib.h>

extern "C" {
void __gcov_dump(void);
void __gcov_reset(void);
}

namespace {

// GCC 16.1 libgcov (libgcc/libgcov.h): the head of struct gcov_info, layout read from a compiled
// object (.data..LPBX0): version "B61*" @0, next @8, stamp @0x10, checksum @0x14, filename @0x18; the
// merge-function table and the function list follow. __gcov_root.list is the head of libgcov's list.
struct GcovInfoHead {
    uint32_t version;
    uint32_t pad;
    GcovInfoHead* next;
    uint32_t stamp;
    uint32_t checksum;
    const char* filename;
};
static_assert(sizeof(void*) == 8, "layout is the 64-bit one");
static_assert(__builtin_offsetof(GcovInfoHead, filename) == 0x18, "gcov_info head layout");
constexpr uint32_t kGcov16Version = 0x4236312a;  // bytes "*16B" = "B61*" (GCC 16.1)

struct GcovRootHead {
    GcovInfoHead* list;
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
int g_dumps = 0;

bool SendAll(int fd, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size) {
        const ssize_t n = send(fd, p, size, 0);
        if (n <= 0) return false;
        p += n;
        size -= size_t(n);
    }
    return true;
}

}  // namespace

extern "C" GcovRootHead __gcov_root;

// Counters of another build in the files would be merged into this build's or rejected: the first run of
// a new instrumented NRO deletes the old *.gcda (build-id.txt holds the id of the build that wrote them).
extern "C" int MlbPgoNewBuildWipe(const char* dir, uint64_t id) {
    char idPath[768], text[32], old[32] = {};
    snprintf(idPath, sizeof idPath, "%s/build-id.txt", dir);
    snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(id));
    if (FILE* f = fopen(idPath, "rb")) {
        const size_t n = fread(old, 1, 16, f);
        fclose(f);
        if (n == 16 && memcmp(old, text, 16) == 0) return 0;
    }
    int removed = 0;
    if (DIR* d = opendir(dir)) {
        while (const dirent* e = readdir(d)) {
            const size_t n = strlen(e->d_name);
            if (n > 5 && strcmp(e->d_name + n - 5, ".gcda") == 0) {
                char path[768];
                snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
                if (remove(path) == 0) ++removed;
            }
        }
        closedir(d);
    }
    if (FILE* f = fopen(idPath, "wb")) {
        fwrite(text, 1, 16, f);
        fclose(f);
    }
    return removed;
}

extern "C" int MlbPgoSetProfileDir(const char* dir) {
    // Validate every node before touching any.
    int count = 0;
    uint64_t id = 1469598103934665603ull;  // FNV-1a over every TU's stamp+checksum: names this build
    for (GcovInfoHead* info = __gcov_root.list; info; info = info->next) {
        id = (id ^ info->stamp) * 1099511628211ull;
        id = (id ^ info->checksum) * 1099511628211ull;
        if (info->version != kGcov16Version || !info->filename) return -1;
        const char* base = info->filename;
        const intptr_t distance = reinterpret_cast<intptr_t>(base) - reinterpret_cast<intptr_t>(&__gcov_root);
        if (distance > (1 << 30) || distance < -(1 << 30)) return -1;  // not in this image
        if (++count > 1000000) return -1;
    }
    mkdir(dir, 0777);  // libgcov (this build) does not create directories
    MlbPgoNewBuildWipe(dir, id);
    const size_t dirLen = strlen(dir);
    for (GcovInfoHead* info = __gcov_root.list; info; info = info->next) {
        const char* name = info->filename;
        for (const char* p = name; *p; ++p) {
            if (*p == '\\' || *p == '/') name = p + 1;  // the mangled name has neither
        }
        char* fixed = static_cast<char*>(malloc(dirLen + 1 + strlen(name) + 1));
        if (!fixed) return -1;
        memcpy(fixed, dir, dirLen);
        fixed[dirLen] = '/';
        strcpy(fixed + dirLen + 1, name);
        info->filename = fixed;  // leaked on purpose: libgcov keeps the pointer
    }
    return count;
}

extern "C" int MlbPgoDumpAndReset(void) {
    pthread_mutex_lock(&g_lock);
    __gcov_dump();
    __gcov_reset();
    const int n = ++g_dumps;
    pthread_mutex_unlock(&g_lock);
    return n;
}

extern "C" uint32_t MlbPgoParseIpv4(const char* text) {
    uint32_t parts[4] = {};
    int part = 0, digits = 0;
    for (const char* p = text; p && *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            parts[part] = parts[part] * 10 + uint32_t(*p - '0');
            if (++digits > 3 || parts[part] > 255) return 0;
        } else if (*p == '.' && digits && part < 3) {
            ++part;
            digits = 0;
        } else if (*p == '\n' || *p == '\r' || *p == ' ') {
            break;
        } else {
            return 0;
        }
    }
    if (part != 3 || !digits) return 0;
    // network order == the bytes a.b.c.d in memory order
    const uint8_t bytes[4] = {uint8_t(parts[0]), uint8_t(parts[1]), uint8_t(parts[2]), uint8_t(parts[3])};
    uint32_t out;
    memcpy(&out, bytes, 4);
    return out;
}

// Wire format (little endian), one connection:
//   "MLBPGO1\n"
//   per file: u16 nameLen, name (no directory), u64 rawSize, then the zlib stream of the file in chunks
//             (u32 chunkLen, chunk bytes), ended by a u32 0 chunk
//   u16 0 ends the stream.
// Memory is bounded (two 32 KiB buffers + zlib's ~270 KB state): the game is using most of the heap.
extern "C" int MlbPgoStream(uint32_t ipv4_be, uint16_t port, const char* dir) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ipv4_be;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    constexpr size_t kBuf = 32768;
    unsigned char* in = static_cast<unsigned char*>(malloc(kBuf));
    unsigned char* out = static_cast<unsigned char*>(malloc(kBuf));
    int files = 0;
    bool ok = in && out && SendAll(fd, "MLBPGO1\n", 8);
    DIR* d = ok ? opendir(dir) : nullptr;
    while (d && ok) {
        const dirent* e = readdir(d);
        if (!e) break;
        const size_t nameLen = strlen(e->d_name);
        if (nameLen < 6 || strcmp(e->d_name + nameLen - 5, ".gcda") != 0 || nameLen > 0xFFFF) continue;
        char path[768];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        const uint16_t nl = uint16_t(nameLen);
        const uint64_t rawSize = size > 0 ? uint64_t(size) : 0;
        ok = SendAll(fd, &nl, 2) && SendAll(fd, e->d_name, nameLen) && SendAll(fd, &rawSize, 8);
        z_stream z{};
        if (ok && deflateInit(&z, 1) != Z_OK) ok = false;
        bool finished = false;
        while (ok && !finished) {
            const size_t got = fread(in, 1, kBuf, f);
            z.next_in = in;
            z.avail_in = uInt(got);
            const int flush = got < kBuf ? Z_FINISH : Z_NO_FLUSH;
            do {
                z.next_out = out;
                z.avail_out = uInt(kBuf);
                const int rc = deflate(&z, flush);
                if (rc == Z_STREAM_ERROR) { ok = false; break; }
                const uint32_t produced = uint32_t(kBuf - z.avail_out);
                if (produced && !(SendAll(fd, &produced, 4) && SendAll(fd, out, produced))) { ok = false; break; }
                if (rc == Z_STREAM_END) finished = true;
            } while (z.avail_out == 0 && !finished);
            if (got < kBuf && !finished && ok) ok = false;  // Z_FINISH must end the stream
        }
        if (ok) {
            deflateEnd(&z);
            const uint32_t zero = 0;
            ok = SendAll(fd, &zero, 4);
            if (ok) ++files;
        }
        fclose(f);
    }
    if (d) closedir(d);
    const uint16_t end = 0;
    ok = ok && SendAll(fd, &end, 2);
    close(fd);
    free(in);
    free(out);
    return ok ? files : -1;
}
