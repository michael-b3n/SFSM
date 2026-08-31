#pragma once

#include "sfsm/callable.hpp"
#include "sfsm/states.hpp"
#include "sfsm/transition.hpp"

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sfsm
{

///
/// Concept describing a row of a machine, which is either a transition or an entry or exit hook.
///
template<typename R>
concept row_like = transition_like<R> || hook_like<R>;

namespace detail
{

///
/// Checks that every state a row refers to belongs to the states of the machine.
///
template<states_like States, row_like Row>
[[nodiscard]] constexpr auto row_states_contained() -> bool
{
  if constexpr(transition_like<Row>)
  {
    constexpr auto source_contained = States::template is_state_contained<typename Row::source_state_type>;
    constexpr auto target_contained = States::template is_state_contained<typename Row::target_state_type>;
    return source_contained && target_contained;
  }
  else
  {
    return States::template is_state_contained<typename Row::state_type>;
  }
}

///
/// Checks if a row is a transition reacting to an event.
///
template<event_like Event, row_like Row>
[[nodiscard]] constexpr auto row_handles_event() -> bool
{
  if constexpr(transition_like<Row>)
  {
    return std::is_same_v<typename Row::event_type, Event>;
  }
  else
  {
    return false;
  }
}

///
/// Checks if a row is a transition leaving a state and reacting to an event.
///
template<state_like State, event_like Event, row_like Row>
[[nodiscard]] constexpr auto row_handles_event() -> bool
{
  if constexpr(transition_like<Row>)
  {
    return std::is_same_v<typename Row::source_state_type, State> && std::is_same_v<typename Row::event_type, Event>;
  }
  else
  {
    return false;
  }
}

///
/// Counts the rows that are the hook of the same role and state as Row, the row itself included.
/// A transition is not a hook and counts as zero.
///
template<row_like Row, row_like... AllRows>
[[nodiscard]] constexpr auto same_hook_count() -> std::size_t
{
  if constexpr(transition_like<Row>)
  {
    return 0;
  }
  else
  {
    return (
      (is_hook_for<Row::role, typename Row::state_type, AllRows> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0}
    );
  }
}

///
/// Checks that no state has two entry hooks and that none has two exit hooks.
///
/// Rows is named twice on purpose. The fold expands the first one, which is the row being looked
/// at, while the second is a nested expansion and is therefore handed to every call whole. So every
/// row is counted against the full list, the two are not walked in lockstep. A row counts itself,
/// which is why one is the number a unique hook reaches.
///
template<row_like... Rows>
[[nodiscard]] constexpr auto hooks_are_unique() -> bool
{
  return ((same_hook_count<Rows, Rows...>() <= 1) && ...);
}

///
/// Index of the first row that is a transition, the count of the rows if there is none.
///
template<row_like... Rows>
[[nodiscard]] constexpr auto first_transition_index() -> std::size_t
{
  auto index = std::size_t{0};
  const auto found = ((transition_like<Rows> ? true : (++index, false)) || ...);
  return found ? index : sizeof...(Rows);
}

///
/// Index of the hook of a role and a state, the count of the rows if there is none.
///
template<callable_role Role, state_like State, row_like... Rows>
  requires(is_hook_role<Role>)
[[nodiscard]] constexpr auto hook_index() -> std::size_t
{
  auto index = std::size_t{0};
  const auto found = ((is_hook_for<Role, State, Rows> ? true : (++index, false)) || ...);
  return found ? index : sizeof...(Rows);
}

} // namespace detail

///
/// Helper type containing the rows of a state machine, its transitions and its entry and exit
/// hooks. It owns the states, so a row only refers to them by type.
///
template<states_like States, row_like... Rows>
class transitions final
{
  // Typedefs
  using row_tuple_type = std::tuple<Rows...>;

  // Variables
  States states_;
  row_tuple_type rows_;

public: // Typedefs
  using states_type = States;
  template<std::size_t I>
  using row_at = std::tuple_element_t<I, row_tuple_type>;
  template<std::size_t I>
  using state_at = typename states_type::template state_at<I>;

public: // Constants
  static constexpr std::size_t row_count = sizeof...(Rows);
  static constexpr std::size_t transition_count =
    ((transition_like<Rows> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
  template<callable_role Role, state_like State>
    requires(is_hook_role<Role>)
  static constexpr bool has_hook = (detail::is_hook_for<Role, State, Rows> || ...);

  // Checks
  static_assert(transition_count > 0, "at least one transition must be available");
  static_assert(
    (detail::row_states_contained<states_type, Rows>() && ...),
    "every state a transition or a hook refers to must be contained in states"
  );
  static_assert(
    detail::hooks_are_unique<Rows...>(), "a state must not have more than one entry hook and not more than one exit hook"
  );

public: // Typedefs
  ///
  /// State the machine starts in, the source state of the first transition.
  ///
  using initial_state_type = typename row_at<detail::first_transition_index<Rows...>()>::source_state_type;

  ///
  /// One row of the transition table with everything resolved to the objects the machine holds.
  ///
  template<state_like SourceState, event_like Event, typename Guard, typename Action, state_like TargetState>
    requires(guard_like<Guard, SourceState, Event, TargetState> && action_like<Action, SourceState, Event, TargetState>)
  struct element final
  {
    static_assert(states_type::template is_state_contained<SourceState>, "source state must be contained in states");
    static_assert(states_type::template is_state_contained<TargetState>, "target state must be contained in states");

    // Variables
    std::reference_wrapper<SourceState> source_state;
    std::reference_wrapper<Guard> guard;
    std::reference_wrapper<Action> action;
    std::reference_wrapper<TargetState> target_state;
  };

  template<std::size_t I>
  using element_at = element<
    typename row_at<I>::source_state_type,
    typename row_at<I>::event_type,
    typename row_at<I>::guard_type,
    typename row_at<I>::action_type,
    typename row_at<I>::target_state_type>;

public: // Static
  ///
  /// Checks if any transition of the table reacts to an event.
  /// \tparam Event generic event type
  /// \return true, if a transition is triggered by Event, false otherwise
  ///
  template<event_like Event>
  [[nodiscard]] static constexpr auto handles_event() -> bool
  {
    return (detail::row_handles_event<Event, Rows>() || ...);
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
    return (detail::row_handles_event<State, Event, Rows>() || ...);
  }

public: // Constructor
  constexpr transitions(states_type machine_states, Rows... rows)
    : states_{std::move(machine_states)}
    , rows_{std::move(rows)...}
  {
  }

public: // Accessors
  ///
  /// Access the object of a state, the table owns one per state type.
  /// \tparam I index of the state
  /// \return reference to the stored state
  ///
  template<std::size_t I>
    requires(I < states_type::state_count)
  [[nodiscard]] constexpr auto state() -> std::reference_wrapper<state_at<I>>
  {
    return states_.template state<I>();
  }

  ///
  /// \see non const accessor state
  ///
  template<std::size_t I>
    requires(I < states_type::state_count)
  [[nodiscard]] constexpr auto state() const -> std::reference_wrapper<const state_at<I>>
  {
    return states_.template state<I>();
  }

  ///
  /// Access a row of the table that is a transition.
  /// \tparam I index of the row
  /// \return references to the state, guard and action objects of the row
  ///
  template<std::size_t I>
    requires(I < row_count && transition_like<row_at<I>>)
  [[nodiscard]] constexpr auto transition() -> element_at<I>
  {
    using row_t = row_at<I>;
    constexpr auto source_index = states_type::template state_index_of<typename row_t::source_state_type>();
    constexpr auto target_index = states_type::template state_index_of<typename row_t::target_state_type>();
    auto& row = std::get<I>(rows_);
    return element_at<I>{
      .source_state = states_.template state<source_index>(),
      .guard = row.guard(),
      .action = row.action(),
      .target_state = states_.template state<target_index>(),
    };
  }

  ///
  /// Access the entry or exit hook of a state.
  /// \tparam Role entry or exit
  /// \tparam State state the hook belongs to
  /// \return reference to the stored callable of the hook
  ///
  template<callable_role Role, state_like State>
    requires(is_hook_role<Role> && has_hook<Role, State>)
  [[nodiscard]] constexpr auto hook() -> decltype(auto)
  {
    constexpr auto index = detail::hook_index<Role, State, Rows...>();
    return std::get<index>(rows_).callable();
  }
};

template<states_like States, row_like... Rows>
transitions(States, Rows...) -> transitions<States, Rows...>;
// clang-format off
template<typename T>
struct is_transitions final : public std::false_type {};
template<typename... T>
struct is_transitions<transitions<T...>> final : public std::true_type {};
template<typename T>
inline constexpr auto is_transitions_v = is_transitions<T>::value;
template<typename T>
concept transitions_like = is_transitions_v<T>;
// clang-format on

} // namespace sfsm
