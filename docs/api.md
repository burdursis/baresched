# Public API contract

Include `scheduler.hpp`. All APIs are synchronous; there is no locking, allocation,
or ownership transfer. Use them from one main-loop context. Callbacks must return
without throwing and must not recursively call `Run()` or `Start()`. A callback
may disable itself with `DisableTask(itsOwnId)`; defer period changes and re-enable
until after it returns. The timing unit and comparison constraints are described
in the [timing model](timing-model.md).

## Capacity and types

Instantiate the class as `Scheduler<MaxTasks>`, where `MaxTasks` is a `uint32_t`
compile-time value between 1 and 32 inclusive. A class-level `static_assert`
rejects capacities outside that range. There is no default template argument.
For example, use `Scheduler<8> scheduler(&ReadTicks)` for an eight-slot scheduler.
An existing application can migrate from `Scheduler` to `Scheduler<32>` or define
an application alias such as `using ApplicationScheduler = Scheduler<8>;`.

All methods retain their existing arguments and behavior. Every scan, task ID
bound, and the fixed task array uses the selected capacity. Both slot masks
remain `uint32_t`, and no dynamic allocation is introduced.

```cpp
using Time = uint32_t;
using Callback = void (*)(void*);
using Now = Time (*)();
```

These aliases belong to each `Scheduler<MaxTasks>` specialization. The public
constant `Scheduler<MaxTasks>::MAX_TASKS` is retained for compatibility and equals
`MaxTasks`; it no longer fixes the implementation at 32. Task IDs are slot indices
in `0..MaxTasks-1`, not pointers. Different capacities are distinct C++ types;
use the matching specialization's nested `Task` and `TaskDiagnostics` types.
`Task` is a public metadata type, but the scheduler's actual `tasks_` array is
private; constructing a `Task` does not register it.
`Task` and `TaskDiagnostics` default constructors zero their fields and initialize
pointers to null. Diagnostics are copied out through the API below.

## `Scheduler<MaxTasks>(Now now)`

Constructs an empty, stopped scheduler. `now` must point to a valid, quick,
consistent `Time (*)()` clock function; it is borrowed and must remain valid.
No time is read during construction. A null function is not rejected, but using
an operation that reads it is invalid. There is no status return.

## AddTask(callback, context, period, offset, allowedLateness, relativeDeadline, wcet)

Registers a task and enables its slot. Returns an ID in 0..MaxTasks-1, or -1 for invalid
parameters or exhausted capacity. On failure no task is added.

| Parameter | Meaning / constraint |
| --- | --- |
| `callback` | Non-null `void (*)(void*)` callback |
| `context` | Borrowed application state; null is allowed if the callback supports it |
| `period` | Release interval; `0 < period < 2^31` |
| `offset` | Initial phase; `0 <= offset < period` |
| `allowedLateness` | Start tolerance; may be zero |
| `relativeDeadline` | Release-relative completion deadline; `0 < deadline <= period` |
| `wcet` | Budget for one callback; `0 < wcet <= deadline` |

All timing values must be below `2^31`, and
`allowedLateness + wcet <= relativeDeadline` must hold. Keep callback/context valid
while the task can execute. Register before `Start()`; the function does not
reject calls after startup, but initializes the new release to 0. No remove API
exists, and disabling a task does not free its slot. Successful registration
initializes diagnostics to zero.

## Start(Time epoch)

Starts scheduling and sets every enabled task's release to `epoch + offset`.
`epoch` uses the same counter domain as `Now` and must satisfy the half-range
comparison assumptions. It does not read the clock, invoke callbacks, enable
disabled tasks, or reset diagnostics. Return type is `void`.

Calling it again restarts enabled timelines. A first delay configured by
`EnableTask` before startup is overwritten by `epoch + offset`. For immediate
startup, use `scheduler.Start(ReadTicks())` after registration.

## bool Run()

Selects and, if admitted, runs at most one enabled callback. Takes no parameters.
Returns true only when a callback ran; false before startup, when no eligible
candidate exists, or when the chosen candidate fails WCET protection.

