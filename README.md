# SFSM

A simple finite state machine for C++20, header only.

Unlike a label based machine the states carry data. The machine owns one object per state and
hands the current one to the guards, actions and hooks.

## Example

```cpp
#include <sfsm/sfsm.hpp>

// States, each one holds its own data.
struct idle final { int runs{0}; };
struct running final { int progress{0}; };

// Events.
struct start final { int budget{0}; };
struct tick final {};
struct stop final {};

auto machine = sfsm::sfsm(
  sfsm::states<idle, running>{idle{}, running{}},

  // A guard decides whether the transition may fire, an action runs when it does.
  sfsm::make_transition<idle, start, running>(
    sfsm::guard([](const start& event) { return event.budget > 0; })),

  // A self transition, source and target are the same object.
  sfsm::make_transition<running, tick, running>(
    sfsm::action([](running& source) { ++source.progress; })),

  // Without a guard, so it always fires.
  sfsm::make_transition<running, stop, idle>(
    sfsm::action([](running& source) { source.progress = 0; })),

  // Runs whenever a transition enters idle, not when the machine starts there.
  sfsm::on_entry<idle>([](idle& state) { ++state.runs; }));

machine.process_event(start{});             // false, the guard refused
machine.process_event(start{.budget = 2});  // true, now in running
machine.process_event(tick{});              // true, progress is 1
machine.process_event(stop{});              // true, back in idle
machine.state<idle>().runs;                 // 1
```

A machine is written as its states followed by its rows. A row is either a transition or a hook.
Transitions are tried in the order they are listed, and the machine starts in the source state of
the first one. `process_event` answers whether the event ran. Everything is `constexpr`, so a whole
run can happen at compile time.

Guards and actions declare the arguments they need in the order source state, event, target state,
and may leave out any of them. A guard sees both states as `const` and returns `bool`, an action may
modify them and returns `void`. A transition that fires runs the exit hook of its source state, then
its action, then the entry hook of its target state.

## Nested events

A transition is never interrupted, so an event dispatched from a guard, an action or a hook cannot
run at that point. The queue policy, the second template argument of `sfsm`, decides what becomes of
it. The default `sfsm::no_queue` refuses it. `sfsm::queue_one` takes it over once the transition is
settled and runs it as soon as that transition has completed:

```cpp
auto machine = sfsm::make_sfsm<sfsm::queue_one>(
  sfsm::states<idle, running>{idle{}, running{}},
  sfsm::make_transition<idle, start, running>(),
  sfsm::make_transition<running, stop, idle>());
```

The queue holds one event by value, with neither type erasure nor an allocation, so a machine that
queues still runs at compile time. A machine given `no_queue` holds nothing at all for it.

## Behaviour

- **Threads.** `sfsm` is a value with no synchronization and follows the contract of the standard
  containers: concurrent readers are fine, a writer needs exclusive access.
- **Exceptions.** If a guard, an exit hook or an action throws, the machine stays in the source
  state; if an entry hook throws, it already is in the target state. Either way it dispatches on.
- **Types.** Everything is stored by value, so every state, event and callable type is spelled
  unqualified: `machine.state<idle>()`, never `machine.state<const idle&>()`.

## Requirements

C++20 and a standard library, nothing else. No header throws, catches or synchronizes on its own.

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

That lands in `build/install`. Pick another prefix with `-DCMAKE_INSTALL_PREFIX=/your/prefix`, then
point a consumer at it with `CMAKE_PREFIX_PATH` and use `find_package(sfsm REQUIRED)`.

## License

MIT, see [LICENSE](LICENSE).
