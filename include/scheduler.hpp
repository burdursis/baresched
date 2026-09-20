#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

// Cooperative periodic scheduling with fixed storage and caller-provided ticks.
// See docs/api.md for contracts and docs/design.md for integration limits.
#include <stdint.h>

namespace scheduler_detail
{
    // Comparisons require timestamps less than 2^31 ticks apart and a target
    // whose uint32_t-to-int32_t conversion preserves the signed difference.
    static inline bool TimeReached(uint32_t now, uint32_t time)
    {
        return static_cast<int32_t>(now - time) >= 0;
    }

    static inline bool TimeBefore(uint32_t a, uint32_t b)
    {
        return static_cast<int32_t>(a - b) < 0;
    }

    static inline bool TimeAfter(uint32_t a, uint32_t b)
    {
        return static_cast<int32_t>(a - b) > 0;
    }
}

template <uint32_t MaxTasks>
class Scheduler
{
    static_assert(MaxTasks >= 1 && MaxTasks <= 32,
                  "MaxTasks must be between 1 and 32");
public:
    using Time     = uint32_t;
    using Callback = void (*)(void*);
    using Now      = Time (*)();

    // Retained as a public capacity constant for existing callers.
    static constexpr uint32_t MAX_TASKS = MaxTasks;

    struct TaskDiagnostics
    {
        uint32_t runCount;

        /*
         * Number of complete periodic invocations skipped because
         * the scheduler was too late to process them.
         */
        uint32_t skippedCount;

        /*
         * Number of invocations dropped because the task could not
         * start within allowedLateness.
         */
        uint32_t startMissCount;

        /*
         * Number of invocations that completed after their
         * configured completion deadline.
         */
        uint32_t deadlineMissCount;

        /*
         * Number of callback executions whose measured execution
         * time exceeded the configured WCET.
         */
        uint32_t wcetOverrunCount;

        /*
         * Delay between the ideal release time and the actual
         * callback start time.
         */
        Time lastLateness;
        Time maxLateness;

        /*
         * Measured callback execution time.
         */
        Time lastExecutionTime;
        Time maxExecutionTime;

        TaskDiagnostics()
            : runCount(0),
              skippedCount(0),
              startMissCount(0),
              deadlineMissCount(0),
              wcetOverrunCount(0),
              lastLateness(0),
              maxLateness(0),
              lastExecutionTime(0),
              maxExecutionTime(0)
        {
        }
    };

    struct Task
    {
        Callback callback;
        void* context;

        Time period;
        Time offset;

        /*
         * Maximum allowed delay between the ideal release time
         * and the actual callback start time.
         */
        Time allowedLateness;

        /*
         * Maximum allowed time from the release time until
         * callback completion.
         */
        Time relativeDeadline;

        /*
         * Worst-case execution time of a single callback
         * invocation.
         */
        Time wcet;

        Time nextRelease;

        TaskDiagnostics diagnostics;

        Task()
            : callback(nullptr),
              context(nullptr),
              period(0),
              offset(0),
              allowedLateness(0),
              relativeDeadline(0),
              wcet(0),
              nextRelease(0),
              diagnostics()
        {
        }
    };

    // now must be non-null, monotonic modulo uint32_t, and quick to read.
    // Use one execution context; callbacks must return without throwing.
    explicit Scheduler(Now now)
        : now_(now), usedMask_(0), enabledMask_(0), started_(false)
    {
    }

    // Register before Start(); returns an enabled slot ID (0..MaxTasks-1), or -1 for
    // invalid parameters / full storage. context remains owned by the caller.
    int AddTask(
        Callback callback,
        void* context,
        Time period,
        Time offset,
        Time allowedLateness,
        Time relativeDeadline,
        Time wcet)
    {
        if (callback == nullptr)
        {
            return -1;
        }

        if (!ValidTaskParameters(period, offset, allowedLateness, relativeDeadline, wcet))
        {
            return -1;
        }

        for (uint32_t i = 0; i < MaxTasks; ++i)
        {
            const uint32_t bit = (1u << i);

            if ((usedMask_ & bit) == 0u)
            {
                Task& task = tasks_[i];

                task.callback         = callback;
                task.context          = context;
                task.period           = period;
                task.offset           = offset;
                task.allowedLateness  = allowedLateness;
                task.relativeDeadline = relativeDeadline;
                task.wcet             = wcet;
                task.nextRelease      = 0;

                task.diagnostics = TaskDiagnostics();

                usedMask_ |= bit;
                enabledMask_ |= bit;

                return static_cast<int>(i);
            }
        }

        return -1;
    }

    // Set enabled releases to epoch + offset. Repeated calls restart their
    // timelines without resetting diagnostics. Disabled tasks are unaffected.
    void Start(Time epoch)
    {
        for (uint32_t i = 0; i < MaxTasks; ++i)
        {
            const uint32_t bit = (1u << i);

            if ((usedMask_ & bit) == 0u)
            {
                continue;
            }

            if ((enabledMask_ & bit) == 0u)
            {
                continue;
            }

            tasks_[i].nextRelease = epoch + tasks_[i].offset;
        }

        started_ = true;
    }

