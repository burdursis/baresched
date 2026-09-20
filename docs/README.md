# Documentation

Start with the [project README](../README.md), then follow the guides below.
They describe the current [implementation](../include/scheduler.hpp) and its
tested behavior; the repository does not require a separate design document.

| Guide | Questions it answers |
| --- | --- |
| [Design and execution flows](design.md) | How is work selected? How do callbacks, background work, and ISRs cooperate? What are the limits? |
| [Timing model](timing-model.md) | What do the timing parameters mean? How are missed releases and timer wrap-around handled? |
| [Public API](api.md) | What does each function accept, return, and change? |
| [Diagnostics and timing logs](diagnostics.md) | What do counters and timing peaks mean? How can WCET assumptions be checked? |
| [Build, test, and coverage](testing.md) | How are dependencies installed, tests run, and coverage reports generated? |

The [basic example](../examples/basic/main.cpp) is a complete, buildable program.
Mermaid blocks render in compatible Markdown viewers; surrounding prose also
explains each decision.
