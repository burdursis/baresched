#ifndef SCHEDULER_TEST_SUPPORT_HPP
#define SCHEDULER_TEST_SUPPORT_HPP

#include "scheduler.hpp"

#include <gtest/gtest.h>

#include <stdint.h>
#include <vector>

namespace scheduler_test
{

using Time = Scheduler<32>::Time;

extern Time gNow;

inline Time FakeNow()
{
    return gNow;
}

struct CallbackContext
{
    uint32_t callCount;
    Time executionTime;

    std::vector<Time> startTimes;

    CallbackContext()
        : callCount(0), executionTime(0), startTimes()
    {
    }
};

inline void TestCallback(void* context)
{
    CallbackContext* callbackContext = static_cast<CallbackContext*>(context);

    ++callbackContext->callCount;

    callbackContext->startTimes.push_back(gNow);

    gNow += callbackContext->executionTime;
}

inline void NoOpCallback(void*)
{
}

template <uint32_t MaxTasks>
inline int AddDefaultTask(
    Scheduler<MaxTasks>& scheduler, CallbackContext* context, Time period = 100, Time offset = 0,
    Time allowedLateness = 20, Time relativeDeadline = 50, Time wcet = 10)
{
    return scheduler.AddTask(&TestCallback, context, period,
        offset, allowedLateness, relativeDeadline, wcet);
}

class SchedulerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        gNow = 0;
    }
};

} // namespace scheduler_test

#endif // SCHEDULER_TEST_SUPPORT_HPP