    // Execute at most one callback. false means no callback ran; selection
    // may still have advanced missed releases and updated diagnostics.
    bool Run()
    {
        if (!started_)
        {
            return false;
        }

        const Time now = now_();

        const int taskId = SelectNextTask(now);

        if (taskId < 0)
        {
            return false;
        }

        if (!CanRunNow(static_cast<uint32_t>(taskId), now))
        {
            return false;
        }

        Dispatch(static_cast<uint32_t>(taskId));

        return true;
    }

    // Exclude a registered task from selection and admission checks.
    // Preserve its slot and diagnostics; return false for an invalid ID.
    bool DisableTask(uint32_t taskId)
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        const uint32_t bit = (1u << taskId);

        enabledMask_ &= ~bit;

        return true;
    }

    // After Start(), release at now + firstDelay (0 means immediately ready).
    // Before Start(), the delay is overwritten by Start's epoch + offset.
    // Return false for an invalid ID or firstDelay >= 2^31.
    bool EnableTask(uint32_t taskId, Time firstDelay)
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        if (!ValidDelta(firstDelay))
        {
            return false;
        }

        Task& task = tasks_[taskId];

        if (started_)
        {
            task.nextRelease = now_() + firstDelay;
        }
        else
        {
            task.nextRelease = firstDelay;
        }

        const uint32_t bit = (1u << taskId);

        enabledMask_ |= bit;

        return true;
    }

    // Enable with a full-period first delay; diagnostics remain unchanged.
    bool EnableTask(uint32_t taskId)
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        return EnableTask(taskId, tasks_[taskId].period);
    }

    // Change only a disabled task. Require 0 < period < 2^31,
    // relativeDeadline <= period, and offset < period; false leaves it unchanged.
    bool SetPeriod(uint32_t taskId, Time period)
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        const uint32_t bit = (1u << taskId);

        /*
         * Timing parameters may only be changed while the task
         * is disabled.
         */
        if ((enabledMask_ & bit) != 0u)
        {
            return false;
        }

        if (period == 0u)
        {
            return false;
        }

        if (!ValidDelta(period))
        {
            return false;
        }

        Task& task = tasks_[taskId];

        /*
         * Multiple pending instances of the same periodic task
         * are not supported.
         */
        if (task.relativeDeadline > period)
        {
            return false;
        }

        if (task.offset >= period)
        {
            return false;
        }

        task.period = period;

        return true;
    }

    // Copy a registered task's counters; false leaves the output unchanged.
    bool GetTaskDiagnostics(uint32_t taskId, TaskDiagnostics& diagnostics) const
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        diagnostics = tasks_[taskId].diagnostics;

        return true;
    }

    // Clear counters without changing release times or enabled state.
    // Return false for an invalid ID.
    bool ResetTaskDiagnostics(uint32_t taskId)
    {
        if (!IsTaskUsed(taskId))
        {
            return false;
        }

        tasks_[taskId].diagnostics = TaskDiagnostics();

        return true;
    }

    /*
     * Return true only after Start(), for 0 < wcet < 2^31, when no enabled
     * task is released and the estimated finish crosses no latest-start point.
     * This is a snapshot based on the supplied WCET, not a time reservation.
     */
    bool CanRunBackground(Time wcet) const
    {
        if (!started_)
        {
            return false;
        }

        if ((wcet == 0u) ||
            !ValidDelta(wcet))
        {
            return false;
        }

        const Time now = now_();

        const Time backgroundFinish = now + wcet;

        for (uint32_t i = 0; i < MaxTasks; ++i)
        {
            const uint32_t bit = (1u << i);

            if ((enabledMask_ & bit) == 0u)
            {
                continue;
            }

            const Task& task = tasks_[i];

            /*
             * Background work is not allowed while any periodic
             * task is already released.
             */
            if (scheduler_detail::TimeReached(now, task.nextRelease))
            {
                return false;
            }

            const Time latestStart = task.nextRelease +
                task.allowedLateness;

            if (scheduler_detail::TimeAfter(backgroundFinish, latestStart))
            {
                return false;
            }
        }

        return true;
    }

