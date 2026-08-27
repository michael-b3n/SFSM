#include "process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace sfsm_test
{

TEST_CASE("construction", "[sfsm]")
{
  const auto machine = make_process();

  static_assert(decltype(machine)::state_count == 3);
  static_assert(decltype(machine)::transition_count == 5);
  static_assert(decltype(machine)::state_index_of<idle>() == 0);
  static_assert(decltype(machine)::state_index_of<stopping>() == 2);

  // The machine starts in the source state of the first transition.
  REQUIRE(machine.is_state<idle>());
  REQUIRE(!machine.is_state<running>());
  REQUIRE(machine.state_index() == decltype(machine)::state_index_of<idle>());
  REQUIRE(machine.state<idle>().runs == 0);
}

TEST_CASE("handles_event", "[sfsm]")
{
  using machine_type = decltype(make_process());

  static_assert(machine_type::handles_event<start>());
  static_assert(machine_type::handles_event<abort>());
  static_assert(!machine_type::handles_event<kick>());
}

TEST_CASE("process lifecycle", "[sfsm]")
{
  auto machine = make_process();

  REQUIRE(machine.process_event(start{.budget = 3}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<running>().budget == 3);
  REQUIRE(machine.state<idle>().refused == 0);

  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<running>().progress == 2);

  REQUIRE(machine.process_event(abort{}));
  REQUIRE(machine.is_state<stopping>());
  REQUIRE(machine.state<stopping>().pending == 2);
  REQUIRE(machine.state<running>().progress == 0);

  REQUIRE(machine.process_event(stopped{}));
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.state<idle>().runs == 1);
  REQUIRE(machine.state<stopping>().pending == 0);

  // The run left everything behind as it found it, so the next one behaves the same.
  REQUIRE(machine.process_event(start{.budget = 1}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<idle>().refused == 0);
}

TEST_CASE("process_event ignores unknown events", "[sfsm]")
{
  auto machine = make_process();

  REQUIRE(!machine.process_event(kick{}));
  REQUIRE(machine.is_state<idle>());

  // The process is idle, so a tick is known but not handled here.
  REQUIRE(!machine.process_event(tick{}));
  REQUIRE(machine.is_state<idle>());
}

TEST_CASE("guards decide on the source state", "[sfsm]")
{
  auto machine = make_process();

  // The budget of the run limits the ticks, the remaining ones are not handled.
  REQUIRE(machine.process_event(start{.budget = 2}));
  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.process_event(tick{}));
  REQUIRE(!machine.process_event(tick{}));
  REQUIRE(machine.state<running>().progress == 2);
  REQUIRE(machine.is_state<running>());
}

TEST_CASE("guards decide on the target state", "[sfsm]")
{
  auto machine = make_process();

  // A start without a budget cannot enter running, the fallback row counts it.
  REQUIRE(machine.process_event(start{}));
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.state<idle>().refused == 1);

  // Leaving a run behind makes running busy, so the next start is refused as well.
  REQUIRE(machine.process_event(start{.budget = 5}));
  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.state<running>().progress == 1);
  machine.reset_to_state<idle>();

  REQUIRE(machine.process_event(start{.budget = 5}));
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.state<idle>().refused == 2);
}

TEST_CASE("reset_to_state", "[sfsm]")
{
  auto machine = make_process();

  REQUIRE(machine.reset_to_state<stopping>());
  REQUIRE(machine.is_state<stopping>());
  REQUIRE(machine.state_index() == 2);

  // No action ran, so the counters are untouched.
  REQUIRE(machine.state<idle>().runs == 0);
  REQUIRE(machine.process_event(stopped{}));
  REQUIRE(machine.state<idle>().runs == 1);
  REQUIRE(machine.is_state<idle>());
}

TEST_CASE("state data is mutable", "[sfsm]")
{
  auto machine = make_process();

  machine.state<idle>().refused = 41;
  REQUIRE(machine.process_event(start{}));
  REQUIRE(machine.state<idle>().refused == 42);

  const auto& const_machine = machine;
  REQUIRE(const_machine.state<idle>().refused == 42);
}

TEST_CASE("default guard and action", "[sfsm]")
{
  using states_type = sfsm::states<idle, running>;

  auto machine = sfsm::sfsm(
    states_type{idle{}, running{}}, sfsm::make_transition<idle, start, running>(), sfsm::make_transition<running, abort, idle>()
  );

  REQUIRE(machine.process_event(start{}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.process_event(abort{}));
  REQUIRE(machine.is_state<idle>());
}

TEST_CASE("self transition", "[sfsm]")
{
  using states_type = sfsm::states<running>;

  auto counter = 0;
  auto machine = sfsm::sfsm(
    states_type{running{}},
    // Source and target are the same object here, so the action sees its progress twice.
    sfsm::make_transition<running, tick, running>(sfsm::action(
      [&counter](running& source, running& target)
      {
        ++source.progress;
        counter += target.progress;
      }
    ))
  );

  static_assert(decltype(machine)::state_count == 1);
  static_assert(decltype(machine)::transition_count == 1);

  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<running>().progress == 2);
  REQUIRE(counter == 3);
  REQUIRE(!machine.process_event(start{}));
  REQUIRE(counter == 3);
}

