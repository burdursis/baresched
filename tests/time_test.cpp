#include "test_support.hpp"

namespace scheduler_test
{

/* Time helper tests */
TEST_F(SchedulerTest, TimeReachedHandlesNormalAndWrappedTime)
{
    EXPECT_TRUE(scheduler_detail::TimeReached(100, 100));

    EXPECT_TRUE(scheduler_detail::TimeReached(110, 100));

    EXPECT_FALSE(scheduler_detail::TimeReached(90, 100));

    EXPECT_TRUE(scheduler_detail::TimeReached(0x00000010u, 0xFFFFFFF0u));

    EXPECT_FALSE(scheduler_detail::TimeReached(0xFFFFFFF0u, 0x00000010u));
}

TEST_F(SchedulerTest, TimeBeforeHandlesNormalAndWrappedTime)
{
    EXPECT_TRUE(scheduler_detail::TimeBefore(10, 20));

    EXPECT_FALSE(scheduler_detail::TimeBefore(20, 10));

    EXPECT_TRUE(scheduler_detail::TimeBefore(0xFFFFFFF0u, 0x00000010u));
}

TEST_F(SchedulerTest, TimeAfterHandlesNormalAndWrappedTime)
{
    EXPECT_TRUE(scheduler_detail::TimeAfter(20, 10));

    EXPECT_FALSE(scheduler_detail::TimeAfter(10, 20));

    EXPECT_TRUE(scheduler_detail::TimeAfter(0x00000010u, 0xFFFFFFF0u));
}

/* Wrap-around */
TEST_F(SchedulerTest, PeriodicSchedulingWorksAcrossUint32WrapAround)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    const int id = AddDefaultTask(scheduler, &context, 32, 0, 8, 16, 4);

    ASSERT_GE(id, 0);

    const Time epoch = 0xFFFFFFF0u;

    scheduler.Start(epoch);

    gNow = epoch;

    ASSERT_TRUE(scheduler.Run());

    /*
     * 0xFFFFFFF0 + 32 wraps to 0x00000010.
     */
    gNow = 0x0000000Fu;

    EXPECT_FALSE(scheduler.Run());

    gNow = 0x00000010u;

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(context.callCount, 2u);
}

} // namespace scheduler_test
