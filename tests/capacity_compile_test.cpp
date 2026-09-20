#include "../include/scheduler.hpp"

uint32_t Now()
{
    return 0;
}

int main()
{
    Scheduler<SCHEDULER_TEST_CAPACITY> scheduler(&Now);
    scheduler.Start(0);
    return scheduler.Run() ? 1 : 0;
}
