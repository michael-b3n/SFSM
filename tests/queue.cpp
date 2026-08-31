#include "process.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sfsm_test
{
namespace
{

///
/// States and events of a machine whose rows dispatch again while a transition is on the way.
///
struct low final
{
  int entries{0};
  int exits{0};
};
struct high final
{
  int entries{0};
};
struct up final
{};
struct down final
{};

///
/// A row cannot name the machine it belongs to, the type of the machine follows from the very
/// lambdas that would have to name it. The sink breaks the circle: the rows call through it and
/// the test fills it in once the machine stands. It fires once, so a machine that dispatches its
/// own event again does not run forever.
///
struct event_sink final
{
  // Variables
  std::function<void()> callback;
  bool armed{false};
  int calls{0};
};

///
/// Calls a sink and disarms it, so a row that dispatches its own event again comes to an end.
///
auto fire(event_sink& sink) -> void
{
  if(sink.armed && sink.callback)
  {
    sink.armed = false;
    ++sink.calls;
    sink.callback();
  }
}

///
/// An event that owns something, so it can be moved and not copied.
///
struct payload final
{
  std::unique_ptr<int> value;
};

///
/// Whether a machine dispatches an event that is left where it is, and whether it takes one over.
/// The requires expressions are spelled out here and not in the test, so that the substitution
/// happens in a template, where a failure is an answer and not an error.
///
template<typename Machine, typename Event>
concept dispatches_lvalue = requires(Machine& machine, Event& event) { machine.process_event(event); };
template<typename Machine, typename Event>
concept dispatches_rvalue = requires(Machine& machine, Event event) { machine.process_event(std::move(event)); };

///
/// A machine reacting to the event that cannot be copied, with whatever queue policy it is given.
///
template<typename Queue>
auto make_payload_machine()
{
  return sfsm::make_sfsm<Queue>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, payload, high>(sfsm::action([](const payload& event, high& target)
                                                           { target.entries += *event.value; })),
    sfsm::make_transition<high, down, low>()
  );
}

///
/// The sink of the machine below. A lambda that captures is not assignable and would take the
/// assignment of the whole table with it, so the rows of that machine reach their sink here
/// instead of capturing one.
///
event_sink shared_sink{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

///
/// A machine whose table is assignable, low -> up -> high -> down -> low with a kick that queues
/// through the sink and then throws, so that a dispatch ends with an event left in the queue.
///
auto make_assignable_process()
{
  return sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, kick, low>(sfsm::action(
      [](low&)
      {
        fire(shared_sink);
        throw std::runtime_error{"action"};
      }
    )),
    sfsm::make_transition<low, up, high>(sfsm::action([](high& target) { ++target.entries; })),
    sfsm::make_transition<high, down, low>(sfsm::action([](low& target) { ++target.entries; }))
  );
}

///
/// The machine of the tests below, low -> up -> high -> down -> low. The action of up calls the
/// sink, so that a row can dispatch while the transition it belongs to is on the way.
///
auto make_queued_pair(event_sink& sink)
{
  return sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(sfsm::action(
      [&sink](high& target)
      {
        ++target.entries;
        fire(sink);
      }
    )),
    sfsm::make_transition<high, down, low>(sfsm::action([](low& target) { ++target.entries; }))
  );
}

} // namespace

TEST_CASE("the queue is part of the machine type", "[queue]")
{
  using transitions_type = decltype(make_process_transitions());
  using refusing_type = decltype(make_process());
  using queueing_type = decltype(make_queued_process());

  // The policy is the whole of the answer, no_queue is the one that says there is no queue.
  static_assert(!sfsm::queues_event_v<sfsm::no_queue>);
  static_assert(sfsm::queues_event_v<sfsm::queue_one>);
  static_assert(!refusing_type::queues_event);
  static_assert(queueing_type::queues_event);

  // Either way the answer is whether the event runs, so either way it is a bool.
  static_assert(std::is_same_v<decltype(std::declval<refusing_type&>().process_event(start{})), bool>);
  static_assert(std::is_same_v<decltype(std::declval<queueing_type&>().process_event(start{})), bool>);

  // The queue is the only difference, the table and everything that follows from it is the same.
  static_assert(queueing_type::state_count == refusing_type::state_count);
  static_assert(queueing_type::transition_count == refusing_type::transition_count);
  static_assert(queueing_type::handles_event<start>());
  static_assert(!queueing_type::handles_event<kick>());

  // The same question for one state, which is what a queued event is answered with.
  static_assert(queueing_type::handles_event<idle, start>());
  static_assert(!queueing_type::handles_event<running, start>());
  static_assert(queueing_type::handles_event<running, tick>());
  static_assert(!queueing_type::handles_event<idle, tick>());
  static_assert(queueing_type::state_index_of<stopping>() == refusing_type::state_index_of<stopping>());

  // The events of the table, every type once, no matter how many transitions react to it.
  static_assert(std::is_same_v<sfsm::event_variant_t<transitions_type>, std::variant<start, tick, abort, stopped>>);

  // A machine given no_queue holds nothing for one, so the queue it does not have costs it nothing.
  static_assert(sizeof(refusing_type) < sizeof(queueing_type));

  SUCCEED("the statics hold");
}

