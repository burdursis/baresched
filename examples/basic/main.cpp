#include "scheduler.hpp"

#include <inttypes.h>
#include <stdio.h>

namespace
{
using ApplicationScheduler = Scheduler<8>;
ApplicationScheduler::Time ticks = 0;
uint32_t serialPollCount = 0;

ApplicationScheduler::Time ReadTicks()
{
    return ticks;
}

struct Work
{
    ApplicationScheduler::Time executionTicks;
    uint32_t steps;
};

void RunStep(void* context)
{
    Work& work = *static_cast<Work*>(context);
    ++work.steps;
    ticks += work.executionTicks; // Simulate one bounded step, without sleeping.
}

void PollSerialStep()
{
    ++serialPollCount;
    ticks += 1; // Simulate checking a buffer; never wait for a byte.
}

bool PrintDiagnostics(const ApplicationScheduler& scheduler, uint32_t id, const char* name)
{
    ApplicationScheduler::TaskDiagnostics diagnostics;
    if (!scheduler.GetTaskDiagnostics(id, diagnostics))
    {
        return false;
    }

    printf("%s: runs=%" PRIu32 " skipped=%" PRIu32 " startMiss=%" PRIu32
           " deadlineMiss=%" PRIu32 " wcetOverrun=%" PRIu32
           " lastLateness=%" PRIu32 " maxLateness=%" PRIu32
           " lastExec=%" PRIu32 " maxExec=%" PRIu32 "\n",
           name, diagnostics.runCount, diagnostics.skippedCount,
           diagnostics.startMissCount, diagnostics.deadlineMissCount,
           diagnostics.wcetOverrunCount, diagnostics.lastLateness,
           diagnostics.maxLateness, diagnostics.lastExecutionTime,
           diagnostics.maxExecutionTime);
    return true;
}
} // namespace

int main()
{
    ApplicationScheduler scheduler(&ReadTicks);
    Work fast = {2, 0};
    Work slow = {4, 0};

    // Arguments: callback, context, period, offset, lateness, deadline, WCET.
    const int fastId = scheduler.AddTask(&RunStep, &fast, 20, 0, 3, 5, 2);
    const int slowId = scheduler.AddTask(&RunStep, &slow, 100, 7, 5, 9, 4);
    if (fastId < 0 || slowId < 0)
    {
        fputs("Task registration failed.\n", stderr);
        return 1;
    }

    scheduler.Start(ReadTicks());
    bool reconfigured = false;
    while (ticks < 400)
    {
        if (!reconfigured && ticks >= 150)
        {
            const uint32_t id = static_cast<uint32_t>(slowId);
            if (!scheduler.DisableTask(id) ||
                !scheduler.SetPeriod(id, 60) ||
                !scheduler.EnableTask(id))
            {
                fputs("Task reconfiguration failed.\n", stderr);
                return 1;
            }
            printf("Slow task re-enabled at tick %" PRIu32
                   "; first release at tick %" PRIu32 ".\n", ticks, ticks + 60);
            reconfigured = true;
        }

        scheduler.Run();
        if (scheduler.CanRunBackground(1))
        {
            PollSerialStep();
        }
        ++ticks; // Simulate main-loop overhead and idle time.
    }

    if (!PrintDiagnostics(scheduler, static_cast<uint32_t>(fastId), "fast") ||
        !PrintDiagnostics(scheduler, static_cast<uint32_t>(slowId), "slow"))
    {
        fputs("Could not read task diagnostics.\n", stderr);
        return 1;
    }
    printf("Serial polling steps: %" PRIu32 "\n", serialPollCount);
    return 0;
}
