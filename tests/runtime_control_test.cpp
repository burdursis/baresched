#include "test_support.hpp"

namespace scheduler_test
{
namespace
{
using RuntimeScheduler = Scheduler<8>;

struct SelfDisablingContext
{
    RuntimeScheduler& scheduler;
    uint32_t taskId;
    Time executionTime;
    bool disableOnNextCall;
    bool disableSucceeded;
    uint32_t completedCalls;
    std::vector<Time> startTimes;

    explicit SelfDisablingContext(RuntimeScheduler& owner)
        : scheduler(owner),
          taskId(RuntimeScheduler::MAX_TASKS),
          executionTime(7),
          disableOnNextCall(true),
          disableSucceeded(false),
          completedCalls(0)
    {
    }
};

void SelfDisablingCallback(void* data)
{
    SelfDisablingContext& context = *static_cast<SelfDisablingContext*>(data);
    context.startTimes.push_back(gNow);
    if (context.disableOnNextCall)
    {
        context.disableSucceeded = context.scheduler.DisableTask(context.taskId);
        context.disableOnNextCall = false;
    }

    // Disabling only prevents future dispatches; this invocation finishes normally.
    gNow += context.executionTime;
    ++context.completedCalls;
}
} // namespace

class RuntimeControlTest : public SchedulerTest
{
protected:
    RuntimeScheduler scheduler;
    SelfDisablingContext context;

    RuntimeControlTest() : scheduler(&FakeNow), context(scheduler)
    {
    }

    void RegisterTask(RuntimeScheduler::Callback callback = &SelfDisablingCallback)
    {
        const int id = scheduler.AddTask(callback, &context, 100, 0, 20, 50, 10);
        ASSERT_GE(id, 0);
        context.taskId = static_cast<uint32_t>(id);
    }
};

TEST_F(RuntimeControlTest, SelfDisableFinishesCurrentInvocationAndPreventsLaterDispatches)
{
    RegisterTask();
    scheduler.Start(0);
    gNow = 5;

    ASSERT_TRUE(scheduler.Run());
    EXPECT_TRUE(context.disableSucceeded);
    EXPECT_EQ(context.completedCalls, 1u);
    EXPECT_EQ(gNow, 12u); // The callback continued after DisableTask returned.
    EXPECT_FALSE(scheduler.Run());

    for (Time now : {100u, 200u, 1000u})
    {
        gNow = now;
        EXPECT_FALSE(scheduler.Run());
        EXPECT_TRUE(scheduler.CanRunBackground(10));
    }
    EXPECT_EQ(context.startTimes, std::vector<Time>({5}));
    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 1u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
}

TEST_F(RuntimeControlTest, SelfDisablingInvocationUpdatesLastMeasurementsAndPreservesMaxima)
{
    RegisterTask();
    context.disableOnNextCall = false;
    context.executionTime = 9;
    scheduler.Start(0);
    gNow = 12;
    ASSERT_TRUE(scheduler.Run());

    context.disableOnNextCall = true;
    context.executionTime = 6;
    gNow = 104;
    ASSERT_TRUE(scheduler.Run());
    EXPECT_TRUE(context.disableSucceeded);

    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 2u);
    EXPECT_EQ(diagnostics.lastExecutionTime, 6u);
    EXPECT_EQ(diagnostics.maxExecutionTime, 9u);
    EXPECT_EQ(diagnostics.lastLateness, 4u);
    EXPECT_EQ(diagnostics.maxLateness, 12u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);
    gNow = 200;
    EXPECT_FALSE(scheduler.Run());
    EXPECT_EQ(context.completedCalls, 2u);
}

