#include "test_support.hpp"

namespace scheduler_test
{

/* Background work */
TEST_F(SchedulerTest, BackgroundWorkIsRejectedWhenTaskIsReady)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    ASSERT_GE(AddDefaultTask(scheduler, &context), 0);

    scheduler.Start(0);

    EXPECT_FALSE(scheduler.CanRunBackground(1));
}

TEST_F(SchedulerTest, BackgroundWorkIsAllowedWhenEnoughSlackExists)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    ASSERT_GE(AddDefaultTask(scheduler, &context, 100, 0, 10, 30, 5), 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());

    /*
     * Next release = 100
     * latest start = 110
     */
    gNow = 0;

    EXPECT_TRUE(scheduler.CanRunBackground(110));
}

TEST_F(SchedulerTest, BackgroundWorkIsRejectedWhenItWouldCrossLatestStart)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    ASSERT_GE(AddDefaultTask(scheduler, &context, 100, 0, 10, 30, 5), 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());

    gNow = 0;

    EXPECT_FALSE(scheduler.CanRunBackground(111));
}

TEST_F(SchedulerTest, BackgroundRejectsInvalidWcet)
{
    Scheduler<32> scheduler(&FakeNow);

    scheduler.Start(0);

    EXPECT_FALSE(scheduler.CanRunBackground(0));

    EXPECT_FALSE(scheduler.CanRunBackground(0x80000000u));
}

TEST_F(SchedulerTest, BackgroundIgnoresDisabledTasks)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context);

    ASSERT_GE(id, 0);

    scheduler.Start(0);

    ASSERT_TRUE(scheduler.DisableTask(static_cast<uint32_t>(id)));

    EXPECT_TRUE(scheduler.CanRunBackground(10));
}

} // namespace scheduler_test