Selection may skip or drop releases and update diagnostics even when false is
returned. Ready tasks are ordered by earliest latest start, earliest completion
deadline, and then lowest slot ID. A blocked candidate does not trigger a search
for another candidate. After execution the release advances by one period from
its prior ideal release. See the [admission-to-dispatch gap](design.md#admission-to-dispatch-gap)
for the limits of checks made using the selection timestamp.

## bool DisableTask(uint32_t taskId)

Removes a registered task from selection and admission checks. Returns false for
an unregistered or out-of-range ID; otherwise true, even if already disabled.
It does not interrupt execution, release the slot, change `nextRelease`, or clear
diagnostics. Calling it on the current task from its own callback is supported:
the callback finishes normally, `Run()` returns true, and `Dispatch()` updates
diagnostics and advances the release even though the task is now disabled.
The task remains excluded until externally enabled. Repeated disable calls are
safe and still return true. Do not call it concurrently from an ISR or thread.

## bool EnableTask(uint32_t taskId, Time firstDelay)

Enables a registered task and establishes a new first release. Returns false for
an invalid ID or `firstDelay >= 2^31`; failure leaves state unchanged. Delay 0 is
valid. Once started, it samples `Now` and sets `nextRelease = now + firstDelay`.
Before startup it stores `firstDelay`, which `Start()` later overwrites with
`epoch + offset`. Diagnostics are preserved.

Calling this on an already enabled task also resets its release. Runtime enable
does not apply the original phase offset. `EnableTask(id, 0)` makes the task
immediately eligible, subject to normal selection and admission.

Re-enable a self-disabled task from the main loop after its callback has returned.
Calling `EnableTask` on the executing task inside its callback is unsupported:
`Dispatch()` can overwrite the newly established release during final bookkeeping.
This also applies to `DisableTask` / `SetPeriod` / `EnableTask` inside one callback;
the calls can succeed without honoring the intended new first release.

## bool EnableTask(uint32_t taskId)

Equivalent to enabling with `firstDelay = task.period`. Returns false for an
invalid ID; otherwise true. It preserves diagnostics. After startup, enabling at
tick 100 with period 20 schedules the first release at 120, followed by 140, 160,
and so on. The same pre-start behavior as the explicit-delay overload applies.
The same restriction on re-enabling the executing task inside a callback applies.

## bool SetPeriod(uint32_t taskId, Time period)

Changes a registered, **disabled** task's period. Returns false for an invalid
ID, an enabled task, zero period, `period >= 2^31`, `period < relativeDeadline`,
or `period <= offset`. Failure leaves the old period intact. Success returns
true without changing the current release, other parameters, or diagnostics.

Recommended sequence, from the main loop after the callback returns: disable,
set period, enable, checking each result. A failed
period update leaves the task disabled with its old period. Default re-enable
waits one full new period. The [example](../examples/basic/main.cpp) implements
this sequence with error handling.

## bool CanRunBackground(Time wcet) const

Checks whether a short background operation can start now, using its configured
WCET in counter ticks. Returns false before startup, for `wcet == 0` or
`wcet >= 2^31`, when an enabled task is already released, or when the estimated
finish exceeds any enabled task's latest-start point. Equality is allowed.

Returns true for valid WCET if there are no enabled tasks. Does not run work,
advance releases, or update diagnostics. The decision is a snapshot, not a
reservation: perform the operation immediately and check again for each later
operation. The application must ensure that the stated WCET holds.

## bool GetTaskDiagnostics(uint32_t taskId, TaskDiagnostics& diagnostics) const

Copies a registered task's measurements into the caller's `diagnostics` object.
Returns true on success; false for an invalid ID, leaving the output unchanged.
It can read enabled or disabled tasks, before or after startup. It does not read
the clock or change scheduler state. Editing the copy does not edit internal
counters. Reading concurrently with scheduler execution is unsupported.

For a `Scheduler<8>` instance named `scheduler`:

```cpp
Scheduler<8>::TaskDiagnostics snapshot;
if (scheduler.GetTaskDiagnostics(taskId, snapshot))
{
    // Inspect snapshot.runCount, snapshot.maxLateness, and other fields.
}
```

## bool ResetTaskDiagnostics(uint32_t taskId)

Zeroes all diagnostics for a registered task and returns true. Returns false for
an invalid ID. Works for enabled or disabled tasks without reading the clock or
changing release times, configuration, or enabled state. Use between callbacks
when intentionally starting a new measurement interval. Disable/enable,
`SetPeriod`, and `Start` never reset diagnostics automatically.

See [diagnostic field meanings](diagnostics.md) for interpretation and limits.
