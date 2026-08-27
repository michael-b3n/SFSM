# SFSM

A simple finite state machine for C++20, header only.

Unlike a label based machine the states carry data. The machine owns one object per state type and
hands the current one to the guards, actions and hooks, so a run keeps its data in the state it
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
    sfsm::guard([](const start& event, const running& target) { return target.progress == 0 && event.budget > 0; }),
    sfsm::action([](const start& event, running& target) { target.budget = event.budget; })),

  // A self transition, source and target are the same object.
  sfsm::make_transition<running, tick, running>(
    sfsm::guard([](const running& source) { return source.progress < source.budget; }),
    sfsm::action([](running& source) { ++source.progress; })),

  // Without a guard, so it always fires.
  sfsm::make_transition<running, stop, idle>(
    sfsm::action([](running& source) { source.progress = 0; })),

  // Runs whenever a transition enters idle, not when the machine starts there.
  sfsm::on_entry<idle>([](idle& state) { ++state.runs; }));

machine.process_event(start{.budget = 2});  // true, now in running
machine.process_event(tick{});              // true, progress is 1
machine.process_event(stop{});              // true, back in idle
machine.state<idle>().runs;                 // 1
```

Everything is `constexpr`, so a whole machine run can happen at compile time.

The machine is called `sfsm::sfsm`, so the namespace and the class share a name. That is fine
everywhere except after a using directive: `using namespace sfsm;` brings the class into the scope
the namespace itself lives in, and from there on the plain name `sfsm` is ambiguous. Qualify the
names instead of importing the namespace, the tests do the same.

## How it behaves

**Rows.** A machine is written as its states followed by its rows, and a row is either a transition
or a hook. They may be written in any order, only the transitions among them are tried in the order
they are listed. Two transitions may share a source state and an event as long as the more specific
one comes first. The machine starts in the source state of the first transition, `reset_to_state`
moves it without running anything.

**Guards and actions.** `sfsm::guard` and `sfsm::action` mark which callable is which, so the two
may be written in either order and either of them may be left out. The defaults are `sfsm::always`
and `sfsm::noop`. A guard decides whether a transition may fire and must return exactly `bool`; it
sees both states as `const`, so it can refuse because the target is busy without being able to
change anything. Guards of transitions that do not fire run as well. An action may modify both
states and must return `void`, so a callable meant as a guard is not silently taken for one.

**Hooks.** `sfsm::on_entry<State>` and `sfsm::on_exit<State>` mark a callable as a hook of a state,
and a machine holds at most one of each per state. A hook belongs to its state and not to a single
transition, so it takes that state or nothing and never the event. Neither the state the machine
starts in nor `reset_to_state` runs a hook, only a transition does.

**Arguments.** Guards and actions declare the arguments they need in the canonical order source
state, event, target state, and may leave out any of them. All eight subsequences are accepted, a
swapped order is not.

**Types.** Everything a machine is handed it stores by value, so every state, event and callable
type is spelled unqualified: `machine.state<idle>()`, never `machine.state<const idle&>()`. The
concepts insist on it rather than stripping the qualifiers behind your back, so a type that is not
spelled that way is a constraint failure and not a surprise.

**Order.** A transition that fires runs the exit hook of its source state, then its action, then the
entry hook of its target state. The state index moves between the action and the entry hook, so an
action still sees the state it is leaving and a hook always sees its own state. In a self transition
both hooks run on the same object.

**Run to completion.** A transition is never interrupted. An event dispatched from a guard, an
action or a hook is refused and `process_event` returns false, `reset_to_state` refuses as well.

**Exceptions.** If a guard, an exit hook or an action throws, the machine stays in the source state.
If an entry hook throws, it already is in the target state. Either way it is ready to dispatch again.

**Threads.** `sfsm` is a value with no synchronization, and follows the contract of the standard
containers: concurrent readers are fine, a writer needs exclusive access. A machine shared between
threads has to be guarded by its owner, a wrapper that does it comes later.

## Headers

| Header | Content |
| --- | --- |
| `sfsm/sfsm.hpp` | `sfsm`, the state machine, and the umbrella over everything below |
| `sfsm/meta.hpp` | the type traits, and the concepts `state_like` and `event_like` |
| `sfsm/callable.hpp` | `guard`, `action`, `on_entry`, `on_exit`, the role `callable_role`, the defaults `always` and `noop`, and the concepts `callable_like`, `guard_like`, `action_like`, `hook_callable_like`, `hook_like` |
| `sfsm/states.hpp` | `states`, the states of a machine |
| `sfsm/transition.hpp` | `transition` and `make_transition` |
| `sfsm/transitions.hpp` | `transitions`, the rows of a machine, and the concept `row_like` |

## Requirements

C++20 and a standard library, nothing else. The headers are free of exceptions and allocations of
their own, so `-fno-exceptions` builds are fine, and none of them needs a threading capable
standard library.

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

With a checkout next to your project, `add_subdirectory(external/SFSM)` and the same
`target_link_libraries` do the job. To install instead:

```bash
cmake -B build -DSFSM_BUILD_TESTS=OFF
cmake --install build
```

That lands in `build/install`, which is the default prefix of a top level SFSM build, because the
usual prefix of CMake is a system directory that a header only library has no reason to ask for.
Pick another one at configure time with `-DCMAKE_INSTALL_PREFIX=/your/prefix` or at install time
with `--prefix /your/prefix`, then point a consumer at it with `CMAKE_PREFIX_PATH` and use
`find_package(sfsm REQUIRED)`.

Tests and install rules only appear when SFSM is the top level project, `SFSM_BUILD_TESTS` and
`SFSM_INSTALL` override that. The default prefix is only chosen on the first configure run of a
build directory, so an existing one keeps whatever it was configured with.

## Tests

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Catch2 v3 is used, taken from the system if it is installed and fetched otherwise. Every header is
also compiled on its own to keep it self contained.

clang-tidy reads the same build directory. The tests are what it is pointed at, because they are
what instantiates the templates, and `.clang-tidy` narrows the report to the headers of the library:

```bash
clang-tidy -p build tests/sfsm.cpp tests/meta.cpp tests/hooks.cpp
```

## License

MIT, see [LICENSE](LICENSE).
