// Unit test for the MPEG4Source PCM BITS_PER_SAMPLE fix.
//
// What it proves (fail-before / pass-after) on the exact constructor logic:
//   OLD: missing BITS_PER_SAMPLE -> CHECK fires -> SIGABRT (process abort).
//   NEW: missing BITS_PER_SAMPLE -> graceful mIsPcm=false, no abort.
//   NEW with well-formed PCM input behaves identically to OLD.
//
// The stubs below mirror the AOSP signatures used by the block under test:
//   AMediaFormat_getInt32, SampleTable::getMaxSampleSize, CHECK (abort).
// Compile: g++ -std=c++17 -o test_pcm_fix test_pcm_fix.cpp && ./test_pcm_fix

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ---- minimal stubs mirroring AOSP ----
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s\n", #x); abort(); } } while (0)

typedef int32_t status_t;
const status_t OK = 0;

struct StubFormat {
    bool has_bits_per_sample = false;
    bool has_channel_count = false;
    int32_t bits_per_sample = 0;
    int32_t channel_count = 0;
    int32_t max_input_size = -1;  // -1 = never set
};

static bool AMediaFormat_getInt32(StubFormat* f, const char* key, int32_t* out) {
    if (!strcmp(key, "bits-per-sample") && f->has_bits_per_sample) { *out = f->bits_per_sample; return true; }
    if (!strcmp(key, "channel-count") && f->has_channel_count) { *out = f->channel_count; return true; }
    return false;
}
static void AMediaFormat_setInt32(StubFormat* f, const char* key, int32_t v) {
    if (!strcmp(key, "max-input-size")) f->max_input_size = v;
}

struct StubSampleTable {
    size_t max_sample_size = 0;
    status_t getMaxSampleSize(size_t* out) { *out = max_sample_size; return OK; }
};

static const int32_t kMaxPcmFrameSize = 2048;

// ---- OLD constructor logic, verbatim shape ----
static void old_pcm_block(StubFormat* mFormat, StubSampleTable* mSampleTable, bool* mIsPcm) {
    if (*mIsPcm) {
        int32_t numChannels = 0;
        int32_t bitsPerSample = 0;
        CHECK(AMediaFormat_getInt32(mFormat, "bits-per-sample", &bitsPerSample));
        CHECK(AMediaFormat_getInt32(mFormat, "channel-count", &numChannels));

        int32_t bytesPerSample = bitsPerSample >> 3;
        int32_t pcmSampleSize = bytesPerSample * numChannels;

        size_t maxSampleSize;
        status_t err = mSampleTable->getMaxSampleSize(&maxSampleSize);
        if (err != OK || maxSampleSize != static_cast<size_t>(pcmSampleSize)
               || bitsPerSample != 16) {
            // Not supported
            *mIsPcm = false;
        } else {
            AMediaFormat_setInt32(mFormat, "max-input-size", pcmSampleSize * kMaxPcmFrameSize);
        }
    }
}

// ---- NEW constructor logic, verbatim shape (the fix) ----
static void new_pcm_block(StubFormat* mFormat, StubSampleTable* mSampleTable, bool* mIsPcm) {
    if (*mIsPcm) {
        int32_t numChannels = 0;
        int32_t bitsPerSample = 0;
        // An enca track can resolve to raw audio via frma without BITS_PER_SAMPLE
        // ever being set; treat missing keys as unsupported instead of aborting.
        const bool hasPcmInfo =
                AMediaFormat_getInt32(mFormat, "bits-per-sample", &bitsPerSample)
                && AMediaFormat_getInt32(mFormat, "channel-count", &numChannels);

        int32_t bytesPerSample = bitsPerSample >> 3;
        int32_t pcmSampleSize = bytesPerSample * numChannels;

        size_t maxSampleSize;
        status_t err = mSampleTable->getMaxSampleSize(&maxSampleSize);
        if (!hasPcmInfo || err != OK || maxSampleSize != static_cast<size_t>(pcmSampleSize)
               || bitsPerSample != 16) {
            // Not supported
            *mIsPcm = false;
        } else {
            AMediaFormat_setInt32(mFormat, "max-input-size", pcmSampleSize * kMaxPcmFrameSize);
        }
    }
}

