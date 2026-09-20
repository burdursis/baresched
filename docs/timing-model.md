# Timing model

## Ticks and task parameters

`Scheduler<MaxTasks>::Time` is `uint32_t`. All timestamps, durations, and diagnostics use the
unit of the caller's free-running counter. A tick may be 10 ns, 100 ns, 1 us, or
1 ms; the scheduler does not perform physical-unit conversions.

| Parameter | Meaning |
| --- | --- |
| `period` | Ideal interval between releases |
| `offset` | Initial phase relative to the epoch passed to `Start()` |
| `allowedLateness` | Maximum permitted delay from release to callback start |
| `wcet` | Configured worst-case duration of one invocation |
| `relativeDeadline` | Completion limit measured from the ideal release |
| `nextRelease` | Ideal release of the current invocation, not the last finish time |

```text
first release      = epoch + offset
latestStart        = nextRelease + allowedLateness
absoluteDeadline   = nextRelease + relativeDeadline
estimated finish   = now + wcet
next release       = previous release + period
```

Example in microsecond ticks:

```text
period = 10000, offset = 0, allowedLateness = 50, wcet = 50, relativeDeadline = 100

release                 latest start                 deadline
10.000 ms               10.050 ms                    10.100 ms
   |<-- allowed lateness -->|<------ WCET budget ------>|
   |<---------------- relative deadline -------------->|
```

Starting at 10.050 ms and finishing at 10.100 ms is allowed. Boundary equality is
not a violation: start miss, WCET overrun, and deadline miss checks use strict
exceedance. Admission assumes the stated WCET and the clock snapshot described
in [design limits](design.md#admission-to-dispatch-gap).

## Validation

Registration requires a non-null callback, nonzero `period`, `relativeDeadline`,
and `wcet`, and every relative value below `0x80000000`. It also requires:

```text
offset < period
relativeDeadline <= period
wcet <= relativeDeadline
allowedLateness + wcet <= relativeDeadline
```

The last comparison is implemented as `allowedLateness <= relativeDeadline - wcet`
after validating WCET, avoiding addition overflow. `relativeDeadline <= period`
reflects the model of at most one pending invocation per task. Validation checks
individual parameters; it does not prove the entire task set is schedulable.

Offsets spread simultaneous releases without creating CPU capacity. With epoch
0, task A at period 5/offset 0 releases at 0, 5, 10, 15; task B at period 10/offset
1 releases at 1, 11, 21. A later `EnableTask` establishes a new timeline independent
of the original offset.

## Drift-free releases and missed work

After dispatch, the scheduler uses `nextRelease = release + period`, never
`finishTime + period`. Callback execution or lateness therefore does not move
the ideal periodic grid. See [timing logs](diagnostics.md#interval-jitter-and-start-lateness)
for the resulting positive/negative jitter peak pairs.

When a task is released, selection computes `lateness = now - nextRelease`.
If at least a full period has elapsed, it skips `lateness / period` releases,
adds that number to `skippedCount`, and advances `nextRelease` by those periods.
It then checks the remaining current release against its latest-start point.
If that point has passed, it increments `startMissCount` and advances one more
period. Skipped invocations are never replayed as a burst.

For `period=10`, `nextRelease=10`, and `now=35`:

| Release | Result |
| --- | --- |
| 10 | Skipped old invocation |
| 20 | Skipped old invocation |
| 30 | Current invocation; runs only if its start window and WCET admission allow it |
| 40 | Next release after processing the current invocation |

With allowed lateness 5, WCET 1, and deadline 6, tick 35 is still inside the
current start window. With allowed lateness 2, the tick-30 invocation is dropped.
The first case increments `skippedCount` by 2; the second also increments
`startMissCount` by 1. Other ready tasks and blocking checks may still delay dispatch.

## Counter wrap-around

A 32-bit counter naturally advances from `0xFFFFFFFF` to 0. Direct comparisons
such as `now >= target` do not work across this boundary. Internal helpers use
subtraction followed by signed interpretation:

```cpp
static inline bool TimeReached(uint32_t now, uint32_t target)
{
    return static_cast<int32_t>(now - target) >= 0;
}
```

`TimeBefore` uses `< 0`; `TimeAfter` uses `> 0`. The C++11 target must implement
unsigned-to-signed conversion with the expected signed-difference behavior.
Compared timestamps must be strictly less than half the range apart:

```text
delta < 2^31 ticks
```

For example, starting at `0xFFFFFFF0` with period 32 gives a next release of
`0x00000010`. The release is still in the future at `0x0000000F` and ready at
`0x00000010`.

| Tick duration | Half-range horizon, approximately |
| --- | --- |
| 10 ns | 21.47 seconds |
| 100 ns | 214.75 seconds |
| 1 us | 35.79 minutes |
| 1 ms | 24.86 days |

Keep idle gaps, callback durations, and the distances between all timestamps
being compared inside that horizon. Individual valid parameters can still form
an invalid combined horizon: for example, a future release plus allowed lateness
may be too far from another candidate's finish for signed ordering to be
unambiguous. Choose periods, delays, epochs, and tolerances so these comparisons
remain valid. The scheduler does not validate the complete horizon at runtime.

The clock must advance monotonically modulo uint32 wrap-around, without arbitrary
backward adjustments. Unit conversion is the application's responsibility and
must also avoid overflow.
