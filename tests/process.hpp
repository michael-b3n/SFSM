#pragma once

#include <sfsm/sfsm.hpp>

///
/// The tests qualify every name with sfsm::, they do not import the namespace. A
/// "using namespace sfsm" would pull the class sfsm into the scope the namespace itself lives in
/// and make the name ambiguous, which is the price of calling the machine sfsm::sfsm.
///
namespace sfsm_test
{

///
/// States of a process. Unlike labels of a classic state machine they carry the data of the run,
/// and they keep it while another state is active.
///
struct idle final
{
  int runs{0};
  int refused{0};
};
struct running final
{
  int progress{0};
  int budget{0};
};
struct stopping final
{
  int pending{0};
};

///
/// Events of a process.
///
struct start final
{
  int budget{0};
};
struct tick final
{};
struct abort final
{};
struct stopped final
{};
struct kick final
{};

using process_states_type = sfsm::states<idle, running, stopping>;

///
/// Rows of a process running idle -> running -> stopping -> idle. It only starts if the running
/// state is free, which is a question about the target, not about the source. Guards and actions
/// cover every subsequence of the canonical source, event, target order.
///
/// The table is handed out on its own so that the same process can be built under either dispatch
/// policy without the rows being written down twice.
///
constexpr auto make_process_transitions()
{
  return sfsm::transitions(
    process_states_type{idle{}, running{}, stopping{}},
    // The target has to be free and the run needs a budget, otherwise the next row takes over.
    sfsm::make_transition<idle, start, running>(
      sfsm::guard([](const start& event, const running& target) { return target.progress == 0 && event.budget > 0; }),
      sfsm::action([](idle&, const start& event, running& target) { target.budget = event.budget; })
    ),
    sfsm::make_transition<idle, start, idle>(sfsm::action([](idle& source) { ++source.refused; })),
    // Self transition, so source and target are the same object.
    sfsm::make_transition<running, tick, running>(
      // The action may be written before the guard, the wrappers say which is which.
      sfsm::action([](running& source) { ++source.progress; }),
      sfsm::guard([](const running& source) { return source.progress < source.budget; })
    ),
    // Hands the progress over to the target and leaves the source free for the next run.
    sfsm::make_transition<running, abort, stopping>(sfsm::action(
      [](running& source, stopping& target)
      {
        target.pending = source.progress;
        source.progress = 0;
        source.budget = 0;
      }
    )),
    sfsm::make_transition<stopping, stopped, idle>(
      sfsm::guard([](const stopping& source) { return source.pending >= 0; }),
      sfsm::action(
        [](stopping& source, idle& target)
        {
          source.pending = 0;
          ++target.runs;
        }
      )
    )
  );
}

///
/// \see make_process_transitions
///
constexpr auto make_process()
{
  return sfsm::sfsm(make_process_transitions());
}

///
/// The same process as a machine that queues an event dispatched from a guard, an action or a
/// hook instead of refusing it. The queue is one slot in the machine and allocates nothing, so
/// this one runs at compile time like the machine above.
/// \see make_process_transitions
///
constexpr auto make_queued_process()
{
  return sfsm::make_sfsm<sfsm::queue_one>(make_process_transitions());
}

} // namespace sfsm_test