TEST_F(RuntimeControlTest, SelfDisableDoesNotBlockOrShiftAnotherTasksPeriodicReleases)
{
    RegisterTask();
    CallbackContext other;
    other.executionTime = 2;
    const int otherId = AddDefaultTask(scheduler, &other, 50, 20, 5, 10, 2);
    ASSERT_GE(otherId, 0);
    scheduler.Start(0);
    gNow = 5;
    ASSERT_TRUE(scheduler.Run());
    ASSERT_TRUE(context.disableSucceeded);

    for (Time release : {20u, 70u, 120u, 170u, 220u})
    {
        gNow = release - 1;
        EXPECT_FALSE(scheduler.Run());
        gNow = release;
        ASSERT_TRUE(scheduler.Run());
        EXPECT_FALSE(scheduler.Run());
    }
    EXPECT_EQ(context.completedCalls, 1u);
    EXPECT_EQ(other.startTimes, std::vector<Time>({20, 70, 120, 170, 220}));
    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(otherId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 5u);
    EXPECT_EQ(diagnostics.maxExecutionTime, 2u);
    EXPECT_EQ(diagnostics.maxLateness, 0u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
}

TEST_F(RuntimeControlTest, DisablingTwiceInsideOneCallbackIsIdempotent)
{
    RegisterTask([](void* data)
    {
        SelfDisablingCallback(data);
        SelfDisablingContext& task = *static_cast<SelfDisablingContext*>(data);
        EXPECT_TRUE(task.disableSucceeded);
        EXPECT_TRUE(task.scheduler.DisableTask(task.taskId));
    });
    scheduler.Start(0);
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(context.completedCalls, 1u);
    EXPECT_TRUE(scheduler.DisableTask(context.taskId)); // Also safe externally.
    gNow = 100;
    EXPECT_FALSE(scheduler.Run());
    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 1u);
    EXPECT_EQ(diagnostics.lastExecutionTime, 7u);
}

TEST_F(RuntimeControlTest, InvalidDisableIdsInsideCallbackAreRejectedWithoutAffectingOtherTasks)
{
    RegisterTask([](void* data)
    {
        SelfDisablingContext& task = *static_cast<SelfDisablingContext*>(data);
        EXPECT_FALSE(task.scheduler.DisableTask(7)); // In range, but unregistered.
        EXPECT_FALSE(task.scheduler.DisableTask(RuntimeScheduler::MAX_TASKS));
        EXPECT_FALSE(task.scheduler.DisableTask(UINT32_MAX));
        SelfDisablingCallback(data);
    });
    CallbackContext other;
    ASSERT_EQ(AddDefaultTask(scheduler, &other, 100, 20), 1);
    scheduler.Start(0);
    ASSERT_TRUE(scheduler.Run());
    EXPECT_TRUE(context.disableSucceeded);
    gNow = 20;
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(other.callCount, 1u);
    EXPECT_EQ(context.completedCalls, 1u);
}

TEST_F(RuntimeControlTest, ExternalDefaultEnableAfterSelfDisableWaitsAFullPeriodAndKeepsDiagnostics)
{
    RegisterTask();
    scheduler.Start(0);
    gNow = 5;
    ASSERT_TRUE(scheduler.Run());
    ASSERT_TRUE(context.disableSucceeded);

    gNow = 250;
    ASSERT_TRUE(scheduler.EnableTask(context.taskId));
    EXPECT_FALSE(scheduler.Run());
    gNow = 349;
    EXPECT_FALSE(scheduler.Run());
    gNow = 350;
    ASSERT_TRUE(scheduler.Run());
    gNow = 449;
    EXPECT_FALSE(scheduler.Run());
    gNow = 450;
    ASSERT_TRUE(scheduler.Run());

    EXPECT_EQ(context.startTimes, std::vector<Time>({5, 350, 450}));
    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 3u);
    EXPECT_EQ(diagnostics.lastLateness, 0u);
    EXPECT_EQ(diagnostics.maxLateness, 5u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
}

TEST_F(RuntimeControlTest, ExternalImmediateEnableAfterSelfDisableStartsANewPeriodicTimeline)
{
    RegisterTask();
    scheduler.Start(0);
    ASSERT_TRUE(scheduler.Run());
    ASSERT_TRUE(context.disableSucceeded);

    gNow = 250;
    ASSERT_TRUE(scheduler.EnableTask(context.taskId, 0));
    ASSERT_TRUE(scheduler.Run());
    EXPECT_FALSE(scheduler.Run());
    gNow = 349;
    EXPECT_FALSE(scheduler.Run());
    gNow = 350;
    ASSERT_TRUE(scheduler.Run());

    EXPECT_EQ(context.startTimes, std::vector<Time>({0, 250, 350}));
    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 3u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
}

