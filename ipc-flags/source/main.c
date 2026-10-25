#include <fat.h>
#include <gccore.h>
#include <ogc/ios.h>
#include <ogc/ipc.h>
#include <ogc/lwp_watchdog.h>
#include <sdcard/wiisd_io.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    PPC_MESSAGE = 0xCD000000,
    PPC_CONTROL = 0xCD000004,
    ARM_MESSAGE = 0xCD000008,
    PPC_IRQ_FLAG = 0xCD000030,
    X1 = 0x01,
    Y2 = 0x02,
    Y1 = 0x04,
    X2 = 0x08,
    IPC_IRQ = 0x40000000,
    TRIALS = 256,
    TIMEOUT_US = 100000,
    INVALID_COMMAND = 0xFE,
};

typedef struct {
    u32 before;
    u32 after;
    u32 ticks;
} Sample;

typedef struct {
    unsigned count;
    unsigned retained;
    unsigned cleared;
    unsigned already_clear;
    unsigned raised;
    u32 min_ticks;
    u32 max_ticks;
} Summary;

typedef struct {
    int ios;
    int revision;
    Summary x1[2];
    Summary x2[2];
    bool complete;
    bool recovered;
} Results;

static u32 g_request[16] ATTRIBUTE_ALIGN(64);

static inline u32 read_register(u32 address) {
    u32 value;
    __asm__ volatile("lwz %0,0(%1); sync"
                     : "=r"(value)
                     : "b"(address)
                     : "memory");
    return value;
}

static inline void write_register(u32 address, u32 value) {
    __asm__ volatile("stw %0,0(%1); eieio"
                     :
                     : "r"(value), "b"(address)
                     : "memory");
}

static bool wait_x2_clear(void) {
    const u64 start = gettime();
    while (read_register(PPC_CONTROL) & X2) {
        if (gettime() - start > microsecs_to_ticks(TIMEOUT_US)) return false;
    }
    return true;
}

static Sample measure(u32 flag, bool zero_write) {
    Sample sample;
    u32 start, end;
    const u32 target = zero_write ? PPC_CONTROL : PPC_MESSAGE;
    const u32 value =
        flag == X1 && !zero_write ? MEM_VIRTUAL_TO_PHYSICAL(g_request) : 0;
    __asm__ volatile(
        "mftb %2\n"
        "stw %5,0(%4)\n"
        "eieio\n"
        "lwz %0,0(%4)\n"
        "sync\n"
        "stw %6,0(%7)\n"
        "eieio\n"
        "lwz %1,0(%4)\n"
        "sync\n"
        "mftb %3\n"
        : "=&r"(sample.before), "=&r"(sample.after), "=&r"(start), "=&r"(end)
        : "b"(PPC_CONTROL), "r"(flag), "r"(value), "b"(target)
        : "memory");
    sample.ticks = end - start;
    return sample;
}

static void prepare_request(void) {
    memset(g_request, 0, sizeof(g_request));
    g_request[0] = INVALID_COMMAND;
    DCFlushRange(g_request, sizeof(g_request));
    write_register(PPC_MESSAGE, MEM_VIRTUAL_TO_PHYSICAL(g_request));
}

static bool wait_reply(void) {
    const u64 start = gettime();
    while (gettime() - start <= microsecs_to_ticks(TIMEOUT_US)) {
        const u32 control = read_register(PPC_CONTROL);
        if (control & Y2) write_register(PPC_CONTROL, Y2);
        if (control & Y1) {
            if (read_register(ARM_MESSAGE) !=
                MEM_VIRTUAL_TO_PHYSICAL(g_request))
                return false;
            write_register(PPC_CONTROL, Y1);
            write_register(PPC_CONTROL, X2);
            write_register(PPC_IRQ_FLAG, IPC_IRQ);
            return wait_x2_clear();
        }
    }
    return false;
}

