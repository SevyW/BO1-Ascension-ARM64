// native_perf.cpp - what the FPS counter's "full" mode shows besides frame times: how busy the CPU and the GPU are.
//
// CPU: this process's CPU time over wall time, as a share of all cores (clock_gettime).
// GPU: there is no portable figure. Android kernels publish one per vendor in sysfs, when the app may read it:
//   Qualcomm Adreno  /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage   "37 %"
//                    /sys/class/kgsl/kgsl-3d0/gpubusy               "<busy> <total>" since the last read
//   ARM Mali         /sys/class/misc/mali0/device/utilization       "37"
// -1 when none can be read (desktop Linux, a phone that denies the files).
//
// This file is not rewritten (tools/p32).
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

namespace
{
double Seconds(clockid_t clock)
{
    timespec ts;
    clock_gettime(clock, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

bool ReadLine(const char *path, char *line, int size)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    const bool ok = fgets(line, size, file) != nullptr;
    fclose(file);
    return ok;
}
}

// share of all cores this process used since the previous call, 0..100
extern "C" float bo1_native_cpu_percent(void)
{
    static double lastWall, lastCpu;
    static float last;
    const double wall = Seconds(CLOCK_MONOTONIC), cpu = Seconds(CLOCK_PROCESS_CPUTIME_ID);
    if (lastWall > 0.0 && wall - lastWall > 0.05)
    {
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores < 1)
            cores = 1;
        last = (float)((cpu - lastCpu) / (wall - lastWall) / (double)cores * 100.0);
    }
    if (lastWall <= 0.0 || wall - lastWall > 0.05)
    {
        lastWall = wall;
        lastCpu = cpu;
    }
    return last;
}

// 0..100, or -1 when the system does not say
extern "C" int bo1_native_gpu_percent(void)
{
    char line[128];
    if (ReadLine("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", line, sizeof(line)))
        return atoi(line);
    if (ReadLine("/sys/class/kgsl/kgsl-3d0/gpubusy", line, sizeof(line)))
    {
        long busy = 0, total = 0;
        if (sscanf(line, "%ld %ld", &busy, &total) == 2 && total > 0)
            return (int)(busy * 100 / total);
        return 0;
    }
    if (ReadLine("/sys/class/misc/mali0/device/utilization", line, sizeof(line)))
        return atoi(line);
    return -1;
}

// a clock for frame times: seconds, sub-millisecond
extern "C" double bo1_native_seconds(void)
{
    return Seconds(CLOCK_MONOTONIC);
}