TEST_F(RuntimeControlTest, SelfDisableMeasuresAnInvocationThatFinishesAcrossUint32Wrap)
{
    RegisterTask();
    const Time epoch = 0xFFFFFFF0u;
    gNow = epoch;
    scheduler.Start(epoch);
    gNow = 0xFFFFFFFDu; // Thirteen ticks late; seven ticks of work wrap to 4.
    ASSERT_TRUE(scheduler.Run());
    EXPECT_TRUE(context.disableSucceeded);
    EXPECT_EQ(context.completedCalls, 1u);
    EXPECT_EQ(gNow, 4u);

    RuntimeScheduler::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(context.taskId, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 1u);
    EXPECT_EQ(diagnostics.lastExecutionTime, 7u);
    EXPECT_EQ(diagnostics.maxExecutionTime, 7u);
    EXPECT_EQ(diagnostics.lastLateness, 13u);
    EXPECT_EQ(diagnostics.maxLateness, 13u);
    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);
    gNow = 84; // The old periodic release wrapped too; the task remains disabled.
    EXPECT_FALSE(scheduler.Run());

    ASSERT_TRUE(scheduler.EnableTask(context.taskId, 0));
    ASSERT_TRUE(scheduler.Run());
    gNow = 183;
    EXPECT_FALSE(scheduler.Run());
    gNow = 184;
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(context.startTimes, std::vector<Time>({0xFFFFFFFDu, 84, 184}));
}

TEST_F(RuntimeControlTest, ExternalDefaultEnableAfterSelfDisableSchedulesAcrossUint32Wrap)
{
    RegisterTask();
    gNow = 0xFFFFFF00u;
    scheduler.Start(gNow);
    ASSERT_TRUE(scheduler.Run());
    ASSERT_TRUE(context.disableSucceeded);

    gNow = 0xFFFFFFF0u;
    ASSERT_TRUE(scheduler.EnableTask(context.taskId)); // +100 wraps to tick 84.
    EXPECT_FALSE(scheduler.Run());
    gNow = 83;
    EXPECT_FALSE(scheduler.Run());
    gNow = 84;
    ASSERT_TRUE(scheduler.Run());
    gNow = 183;
    EXPECT_FALSE(scheduler.Run());
    gNow = 184;
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(context.startTimes, std::vector<Time>({0xFFFFFF00u, 84, 184}));
}

// Characterization of an UNSUPPORTED pattern, not a promise of callback reconfiguration.
// Any intentional support must update Dispatch() and this test together.
TEST_F(RuntimeControlTest, UnsupportedCallbackReconfigurationHasItsEnableReleaseOverwrittenByDispatch)
{
    RegisterTask([](void* data)
    {
        SelfDisablingContext& task = *static_cast<SelfDisablingContext*>(data);
        task.startTimes.push_back(gNow);
        gNow += 10;
        if (task.completedCalls == 0)
        {
            EXPECT_TRUE(task.scheduler.DisableTask(task.taskId));
            EXPECT_TRUE(task.scheduler.SetPeriod(task.taskId, 200));
            EXPECT_TRUE(task.scheduler.EnableTask(task.taskId));
            // At tick 15, EnableTask requests a first release at 15 + 200 = 215.
            EXPECT_EQ(gNow, 15u);
        }
        ++task.completedCalls;
    });
    scheduler.Start(0);
    gNow = 5;
    ASSERT_TRUE(scheduler.Run());

    gNow = 199;
    EXPECT_FALSE(scheduler.Run());
    gNow = 200;
    // Dispatch used the captured release 0 plus the NEW period 200, overwriting 215.
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(context.startTimes, std::vector<Time>({5, 200}));
    gNow = 215;
    EXPECT_FALSE(scheduler.Run());
    gNow = 400;
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(context.startTimes, std::vector<Time>({5, 200, 400}));
}

} // namespace scheduler_test
