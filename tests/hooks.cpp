#include "process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace sfsm_test
{

namespace
{

///
/// A machine writing every step of a transition into a log, so that the order can be checked.
/// The hooks are written before the transitions, only the transitions decide where it starts.
///
auto make_logged(std::vector<std::string>& log)
{
  return sfsm::sfsm(
    sfsm::states<idle, running>{idle{}, running{}},
    sfsm::on_exit<idle>([&log](const idle& state) { log.emplace_back("exit idle " + std::to_string(state.runs)); }),
    sfsm::on_entry<running>(
      [&log](running& state)
      {
        state.progress = 1;
        log.emplace_back("entry running");
      }
    ),
    sfsm::on_exit<running>([&log]() { log.emplace_back("exit running"); }),
    sfsm::on_entry<idle>(
      [&log](idle& state)
      {
        ++state.runs;
        log.emplace_back("entry idle");
      }
    ),
    sfsm::make_transition<idle, start, running>(sfsm::action([&log]() { log.emplace_back("action start"); })),
    sfsm::make_transition<running, abort, idle>(sfsm::action([&log]() { log.emplace_back("action abort"); }))
  );
}

///
/// Whether a role may be handed to has_hook or to role_count at all, which is a different question
/// from what the answer is. A role of the other half has to fail the constraint and not come back
/// as a count of nothing, which is what a state without that hook and a typo would share.
///
template<typename Transitions, sfsm::callable_role Role>
concept has_hook_queryable = requires { Transitions::template has_hook<Role, idle>; };

template<sfsm::callable_role Role>
concept role_count_queryable = requires { sfsm::detail::role_count<Role, decltype(sfsm::guard(sfsm::always))>; };

} // namespace

TEST_CASE("hooks are rows next to the transitions", "[hooks]")
{
  std::vector<std::string> log;
  const auto machine = make_logged(log);

  // Only the transitions are counted, and only they decide which state the machine starts in.
  static_assert(decltype(machine)::transition_count == 2);
  static_assert(decltype(machine)::state_count == 2);
  static_assert(decltype(machine)::handles_event<start>());
  static_assert(!decltype(machine)::handles_event<tick>());

  // Entering the first state is not a transition, so no hook ran yet.
  REQUIRE(machine.is_state<idle>());
  REQUIRE(log.empty());
}

TEST_CASE("a hook is counted against every row", "[hooks]")
{
  // The uniqueness check names its row pack twice, once expanded and once not, which is easy to
  // misread as a lockstep walk over the two. Pin the counting down so it cannot regress quietly.
  using row_transition = decltype(sfsm::make_transition<idle, start, running>());
  using entry_idle = decltype(sfsm::on_entry<idle>(sfsm::noop));
  using exit_idle = decltype(sfsm::on_exit<idle>(sfsm::noop));
  using entry_running = decltype(sfsm::on_entry<running>(sfsm::noop));
  using entry_idle_other = decltype(sfsm::on_entry<idle>([](idle& state) { ++state.runs; }));
  static_assert(!std::is_same_v<entry_idle, entry_idle_other>);

  // A row counts itself, so one is what a hook without a twin reaches, and a transition is no hook.
  static_assert(sfsm::detail::same_hook_count<entry_idle, row_transition, entry_idle, exit_idle, entry_running>() == 1);
  static_assert(sfsm::detail::same_hook_count<row_transition, row_transition, entry_idle>() == 0);

  // Same role and same state is what counts, the callable type has nothing to do with it.
  static_assert(sfsm::detail::same_hook_count<entry_idle, entry_idle, entry_idle_other>() == 2);

  static_assert(sfsm::detail::hooks_are_unique<row_transition, entry_idle, exit_idle, entry_running>());

  // The two entry hooks of idle sit at the ends of the list, so a lockstep walk would miss them.
  static_assert(!sfsm::detail::hooks_are_unique<entry_idle, row_transition, exit_idle, entry_running, entry_idle_other>());
}

TEST_CASE("only entry and exit are a hook role", "[hooks]")
{
  // Half of callable_role belongs to a transition, so the hook side must refuse those two rather
  // than answer that the state has no such hook, which is the same answer a typo would get.
  namespace detail = sfsm::detail;
  static_assert(sfsm::is_hook_role<sfsm::callable_role::entry>);
  static_assert(sfsm::is_hook_role<sfsm::callable_role::exit>);
  static_assert(!sfsm::is_hook_role<sfsm::callable_role::guard>);
  static_assert(!sfsm::is_hook_role<sfsm::callable_role::action>);

  using transitions_t = decltype(sfsm::transitions(
    sfsm::states<idle, running>{idle{}, running{}},
    sfsm::on_entry<idle>(sfsm::noop),
    sfsm::make_transition<idle, start, running>()
  ));

  // Asking with a role at all is what the two halves differ in, not what the answer turns out to be.
  static_assert(has_hook_queryable<transitions_t, sfsm::callable_role::entry>);
  static_assert(has_hook_queryable<transitions_t, sfsm::callable_role::exit>);
  static_assert(!has_hook_queryable<transitions_t, sfsm::callable_role::guard>);
  static_assert(!has_hook_queryable<transitions_t, sfsm::callable_role::action>);

  // The answer of a role that is one is still the one it always was.
  static_assert(transitions_t::has_hook<sfsm::callable_role::entry, idle>);
  static_assert(!transitions_t::has_hook<sfsm::callable_role::exit, idle>);
  static_assert(!transitions_t::has_hook<sfsm::callable_role::entry, running>);

  // The transition side is the same rule read the other way round.
  static_assert(sfsm::is_part_role<sfsm::callable_role::guard>);
  static_assert(sfsm::is_part_role<sfsm::callable_role::action>);
  static_assert(!sfsm::is_part_role<sfsm::callable_role::entry>);
  static_assert(!sfsm::is_part_role<sfsm::callable_role::exit>);

  static_assert(role_count_queryable<sfsm::callable_role::guard>);
  static_assert(role_count_queryable<sfsm::callable_role::action>);
  static_assert(!role_count_queryable<sfsm::callable_role::entry>);
  static_assert(!role_count_queryable<sfsm::callable_role::exit>);

  static_assert(detail::role_count<sfsm::callable_role::guard, decltype(sfsm::guard(sfsm::always))> == 1);
  static_assert(detail::role_count<sfsm::callable_role::action, decltype(sfsm::guard(sfsm::always))> == 0);
}

TEST_CASE("a transition runs exit, action and entry in this order", "[hooks]")
{
  std::vector<std::string> log;
  auto machine = make_logged(log);

  REQUIRE(machine.process_event(start{}));
  REQUIRE(log == std::vector<std::string>{"exit idle 0", "action start", "entry running"});
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<running>().progress == 1);

  log.clear();
  REQUIRE(machine.process_event(abort{}));
  REQUIRE(log == std::vector<std::string>{"exit running", "action abort", "entry idle"});
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.state<idle>().runs == 1);

  // The exit hook of idle sees what the entry hook of idle left behind.
  log.clear();
  REQUIRE(machine.process_event(start{}));
  REQUIRE(log.front() == "exit idle 1");
}

