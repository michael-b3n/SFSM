#include "process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <tuple>

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

  // A guard has to answer with a bool and cannot modify a state, an action may do both.
  static_assert(sfsm::guard_like<decltype([]() { return true; }), idle, start, running>);
  static_assert(sfsm::guard_like<decltype([](const idle&, const running&) { return true; }), idle, start, running>);
  static_assert(sfsm::guard_like<sfsm::always_type, idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([]() {}), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([]() { return 1; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](const start& event) { return &event; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](idle&) { return true; }), idle, start, running>);
  static_assert(!sfsm::guard_like<decltype([](running&) { return true; }), idle, start, running>);

  static_assert(sfsm::action_like<decltype([]() {}), idle, start, running>);
  static_assert(sfsm::action_like<sfsm::noop_type, idle, start, running>);
  static_assert(sfsm::action_like<decltype([](idle&, running&) { return 1; }), idle, start, running>);

  static_assert(sfsm::is_states_v<process_states_type>);
  static_assert(!sfsm::is_states_v<idle>);
  static_assert(sfsm::states_like<process_states_type>);

  using transition_type = decltype(sfsm::make_transition<idle, start, running>());
  static_assert(sfsm::is_transition_v<transition_type>);
  static_assert(!sfsm::is_transition_v<start>);
  static_assert(!sfsm::is_transitions_v<transition_type>);
  static_assert(sfsm::is_transitions_v<sfsm::transitions<process_states_type, transition_type>>);
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
