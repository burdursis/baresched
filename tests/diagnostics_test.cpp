#include "test_support.hpp"

namespace scheduler_test
{

/* Diagnostics */
TEST_F(SchedulerTest, TracksRunCountLatenessAndExecutionTime)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    context.executionTime = 5;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 20, 50, 15);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    gNow = 0;

    ASSERT_TRUE(scheduler.Run());

    context.executionTime = 10;

    gNow = 105;

    ASSERT_TRUE(scheduler.Run());

    context.executionTime = 7;

    gNow = 202;

    ASSERT_TRUE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics diagnostics;

    ASSERT_TRUE(scheduler.GetTaskDiagnostics(static_cast<uint32_t>(id), diagnostics));

    EXPECT_EQ(diagnostics.runCount, 3u);

    EXPECT_EQ(diagnostics.lastLateness, 2u);

    EXPECT_EQ(diagnostics.maxLateness, 5u);

    EXPECT_EQ(diagnostics.lastExecutionTime, 7u);

    EXPECT_EQ(diagnostics.maxExecutionTime, 10u);

    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);

    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
}

TEST_F(SchedulerTest, DetectsWcetOverrunAndDeadlineMiss)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    context.executionTime = 25;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 10, 20, 5);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics diagnostics;

    ASSERT_TRUE(scheduler.GetTaskDiagnostics(static_cast<uint32_t>(id), diagnostics));

    EXPECT_EQ(diagnostics.runCount, 1u);

    EXPECT_EQ(diagnostics.lastExecutionTime, 25u);

    EXPECT_EQ(diagnostics.maxExecutionTime, 25u);

    EXPECT_EQ(diagnostics.wcetOverrunCount, 1u);

    EXPECT_EQ(diagnostics.deadlineMissCount, 1u);
}

TEST_F(SchedulerTest, ResetDiagnosticsClearsAllCounters)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    context.executionTime = 25;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 10, 20, 5);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());

    ASSERT_TRUE(scheduler.ResetTaskDiagnostics(static_cast<uint32_t>(id)));

    Scheduler<32>::TaskDiagnostics diagnostics;

    ASSERT_TRUE(scheduler.GetTaskDiagnostics(static_cast<uint32_t>(id), diagnostics));

    EXPECT_EQ(diagnostics.runCount, 0u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
    EXPECT_EQ(diagnostics.deadlineMissCount, 0u);
    EXPECT_EQ(diagnostics.wcetOverrunCount, 0u);
    EXPECT_EQ(diagnostics.lastLateness, 0u);
    EXPECT_EQ(diagnostics.maxLateness, 0u);
    EXPECT_EQ(diagnostics.lastExecutionTime, 0u);
    EXPECT_EQ(diagnostics.maxExecutionTime, 0u);
}

TEST_F(SchedulerTest, DiagnosticsAreSnapshotsAndResetPreservesNextRelease)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context);
    ASSERT_GE(id, 0);
    scheduler.Start(0);
    ASSERT_TRUE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics snapshot;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, snapshot));
    snapshot.runCount = 99;
    EXPECT_FALSE(scheduler.GetTaskDiagnostics(Scheduler<32>::MAX_TASKS, snapshot));
    EXPECT_EQ(snapshot.runCount, 99u); // Failed reads leave the output intact.
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, snapshot));
    EXPECT_EQ(snapshot.runCount, 1u); // Editing a snapshot does not edit the task.

    ASSERT_TRUE(scheduler.ResetTaskDiagnostics(id));
    gNow = 99;
    EXPECT_FALSE(scheduler.Run());
    gNow = 100;
    ASSERT_TRUE(scheduler.Run());
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, snapshot));
    EXPECT_EQ(snapshot.runCount, 1u);
    EXPECT_EQ(context.callCount, 2u);
}

} // namespace scheduler_test
