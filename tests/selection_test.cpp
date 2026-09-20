#include "test_support.hpp"

namespace scheduler_test
{

/* Selection policy */
TEST_F(SchedulerTest, SelectsTaskWithEarliestLatestStart)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext taskA;
    CallbackContext taskB;

    ASSERT_GE(AddDefaultTask(scheduler, &taskA, 100, 0, 20, 40, 1), 0);

    ASSERT_GE(AddDefaultTask(scheduler, &taskB, 100, 0, 10, 40, 1), 0);

    scheduler.Start(0);

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(taskB.callCount, 1u);

    EXPECT_EQ(taskA.callCount, 0u);

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(taskA.callCount, 1u);
}

TEST_F(SchedulerTest, UsesDeadlineAsTieBreaker)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext taskA;
    CallbackContext taskB;

    ASSERT_GE(AddDefaultTask(scheduler, &taskA, 100, 0, 10, 40, 1), 0);

    ASSERT_GE(AddDefaultTask(scheduler, &taskB, 100, 0, 10, 20, 1), 0);

    scheduler.Start(0);

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(taskB.callCount, 1u);
}

TEST_F(SchedulerTest, KeepsFirstTaskWhenTieBreakerDeadlineIsLater)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext taskA;
    CallbackContext taskB;

    ASSERT_GE(AddDefaultTask(scheduler, &taskA, 100, 0, 10, 20, 1), 0);

    ASSERT_GE(AddDefaultTask(scheduler, &taskB, 100, 0, 10, 40, 1), 0);

    scheduler.Start(0);

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(taskA.callCount, 1u);

    EXPECT_EQ(taskB.callCount, 0u);
}

/* Candidate WCET protection */
TEST_F(SchedulerTest, RejectsCandidateThatWouldBlockFutureTask)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext current;
    CallbackContext future;

    ASSERT_GE(AddDefaultTask(scheduler, &current, 100, 0, 50, 80, 20), 0);

    ASSERT_GE(AddDefaultTask(scheduler, &future, 100, 10, 5, 20, 5), 0);

    scheduler.Start(0);

    gNow = 0;

    EXPECT_FALSE(scheduler.Run());

    EXPECT_EQ(current.callCount, 0u);

    gNow = 10;

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(future.callCount, 1u);

    EXPECT_TRUE(scheduler.Run());

    EXPECT_EQ(current.callCount, 1u);
}

TEST_F(SchedulerTest, RejectsCandidateThatWouldBlockAlreadyReleasedTask)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext taskA;
    CallbackContext taskB;

    ASSERT_GE(AddDefaultTask(scheduler, &taskA, 100, 0, 10, 30, 20), 0);

    ASSERT_GE(AddDefaultTask(scheduler, &taskB, 100, 0, 15, 20, 1), 0);

    scheduler.Start(0);

    EXPECT_FALSE(scheduler.Run());

    EXPECT_EQ(taskA.callCount, 0u);

    EXPECT_EQ(taskB.callCount, 0u);
}

TEST_F(SchedulerTest, FullyEqualConstraintsKeepLowerSlotId)
{
    Scheduler<32> scheduler(&FakeNow);
    CallbackContext first;
    CallbackContext second;
    ASSERT_EQ(AddDefaultTask(scheduler, &first), 0);
    ASSERT_EQ(AddDefaultTask(scheduler, &second), 1);
    scheduler.Start(0);

    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(first.callCount, 1u);
    EXPECT_EQ(second.callCount, 0u);
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(second.callCount, 1u);
}

} // namespace scheduler_test
