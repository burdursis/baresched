# Build, test, and coverage

## Requirements and setup

- C++11 compiler for the library/example and CMake 3.20 or later (the existing minimum is retained).
- GNU Make and a POSIX shell for convenience targets.
- GoogleTest with its CMake package (`GTest::gtest`) and a compatible host compiler for tests.
- GCC, a matching `gcov`, and `gcovr` for coverage.

For Debian/Ubuntu hosts, install the distribution packages:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake libgtest-dev gcovr
```

The workflow has been verified with GCC 13.3, CMake 3.28, GoogleTest 1.14, and
gcovr 7.0. Other hosts can provide these tools through their package manager.
CMake itself never downloads dependencies. If GoogleTest is installed in a custom
prefix, set `CMAKE_PREFIX_PATH` or `GTest_DIR` when configuring.

## Normal build

Run from the repository root:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel 2
ctest --test-dir build/debug --output-on-failure
./build/debug/examples/basic/scheduler_example
```

The example compiles as strict C++11, and the production header is also checked
with C++11, exceptions disabled, and RTTI disabled. The test target requests at
least C++11; GoogleTest may raise its effective language standard through imported
compile features. The installed GoogleTest 1.14 package requires C++14, so host
tests use C++14 in this environment. This does not affect firmware consumers of
the header. Both host targets disable compiler extensions.
GCC/Clang targets use `-Wall -Wextra -Wpedantic`; MSVC targets use `/W4`.
Compiler and coverage options are target-specific. Normal builds are not
instrumented and do not require gcovr.

| CMake option | Default | Effect |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Find GoogleTest, build `scheduler_tests`, register CTest cases |
| `SCHEDULER_BUILD_EXAMPLES` | `ON` | Build the deterministic `scheduler_example` host program |
| `SCHEDULER_ENABLE_COVERAGE` | `OFF` | Instrument the test executable; requires testing and GCC |
| `SCHEDULER_GCOV_EXECUTABLE` | `gcov` | Select the gcov executable matching the coverage compiler |

`include(CTest)` enables testing when `BUILD_TESTING` is on. The example is also
a CTest smoke test when both examples and testing are enabled. No tests or
GoogleTest dependency are added when testing is off.

## Convenience Make targets

```sh
make help
make build
make test
make example
make coverage
make clean
```

| Target | Action / output |
| --- | --- |
| `configure` | Configure the normal tree, default `build/debug` |
| `build` | Configure and build tests and the host example |
| `test` | Build, then run all CTest cases |
| `example` | Build, then run `scheduler_example` |
| `coverage` | Instrument host tests in `build/coverage`, run them, and produce reports |
| `clean` | Clean compiled targets in normal/coverage trees; keep CMake caches and reports |

```sh
make test JOBS=4
make test BUILD_DIR=build/release BUILD_TYPE=Release
make coverage CMAKE_ARGS='-DCMAKE_CXX_COMPILER=g++ -DSCHEDULER_GCOV_EXECUTABLE=gcov'
```

The wrapper always enables tests and examples for normal builds; its coverage
build disables the example so reports come from scheduler unit tests only.
Use direct CMake for other option combinations. `BUILD_DIR` and `COVERAGE_DIR`
must be separate, preferably non-nested trees. Use a new directory when changing
compilers. For multi-configuration generators, use direct CMake commands with
`--config Debug` for build and `-C Debug` for CTest.

## Firmware/library integration

```sh
cmake -S . -B build/library -DBUILD_TESTING=OFF -DSCHEDULER_BUILD_EXAMPLES=OFF
cmake --build build/library
```

The `scheduler::scheduler` library is an `INTERFACE` target. It propagates the
include path and C++11 requirement without producing a binary or requiring host
I/O. To consume it from a firmware CMake project:

```cmake
set(BUILD_TESTING OFF CACHE BOOL "Disable host tests" FORCE)
set(SCHEDULER_BUILD_EXAMPLES OFF CACHE BOOL "Disable host example" FORCE)
add_subdirectory(path/to/scheduler)
target_link_libraries(firmware PRIVATE scheduler::scheduler)
```

Here `firmware` is an existing application target, and `path/to/scheduler` is its
checkout path. `BUILD_TESTING` is a shared CTest option; setting it off also affects
other subprojects that honor it. Alternatively, add `include/` to your compiler's
include path and include `scheduler.hpp` directly.

## Example behavior

The [example source](../examples/basic/main.cpp) uses `Scheduler<8>` with an integer clock advanced
by bounded callback steps and loop overhead. A fast task has period 20/offset 0;
a slower task has period 100/offset 7. At tick 150, the slow task is disabled,
changed to period 60, and re-enabled with the default full-period delay.
The first new release is therefore 210. Diagnostics include runs before and
after this change. Serial-style polling is admitted separately on each loop.

Expected output:

```text
Slow task re-enabled at tick 150; first release at tick 210.
fast: runs=20 skipped=0 startMiss=0 deadlineMiss=0 wcetOverrun=0 lastLateness=0 maxLateness=0 lastExec=2 maxExec=2
slow: runs=6 skipped=0 startMiss=0 deadlineMiss=0 wcetOverrun=0 lastLateness=0 maxLateness=1 lastExec=4 maxExec=4
Serial polling steps: 168
```

The example logs only reconfiguration and final results. On hardware, replace the
clock and steps with actual bounded operations, account for logging overhead, and
measure WCET under realistic interrupt load. This host example does not simulate
a UART, measure physical time, or prove real-time feasibility.

## Test strategy

