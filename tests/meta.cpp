#include "process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <tuple>
#include <type_traits>

namespace sfsm_test
{

TEST_CASE("helper traits", "[meta]")
{
  static_assert(sfsm::detail::count_type<int>::value == 0);
  static_assert(sfsm::detail::count_type<int, double>::value == 0);
  static_assert(sfsm::detail::count_type<int, int>::value == 1);
  static_assert(sfsm::detail::count_type<int, double, int, char, int>::value == 2);

  static_assert(sfsm::detail::are_unique<int>::value);
  static_assert(sfsm::detail::are_unique<int, double>::value);
  static_assert(sfsm::detail::are_unique<int, double, char>::value);
  static_assert(!sfsm::detail::are_unique<int, int>::value);
  static_assert(!sfsm::detail::are_unique<int, double, int>::value);
  static_assert(!sfsm::detail::are_unique<int, double, double>::value);

  static_assert(sfsm::detail::type_index<std::tuple<int, double, char>, int>::index == 0);
  static_assert(sfsm::detail::type_index<std::tuple<int, double, char>, double>::index == 1);
  static_assert(sfsm::detail::type_index<std::tuple<int, double, char>, char>::index == 2);
}

TEST_CASE("concepts", "[meta]")
{
  static_assert(sfsm::state_like<idle>);
  static_assert(sfsm::state_like<std::string>);
  static_assert(!sfsm::state_like<idle*>);
  static_assert(sfsm::event_like<start>);

  // Everything is stored by value, so every type has to be spelled unqualified. Without that rule
  // a pointer with a const on it would slip past the check above, and every type the library ever
  // stores would have to be stripped at the place it is stored.
  static_assert(!sfsm::state_like<const idle>);
  static_assert(!sfsm::state_like<idle&>);
  static_assert(!sfsm::state_like<const idle&>);
  static_assert(!sfsm::state_like<idle* const>);
  static_assert(!sfsm::event_like<const start&>);
  static_assert(!sfsm::callable_like<const decltype([]() { return true; }), idle, start, running>);
  static_assert(!sfsm::hook_callable_like<decltype([](idle&) {})&, idle>);

  // Every subsequence of source state, event and target state is a valid callable.
  static_assert(sfsm::callable_like<decltype([]() { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](idle&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](const start&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](running&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](idle&, const start&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](idle&, running&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](const start&, running&) { return true; }), idle, start, running>);
  static_assert(sfsm::callable_like<decltype([](idle&, const start&, running&) { return true; }), idle, start, running>);

  // The canonical order is fixed, a swapped argument list is not a callable.
  static_assert(!sfsm::callable_like<decltype([](const start&, idle&) { return true; }), idle, start, running>);
  static_assert(!sfsm::callable_like<decltype([](running&, idle&) { return true; }), idle, start, running>);
  static_assert(!sfsm::callable_like<decltype([](running&, const start&) { return true; }), idle, start, running>);
  static_assert(!sfsm::callable_like<decltype([](int, int, int) { return true; }), idle, start, running>);

  // A guard has to answer with a bool and cannot modify a state.
  static_assert(sfsm::guard_like<decltype([]() { return true; }), idle, start, running>);
  static_assert(sfsm::guard_like<decltype([](const idle&, const running&) { return true; }), idle, start, running>);
  static_assert(sfsm::guard_like<sfsm::always_type, idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([]() {}), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([]() { return 1; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](const start& event) { return &event; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](idle&) { return true; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](running&) { return true; }), idle, start, running>);

  // An action has nothing to answer, so it has to return void. A callable that returns something
  // was meant as a guard often enough that saying so is worth more than ignoring the result.
  static_assert(sfsm::action_like<decltype([]() {}), idle, start, running>);
  static_assert(sfsm::action_like<sfsm::noop_type, idle, start, running>);
  static_assert(sfsm::action_like<decltype([](idle&, running&) {}), idle, start, running>);
  static_assert(!sfsm::action_like<decltype([](idle&, running&) { return 1; }), idle, start, running>);
  static_assert(!sfsm::action_like<sfsm::always_type, idle, start, running>);

  static_assert(sfsm::is_states_v<process_states_type>);
  static_assert(!sfsm::is_states_v<idle>);
  static_assert(sfsm::states_like<process_states_type>);

  // A hook belongs to a state and takes that state or nothing, the event is not part of it.
  static_assert(sfsm::hook_callable_like<decltype([]() {}), idle>);
  static_assert(sfsm::hook_callable_like<decltype([](idle&) {}), idle>);
  static_assert(!sfsm::hook_callable_like<decltype([](running&) {}), idle>);
  static_assert(!sfsm::hook_callable_like<decltype([](const start&) {}), idle>);

  using transition_type = decltype(sfsm::make_transition<idle, start, running>());
  static_assert(sfsm::is_transition_v<transition_type>);
  static_assert(!sfsm::is_transition_v<start>);
  static_assert(!sfsm::is_transitions_v<transition_type>);
  static_assert(sfsm::is_transitions_v<sfsm::transitions<process_states_type, transition_type>>);

  // The role is part of the type, so a guard cannot be taken for an action or for a hook.
  using guard_type = decltype(sfsm::guard(sfsm::always));
  using action_type = decltype(sfsm::action(sfsm::noop));
  using entry_type = decltype(sfsm::on_entry<idle>(sfsm::noop));
  using exit_type = decltype(sfsm::on_exit<idle>(sfsm::noop));

  static_assert(sfsm::marked_guard_like<guard_type>);
  static_assert(!sfsm::marked_guard_like<action_type>);
  static_assert(!sfsm::marked_guard_like<sfsm::always_type>);
  static_assert(sfsm::marked_action_like<action_type>);
  static_assert(sfsm::entry_hook_like<entry_type>);
  static_assert(sfsm::exit_hook_like<exit_type>);
  static_assert(!sfsm::entry_hook_like<exit_type>);
  static_assert(sfsm::hook_like<entry_type> && sfsm::hook_like<exit_type>);
  static_assert(!sfsm::hook_like<guard_type>);

  // A row of a machine is a transition or a hook, and nothing else.
  static_assert(sfsm::row_like<transition_type>);
  static_assert(sfsm::row_like<entry_type>);
  static_assert(!sfsm::row_like<guard_type>);
  static_assert(!sfsm::row_like<idle>);
}

TEST_CASE("marked guards and actions", "[transition]")
{
  // The order the two are written in does not matter, and a missing one falls back to the default.
  using written_type =
    decltype(sfsm::make_transition<idle, start, running>(sfsm::guard(sfsm::always), sfsm::action(sfsm::noop)));
  using swapped_type =
    decltype(sfsm::make_transition<idle, start, running>(sfsm::action(sfsm::noop), sfsm::guard(sfsm::always)));
  using default_type = decltype(sfsm::make_transition<idle, start, running>());

  static_assert(std::is_same_v<written_type, swapped_type>);
  static_assert(std::is_same_v<written_type, default_type>);
  static_assert(std::is_same_v<written_type::guard_type, sfsm::always_type>);
  static_assert(std::is_same_v<written_type::action_type, sfsm::noop_type>);

  // Only the wrapped callable is kept, the wrapper carries the role and nothing else.
  const auto counting = [](idle& source) { ++source.refused; };
  using counting_type = decltype(sfsm::make_transition<idle, start, idle>(sfsm::action(counting)));
  static_assert(std::is_same_v<counting_type::action_type, std::remove_cvref_t<decltype(counting)>>);
  static_assert(std::is_same_v<counting_type::guard_type, sfsm::always_type>);
}

TEST_CASE("states", "[states]")
{
  static_assert(process_states_type::state_count == 3);
  static_assert(process_states_type::is_state_contained<idle>);
  static_assert(process_states_type::is_state_contained<stopping>);
  static_assert(!process_states_type::is_state_contained<start>);
  static_assert(process_states_type::state_index_of<idle>() == 0);
  static_assert(process_states_type::state_index_of<running>() == 1);
  static_assert(process_states_type::state_index_of<stopping>() == 2);

  // The states are deducible from the constructor arguments.
  static_assert(std::is_same_v<decltype(sfsm::states{idle{}, running{}, stopping{}}), process_states_type>);

  auto machine_states = process_states_type{idle{.runs = 3}, running{}, stopping{}};
  REQUIRE(machine_states.state<0>().get().runs == 3);
  machine_states.state<0>().get().runs = 7;
  REQUIRE(machine_states.state<0>().get().runs == 7);
  REQUIRE(machine_states.state<1>().get().progress == 0);
}

} // namespace sfsm_test