static int failures = 0;
#define EXPECT(cond, msg) do { \
    if (cond) { printf("PASS: %s\n", msg); } \
    else { printf("FAIL: %s\n", msg); failures++; } } while (0)

// Run fn in a child; return the signal it died with (0 = exited normally).
template <typename Fn>
static int run_in_child(Fn fn, StubFormat* fmt, StubSampleTable* tbl, bool is_pcm) {
    pid_t pid = fork();
    if (pid == 0) {
        bool m = is_pcm;
        fn(fmt, tbl, &m);
        _exit(m ? 10 : 11);  // 10 = stayed PCM, 11 = downgraded
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) return WTERMSIG(status);
    if (WIFEXITED(status)) return -WEXITSTATUS(status);  // negative = clean exit code
    return -99;
}

int main() {
    // The attacker-shaped input: enca -> frma -> sowt. frma sets mime audio/raw,
    // channel-count and sample-rate, but BITS_PER_SAMPLE is never set.
    StubFormat attacker;
    attacker.has_bits_per_sample = false;
    attacker.has_channel_count = true;
    attacker.channel_count = 2;
    StubSampleTable tbl;
    tbl.max_sample_size = 1024;

    // 1. FAIL-BEFORE: old logic aborts on the attacker-shaped input.
    int sig = run_in_child(old_pcm_block, &attacker, &tbl, true);
    EXPECT(sig == SIGABRT, "old logic aborts (SIGABRT) when BITS_PER_SAMPLE is missing");

    // 2. PASS-AFTER: new logic survives the same input, downgrades gracefully.
    sig = run_in_child(new_pcm_block, &attacker, &tbl, true);
    EXPECT(sig == -11, "new logic does not abort and sets mIsPcm=false on missing keys");

    // 3. No behaviour change for well-formed PCM: 16-bit stereo, sizes consistent.
    StubFormat good;
    good.has_bits_per_sample = true; good.bits_per_sample = 16;
    good.has_channel_count = true; good.channel_count = 2;
    StubSampleTable tbl2;
    tbl2.max_sample_size = 4;  // bytesPerSample(2) * numChannels(2)
    int r_old = run_in_child(old_pcm_block, &good, &tbl2, true);
    int r_new = run_in_child(new_pcm_block, &good, &tbl2, true);
    EXPECT(r_old == -10 && r_new == -10,
           "well-formed 16-bit PCM keeps mIsPcm=true under both old and new logic");

    // 4. Well-formed but unsupported PCM (e.g. 24-bit) still downgrades, no abort.
    StubFormat unsupported;
    unsupported.has_bits_per_sample = true; unsupported.bits_per_sample = 24;
    unsupported.has_channel_count = true; unsupported.channel_count = 2;
    StubSampleTable tbl3;
    tbl3.max_sample_size = 6;
    sig = run_in_child(new_pcm_block, &unsupported, &tbl3, true);
    EXPECT(sig == -11, "24-bit PCM still downgrades gracefully under new logic");

    // 5. Missing channel-count alone also handled (second CHECK in old code).
    StubFormat no_channels;
    no_channels.has_bits_per_sample = true; no_channels.bits_per_sample = 16;
    no_channels.has_channel_count = false;
    sig = run_in_child(new_pcm_block, &no_channels, &tbl, true);
    EXPECT(sig == -11, "missing channel-count alone downgrades gracefully, no abort");

    if (failures == 0) printf("\nAll tests passed.\n");
    else printf("\n%d test(s) FAILED.\n", failures);
    return failures != 0;
}