private:
    static bool ValidDelta(Time value)
    {
        return value < 0x80000000u;
    }

    static bool ValidTaskParameters(Time period, Time offset, Time allowedLateness,
        Time relativeDeadline, Time wcet)
    {
        if (period == 0u)
        {
            return false;
        }

        if (relativeDeadline == 0u)
        {
            return false;
        }

        if (wcet == 0u)
        {
            return false;
        }

        if (!ValidDelta(period) ||
            !ValidDelta(offset) ||
            !ValidDelta(allowedLateness) ||
            !ValidDelta(relativeDeadline) ||
            !ValidDelta(wcet))
        {
            return false;
        }

        if (offset >= period)
        {
            return false;
        }

        /*
         * Multiple pending instances of the same periodic task
         * are not supported.
         */
        if (relativeDeadline > period)
        {
            return false;
        }

        if (wcet > relativeDeadline)
        {
            return false;
        }

        /*
         * A task starting at the latest allowed start time must
         * still be able to finish before its completion deadline.
         *
         * allowedLateness + wcet <= relativeDeadline
         *
         * This form avoids unsigned addition overflow.
         */
        if (allowedLateness >
            (relativeDeadline - wcet))
        {
            return false;
        }

        return true;
    }

    bool IsTaskUsed(uint32_t taskId) const
    {
        if (taskId >= MaxTasks)
        {
            return false;
        }

        const uint32_t bit = (1u << taskId);

        return (usedMask_ & bit) != 0u;
    }

    int SelectNextTask(Time now)
    {
        int best = -1;

        Time bestLatestStart = 0;
        Time bestDeadline = 0;

        for (uint32_t i = 0; i < MaxTasks; ++i)
        {
            const uint32_t bit = (1u << i);

            if ((enabledMask_ & bit) == 0u)
            {
                continue;
            }

            Task& task = tasks_[i];

            if (!scheduler_detail::TimeReached(now, task.nextRelease))
            {
                continue;
            }

            /*
             * Skip complete old periods instead of replaying them
             * as a catch-up burst.
             */
            const Time lateness = now - task.nextRelease;

            if (lateness >= task.period)
            {
                const Time skipped =
                    lateness / task.period;

                task.diagnostics.skippedCount += skipped;

                task.nextRelease += skipped * task.period;
            }

            const Time latestStart = task.nextRelease +
                task.allowedLateness;

            /*
             * Drop the current invocation if its allowed start
             * lateness has already been exceeded.
             */
            if (scheduler_detail::TimeAfter(now, latestStart))
            {
                ++task.diagnostics.startMissCount;

                task.nextRelease += task.period;

                continue;
            }

            const Time absoluteDeadline = task.nextRelease +
                task.relativeDeadline;

            /*
             * Select the task with the earliest latest-start
             * constraint.
             *
             * If two tasks have the same latest-start time,
             * use the earliest completion deadline as a
             * tie-breaker.
             */
            if ((best < 0) ||
                scheduler_detail::TimeBefore(latestStart, bestLatestStart) ||
                ((latestStart == bestLatestStart) &&
                 scheduler_detail::TimeBefore(absoluteDeadline, bestDeadline)))
            {
                best = static_cast<int>(i);

                bestLatestStart = latestStart;

                bestDeadline = absoluteDeadline;
            }
        }

        return best;
    }

    bool CanRunNow(uint32_t candidateId, Time now) const
    {
        const Task& candidate = tasks_[candidateId];

        const Time candidateFinish = now + candidate.wcet;

        /*
         * Since execution is non-preemptive, the selected candidate
         * must not occupy the CPU beyond another task's latest
         * allowed start time.
         */
        for (uint32_t i = 0; i < MaxTasks; ++i)
        {
            if (i == candidateId)
            {
                continue;
            }

            const uint32_t bit = (1u << i);

            if ((enabledMask_ & bit) == 0u)
            {
                continue;
            }

            const Task& other = tasks_[i];

            const Time otherLatestStart = other.nextRelease +
                other.allowedLateness;

            if (scheduler_detail::TimeAfter(candidateFinish, otherLatestStart))
            {
                return false;
            }
        }

        return true;
    }

    void Dispatch(uint32_t id)
    {
        Task& task = tasks_[id];

        const Time release = task.nextRelease;

        const Time deadline = release +
            task.relativeDeadline;

        /*
         * Read the clock immediately before entering the callback.
         *
         * This means scheduler selection and dispatch overhead is
         * included in the measured lateness.
         * The admission checks used the earlier Run() sample; they are not
         * repeated here. Budget for this gap and interrupt latency on target.
         */
        const Time executionStart = now_();

        const Time lateness = executionStart - release;

        task.diagnostics.lastLateness = lateness;

        if (lateness >
            task.diagnostics.maxLateness)
        {
            task.diagnostics.maxLateness = lateness;
        }

        task.callback(task.context);

        const Time finishTime = now_();

        const Time executionTime = finishTime - executionStart;

        task.diagnostics.lastExecutionTime = executionTime;

        if (executionTime >
            task.diagnostics.maxExecutionTime)
        {
            task.diagnostics.maxExecutionTime = executionTime;
        }

        ++task.diagnostics.runCount;

        /*
         * Detect a measured WCET violation.
         */
        if (executionTime > task.wcet)
        {
            ++task.diagnostics.wcetOverrunCount;
        }

        /*
         * Detect a completion deadline miss.
         */
        if (scheduler_detail::TimeAfter(finishTime, deadline))
        {
            ++task.diagnostics.deadlineMissCount;
        }

        /*
         * Preserve the ideal periodic timeline and avoid drift.
         *
         * Do not use:
         *
         * nextRelease = finishTime + period;
         */
        task.nextRelease = release + task.period;
    }

private:
    Now now_;

    Task tasks_[MaxTasks];

    uint32_t usedMask_;
    uint32_t enabledMask_;

    bool started_;
};

// C++11 requires a definition when the constant is odr-used (for example,
// when a caller binds it to a reference).
template <uint32_t MaxTasks>
constexpr uint32_t Scheduler<MaxTasks>::MAX_TASKS;

#endif // SCHEDULER_HPP
