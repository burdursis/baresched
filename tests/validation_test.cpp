#include "test_support.hpp"

namespace scheduler_test
{

/* AddTask validation */
TEST_F(SchedulerTest, AddTaskRejectsNullCallback)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(nullptr, nullptr, 100, 0, 10, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsZeroPeriod)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 0, 0, 10, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsZeroDeadline)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 0, 0, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsZeroWcet)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 20, 0), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsInvalidPeriodDelta)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 0x80000000u, 0, 10, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsInvalidOffsetDelta)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0x80000000u, 10, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsInvalidAllowedLatenessDelta)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 0x80000000u, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsInvalidDeadlineDelta)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 0x80000000u, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsInvalidWcetDelta)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 20, 0x80000000u), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsOffsetEqualToPeriod)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 100, 10, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsDeadlineGreaterThanPeriod)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 101, 5), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsWcetGreaterThanDeadline)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 0, 20, 21), -1);
}

TEST_F(SchedulerTest, AddTaskRejectsImpossibleAllowedLateness)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 16, 20, 5), -1);
}

TEST_F(SchedulerTest, AddTaskAcceptsValidConfiguration)
{
    Scheduler<32> scheduler(&FakeNow);

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 20, 5), 0);
}

/* Capacity */
TEST_F(SchedulerTest, SupportsExactlyThirtyTwoTasks)
{
    Scheduler<32> scheduler(&FakeNow);

    for (uint32_t i = 0;
         i < Scheduler<32>::MAX_TASKS;
         ++i)
    {
        EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 20,
                5), static_cast<int>(i));
    }

    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 10, 20, 5), -1);
}

/* Invalid task IDs */
TEST_F(SchedulerTest, PublicApisRejectInvalidTaskIds)
{
    Scheduler<32> scheduler(&FakeNow);

    CallbackContext context;

    ASSERT_EQ(AddDefaultTask(scheduler, &context), 0);

    Scheduler<32>::TaskDiagnostics diagnostics;

    EXPECT_FALSE(scheduler.DisableTask(1));

    EXPECT_FALSE(scheduler.DisableTask(Scheduler<32>::MAX_TASKS));

    EXPECT_FALSE(scheduler.EnableTask(1));

    EXPECT_FALSE(scheduler.EnableTask(Scheduler<32>::MAX_TASKS));

    EXPECT_FALSE(scheduler.EnableTask(1, 0));

    EXPECT_FALSE(scheduler.SetPeriod(1, 100));

    EXPECT_FALSE(scheduler.GetTaskDiagnostics(1, diagnostics));

    EXPECT_FALSE(scheduler.GetTaskDiagnostics(Scheduler<32>::MAX_TASKS, diagnostics));

    EXPECT_FALSE(scheduler.ResetTaskDiagnostics(1));
}

} // namespace scheduler_test