TEST_CASE("hooks stay out of the way of a refused event", "[hooks]")
{
  std::vector<std::string> log;
  auto machine = make_logged(log);

  // Neither an unknown event nor one the current state ignores reaches a hook.
  REQUIRE(!machine.process_event(kick{}));
  REQUIRE(!machine.process_event(abort{}));
  REQUIRE(log.empty());

  // A reset is not a transition either.
  REQUIRE(machine.reset_to_state<running>());
  REQUIRE(log.empty());
}

TEST_CASE("a refusing guard runs no hook", "[hooks]")
{
  auto exits = 0;
  auto entries = 0;
  auto machine = sfsm::sfsm(
    sfsm::states<idle, running>{idle{}, running{}},
    sfsm::on_exit<idle>([&exits]() { ++exits; }),
    sfsm::on_entry<running>([&entries]() { ++entries; }),
    sfsm::make_transition<idle, start, running>(sfsm::guard([](const start& event) { return event.budget > 0; }))
  );

  REQUIRE(!machine.process_event(start{}));
  REQUIRE(exits == 0);
  REQUIRE(entries == 0);

  REQUIRE(machine.process_event(start{.budget = 1}));
  REQUIRE(exits == 1);
  REQUIRE(entries == 1);
}

TEST_CASE("a self transition leaves and enters the same state", "[hooks]")
{
  std::vector<std::string> log;
  auto machine = sfsm::sfsm(
    sfsm::states<running>{running{}},
    sfsm::on_entry<running>([&log](const running& state) { log.emplace_back("entry " + std::to_string(state.progress)); }),
    sfsm::on_exit<running>([&log](const running& state) { log.emplace_back("exit " + std::to_string(state.progress)); }),
    sfsm::make_transition<running, tick, running>(sfsm::action([](running& source) { ++source.progress; }))
  );

  REQUIRE(machine.process_event(tick{}));
  REQUIRE(log == std::vector<std::string>{"exit 0", "entry 1"});
  REQUIRE(machine.is_state<running>());
}

TEST_CASE("hooks run at compile time", "[hooks]")
{
  constexpr auto run = []()
  {
    auto machine = sfsm::sfsm(
      sfsm::states<idle, running>{idle{}, running{}},
      sfsm::on_exit<idle>([](idle& state) { ++state.refused; }),
      sfsm::on_entry<running>([](running& state) { ++state.progress; }),
      sfsm::make_transition<idle, start, running>(),
      sfsm::make_transition<running, abort, idle>(sfsm::action([](idle& target) { ++target.runs; }))
    );
    machine.process_event(start{});
    machine.process_event(abort{});
    machine.process_event(start{});
    return std::tuple{machine.state<idle>().refused, machine.state<running>().progress, machine.state<idle>().runs};
  };

  static_assert(std::get<0>(run()) == 2);
  static_assert(std::get<1>(run()) == 2);
  static_assert(std::get<2>(run()) == 1);

  REQUIRE(std::get<0>(run()) == 2);
}

} // namespace sfsm_test