`tests/main.cpp` initializes GoogleTest. Shared fixtures and callbacks live in
`test_support.hpp/.cpp`. Each test starts with a reset fake clock; callbacks
advance it to simulate execution time. No correctness assertion depends on
`sleep()` or Linux scheduling. One advancing-clock test explicitly records the
existing admission-to-dispatch timing limitation.

| File | Behavior exercised |
| --- | --- |
| `time_test.cpp` | Time helpers and periodic scheduling across uint32 wrap-around |
| `validation_test.cpp` | Registration, invalid parameters/IDs, original 32-slot behavior |
| `capacity_test.cpp` | Capacities 1/8/32, overflow rejection, all slots, highest-bit control, capacity-specific ID bounds, smaller object sizes |
| `lifecycle_test.cpp` | Start/offset, disable/enable, custom delay, period changes, restart |
| `runtime_control_test.cpp` | Callback self-disable, diagnostics, unaffected peers, repeated disable, invalid IDs, external re-enable, wrap-around, unsupported callback reconfiguration |
| `selection_test.cpp` | Latest-start order, deadline/full ties, WCET protection, no fallback candidate |
| `periodic_test.cpp` | No drift, skipped periods, current release admission, no catch-up bursts |
| `background_test.cpp` | Ready-task rejection, slack admission, invalid WCET, disabled tasks |
| `diagnostics_test.cpp` | Measurements, violations, snapshots, reset without timeline changes |
| `boundary_test.cpp` | Exact boundaries, pre-start enable, wrap during a callback, dispatch overhead |

The original behavior tests use `Scheduler<32>`. Typed capacity tests run for
`Scheduler<1>`, `Scheduler<8>`, and `Scheduler<32>`; they fill all slots, reject
additional registrations even after disabling tasks, execute every slot across
two periods, and exercise the highest mask bit. Storage tests verify that smaller
capacities yield smaller objects.

During CMake configuration with `BUILD_TESTING=ON`,
`capacity_compile_checks.cmake` compiles `capacity_compile_test.cpp` in strict
C++11 for capacities 1, 8, and 32, then verifies that 0 and 33 fail with the
capacity `static_assert` diagnostic. Configuration fails if any result is wrong.
These compile checks are independent of GoogleTest's language requirement and
are not counted as CTest runtime cases.

Runtime-control tests advance the fake clock inside callbacks to verify that
self-disable allows the current invocation to finish and preserves diagnostics.
They check subsequent periodic grids after external default/immediate re-enable,
including counter wrap-around. The explicitly named unsupported-reconfiguration
test demonstrates dispatch overwriting a release set inside the callback; it
records the limitation rather than promising support for that sequence.

Run a subset or list the tests:

```sh
./build/debug/tests/scheduler_tests --gtest_filter='SchedulerTest.Background*'
ctest --test-dir build/debug -R Wrap --output-on-failure
ctest --test-dir build/debug --show-only
```

Production code uses no dynamic allocation. Test-only vectors store observed
start times; GoogleTest and host printing are not part of the bare-metal library.
Tests specify behavior, including equality at deadlines and the difference
between skipped old periods and a missed start for the current invocation.

## Coverage

```sh
make coverage
```

Equivalent direct CMake workflow:

```sh
cmake -S . -B build/coverage -DCMAKE_BUILD_TYPE=Debug \
    -DSCHEDULER_ENABLE_COVERAGE=ON -DSCHEDULER_BUILD_EXAMPLES=OFF
cmake --build build/coverage --target coverage --parallel 2
```

The `coverage` target builds its test executable, clears old `.gcda` counters in
that build tree, runs CTest, and invokes gcovr. If examples are explicitly enabled,
the target also builds their executable before CTest. Test/report failures fail
the target. Repeated coverage runs start from fresh counters.

- `build/coverage/reports/index.html`: HTML summary and per-line details.
- `build/coverage/reports/coverage.xml`: Cobertura XML for CI.
- Terminal: line, function, and branch summaries.

Reports include production headers under `include/`, excluding test and GoogleTest
code. Compiler-generated unreachable and exception branches are filtered out.
The goal is 100% line/function coverage and meaningful branch coverage; there is
no enforced percentage threshold. High coverage is not proof of timing correctness
for all configurations or hardware. A failed run may leave an older report, so
always check the command exit status.

To inspect existing counters manually after a successful run:

```sh
gcovr --root . --filter 'include/.*' --gcov-executable gcov \
    --exclude-unreachable-branches --exclude-throw-branches \
    --html-details build/coverage/reports/manual.html \
    --print-summary build/coverage
```

The final search path prevents collecting unrelated build trees. Use a matching
compiler/gcov pair, such as `g++-13` and `gcov-13`, if multiple toolchains are
installed. The supported coverage path is GCC; Clang coverage is not configured.

## Sharing changes

The root [VERSION](../VERSION) file defines the project version as
`major.minor.patch`. CMake reads it directly and reconfigures when it changes.
When updating the version, also update the displayed version in the root README.

`.gitignore` excludes build outputs and local editor/agent files;
`.gitattributes` sets LF line endings for source. Before committing, run tests,
coverage, and the example, then review documentation links and the staged diff.
The repository is licensed under [MIT](../LICENSE).

For a checkout without a Git repository, initialize one first with `git init`.
Then stage the project files and review them:

```sh
git add README.md LICENSE VERSION CMakeLists.txt Makefile .gitignore .gitattributes include tests examples docs scripts
git diff --cached --check
git diff --cached --stat
git commit -m "Document scheduler behavior and organize build, tests and example"
```

Configure your own remote and branch before publishing. No command here pushes
automatically.
