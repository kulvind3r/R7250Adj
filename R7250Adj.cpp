/*
 * R7250Adj v1.0
 * AMD Ryzen 7 250 (Phoenix/Hawk Point) power parameter tuning via PawnIO.
 *
 * Build: x86_64-w64-mingw32-g++ -O2 -static -o R7250Adj.exe R7250Adj.cpp -lshlwapi
 * Requires: PawnIO driver (https://pawnio.eu) + Administrator privileges.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cpuid.h>

#define R7250ADJ_VERSION "2026.03.18"

// CPUID brand string gate — runs before any driver interaction
static const char* REQUIRED_CPU_BRAND = "Ryzen 7 250";

// ─── PawnIO IOCTL protocol ───────────────────────────────────────────────────
// Source: ECReader (https://github.com/kulvind3r/ECReader) / LibreHardwareMonitor
#define PAWNIO_DEVICE_TYPE    41394u
#define IOCTL_PAWNIO_LOAD_BIN CTL_CODE(PAWNIO_DEVICE_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PAWNIO_EXECUTE  CTL_CODE(PAWNIO_DEVICE_TYPE, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FN_NAME_BYTES         32

// ─── Device paths ────────────────────────────────────────────────────────────
// Try new path first (PawnIO >= 2.1.0), fall back to old. Source: UXTU PawnIO.cs
static const char* DEVICE_PATH_NEW = "\\\\.\\GLOBALROOT\\Device\\PawnIO";
static const char* DEVICE_PATH_OLD = "\\\\.\\PawnIO";

// ─── PCI bus mutex ───────────────────────────────────────────────────────────
// System-wide convention shared with HWiNFO64 and similar tools. Source: UXTU RyzenSmu.cs
static const char*  PCI_MUTEX_NAME   = "Global\\Access_PCI";
static const DWORD  MUTEX_TIMEOUT_MS = 10;

// ─── MP1 mailbox registers for Phoenix/Hawk Point (FT6/FP7/FP8 socket) ──────
// Dual-source confirmed: RyzenAdj nb_smu_ops.c MP1_C2PMSG_*_ADDR_2 (FAM_HAWKPOINT)
//                        UXTU RyzenSmu.cs Socket_FT6_FP7_FP8() else branch
static const uint32_t MP1_ADDR_MSG  = 0x3B10528u;
static const uint32_t MP1_ADDR_RSP  = 0x3B10578u;  // NOT 0x3B10564 - that is the older _ADDR_1 variant
static const uint32_t MP1_ADDR_ARG  = 0x3B10998u;
static const uint32_t MP1_ARG_SLOTS = 6u;
static const uint16_t SMU_POLL_MAX  = 8192u;

// ─── SMU MP1 command IDs for Phoenix/Hawk Point ──────────────────────────────
// Dual-source confirmed: RyzenAdj api.c set_*() FAM_HAWKPOINT _do_adjust()
//                        UXTU RyzenSmu.cs SMUCommands.commands (true = MP1 bus)
static const uint32_t SMU_CMD_SET_STAPM = 0x14u;
static const uint32_t SMU_CMD_SET_FAST  = 0x15u;
static const uint32_t SMU_CMD_SET_SLOW  = 0x16u;
static const uint32_t SMU_CMD_SET_TCTL  = 0x19u;

// ─── PM table layout — version 0x4C0009 ──────────────────────────────────────
// Float index = byte offset / 4. Source: RyzenAdj api.c get_*() switch on table_ver.
// All four offsets confirmed on physical Ryzen 7 250 hardware via ryzenadj --dump-table.
static const uint32_t PM_TABLE_VERSION = 0x4C0009u;
static const uint32_t PM_TABLE_FLOATS  = 704u;  // 0xB00 bytes / 4
static const int      PM_IDX_STAPM     = 0;     // byte 0x000
static const int      PM_IDX_FAST      = 2;     // byte 0x008
static const int      PM_IDX_SLOW      = 4;     // byte 0x010
static const int      PM_IDX_TCTL      = 16;    // byte 0x040

// ─── CPU codename IDs (Layer 2 check, post-driver) ───────────────────────────
// Ryzen 7 250 may report Phoenix (23) or HawkPoint (30) depending on die stepping.
// Both share identical MP1 mailbox addresses and SMU commands in UXTU Socket_FT6_FP7_FP8().
// Source: RyzenSMU.p get_code_name() enum
static const int      PHOENIX_ID     = 23;      // CPU_Phoenix  (Family 19h, Model 74h/75h)
static const int      HAWK_POINT_ID  = 30;      // CPU_HawkPoint (Family 19h, Model 7Ch)

// ─── Hardware safety limits — AMD official spec for Ryzen 7 250 ───────────────
// tJMax = 100 C, cTDP = 15-30 W. Source: https://www.techpowerup.com/cpu-specs/ryzen-7-250.c4011
// Minimum power floor set to 10 W for system responsiveness.
static const float    TCTL_MIN_C     = 60.0f;
static const float    TCTL_MAX_C     = 100.0f;
static const uint32_t STAPM_MIN_MW   = 10000u;
static const uint32_t STAPM_MAX_MW   = 53000u;
static const uint32_t FAST_MIN_MW    = 10000u;
static const uint32_t FAST_MAX_MW    = 53000u;
static const uint32_t SLOW_MIN_MW    = 10000u;
static const uint32_t SLOW_MAX_MW    = 43000u;

// ─── Module-level state ───────────────────────────────────────────────────────
static HANDLE g_device   = INVALID_HANDLE_VALUE;
static HANDLE g_pciMutex = NULL;

// Forward declaration — closeDevice() is defined after openDevice() but called within it
static void closeDevice();


// ─── CPU Identity — Layer 1 ───────────────────────────────────────────────────
// Reads the CPUID brand string directly (no OS API, no driver).
// Must run before any PawnIO interaction.

static bool isRyzen7250() {
    uint32_t regs[12];
    __cpuid(0x80000002, regs[0],  regs[1],  regs[2],  regs[3]);
    __cpuid(0x80000003, regs[4],  regs[5],  regs[6],  regs[7]);
    __cpuid(0x80000004, regs[8],  regs[9],  regs[10], regs[11]);

    char brand[49] = {0};
    memcpy(brand, regs, 48);

    return strstr(brand, REQUIRED_CPU_BRAND) != NULL;
}


// ─── Service check ────────────────────────────────────────────────────────────

static bool isPawnIORunning() {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        // SCM open failure likely means not running as Administrator
        fprintf(stderr, "Error: Cannot connect to Service Control Manager. Run as Administrator.\n");
        return false;
    }

    SC_HANDLE svc = OpenServiceA(scm, "PawnIO", SERVICE_QUERY_STATUS);
    if (svc == NULL) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST)
            fprintf(stderr, "Error: PawnIO driver is not installed. Download from https://pawnio.eu\n");
        else
            fprintf(stderr, "Error: Cannot query PawnIO service (error %lu).\n", err);
        return false;
    }

    SERVICE_STATUS status = {0};
    BOOL ok = QueryServiceStatus(svc, &status);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);

    if (!ok) {
        fprintf(stderr, "Error: Cannot query PawnIO service status (error %lu).\n", GetLastError());
        return false;
    }

    if (status.dwCurrentState != SERVICE_RUNNING) {
        fprintf(stderr, "Error: PawnIO driver is not running (state %lu). Start it with: sc start PawnIO\n",
                status.dwCurrentState);
        return false;
    }

    return true;
}

// ─── PawnIO Core ──────────────────────────────────────────────────────────────

static bool pawnioExecute(const char* fn, LONG64* in, int inCount, LONG64* out, int outCount) {
    int inBufSize = FN_NAME_BYTES + inCount * (int)sizeof(LONG64);
    BYTE* inBuf   = (BYTE*)calloc((size_t)inBufSize, 1);
    if (!inBuf) return false;

    // Function name occupies the first FN_NAME_BYTES — zero-padded, never null-terminated past end
    size_t fnLen = strlen(fn);
    memcpy(inBuf, fn, fnLen < (size_t)(FN_NAME_BYTES - 1) ? fnLen : (size_t)(FN_NAME_BYTES - 1));

    if (inCount > 0 && in != NULL)
        memcpy(inBuf + FN_NAME_BYTES, in, (size_t)inCount * sizeof(LONG64));

    int outBufSize = outCount > 0 ? outCount * (int)sizeof(LONG64) : 1;
    BYTE* outBuf   = (BYTE*)calloc((size_t)outBufSize, 1);
    if (!outBuf) { free(inBuf); return false; }

    DWORD returned = 0;
    BOOL  ok = DeviceIoControl(g_device, IOCTL_PAWNIO_EXECUTE,
                                inBuf, (DWORD)inBufSize,
                                outBuf, (DWORD)outBufSize,
                                &returned, NULL);

    if (ok && out != NULL && returned > 0) {
        DWORD copySize = returned < (DWORD)outBufSize ? returned : (DWORD)outBufSize;
        memcpy(out, outBuf, copySize);
    }

    free(inBuf);
    free(outBuf);
    return ok != FALSE;
}

static bool openDevice() {
    // Layer 1: CPUID brand string — zero hardware risk, runs before any driver access
    if (!isRyzen7250()) {
        fprintf(stderr, "Error: This tool only supports AMD Ryzen 7 250. Detected CPU does not match.\n");
        return false;
    }

    // Confirm PawnIO service is installed and running before attempting device open
    if (!isPawnIORunning())
        return false;

    g_device = CreateFileA(DEVICE_PATH_NEW,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);

    if (g_device == INVALID_HANDLE_VALUE)
        g_device = CreateFileA(DEVICE_PATH_OLD,
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);

    if (g_device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Cannot open PawnIO device (error %lu). Are you running as Administrator?\n",
                GetLastError());
        return false;
    }

    // Locate RyzenSMU.bin alongside the exe — never from user-controlled input
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSep = strrchr(exePath, '\\');
    if (lastSep) *(lastSep + 1) = '\0';

    char binPath[MAX_PATH] = {0};
    snprintf(binPath, MAX_PATH, "%sRyzenSMU.bin", exePath);

    FILE* f = fopen(binPath, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot find RyzenSMU.bin at: %s\n"
                        "       Place RyzenSMU.bin in the same directory as R7250Adj.exe.\n", binPath);
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
        return false;
    }

    fseek(f, 0, SEEK_END);
    long binSize = ftell(f);
    rewind(f);

    BYTE* binBuf = (BYTE*)malloc((size_t)binSize);
    if (!binBuf) {
        fclose(f);
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
        return false;
    }
    fread(binBuf, 1, (size_t)binSize, f);
    fclose(f);

    DWORD returned = 0;
    BOOL  loaded   = DeviceIoControl(g_device, IOCTL_PAWNIO_LOAD_BIN,
                                      binBuf, (DWORD)binSize,
                                      NULL, 0, &returned, NULL);
    free(binBuf);

    if (!loaded) {
        fprintf(stderr, "Error: Failed to load RyzenSMU.bin into PawnIO (error %lu).\n", GetLastError());
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
        return false;
    }

    // Create or open the system-wide PCI mutex (shared convention with HWiNFO64)
    g_pciMutex = CreateMutexA(NULL, FALSE, PCI_MUTEX_NAME);
    if (g_pciMutex == NULL)
        fprintf(stderr, "Warning: Could not create PCI mutex. Coordination with HWiNFO disabled.\n");

    // Layer 2: PawnIO codename — confirms silicon family after driver load
    LONG64 codeNameOut[1] = {-1};
    pawnioExecute("ioctl_get_code_name", NULL, 0, codeNameOut, 1);
    bool isSupportedCpu = (codeNameOut[0] == (LONG64)PHOENIX_ID    ||
                           codeNameOut[0] == (LONG64)HAWK_POINT_ID);
    if (!isSupportedCpu) {
        fprintf(stderr, "Error: Unsupported CPU codename %lld. R7250Adj requires Phoenix/HawkPoint.\n",
                codeNameOut[0]);
        closeDevice();
        return false;
    }

    return true;
}

static void closeDevice() {
    if (g_pciMutex != NULL) { CloseHandle(g_pciMutex); g_pciMutex = NULL; }
    if (g_device != INVALID_HANDLE_VALUE) { CloseHandle(g_device); g_device = INVALID_HANDLE_VALUE; }
}


// ─── SMU Mailbox ──────────────────────────────────────────────────────────────

static bool acquirePciMutex() {
    if (g_pciMutex == NULL) return true;
    DWORD r = WaitForSingleObject(g_pciMutex, MUTEX_TIMEOUT_MS);
    // WAIT_ABANDONED: previous holder crashed — kernel has already released, safe to proceed
    return (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
}

static void releasePciMutex() {
    if (g_pciMutex != NULL) ReleaseMutex(g_pciMutex);
}

static bool readReg32(uint32_t reg, uint32_t* value) {
    LONG64 inBuf[1]  = { (LONG64)reg };
    LONG64 outBuf[1] = { 0 };
    if (!pawnioExecute("ioctl_read_smu_register", inBuf, 1, outBuf, 1)) return false;
    *value = (uint32_t)(outBuf[0] & 0xFFFFFFFFu);
    return true;
}

static bool writeReg32(uint32_t reg, uint32_t value) {
    LONG64 inBuf[2] = { (LONG64)reg, (LONG64)value };
    return pawnioExecute("ioctl_write_smu_register", inBuf, 2, NULL, 0);
}

static bool sendSmuCommand(uint32_t msgId, uint32_t arg0) {
    if (!acquirePciMutex()) {
        fprintf(stderr, "Error: Could not acquire PCI mutex (timeout).\n");
        return false;
    }

    // Wait for mailbox idle — non-zero RSP means the previous command has finished
    uint32_t rsp  = 0;
    bool     idle = false;
    for (uint16_t i = 0; i < SMU_POLL_MAX; i++) {
        if (readReg32(MP1_ADDR_RSP, &rsp) && rsp != 0) { idle = true; break; }
    }
    if (!idle) {
        fprintf(stderr, "Error: SMU mailbox idle timeout.\n");
        releasePciMutex();
        return false;
    }

    if (!writeReg32(MP1_ADDR_RSP, 0u)) {
        fprintf(stderr, "Error: Failed to clear SMU response register.\n");
        releasePciMutex();
        return false;
    }

    for (uint32_t slot = 0; slot < MP1_ARG_SLOTS; slot++) {
        uint32_t slotValue = (slot == 0u) ? arg0 : 0u;
        if (!writeReg32(MP1_ADDR_ARG + (slot * 4u), slotValue)) {
            fprintf(stderr, "Error: Failed to write SMU argument slot %u.\n", slot);
            releasePciMutex();
            return false;
        }
    }

    if (!writeReg32(MP1_ADDR_MSG, msgId)) {
        fprintf(stderr, "Error: Failed to write SMU command register.\n");
        releasePciMutex();
        return false;
    }

    // Poll for acknowledgement
    rsp  = 0;
    bool done = false;
    for (uint16_t i = 0; i < SMU_POLL_MAX; i++) {
        if (readReg32(MP1_ADDR_RSP, &rsp) && rsp != 0) { done = true; break; }
    }

    releasePciMutex();

    if (!done) {
        fprintf(stderr, "Error: SMU response timeout after command 0x%02X.\n", msgId);
        return false;
    }
    if (rsp != 0x01u) {
        // SMU response codes: 0x01=OK, 0xFE=UnknownCmd, 0xFD=PrereqNotMet, 0xFC=Busy, 0xFF=Failed
        fprintf(stderr, "Error: SMU rejected command 0x%02X (status 0x%02X).\n", msgId, rsp);
        return false;
    }
    return true;
}

static bool readPmTable(float* stapm, float* fast, float* slow, float* tctl) {
    if (!acquirePciMutex()) {
        fprintf(stderr, "Error: Could not acquire PCI mutex (timeout).\n");
        return false;
    }

    LONG64 resolveOut[2] = {0, 0};
    pawnioExecute("ioctl_resolve_pm_table", NULL, 0, resolveOut, 2);
    if ((uint32_t)resolveOut[0] != PM_TABLE_VERSION)
        fprintf(stderr, "Warning: PM table version 0x%X (expected 0x%X). Proceeding.\n",
                (uint32_t)resolveOut[0], PM_TABLE_VERSION);

    if (!pawnioExecute("ioctl_update_pm_table", NULL, 0, NULL, 0)) {
        fprintf(stderr, "Error: Failed to update PM table.\n");
        releasePciMutex();
        return false;
    }

    uint32_t tableData[PM_TABLE_FLOATS];
    memset(tableData, 0, sizeof(tableData));
    if (!pawnioExecute("ioctl_read_pm_table", NULL, 0,
                       (LONG64*)tableData, (int)(PM_TABLE_FLOATS / 2))) {
        fprintf(stderr, "Error: Failed to read PM table.\n");
        releasePciMutex();
        return false;
    }

    releasePciMutex();

    // memcpy required — casting uint32_t* directly to float* violates strict aliasing
    memcpy(stapm, &tableData[PM_IDX_STAPM], sizeof(float));
    memcpy(fast,  &tableData[PM_IDX_FAST],  sizeof(float));
    memcpy(slow,  &tableData[PM_IDX_SLOW],  sizeof(float));
    memcpy(tctl,  &tableData[PM_IDX_TCTL],  sizeof(float));
    return true;
}


// ─── Validation ───────────────────────────────────────────────────────────────
// All validation runs before openDevice() — hardware is never contacted on bad input.

static bool validateTctlTemp(float v) {
    if (v < TCTL_MIN_C || v > TCTL_MAX_C) {
        fprintf(stderr, "Error: tctl-temp must be between %.0f and %.0f (degrees C)."
                        " AMD tJMax for Ryzen 7 250 is 100 C.\n", TCTL_MIN_C, TCTL_MAX_C);
        return false;
    }
    return true;
}

static bool validateStapmLimit(uint32_t mw) {
    if (mw < STAPM_MIN_MW || mw > STAPM_MAX_MW) {
        fprintf(stderr, "Error: stapm-limit must be between %u and %u (mW)."
                        " Enforced range for Ryzen 7 250 is 10-53 W.\n",
                        STAPM_MIN_MW, STAPM_MAX_MW);
        return false;
    }
    return true;
}

static bool validateFastLimit(uint32_t mw) {
    if (mw < FAST_MIN_MW || mw > FAST_MAX_MW) {
        fprintf(stderr, "Error: fast-limit must be between %u and %u (mW)."
                        " Enforced range for Ryzen 7 250 is 10-53 W.\n",
                        FAST_MIN_MW, FAST_MAX_MW);
        return false;
    }
    return true;
}

static bool validateSlowLimit(uint32_t mw) {
    if (mw < SLOW_MIN_MW || mw > SLOW_MAX_MW) {
        fprintf(stderr, "Error: slow-limit must be between %u and %u (mW)."
                        " Enforced range for Ryzen 7 250 is 10-43 W.\n",
                        SLOW_MIN_MW, SLOW_MAX_MW);
        return false;
    }
    return true;
}


// ─── CLI ──────────────────────────────────────────────────────────────────────

static void printHelp() {
    printf("R7250Adj v%s - AMD Ryzen 7 250 (Phoenix/Hawk Point) power tuning\n\n", R7250ADJ_VERSION);
    printf("Usage:\n");
    printf("  R7250Adj.exe --info\n");
    printf("  R7250Adj.exe --stapm-limit <milliwatts>   (10000 - 53000)\n");
    printf("  R7250Adj.exe --fast-limit  <milliwatts>   (10000 - 53000)\n");
    printf("  R7250Adj.exe --slow-limit  <milliwatts>   (10000 - 43000)\n");
    printf("  R7250Adj.exe --tctl-temp   <celsius>      (60 - 100)\n\n");
    printf("Requires: PawnIO driver (https://pawnio.eu) and Administrator privileges.\n");
}

static int runInfo() {
    if (!openDevice()) return 2;

    float stapm = 0.0f, fast = 0.0f, slow = 0.0f, tctl = 0.0f;
    if (!readPmTable(&stapm, &fast, &slow, &tctl)) {
        closeDevice();
        return 2;
    }
    closeDevice();

    printf("R7250Adj v%s | AMD Ryzen 7 250 (Phoenix/Hawk Point)\n", R7250ADJ_VERSION);
    printf("PM Table Version : 0x%X\n\n", PM_TABLE_VERSION);

    char v1[24], v2[24], v3[24], v4[24];
    snprintf(v1, sizeof(v1), "%.3f W", stapm);
    snprintf(v2, sizeof(v2), "%.3f W", fast);
    snprintf(v3, sizeof(v3), "%.3f W", slow);
    snprintf(v4, sizeof(v4), "%.3f C", tctl);

    printf("%-18s  %-15s  %s\n", "Parameter",      "Current Limit", "Safe Range");
    printf("%-18s  %-15s  %s\n", "---------",      "-------------", "----------");
    printf("%-18s  %-15s  %s\n", "STAPM Limit",     v1, "10 - 53 W");
    printf("%-18s  %-15s  %s\n", "PPT Fast Limit",  v2, "10 - 53 W");
    printf("%-18s  %-15s  %s\n", "PPT Slow Limit",  v3, "10 - 43 W");
    printf("%-18s  %-15s  %s\n", "Tctl Temp Limit", v4, "60 - 100 C");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        printHelp();
        return 0;
    }

    if (strcmp(cmd, "--info") == 0) {
        return runInfo();
    }

    // Reject unknown commands before checking for a value argument so the error
    // message is about the bad command name, not a missing value.
    bool isKnownCommand = (strcmp(cmd, "--tctl-temp")   == 0 ||
                           strcmp(cmd, "--stapm-limit") == 0 ||
                           strcmp(cmd, "--fast-limit")  == 0 ||
                           strcmp(cmd, "--slow-limit")  == 0);
    if (!isKnownCommand) {
        fprintf(stderr, "Error: Unknown command '%s'.\n\n", cmd);
        printHelp();
        return 1;
    }

    if (argc < 3) {
        fprintf(stderr, "Error: %s requires a value. Use --help for usage.\n", cmd);
        return 1;
    }

    char* endPtr = NULL;

    if (strcmp(cmd, "--tctl-temp") == 0) {
        long val = strtol(argv[2], &endPtr, 10);
        if (*endPtr != '\0') {
            fprintf(stderr, "Error: '%s' is not a valid integer for tctl-temp.\n", argv[2]);
            return 1;
        }
        // validateTctlTemp receives a float — safe cast since input is a whole-degree integer
        if (!validateTctlTemp((float)val)) return 1;
        if (!openDevice()) return 2;
        if (!sendSmuCommand(SMU_CMD_SET_TCTL, (uint32_t)val)) { closeDevice(); return 2; }
        printf("OK: tctl-temp set to %ld C\n", val);
        closeDevice();
        return 0;
    }

    if (strcmp(cmd, "--stapm-limit") == 0) {
        unsigned long val = strtoul(argv[2], &endPtr, 10);
        if (*endPtr != '\0') {
            fprintf(stderr, "Error: '%s' is not a valid integer for stapm-limit.\n", argv[2]);
            return 1;
        }
        if (!validateStapmLimit((uint32_t)val)) return 1;
        if (!openDevice()) return 2;
        if (!sendSmuCommand(SMU_CMD_SET_STAPM, (uint32_t)val)) { closeDevice(); return 2; }
        printf("OK: stapm-limit set to %lu mW (%.3f W)\n", val, (double)val / 1000.0);
        closeDevice();
        return 0;
    }

    if (strcmp(cmd, "--fast-limit") == 0) {
        unsigned long val = strtoul(argv[2], &endPtr, 10);
        if (*endPtr != '\0') {
            fprintf(stderr, "Error: '%s' is not a valid integer for fast-limit.\n", argv[2]);
            return 1;
        }
        if (!validateFastLimit((uint32_t)val)) return 1;
        if (!openDevice()) return 2;
        if (!sendSmuCommand(SMU_CMD_SET_FAST, (uint32_t)val)) { closeDevice(); return 2; }
        printf("OK: fast-limit set to %lu mW (%.3f W)\n", val, (double)val / 1000.0);
        closeDevice();
        return 0;
    }

    if (strcmp(cmd, "--slow-limit") == 0) {
        unsigned long val = strtoul(argv[2], &endPtr, 10);
        if (*endPtr != '\0') {
            fprintf(stderr, "Error: '%s' is not a valid integer for slow-limit.\n", argv[2]);
            return 1;
        }
        if (!validateSlowLimit((uint32_t)val)) return 1;
        if (!openDevice()) return 2;
        if (!sendSmuCommand(SMU_CMD_SET_SLOW, (uint32_t)val)) { closeDevice(); return 2; }
        printf("OK: slow-limit set to %lu mW (%.3f W)\n", val, (double)val / 1000.0);
        closeDevice();
        return 0;
    }

    fprintf(stderr, "Error: Unknown command '%s'.\n\n", cmd);
    printHelp();
    return 1;
}
