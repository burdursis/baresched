#include "test_support.hpp"

namespace scheduler_test
{

namespace
{
Time AdvancingNow()
{
    const Time sample = gNow;
    gNow += 3;
    return sample;
}
} // namespace

TEST_F(SchedulerTest, FinishesAtDeadlineWithoutTimingViolations)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    context.executionTime = 10;
    const int id = AddDefaultTask(scheduler, &context, 100, 0, 20, 30, 10);
    ASSERT_GE(id, 0);
    scheduler.Start(0);

    gNow = 20; // Exactly the latest allowed start.
    ASSERT_TRUE(scheduler.Run());
    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.lastLateness, 20u);
    EXPECT_EQ(diagnostics.lastExecutionTime, 10u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);
}

TEST_F(SchedulerTest, BackgroundCanFinishExactlyAtLatestStart)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    ASSERT_GE(AddDefaultTask(scheduler, &context, 100, 20, 5, 20, 10), 0);
    scheduler.Start(0);

    EXPECT_TRUE(scheduler.CanRunBackground(25));
    EXPECT_FALSE(scheduler.CanRunBackground(26));
    EXPECT_EQ(context.callCount, 0u);
}

TEST_F(SchedulerTest, CandidateCanFinishExactlyAtOtherLatestStart)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext first;
    CallbackContext second;
    first.executionTime = 15;
    ASSERT_GE(AddDefaultTask(scheduler, &first, 100, 0, 0, 20, 15), 0);
    ASSERT_GE(AddDefaultTask(scheduler, &second, 100, 10, 5, 20, 5), 0);
    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());
    ASSERT_EQ(first.callCount, 1u);
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(second.startTimes, std::vector<Time>({15}));
}

TEST_F(SchedulerTest, StartUsesOffsetInsteadOfPreStartEnableDelay)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context, 100, 10);
    ASSERT_GE(id, 0);
    ASSERT_TRUE(scheduler.DisableTask(id));
    ASSERT_TRUE(scheduler.EnableTask(id, 25));
    scheduler.Start(100);

    gNow = 109;
    EXPECT_FALSE(scheduler.Run());
    gNow = 110;
    EXPECT_TRUE(scheduler.Run());
    EXPECT_EQ(context.startTimes, std::vector<Time>({110}));
}

TEST_F(SchedulerTest, CallbackDurationIsMeasuredAcrossClockWrap)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    context.executionTime = 20;
    const int id = AddDefaultTask(scheduler, &context, 100, 0, 0, 20, 20);
    ASSERT_GE(id, 0);
    gNow = 0xFFFFFFF0u;
    scheduler.Start(gNow);

    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(gNow, 4u);
    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.lastExecutionTime, 20u);
    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);
}

// Records the current snapshot-based admission limit; see docs/design.md.
TEST_F(SchedulerTest, DispatchOverheadIsMeasuredButStartWindowIsNotRechecked)
{
    Scheduler<32> scheduler(&AdvancingNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context, 100, 0, 1, 20, 10);
    ASSERT_GE(id, 0);
    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run()); // Selection sampled tick 0; dispatch sampled 3.
    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.lastLateness, 3u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
    EXPECT_EQ(diagnostics.runCount, 1u);
}

} // namespace scheduler_test