TEST_CASE("a queueing machine dispatches like a refusing one", "[queue]")
{
  auto machine = make_queued_process();

  REQUIRE(machine.process_event(start{.budget = 2}));
  REQUIRE(machine.is_state<running>());
  REQUIRE(machine.state<running>().budget == 2);

  REQUIRE(machine.process_event(tick{}));
  REQUIRE(machine.process_event(tick{}));

  // The budget is used up, so the guard of the self transition refuses and no row is left.
  REQUIRE(!machine.process_event(tick{}));
  REQUIRE(machine.state<running>().progress == 2);

  // An event no transition of the table reacts to is ignored as well.
  REQUIRE(!machine.process_event(kick{}));

  REQUIRE(machine.process_event(abort{}));
  REQUIRE(machine.process_event(stopped{}));
  REQUIRE(machine.is_state<idle>());
  REQUIRE(machine.state<idle>().runs == 1);
}

TEST_CASE("an event dispatched from an action is queued", "[queue]")
{
  auto sink = event_sink{};
  auto nested = false;
  auto machine = make_queued_pair(sink);

  sink.callback = [&machine, &nested] { nested = machine.process_event(down{}); };
  sink.armed = true;

  // The action dispatches down while the up transition is on the way, so it cannot run yet.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(nested);
  REQUIRE(sink.calls == 1);

  // By the time the outermost dispatch returned, the queued event has run.
  REQUIRE(machine.is_state<low>());
  REQUIRE(machine.state<high>().entries == 1);
  REQUIRE(machine.state<low>().entries == 1);
}

TEST_CASE("a second event dispatched while one waits is refused", "[queue]")
{
  auto sink = event_sink{};
  auto results = std::vector<bool>{};
  auto machine = make_queued_pair(sink);

  sink.callback = [&machine, &results]
  {
    results.push_back(machine.process_event(down{}));
    results.push_back(machine.process_event(up{}));
  };
  sink.armed = true;

  // The machine holds one event, so the down is taken over and the up that follows it is not.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(results == std::vector{true, false});

  // Only the down ran, so the machine ended in low and never entered high a second time.
  REQUIRE(machine.is_state<low>());
  REQUIRE(machine.state<high>().entries == 1);
  REQUIRE(machine.state<low>().entries == 1);
  REQUIRE(sink.calls == 1);
}

TEST_CASE("a queued event finds the slot free and may queue again", "[queue]")
{
  auto sink = event_sink{};
  auto results = std::vector<bool>{};

  // Both actions call the sink, so the event that was queued can queue one in turn.
  auto machine = sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(sfsm::action(
      [&sink](high& target)
      {
        ++target.entries;
        fire(sink);
      }
    )),
    sfsm::make_transition<high, down, low>(sfsm::action(
      [&sink](low& target)
      {
        ++target.entries;
        fire(sink);
      }
    ))
  );

  // The action of up queues a down, and the down that then runs queues an up of its own. The
  // event leaves the slot before it is dispatched, so the second one finds it free.
  sink.callback = [&]
  {
    if(sink.calls == 1)
    {
      results.push_back(machine.process_event(down{}));
      sink.armed = true;
    }
    else
    {
      results.push_back(machine.process_event(up{}));
    }
  };
  sink.armed = true;

  REQUIRE(machine.process_event(up{}));
  REQUIRE(results == std::vector{true, true});

  // low -> up -> high -> down -> low -> up -> high, so the chain ran to its end.
  REQUIRE(sink.calls == 2);
  REQUIRE(machine.is_state<high>());
  REQUIRE(machine.state<high>().entries == 2);
  REQUIRE(machine.state<low>().entries == 1);
}

TEST_CASE("an event dispatched from a guard is refused", "[queue]")
{
  auto sink = event_sink{};
  auto nested = true;

  auto machine = sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(sfsm::guard(
      [&sink](const low&)
      {
        fire(sink);
        return true;
      }
    )),
    sfsm::make_transition<high, down, low>()
  );

  sink.callback = [&machine, &nested] { nested = machine.process_event(down{}); };
  sink.armed = true;

  // The guard has not decided yet, so there is no state to take the down over for.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(!nested);

  // A queued down would have taken the machine back to low.
  REQUIRE(machine.is_state<high>());
}