static bool check_ipc(void) {
    write_register(PPC_CONTROL, Y1 | Y2);
    write_register(PPC_IRQ_FLAG, IPC_IRQ);
    write_register(PPC_CONTROL, X2);
    if (!wait_x2_clear()) return false;

    prepare_request();
    write_register(PPC_CONTROL, X1);
    return wait_reply();
}

static bool run_trials(u32 flag, Summary cases[2]) {
    for (unsigned trial = 0; trial < TRIALS; ++trial) {
        for (unsigned order = 0; order < 2; ++order) {
            const bool zero_write = (trial + order) % 2;
            if (!wait_x2_clear()) return false;

            if (flag == X1) prepare_request();
            const Sample sample = measure(flag, zero_write);
            Summary* summary = &cases[zero_write];
            if (!summary->count || sample.ticks < summary->min_ticks)
                summary->min_ticks = sample.ticks;
            if (sample.ticks > summary->max_ticks)
                summary->max_ticks = sample.ticks;
            summary->count++;

            if (sample.before & flag) {
                if (sample.after & flag)
                    summary->retained++;
                else
                    summary->cleared++;
            } else {
                if (sample.after & flag)
                    summary->raised++;
                else
                    summary->already_clear++;
            }

            if (flag == X1 && !wait_reply()) return false;
        }
    }
    return true;
}

static void print_summary(FILE* file, const char* flag,
                          const Summary cases[2]) {
    for (unsigned i = 0; i < 2; ++i) {
        const Summary* summary = &cases[i];
        fprintf(file, "%s %-7s %3u  %4u  %4u  %4u  %4u  %lu-%lu\n", flag,
                i ? "zero" : "control", summary->count, summary->retained,
                summary->cleared, summary->already_clear, summary->raised,
                (unsigned long)summary->min_ticks,
                (unsigned long)summary->max_ticks);
    }
}

static void print_results(FILE* file, const Results* results) {
    fprintf(file, "IOS%d r%d\n\n", results->ios, results->revision);
    fprintf(file, "Flag/case     N  1->1  1->0  0->0  0->1  TB ticks\n");
    print_summary(file, "X2", results->x2);
    print_summary(file, "X1", results->x1);
    if (!results->complete)
        fprintf(file, "IPC exchange failed: trials incomplete\n");
    if (!results->recovered) fprintf(file, "IPC unavailable\n");
}

int main(void) {
    VIDEO_Init();
    GXRModeObj* mode = VIDEO_GetPreferredMode(NULL);
    void* framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(framebuffer, 20, 20, mode->fbWidth, mode->xfbHeight,
                 mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    Results results = {.ios = IOS_GetVersion(), .revision = IOS_GetRevision()};
    __IOS_ShutdownSubsystems();
    __MaskIrq(IM_PI_ACR);
    raw_irq_handler_t old_handler = IRQ_Free(IRQ_PI_ACR);
    const u32 interrupt_state = IRQ_Disable();
    results.complete = check_ipc() && run_trials(X2, results.x2) &&
                       check_ipc() && run_trials(X1, results.x1);
    results.recovered = results.complete && check_ipc();
    IRQ_Restore(interrupt_state);

    print_results(stdout, &results);
    fflush(stdout);

    if (results.recovered) {
        IRQ_Request(IRQ_PI_ACR, old_handler, NULL);
        __UnmaskIrq(IM_PI_ACR);
        __IPC_Reinitialize();
        __IOS_InitializeSubsystems();

        FILE* file = fatMountSimple("sd", &__io_wiisd)
                         ? fopen("sd:/ipc-flags.txt", "w")
                         : NULL;
        if (file) {
            print_results(file, &results);
            const bool failed = ferror(file);
            const int closed = fclose(file);
            puts(failed || closed ? "SD log failed"
                                  : "Saved sd:/ipc-flags.txt");
        } else {
            puts("SD log unavailable");
        }
        puts("RESET to exit");
    } else {
        puts("Restart the Wii");
    }
    fflush(stdout);
    while (!results.recovered || !SYS_ResetButtonDown()) VIDEO_WaitVSync();
    return 0;
}
