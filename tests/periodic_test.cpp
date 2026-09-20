#include "test_support.hpp"

namespace scheduler_test
{

/* Periodic behavior and drift */
TEST_F(SchedulerTest, PeriodicTaskDoesNotDrift)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    context.executionTime = 5;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 20, 50, 15);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    gNow = 0;

    EXPECT_TRUE(scheduler.Run());

    context.executionTime = 10;

    gNow = 105;

    EXPECT_TRUE(scheduler.Run());

    context.executionTime = 7;

    gNow = 202;

    EXPECT_TRUE(scheduler.Run());

    ASSERT_EQ(context.startTimes.size(), 3u);

    EXPECT_EQ(context.startTimes[0], 0u);

    EXPECT_EQ(context.startTimes[1], 105u);

    EXPECT_EQ(context.startTimes[2], 202u);

    gNow = 299;

    EXPECT_FALSE(scheduler.Run());

    gNow = 300;

    EXPECT_TRUE(scheduler.Run());
}

/* Miss handling */
TEST_F(SchedulerTest, SkipsOldInvocationsAndDoesNotCatchUp)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 20, 50, 10);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    gNow = 250;

    EXPECT_FALSE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics diagnostics;

    ASSERT_TRUE(scheduler.GetTaskDiagnostics(static_cast<uint32_t>(id), diagnostics));

    EXPECT_EQ(diagnostics.skippedCount, 2u);

    EXPECT_EQ(diagnostics.startMissCount, 1u);

    EXPECT_EQ(diagnostics.runCount, 0u);

    gNow = 299;

    EXPECT_FALSE(scheduler.Run());

    gNow = 300;

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(context.callCount, 1u);
}

TEST_F(SchedulerTest, DropsInvocationWhenAllowedLatenessIsExceeded)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 100, 0, 20, 50, 10);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    gNow = 21;

    EXPECT_FALSE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics diagnostics;

    ASSERT_TRUE(scheduler.GetTaskDiagnostics(static_cast<uint32_t>(id), diagnostics));

    EXPECT_EQ(diagnostics.startMissCount, 1u);

    EXPECT_EQ(diagnostics.runCount, 0u);

    gNow = 100;

    EXPECT_TRUE(scheduler.Run());
}

TEST_F(SchedulerTest, SkipsOldPeriodsButRunsCurrentInvocationWithinItsWindow)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context);
    ASSERT_GE(id, 0);
    scheduler.Start(0);
    gNow = 210;

    ASSERT_TRUE(scheduler.Run());
    EXPECT_FALSE(scheduler.Run()); // No replay of the skipped invocations.
    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.skippedCount, 2u);
    EXPECT_EQ(diagnostics.startMissCount, 0u);
    EXPECT_EQ(diagnostics.lastLateness, 10u);
    EXPECT_EQ(context.startTimes, std::vector<Time>({210}));

    gNow = 300;
    EXPECT_TRUE(scheduler.Run());
}

} // namespace scheduler_test