TEST_CASE("an event dispatched from an exit hook is refused", "[queue]")
{
  auto sink = event_sink{};
  auto nested = true;

  auto machine = sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(),
    sfsm::make_transition<high, down, low>(),
    sfsm::on_exit<low>(
      [&sink](low& state)
      {
        ++state.exits;
        fire(sink);
      }
    )
  );

  sink.callback = [&machine, &nested] { nested = machine.process_event(down{}); };
  sink.armed = true;

  // The exit hook still belongs to the state that is being left, so the down is refused there.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(!nested);
  REQUIRE(machine.state<low>().exits == 1);
  REQUIRE(machine.is_state<high>());
}

TEST_CASE("a queued event is answered with whether it will find a row", "[queue]")
{
  auto sink = event_sink{};
  auto nested = true;
  auto machine = make_queued_pair(sink);

  // The transition on the way leads to high, and no transition leaves high with an up.
  sink.callback = [&machine, &nested] { nested = machine.process_event(up{}); };
  sink.armed = true;

  REQUIRE(machine.process_event(up{}));
  REQUIRE(!nested);

  // It was taken over all the same, and ignored where it then arrived.
  REQUIRE(machine.is_state<high>());
  REQUIRE(machine.state<high>().entries == 1);
}

TEST_CASE("an event dispatched from a hook is queued", "[queue]")
{
  auto sink = event_sink{};

  auto machine = sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(),
    sfsm::make_transition<high, down, low>(),
    sfsm::on_exit<low>([](low& state) { ++state.exits; }),
    sfsm::on_entry<high>(
      [&sink](high& state)
      {
        ++state.entries;
        fire(sink);
      }
    ),
    sfsm::on_entry<low>([](low& state) { ++state.entries; })
  );

  sink.callback = [&machine] { static_cast<void>(machine.process_event(down{})); };
  sink.armed = true;

  // The entry hook of high dispatches down, which runs once the up transition has completed.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(machine.is_state<low>());
  REQUIRE(machine.state<low>().exits == 1);
  REQUIRE(machine.state<low>().entries == 1);
  REQUIRE(machine.state<high>().entries == 1);
}

TEST_CASE("the queue does not change what a row sees", "[queue]")
{
  auto sink = event_sink{};
  auto in_low = false;
  auto index = std::size_t{0};
  auto reset_accepted = true;
  auto machine = make_queued_pair(sink);

  sink.callback = [&]
  {
    in_low = machine.is_state<low>();
    index = machine.state_index();
    reset_accepted = machine.reset_to_state<low>();
  };
  sink.armed = true;

  REQUIRE(machine.process_event(up{}));

  // The state index moves between the action and the entry hook, so the action still sees low.
  REQUIRE(in_low);
  REQUIRE(index == decltype(machine)::state_index_of<low>());

  // A reset would be overwritten by the transition that is on the way, so it refuses, as ever.
  REQUIRE(!reset_accepted);
  REQUIRE(machine.is_state<high>());
}

TEST_CASE("an event no row reacts to never takes the slot", "[queue]")
{
  auto sink = event_sink{};
  auto results = std::vector<bool>{};
  auto machine = make_queued_pair(sink);

  // No transition of this table reacts to a kick, so it is answered where it is dispatched and
  // the slot stays free for the down that follows it.
  sink.callback = [&machine, &results]
  {
    results.push_back(machine.process_event(kick{}));
    results.push_back(machine.process_event(down{}));
  };
  sink.armed = true;

  REQUIRE(machine.process_event(up{}));
  REQUIRE(results == std::vector{false, true});
  REQUIRE(machine.is_state<low>());
}

TEST_CASE("an event that is not copyable is handed over", "[queue]")
{
  auto machine = make_payload_machine<sfsm::queue_one>();
  auto without_queue = make_payload_machine<sfsm::no_queue>();

  // A machine with a queue takes the event over, so it is handed one and never reads an lvalue.
  // That holds for every event, the one that cannot be copied is only the reason for it.
  static_assert(!dispatches_lvalue<decltype(machine), payload>);
  static_assert(!dispatches_lvalue<decltype(machine), up>);
  static_assert(dispatches_rvalue<decltype(machine), payload>);

  // Without a queue the event is only read, so the same one dispatches either way.
  static_assert(dispatches_lvalue<decltype(without_queue), payload>);
  static_assert(dispatches_lvalue<decltype(without_queue), up>);
  static_assert(dispatches_rvalue<decltype(without_queue), payload>);

  REQUIRE(machine.process_event(payload{.value = std::make_unique<int>(2)}));
  REQUIRE(machine.state<high>().entries == 2);
  REQUIRE(machine.is_state<high>());

  auto event = payload{.value = std::make_unique<int>(2)};
  REQUIRE(without_queue.process_event(event));
  REQUIRE(without_queue.state<high>().entries == 2);
}

