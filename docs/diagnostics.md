# Diagnostics and timing logs

Use `GetTaskDiagnostics` to copy measurements and `ResetTaskDiagnostics` to begin a
new observation interval. The output type matches the scheduler capacity, such as
`Scheduler<8>::TaskDiagnostics` for a `Scheduler<8>` instance. Reading or editing the returned copy does not modify
the scheduler. All counters start at zero and remain intact across disable,
enable, period changes, and repeated `Start()` calls.

| Field | Meaning and use |
| --- | --- |
| `runCount` | Completed callback invocations; shows how much work actually ran |
| `skippedCount` | Complete old releases skipped; indicates the scheduler was behind by full periods |
| `startMissCount` | Current invocations dropped during selection because their start window expired |
| `deadlineMissCount` | Callbacks that finished strictly after `release + relativeDeadline` |
| `wcetOverrunCount` | Callbacks whose measured duration strictly exceeded configured WCET |
| `lastLateness` | Most recent measured start minus ideal release |
| `maxLateness` | Largest measured start lateness since registration/reset |
| `lastExecutionTime` | Most recent measured callback duration |
| `maxExecutionTime` | Largest measured callback duration since registration/reset |

Skipped/dropped work does not increment `runCount`, and does not overwrite the
last execution measurements. All fields are `uint32_t`, with durations expressed
in the configured tick unit. Counters wrap rather than saturate. Values copied
before a reset remain independent snapshots.

A self-disabling invocation still counts as an execution. After it returns,
`runCount`, execution-time measurements, and violation counters are updated as
usual; lateness was measured before entering the callback. Work performed after
`DisableTask(itsOwnId)` is included in the measured duration. The disabled task
does not accumulate skipped or start-miss counts on later scheduler iterations.
Externally re-enabling it preserves these measurements and maxima while starting
a new release timeline.

## Measurement boundaries

```mermaid
sequenceDiagram
    participant M as Main loop
    participant S as Scheduler
    participant C as Now
    participant T as Callback
    M->>S: Run()
    S->>C: now_()
    C-->>S: Selection timestamp
    Note over S: Selection and WCET admission
    S->>C: now_()
    C-->>S: executionStart
    Note over S: Record lateness
    S->>T: callback(context)
    T-->>S: return
    S->>C: now_()
    C-->>S: finishTime
    Note over S: Record duration and violations; advance ideal release
    S-->>M: true
```

`executionTime = finishTime - executionStart`. Selection overhead contributes to
lateness, not callback duration. Instrumentation near dispatch and interrupts
during the callback contribute to the measured duration. A WCET violation and a
deadline violation can both be recorded for the same callback, but they are
separate metrics: an overrun may still finish before a loose deadline.

Inspect maximum lateness together with start misses. An invocation dropped before
execution has no measured callback lateness. Conversely, the
[admission-to-dispatch gap](design.md#admission-to-dispatch-gap) can produce measured
lateness above the configured tolerance without incrementing `startMissCount`.
A low `maxLateness` alone therefore does not prove all releases were serviced.

Compare `maxExecutionTime` with configured WCET to find budget violations, then
investigate on hardware. A maximum seen during a test is an observation, not a
proof of worst-case execution time. Keep diagnostic printing outside timing-critical
callbacks; formatting and serial output can themselves block.

## Interval jitter and start lateness

Suppose ideal releases are 10.000, 20.000, 30.000, and 40.000 ms, and actual starts
are 10.000, 20.000, 30.100, and 40.000 ms. The intervals between starts are:

```text
10.000 ms, 10.100 ms, 9.900 ms
```

The interval jitter relative to a 10 ms period is 0, +0.100, and -0.100 ms.
The negative peak compensates for the previous delay because the scheduler
returns to the ideal release grid. It does not indicate a release earlier than
its ideal time. The start lateness values are 0, 0, +0.100, and 0 ms.

```text
interval       = currentStart - previousStart
intervalJitter = interval - period
startLateness  = actualStart - idealRelease
```

Compute interval jitter in a signed type wide enough for the data. It is not
stored by the scheduler; log start timestamps externally if needed. A constant
phase error of +0.100 ms produces perfect 10 ms intervals but nonzero lateness at
every release. Conversely, skipped invocations can make intervals several periods
long without giving the next executed callback a large lateness. Interpret logs
with skipped/missed counts and the relevant enable/restart epochs.