TEST_CASE("refuses a nested transition", "[sfsm]")
{
  using states_type = sfsm::states<idle, running, stopping>;

  // An action needs a handle to the machine, which only exists once the table is built.
  std::function<void()> follow_up;
  auto machine = sfsm::sfsm(
    states_type{idle{}, running{}, stopping{}},
    sfsm::make_transition<idle, start, running>(sfsm::action([&follow_up]() { follow_up(); })),
    sfsm::make_transition<running, abort, stopping>()
  );
  follow_up = [&machine]()
  {
    // The running transition sets the state when it completes, so the nested event is refused.
    REQUIRE(!machine.process_event(abort{}));
    REQUIRE(!machine.reset_to_state<stopping>());

    // A copy does not inherit the running transition of its source, so it accepts the reset.
    auto copy = machine;
    REQUIRE(copy.reset_to_state<stopping>());
  };

  REQUIRE(machine.process_event(start{}));
  REQUIRE(machine.is_state<running>());

  // The machine is usable again, the refused event only had to be dispatched after the transition.
  REQUIRE(machine.process_event(abort{}));
  REQUIRE(machine.is_state<stopping>());
}

TEST_CASE("survives a throwing action", "[sfsm]")
{
  using states_type = sfsm::states<idle, running>;

  auto machine = sfsm::sfsm(
    states_type{idle{}, running{}},
    sfsm::make_transition<idle, start, running>(sfsm::action([](idle&) { throw std::runtime_error{"action"}; })),
    sfsm::make_transition<idle, kick, running>()
  );

  REQUIRE_THROWS_AS(machine.process_event(start{}), std::runtime_error);

  // The transition never completed, so the machine stayed where it was and dispatches again.
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.process_event(kick{}));
  REQUIRE(machine.is_state<running>());
}

TEST_CASE("survives a throwing guard", "[sfsm]")
{
  using states_type = sfsm::states<idle, running>;

  auto throwing = true;
  auto machine = sfsm::sfsm(
    states_type{idle{}, running{}},
    sfsm::make_transition<idle, start, running>(sfsm::guard(
      [&throwing]()
      {
        if(throwing)
        {
          throw std::runtime_error{"guard"};
        }
        return true;
      }
    ))
  );

  REQUIRE_THROWS_AS(machine.process_event(start{}), std::runtime_error);
  REQUIRE(machine.is_state<idle>());

  throwing = false;
  REQUIRE(machine.process_event(start{}));
  REQUIRE(machine.is_state<running>());
}

TEST_CASE("copy and move follow the table", "[sfsm]")
{
  // An action holding a unique_ptr, so the table can be moved but not copied. The copy and move
  // members of the machine are constrained, otherwise it would claim to be copyable here.
  auto unique = sfsm::sfsm(
    sfsm::states<idle, running>{idle{}, running{}},
    sfsm::make_transition<idle, start, running>(sfsm::action([owned = std::make_unique<int>(1)](running& target)
                                                             { target.budget = *owned; }))
  );
  static_assert(!std::is_copy_constructible_v<decltype(unique)>);
  static_assert(std::is_move_constructible_v<decltype(unique)>);
  static_assert(std::is_copy_constructible_v<decltype(make_process())>);

  auto moved = std::move(unique);
  REQUIRE(moved.process_event(start{}));
  REQUIRE(moved.state<running>().budget == 1);
}

TEST_CASE("copies carry the state data", "[sfsm]")
{
  auto machine = make_process();
  REQUIRE(machine.process_event(start{.budget = 3}));

  auto copy = machine;
  REQUIRE(copy.is_state<running>());

  // The states came along, the copy continues the run of the original on its own data.
  REQUIRE(copy.state<running>().budget == 3);
  REQUIRE(copy.process_event(tick{}));
  REQUIRE(copy.state<running>().progress == 1);
  REQUIRE(machine.state<running>().progress == 0);
}

TEST_CASE("runs at compile time", "[sfsm]")
{
  constexpr auto run = []()
  {
    auto machine = make_process();
    machine.process_event(start{.budget = 2});
    machine.process_event(tick{});
    machine.process_event(abort{});
    machine.process_event(stopped{});
    machine.process_event(start{});
    return std::tuple{machine.state<idle>().runs, machine.state<idle>().refused, machine.state_index()};
  };

  static_assert(std::get<0>(run()) == 1);
  static_assert(std::get<1>(run()) == 1);
  static_assert(std::get<2>(run()) == process_states_type::state_index_of<idle>());

  REQUIRE(std::get<0>(run()) == 1);
}

} // namespace sfsm_test
