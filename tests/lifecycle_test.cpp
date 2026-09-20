#include "test_support.hpp"

namespace scheduler_test
{

/* Initial state */
TEST_F(SchedulerTest, RunReturnsFalseBeforeStart)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_FALSE(scheduler.Run());
}

TEST_F(SchedulerTest, BackgroundWorkIsRejectedBeforeStart)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_FALSE(scheduler.CanRunBackground(10));
}

/* Start and offset */
TEST_F(SchedulerTest, StartAppliesTaskOffset)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 100, 10);

    ASSERT_GE(id, 0);

    scheduler.Start(100);

    gNow = 109;

    EXPECT_FALSE(scheduler.Run());

    gNow = 110;

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(context.callCount, 1u);
}

TEST_F(SchedulerTest, StartIgnoresDisabledTask)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext enabledContext;
    CallbackContext disabledContext;

    const int enabled = AddDefaultTask(scheduler, &enabledContext, 100, 10);

    const int disabled = AddDefaultTask(scheduler, &disabledContext, 100, 20);

    ASSERT_GE(enabled, 0);
    ASSERT_GE(disabled, 0);

    EXPECT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(disabled)));

    scheduler.Start(100);

    gNow = 110;

    EXPECT_TRUE(scheduler.Run());

    gNow = 120;

    EXPECT_FALSE(scheduler.Run());

    EXPECT_EQ(enabledContext.callCount, 1u);

    EXPECT_EQ(disabledContext.callCount, 0u);
}

/* Enable / disable */
TEST_F(SchedulerTest, DisableStopsTask)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    EXPECT_FALSE(scheduler.Run());

    EXPECT_EQ(context.callCount, 0u);
}

TEST_F(SchedulerTest, EnableWithZeroDelayMakesTaskImmediatelyEligible)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    gNow = 500;

    ASSERT_TRUE(scheduler.EnableTask(static_cast<uint32_t>(id), 0));

    EXPECT_TRUE(scheduler.Run());

    ASSERT_EQ(context.startTimes.size(), 1u);

    EXPECT_EQ(context.startTimes[0], 500u);
}

TEST_F(SchedulerTest, DefaultEnableStartsAfterOnePeriod)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 100);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    gNow = 500;

    ASSERT_TRUE(scheduler.EnableTask(static_cast<uint32_t>(id)));

    gNow = 599;

    EXPECT_FALSE(scheduler.Run());

    gNow = 600;

    EXPECT_TRUE(scheduler.Run());
}

TEST_F(SchedulerTest, EnableWithDelayCanBeCalledBeforeStart)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context);

    ASSERT_GE(id, 0);

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    EXPECT_TRUE(scheduler.EnableTask(static_cast<uint32_t>(id), 25));
}

TEST_F(SchedulerTest, EnableRejectsInvalidDelay)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context);

    ASSERT_GE(id, 0);

    EXPECT_FALSE(scheduler.EnableTask(static_cast<uint32_t>(id), 0x80000000u));
}

/* SetPeriod */
TEST_F(SchedulerTest, PeriodCanOnlyBeChangedWhileDisabled)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 100, 30, 5, 10, 5);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    EXPECT_FALSE(scheduler.SetPeriod(static_cast<uint32_t>(id), 40));

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    EXPECT_FALSE(scheduler.SetPeriod(static_cast<uint32_t>(id), 0));

    EXPECT_FALSE(scheduler.SetPeriod(static_cast<uint32_t>(id), 0x80000000u));

    /*
     * relativeDeadline = 10, therefore period 5 is invalid.
     */
    EXPECT_FALSE(scheduler.SetPeriod(static_cast<uint32_t>(id), 5));

    /*
     * offset = 30, therefore period 20 is invalid.
     */
    EXPECT_FALSE(scheduler.SetPeriod(static_cast<uint32_t>(id), 20));

    EXPECT_TRUE(scheduler.SetPeriod(static_cast<uint32_t>(id), 40));

    gNow = 100;

    ASSERT_TRUE(scheduler.EnableTask(static_cast<uint32_t>(id)));

    gNow = 139;

    EXPECT_FALSE(scheduler.Run());

    gNow = 140;

    EXPECT_TRUE(scheduler.Run());
}

TEST_F(SchedulerTest, CustomEnableDelayStartsANewTimelineAndPreservesDiagnostics)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context);
    ASSERT_GE(id, 0);
    scheduler.Start(0);
    ASSERT_TRUE(scheduler.Run());

    ASSERT_TRUE(scheduler.DisableTask(id));
    ASSERT_TRUE(scheduler.SetPeriod(id, 200));
    gNow = 500;
    ASSERT_TRUE(scheduler.EnableTask(id, 25));
    gNow = 524;
    EXPECT_FALSE(scheduler.Run());
    gNow = 525;
    ASSERT_TRUE(scheduler.Run());
    gNow = 724;
    EXPECT_FALSE(scheduler.Run());
    gNow = 725;
    ASSERT_TRUE(scheduler.Run());

    EXPECT_EQ(context.startTimes, std::vector<Time>({0, 525, 725}));
    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 3u);
    EXPECT_EQ(diagnostics.skippedCount, 0u);
}

TEST_F(SchedulerTest, RestartReappliesOffsetWithoutClearingDiagnostics)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext context;
    const int id = AddDefaultTask(scheduler, &context, 100, 10);
    ASSERT_GE(id, 0);
    scheduler.Start(0);
    gNow = 10;
    ASSERT_TRUE(scheduler.Run());

    scheduler.Start(500);
    gNow = 509;
    EXPECT_FALSE(scheduler.Run());
    gNow = 510;
    ASSERT_TRUE(scheduler.Run());

    Scheduler<32>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(id, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 2u);
    EXPECT_EQ(context.startTimes, std::vector<Time>({10, 510}));
}

} // namespace scheduler_test