TEST_CASE("an event that is not copyable is moved into the queue", "[queue]")
{
  auto sink = event_sink{};

  auto machine = sfsm::make_sfsm<sfsm::queue_one>(
    sfsm::states<low, high>{low{}, high{}},
    sfsm::make_transition<low, up, high>(sfsm::action(
      [&sink](high& target)
      {
        ++target.entries;
        fire(sink);
      }
    )),
    sfsm::make_transition<high, payload, low>(sfsm::action([](const payload& event, low& target)
                                                           { target.entries += *event.value; }))
  );

  auto nested = false;
  sink.callback = [&machine, &nested] { nested = machine.process_event(payload{.value = std::make_unique<int>(3)}); };
  sink.armed = true;

  // The action hands the event over, so it waits in the queue until the transition has completed.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(nested);
  REQUIRE(machine.is_state<low>());
  REQUIRE(machine.state<low>().entries == 3);
}

TEST_CASE("a throwing row drops the queued event", "[queue]")
{
  auto machine = make_assignable_process();

  shared_sink = event_sink{};
  shared_sink.callback = [&machine] { static_cast<void>(machine.process_event(up{})); };
  shared_sink.armed = true;

  // The action queues an up and then throws, so its own transition never completes.
  REQUIRE_THROWS_AS(machine.process_event(kick{}), std::runtime_error);

  // The transition that took the up over never ran, so the up went with it.
  REQUIRE(machine.is_state<low>());
  REQUIRE(machine.state<high>().entries == 0);

  // The queue is empty, so the next dispatch is the event of that call and nothing else. A down
  // leaves high, so it finds no row here.
  REQUIRE(!machine.process_event(down{}));
  REQUIRE(machine.is_state<low>());

  // And the machine dispatches as it did before, the throw cost it nothing but the queued event.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(machine.state<high>().entries == 1);
  REQUIRE(machine.is_state<high>());
}

TEST_CASE("a copy and an assignment are ready to dispatch", "[queue]")
{
  auto machine = make_assignable_process();
  auto other = make_assignable_process();

  // Nothing in this table captures, so it is assignable and not only copyable.
  static_assert(std::is_copy_constructible_v<decltype(machine)>);
  static_assert(std::is_copy_assignable_v<decltype(machine)>);

  shared_sink = event_sink{};
  shared_sink.callback = [&machine] { static_cast<void>(machine.process_event(up{})); };
  shared_sink.armed = true;

  REQUIRE_THROWS_AS(machine.process_event(kick{}), std::runtime_error);
  REQUIRE(machine.is_state<low>());

  // A copy never inherits a transition that runs in the source, nor an event it queued, so it
  // dispatches from the states it took over and nothing else.
  SECTION("a copy takes the states of the machine")
  {
    auto copy = machine;

    REQUIRE(copy.process_event(up{}));
    REQUIRE(copy.state<high>().entries == 1);
    REQUIRE(copy.is_state<high>());
  }

  SECTION("a machine that is assigned to takes them over")
  {
    other = machine;

    REQUIRE(other.process_event(up{}));
    REQUIRE(other.state<high>().entries == 1);
  }

  // The machine the event was dispatched in dispatches on as well.
  REQUIRE(machine.process_event(up{}));
  REQUIRE(machine.state<high>().entries == 1);
  REQUIRE(machine.is_state<high>());
}

TEST_CASE("a queueing machine runs at compile time", "[queue]")
{
  // The queue is one slot in the machine and allocates nothing, so a machine that has one runs at
  // compile time like one that refuses.
  constexpr auto run = []()
  {
    auto machine = make_queued_process();
    machine.process_event(start{.budget = 2});
    machine.process_event(tick{});
    machine.process_event(abort{});
    machine.process_event(stopped{});
    return std::tuple{machine.state<idle>().runs, machine.state_index()};
  };

  static_assert(std::get<0>(run()) == 1);
  static_assert(std::get<1>(run()) == process_states_type::state_index_of<idle>());

  REQUIRE(std::get<0>(run()) == 1);
}

} // namespace sfsm_test
