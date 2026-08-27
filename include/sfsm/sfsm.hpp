#pragma once

///
/// Umbrella header of the library. Besides the state machine itself it pulls in everything needed
/// to declare one, so it is the only include a user needs.
///

#include "sfsm/callable.hpp"
#include "sfsm/meta.hpp"
#include "sfsm/states.hpp"
#include "sfsm/transitions.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace sfsm
{

///
/// A finite state machine built from a transition table.
///
/// Unlike a label based machine the states carry data: the table owns one object per state type
/// and hands the current one to the guards, actions and hooks. The machine itself only remembers
/// which of them is active, so a state type may occur at most once.
///
/// Transitions are tried in the order they are listed, so two of them may share a source state
/// and an event as long as the more specific one comes first. The machine starts in the source
/// state of the first transition, reset_to_state overrides that.
///
/// A transition that fires runs the exit hook of its source state, then its action, then the
/// entry hook of its target state. A transition is never interrupted, an event dispatched from a
/// guard, an action or a hook is refused.
///
/// Copy and move are constrained on the transition table, so a machine is copyable or movable
/// exactly if its table is. A copy is always ready to dispatch, it never inherits a transition
/// that runs in the source.
///
/// \tparam Transitions transition table of the machine
///
template<transitions_like Transitions>
class sfsm final
{
  // Typedefs
  using transitions_type = Transitions;
  using states_type = typename transitions_type::states_type;

  ///
  /// Marks the machine as transitioning and releases it
  /// when the dispatch ends or a guard, an action or a hook throws.
  ///
  class dispatch_guard final
  {
    // Variables
    sfsm& machine_;

  public: // Structors
    constexpr explicit dispatch_guard(sfsm& machine)
      : machine_{machine}
    {
      machine_.transitioning_ = true;
    }
    dispatch_guard(const dispatch_guard&) = delete;
    dispatch_guard(dispatch_guard&&) = delete;
    auto operator=(const dispatch_guard&) -> dispatch_guard& = delete;
    auto operator=(dispatch_guard&&) -> dispatch_guard& = delete;
    constexpr ~dispatch_guard() { machine_.transitioning_ = false; }
  };

  // Variables
  transitions_type transitions_;
  std::size_t state_index_;
  bool transitioning_{false};

public: // Constants
  static constexpr std::size_t transition_count = transitions_type::transition_count;
  static constexpr std::size_t state_count = states_type::state_count;

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
  ///
  /// Creates a machine from a ready made transition table.
  /// \param table transition table of the machine
  ///
  constexpr sfsm(transitions_type table)
    : transitions_{std::move(table)}
    , state_index_{state_index_of<typename transitions_type::initial_state_type>()}
  {
  }

  ///
  /// Creates a machine from its states and its rows, the transitions and the entry and exit
  /// hooks, which is how a machine is usually written down. The table type follows from the
  /// arguments, so a machine is declared as auto machine = sfsm::sfsm(states, row, row, ...).
  /// \param machine_states states of the machine
  /// \param ...rows transitions and hooks of the machine, at least one transition
  ///
  template<states_like States, row_like... Rows>
    requires(std::same_as<transitions_type, transitions<States, Rows...>>)
  constexpr sfsm(States machine_states, Rows... rows)
    : sfsm(transitions_type{std::move(machine_states), std::move(rows)...})
  {
  }

  constexpr sfsm(const sfsm& other)
    requires(std::copy_constructible<transitions_type>)
    : transitions_{other.transitions_}
    , state_index_{other.state_index_}
  {
  }

  constexpr sfsm(sfsm&& other) noexcept(std::is_nothrow_move_constructible_v<transitions_type>)
    requires(std::move_constructible<transitions_type>)
    : transitions_{std::move(other.transitions_)}
    , state_index_{other.state_index_}
  {
  }

public: // Operators
  constexpr auto operator=(const sfsm& other) & -> sfsm&
    requires(std::assignable_from<transitions_type&, const transitions_type&>)
  {
    transitions_ = other.transitions_;
    state_index_ = other.state_index_;
    transitioning_ = false;
    return *this;
  }

  constexpr auto operator=(sfsm&& other) & noexcept(std::is_nothrow_move_assignable_v<transitions_type>) -> sfsm&
    requires(std::assignable_from<transitions_type&, transitions_type>)
  {
    transitions_ = std::move(other.transitions_);
    state_index_ = other.state_index_;
    transitioning_ = false;
    return *this;
  }

public: // Destructor
  ~sfsm() = default;

public: // Accessors
  ///
  /// Access the current state as its index, for logging or dispatch tables.
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
  /// Moves the machine to a state without running a transition, so no guard, action or hook is
  /// involved. A running transition sets the state when it completes, so it would overwrite the
  /// reset. In this case this function refuses the reset and returns false.
  /// \tparam State state of the state machine
  /// \return true, if the machine was moved, false if it failed (transition is ongoing)
  ///
  template<state_like State>
    requires(states_type::template is_state_contained<State>)
  constexpr auto reset_to_state() -> bool
  {
    if(transitioning_)
    {
      return false;
    }
    state_index_ = state_index_of<State>();
    return true;
  }

  ///
  /// Dispatches an event. The first transition whose source state and event match the machine,
  /// and whose guard accepts, fires: it runs the exit hook of its source state, its action and
  /// the entry hook of its target state. Guard and action are handed the source and the target
  /// state, in a self transition both of them are the same object. Nested transitions are not
  /// allowed and this function returns false if a transition is ongoing.
  /// \param event event to dispatch
  /// \return true, if a transition fired, false if the event was ignored or a transition is ongoing
  ///
  template<event_like Event>
  constexpr auto process_event(const Event& event) -> bool
  {
    return process_event_impl(event, std::make_index_sequence<transitions_type::row_count>{});
  }

private: // Implementation
  ///
  /// \see process_event
  ///
  template<event_like Event, std::size_t... I>
  constexpr auto process_event_impl(const Event& event, std::index_sequence<I...>) -> bool
  {
    if(transitioning_)
    {
      return false;
    }
    const dispatch_guard guard{*this};
    return (try_transition<I>(event) || ...);
  }

  ///
  /// Runs a transition if the row is one and it matches the current state, the event and its guard.
  /// \return true, if the transition fired, false otherwise
  ///
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
      auto row = transitions_.template transition<I>();
      auto& source = row.source_state.get();
      auto& target = row.target_state.get();
      if(!detail::invoke_callable(row.guard.get(), std::as_const(source), event, std::as_const(target)))
      {
        return false;
      }
      run_hook<callable_role::exit, source_state_t>();
      detail::invoke_callable(row.action.get(), source, event, target);
      state_index_ = state_index_of<target_state_t>();
      run_hook<callable_role::entry, target_state_t>();
      return true;
    }
  }

  ///
  /// Runs the entry or exit hook of a state, if the machine has one for it.
  ///
  template<callable_role Role, state_like State>
    requires(is_hook_role<Role>)
  constexpr auto run_hook() -> void
  {
    if constexpr(transitions_type::template has_hook<Role, State>)
    {
      detail::invoke_state_callable(transitions_.template hook<Role, State>().get(), state<State>());
    }
  }
};

template<transitions_like Transitions>
sfsm(Transitions) -> sfsm<Transitions>;
template<states_like States, row_like... Rows>
sfsm(States, Rows...) -> sfsm<transitions<States, Rows...>>;

} // namespace sfsm
