#include "test_support.hpp"

#include <array>
#include <type_traits>

namespace scheduler_test
{

template <typename Capacity>
class SchedulerCapacityTest : public SchedulerTest
{
};

using Capacities = ::testing::Types<
    std::integral_constant<uint32_t, 1>,
    std::integral_constant<uint32_t, 8>,
    std::integral_constant<uint32_t, 32>>;
TYPED_TEST_SUITE(SchedulerCapacityTest, Capacities);

TYPED_TEST(SchedulerCapacityTest, FillsEverySlotAndRejectsOverflow)
{
    const uint32_t capacity = TypeParam::value;
    Scheduler<TypeParam::value> scheduler(&FakeNow);
    std::array<CallbackContext, TypeParam::value> contexts;
    EXPECT_EQ(Scheduler<TypeParam::value>::MAX_TASKS, capacity);

    for (uint32_t i = 0; i < capacity; ++i)
    {
        contexts[i].executionTime = 1;
        ASSERT_EQ(AddDefaultTask(scheduler, &contexts[i], 400, i * 10, 5, 7, 2),
                  static_cast<int>(i));
    }
    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 400, 0, 5, 7, 2), -1);
    scheduler.Start(0);

    // Exercise every mask bit, including bit 31, over two ideal periods.
    for (uint32_t period = 0; period < 2; ++period)
    {
        for (uint32_t i = 0; i < capacity; ++i)
        {
            gNow = period * 400 + i * 10;
            ASSERT_TRUE(scheduler.Run());
            EXPECT_EQ(contexts[i].callCount, period + 1);
            EXPECT_FALSE(scheduler.Run());
        }
    }
}

TYPED_TEST(SchedulerCapacityTest, RejectsIdsOutsideConfiguredCapacity)
{
    Scheduler<TypeParam::value> scheduler(&FakeNow);
    typename Scheduler<TypeParam::value>::TaskDiagnostics diagnostics;
    const uint32_t invalidIds[] = {TypeParam::value, 32, UINT32_MAX};
    for (uint32_t id : invalidIds)
    {
        EXPECT_FALSE(scheduler.DisableTask(id));
        EXPECT_FALSE(scheduler.EnableTask(id));
        EXPECT_FALSE(scheduler.EnableTask(id, 0));
        EXPECT_FALSE(scheduler.SetPeriod(id, 100));
        EXPECT_FALSE(scheduler.GetTaskDiagnostics(id, diagnostics));
        EXPECT_FALSE(scheduler.ResetTaskDiagnostics(id));
    }
}

TYPED_TEST(SchedulerCapacityTest, ControlsAndProtectsTheHighestSlot)
{
    const uint32_t capacity = TypeParam::value;
    Scheduler<TypeParam::value> scheduler(&FakeNow);
    std::array<CallbackContext, TypeParam::value> contexts;
    for (uint32_t i = 0; i < capacity; ++i)
    {
        ASSERT_EQ(AddDefaultTask(scheduler, &contexts[i]), static_cast<int>(i));
        ASSERT_TRUE(scheduler.DisableTask(i));
    }
    EXPECT_EQ(scheduler.AddTask(&NoOpCallback, nullptr, 100, 0, 20, 50, 10), -1);

    const uint32_t last = capacity - 1;
    scheduler.Start(0);
    EXPECT_TRUE(scheduler.CanRunBackground(1));
    ASSERT_TRUE(scheduler.SetPeriod(last, 200));
    ASSERT_TRUE(scheduler.EnableTask(last));
    EXPECT_TRUE(scheduler.CanRunBackground(220));
    EXPECT_FALSE(scheduler.CanRunBackground(221));
    gNow = 199;
    EXPECT_FALSE(scheduler.Run());
    gNow = 200;
    EXPECT_FALSE(scheduler.CanRunBackground(1));
    ASSERT_TRUE(scheduler.Run());
    EXPECT_EQ(contexts[last].callCount, 1u);

    ASSERT_TRUE(scheduler.DisableTask(last));
    ASSERT_TRUE(scheduler.EnableTask(last, 0));
    ASSERT_TRUE(scheduler.Run());
    typename Scheduler<TypeParam::value>::TaskDiagnostics diagnostics;
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(last, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 2u);
    ASSERT_TRUE(scheduler.ResetTaskDiagnostics(last));
    ASSERT_TRUE(scheduler.GetTaskDiagnostics(last, diagnostics));
    EXPECT_EQ(diagnostics.runCount, 0u);
}

TEST(SchedulerCapacityStorageTest, SmallerCapacitiesUseLessMemory)
{
    EXPECT_LT(sizeof(Scheduler<1>), sizeof(Scheduler<8>));
    EXPECT_LT(sizeof(Scheduler<8>), sizeof(Scheduler<32>));
}

} // namespace scheduler_test
