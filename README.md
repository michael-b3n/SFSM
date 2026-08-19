# SFSM

A simple finite state machine for C++20, header only.

Unlike a label based machine the states carry data. The transition table owns one object per state
type and hands the current one to the guards and actions, so a run keeps its data in the state it
belongs to instead of in the machine.

```cpp
#include <sfsm/sfsm.hpp>

struct idle final { int runs{0}; };
struct running final { int progress{0}; int budget{0}; };

struct start final { int budget{0}; };
struct tick final {};
struct stop final {};

auto machine = sfsm::sfsm(
  sfsm::states<idle, running>{idle{}, running{}},

  // The target has to be free and the run needs a budget.
  sfsm::make_transition<idle, start, running>(
    [](const start& event, const running& target) { return target.progress == 0 && event.budget > 0; },
    [](const start& event, running& target) { target.budget = event.budget; }),

  // A self transition, source and target are the same object.
  sfsm::make_transition<running, tick, running>(
    [](const running& source) { return source.progress < source.budget; },
    [](running& source) { ++source.progress; }),

  sfsm::make_transition<running, stop, idle>(
    sfsm::always,
    [](running& source, idle& target) { source.progress = 0; ++target.runs; }));

machine.process_event(start{.budget = 2});  // true, now in running
machine.process_event(tick{});              // true, progress is 1
machine.process_event(stop{});              // true, back in idle
machine.state<idle>().runs;                 // 1
```

Everything is `constexpr`, so a whole machine run can happen at compile time.

## Requirements

C++20 and a standard library, nothing else. The headers are free of exceptions and allocations of
their own, so `-fno-exceptions` builds are fine, and none of them needs a threading capable
standard library.

## The name

The machine is called `sfsm::sfsm`, so the namespace and the class share a name. That is fine
everywhere except after a using directive: `using namespace sfsm;` brings the class into the scope
the namespace itself lives in, and from there on the plain name `sfsm` is ambiguous. Qualify the
names instead of importing the namespace, the tests do the same.

## Using it

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(sfsm
  GIT_REPOSITORY https://github.com/michael-b3n/SFSM.git
  GIT_TAG main
)
FetchContent_MakeAvailable(sfsm)
target_link_libraries(your_target PRIVATE sfsm::sfsm)
```

With a checkout next to your project:

```cmake
add_subdirectory(external/SFSM)
target_link_libraries(your_target PRIVATE sfsm::sfsm)
```

Installed:

```bash
cmake -B build -DSFSM_BUILD_TESTS=OFF
cmake --install build
```

That lands in `build/install`, which is the default prefix of a top level SFSM build. The usual
prefix of CMake is a system directory and installing into it needs the rights that come with it,
which a header only library has no reason to ask for. Pick another one whenever you want, either
at configure time or at install time:

```bash
cmake -B build -DSFSM_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --install build --prefix /your/prefix
```

Point a consumer at the prefix with `CMAKE_PREFIX_PATH` and find it:

```cmake
find_package(sfsm REQUIRED)
target_link_libraries(your_target PRIVATE sfsm::sfsm)
```

Tests and install rules only appear when SFSM is the top level project, `SFSM_BUILD_TESTS` and
`SFSM_INSTALL` override that. The default prefix is only chosen on the first configure run of a
build directory, so an existing one keeps whatever it was configured with.

## Headers

| Header | Content |
| --- | --- |
| `sfsm/sfsm.hpp` | `sfsm`, the state machine, and the umbrella over everything below |
| `sfsm/meta.hpp` | the concepts `state_like`, `event_like`, `callable_like`, `guard_like`, `action_like` and the type traits behind them |
| `sfsm/states.hpp` | `states`, the states of a machine |
| `sfsm/transition.hpp` | `transition`, `make_transition`, and the defaults `always` and `noop` |
| `sfsm/transitions.hpp` | `transitions`, the transition table |
| `sfsm/version.hpp` | version macros and constants |

## How it behaves

**Arguments.** Guards and actions declare the arguments they need in the canonical order source
state, event, target state, and may leave out any of them. All eight subsequences are accepted, a
swapped order is not.

**Guards.** A guard decides whether a transition may fire and must return exactly `bool`. It sees
both states as `const`, so it can refuse because the target is busy without being able to change
anything. Guards of transitions that do not fire run as well.

**Actions.** An action runs when a transition fires, before the state index moves. It gets both
states mutable, in a self transition they are the same object. Its result is ignored.

**Order.** Transitions are tried in the order they are listed, so two rows may share a source state
and an event as long as the more specific one comes first. The machine starts in the source state
of the first transition, `reset_to_state` overrides that.

**Run to completion.** A transition is never interrupted. An event dispatched by a guard or an
action is refused, `process_event` returns false and `is_transitioning()` says why. The state index
moves after the action returned, so an action always sees the state it is leaving.

**Exceptions.** If a guard or an action throws, the machine stays in the state it was in and is
ready to dispatch again.

**Threads.** `sfsm` is a value with no synchronization, and follows the contract of the standard
containers: concurrent readers are fine, a writer needs exclusive access. A machine shared between
threads has to be guarded by its owner, a wrapper that does it comes later.

## Tests

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Catch2 v3 is used, taken from the system if it is installed and fetched otherwise. Every header is
also compiled on its own to keep it self contained.

## License

MIT, see [LICENSE](LICENSE).
