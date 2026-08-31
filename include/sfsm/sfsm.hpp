#pragma once

#include "sfsm/callable.hpp"
#include "sfsm/dispatch.hpp"
#include "sfsm/meta.hpp"
#include "sfsm/states.hpp"
#include "sfsm/transitions.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace sfsm
{

///
/// A finite state machine built from a transition table.
///
/// Unlike a label based machine the states carry data: the table owns one object per state
/// and hands the current one to the guards, actions and hooks. The machine itself only remembers
/// which of them is active, so a state type may occur at most once.
///
/// Transitions are tried in the order they are listed, so two of them may share a source state
/// and an event as long as the more specific one comes first. The machine starts in the source
/// state of the first transition, reset_to_state overrides that.
///
/// A transition that fires runs the exit hook of its source state, then its action, then the
/// entry hook of its target state. A transition is never interrupted, so an event dispatched from
/// a guard, an action or a hook cannot run right away. The \tparam `QueuePolicy` decides what becomes
/// of it: no_queue (default) refuses it, queue_one takes it over once the transition is settled
/// and runs it as soon as that transition has completed.
///
/// Copy and move are constrained on the transition table, so a machine is copyable or movable
/// exactly if its table is. A copy is always ready to dispatch, it never inherits a transition
/// that runs in the source, and never the event that transition queued.
///
/// \tparam Transitions transition table of the machine
/// \tparam QueuePolicy queue policy of the machine, no_queue for one that refuses a nested event
///
template<transitions_like Transitions, queue_policy_like QueuePolicy = no_queue>
class sfsm final
{
  // Typedefs
  using transitions_type = Transitions;
  using states_type = typename transitions_type::states_type;
  using event_queue_type = detail::event_queue<Transitions, QueuePolicy>;

  ///
  /// All phases a transition can be in.
  ///
  enum class transition_phase
  {
    guard,
    exit_hook,
    action,
    entry_hook,
    none
  };

  ///
  /// Exception guard. This object resets needed members of sfsm back to a
  /// well defined state if one of the guards, hooks or actions throw.
  ///
  class exception_guard final
  {
    // Variables
    sfsm& machine_;
    bool active_{true};

  public: // Structors
    constexpr explicit exception_guard(sfsm& machine)
      : machine_{machine}
    {
    }
    exception_guard(const exception_guard&) = delete;
    exception_guard(exception_guard&&) = delete;
    auto operator=(const exception_guard&) -> exception_guard& = delete;
    auto operator=(exception_guard&&) -> exception_guard& = delete;
    constexpr ~exception_guard()
    {
      if(active_)
      {
        machine_.phase_ = transition_phase::none;
        machine_.future_state_index_ = machine_.state_index_;
        if constexpr(queues_event)
        {
          machine_.queue_.clear();
        }
      }
    }

  public: // Modifiers
    constexpr auto deactivate() -> void { active_ = false; }
  };

  // Variables
  transitions_type transitions_;
  std::size_t state_index_;
  std::size_t future_state_index_;
  transition_phase phase_{transition_phase::none};
  [[no_unique_address]] event_queue_type queue_{};

public: // Constants
  static constexpr std::size_t transition_count = transitions_type::transition_count;
  static constexpr std::size_t state_count = states_type::state_count;
  static constexpr bool queues_event = queues_event_v<QueuePolicy>;

public: // Static
  ///
  /// Checks if any transition of the table reacts to an event.
  /// Says nothing about the current state or the guards.
  /// \tparam Event generic event type
  /// \return true, if a transition is triggered by Event, false otherwise
  ///
  template<event_like Event>
  [[nodiscard]] static constexpr auto handles_event() -> bool
  {
    return transitions_type::template handles_event<Event>();
  }

  ///
  /// Checks if a transition leaving a state reacts to an event. Says nothing about the guards.
  /// \tparam State source state of the transition
  /// \tparam Event generic event type
  /// \return true, if a transition leaves State and is triggered by Event, false otherwise
  ///
  template<state_like State, event_like Event>
  [[nodiscard]] static constexpr auto handles_event() -> bool
  {
    return transitions_type::template handles_event<State, Event>();
  }

  ///
  /// Index a state is stored under, so that the value of state_index can be interpreted.
  /// \tparam State state of the state machine
  /// \return index of State
  ///
  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  [[nodiscard]] static constexpr auto state_index_of() -> std::size_t
  {
    return states_type::template state_index_of<State>();
  }

public: // Constructor
  constexpr sfsm(transitions_type table)
    : transitions_{std::move(table)}
    , state_index_{state_index_of<typename transitions_type::initial_state_type>()}
    , future_state_index_{state_index_}
  {
  }

  template<states_like States, row_like... Rows>
    requires(std::same_as<transitions_type, transitions<States, Rows...>>)
  constexpr sfsm(States machine_states, Rows... rows)
    : sfsm{transitions_type{std::move(machine_states), std::move(rows)...}}
  {
  }

  constexpr sfsm(const sfsm& other)
    requires(std::copy_constructible<transitions_type>)
    : transitions_{other.transitions_}
    , state_index_{other.state_index_}
    , future_state_index_{state_index_}
  {
  }

  constexpr sfsm(sfsm&& other) noexcept(std::is_nothrow_move_constructible_v<transitions_type>)
    requires(std::move_constructible<transitions_type>)
    : transitions_{std::move(other.transitions_)}
    , state_index_{other.state_index_}
    , future_state_index_{state_index_}
  {
  }

public: // Operators
  constexpr auto operator=(const sfsm& other) & -> sfsm&
    requires(std::assignable_from<transitions_type&, const transitions_type&>)
  {
    transitions_ = other.transitions_;
    state_index_ = other.state_index_;
    future_state_index_ = state_index_;
    phase_ = transition_phase::none;
    if constexpr(queues_event)
    {
      queue_.clear();
    }
    return *this;
  }

  constexpr auto operator=(sfsm&& other) & noexcept(std::is_nothrow_move_assignable_v<transitions_type>) -> sfsm&
    requires(std::assignable_from<transitions_type&, transitions_type>)
  {
    transitions_ = std::move(other.transitions_);
    state_index_ = other.state_index_;
    future_state_index_ = state_index_;
    phase_ = transition_phase::none;
    if constexpr(queues_event)
    {
      queue_.clear();
    }
    return *this;
  }

public: // Destructor
  ~sfsm() = default;

public: // Accessors
  ///
  /// Access the current state as its index.
  /// \return index of the current state, always smaller than state_count
  /// \see state_index_of
  ///
  [[nodiscard]] constexpr auto state_index() const -> std::size_t { return state_index_; }

  ///
  /// Checks if the machine currently is in a state.
  /// \tparam State state of the state machine
  /// \return true, if the machine is in State, false otherwise
  ///
  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  [[nodiscard]] constexpr auto is_state() const -> bool
  {
    return state_index_ == state_index_of<State>();
  }

  ///
  /// Access the object of a state, active or not.
  /// \tparam State state of the state machine
  /// \return reference to the stored state
  ///
  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  [[nodiscard]] constexpr auto state() -> State&
  {
    return transitions_.template state<state_index_of<State>()>().get();
  }

  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  [[nodiscard]] constexpr auto state() const -> const State&
  {
    return transitions_.template state<state_index_of<State>()>().get();
  }

public: // Modifiers
  ///
  /// Moves the machine to a state. Requires no ongoing transition. A running transition
  /// sets the state when it completes, so it would overwrite the reset.
  /// In such a case this function refuses the reset and returns false.
  /// \tparam State state of the state machine
  /// \return true, if the machine was reset, false if it failed (transition is ongoing)
  ///
  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  constexpr auto reset_to_state() -> bool
  {
    if(phase_ != transition_phase::none)
    {
      return false;
    }
    state_index_ = state_index_of<State>();
    future_state_index_ = state_index_;
    return true;
  }

  ///
  /// Dispatches an event and tries to execute the first transition whose source state and event
  /// match the machine. It first runs the guard, on pass it runs the exit hook of its source state,
  /// its action and the entry hook of its target state. Guard and action are handed the source and
  /// the target state.
  /// \param event event to dispatch
  /// \return true, if a transition fired, false if the event was ignored
  /// The machine ignores an event if a transition is ongoing and `no_queue` is set, the event
  /// is not handled by the current state or, if `queue_one` is set, the event is not handled
  /// from the target state of the ongoing transition.
  ///
  template<event_like Event>
  constexpr auto process_event(const Event& event) -> bool
    requires(!queues_event)
  {
    return process_event_without_queue(event);
  }

  ///
  /// \see process_event
  ///
  template<event_like Event>
  constexpr auto process_event(Event&& event) -> bool
    requires queues_event
  {
    return process_event_with_queue(std::forward<Event>(event));
  }

private: // Implementation
  template<event_like Event>
  constexpr auto process_event_without_queue(const Event& event) -> bool
    requires(!queues_event)
  {
    if(phase_ != transition_phase::none)
    {
      return false;
    }
    return dispatch(event);
  }

  template<typename Event>
    requires(event_like<std::remove_cvref_t<Event>>)
  constexpr auto process_event_with_queue(Event&& event) -> bool
    requires queues_event
  {
    if constexpr(!handles_event<std::remove_cvref_t<Event>>())
    {
      return false;
    }
    else
    {
      switch(phase_)
      {
      // The transition is not settled yet, and the exit hook still belongs to the state that is
      // left, so an event that arrives here is refused.
      case transition_phase::guard: [[fallthrough]];
      case transition_phase::exit_hook: return false;

      // The target state is settled, so the event is taken over and answered with whether a
      // transition of that state reacts to it.
      case transition_phase::action: [[fallthrough]];
      case transition_phase::entry_hook:
        return queue_.push(std::forward<Event>(event)) && handles_event<std::remove_cvref_t<Event>>(future_state_index_);

      case transition_phase::none: break;
      }

      const auto fired = dispatch(event);
      while(const auto queued = queue_.take())
      {
        // this-> spells out the use of the capture, which a generic lambda is warned about otherwise.
        std::visit([this](const auto& queued_event) { std::ignore = this->dispatch(queued_event); }, *queued);
      }
      return fired;
    }
  }

  template<event_like Event>
  constexpr auto dispatch(const Event& event) -> bool
  {
    return try_rows(event, std::make_index_sequence<transitions_type::row_count>{});
  }

  template<event_like Event, std::size_t... I>
  constexpr auto try_rows(const Event& event, std::index_sequence<I...>) -> bool
  {
    return (try_transition<I>(event) || ...);
  }

  template<std::size_t I, event_like Event>
  constexpr auto try_transition([[maybe_unused]] const Event& event) -> bool
  {
    using row_t = typename transitions_type::template row_at<I>;
    if constexpr(!detail::row_handles_event<Event, row_t>())
    {
      return false;
    }
    else
    {
      using source_state_t = typename row_t::source_state_type;
      using target_state_t = typename row_t::target_state_type;
      if(state_index_ != state_index_of<source_state_t>())
      {
        return false;
      }
      // Leaves the machine in a state it can dispatch from again if one of the callables throws.
      auto guard = exception_guard{*this};
      auto row = transitions_.template transition<I>();
      auto& source = row.source_state.get();
      auto& target = row.target_state.get();
      phase_ = transition_phase::guard;
      const auto accepted = detail::invoke_callable(row.guard.get(), std::as_const(source), event, std::as_const(target));
      if(accepted)
      {
        future_state_index_ = state_index_of<target_state_t>();
        phase_ = transition_phase::exit_hook;
        run_hook<callable_role::exit, source_state_t>();
        phase_ = transition_phase::action;
        detail::invoke_callable(row.action.get(), source, event, target);
        state_index_ = state_index_of<target_state_t>();
        phase_ = transition_phase::entry_hook;
        run_hook<callable_role::entry, target_state_t>();
      }
      phase_ = transition_phase::none;
      guard.deactivate();
      return accepted;
    }
  }

  template<callable_role Role, state_like State>
    requires(is_hook_role<Role>)
  constexpr auto run_hook() -> void
  {
    if constexpr(transitions_type::template has_hook<Role, State>)
    {
      detail::invoke_state_callable(transitions_.template hook<Role, State>().get(), state<State>());
    }
  }

  template<event_like Event>
  [[nodiscard]] constexpr auto handles_event(std::size_t state_index) const -> bool
  {
    return []<std::size_t... I>(std::index_sequence<I...>, const std::size_t index)
    {
      return (... || (I == index && handles_event<typename transitions_type::template state_at<I>, Event>()));
    }(std::make_index_sequence<state_count>{}, state_index);
  }
};

template<transitions_like Transitions>
sfsm(Transitions) -> sfsm<Transitions>;
template<states_like States, row_like... Rows>
sfsm(States, Rows...) -> sfsm<transitions<States, Rows...>>;

///
/// Creates a state machine with a queue.
/// \tparam QueuePolicy queue policy of the machine (either `no_queue` or `queue_one`)
/// \param table transition table of the machine
/// \return ready to use state machine, starting in the source state of the first transition
/// \see sfsm
///
template<queue_policy_like QueuePolicy = no_queue, transitions_like Transitions>
[[nodiscard]] constexpr auto make_sfsm(Transitions table) -> sfsm<Transitions, QueuePolicy>
{
  return sfsm<Transitions, QueuePolicy>{std::move(table)};
}

///
/// \see make_sfsm
/// \param machine_states states of the machine
/// \param ...rows transitions and hooks of the machine, at least one transition
///
template<queue_policy_like QueuePolicy = no_queue, states_like States, row_like... Rows>
[[nodiscard]] constexpr auto make_sfsm(States machine_states, Rows... rows) -> sfsm<transitions<States, Rows...>, QueuePolicy>
{
  return sfsm<transitions<States, Rows...>, QueuePolicy>{
    transitions{std::move(machine_states), std::move(rows)...}
  };
}

} // namespace sfsm
