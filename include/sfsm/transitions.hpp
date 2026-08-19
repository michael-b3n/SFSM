#pragma once

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
/// Helper type containing the transition table of a state machine. It owns the states, so a
/// transition only refers to them by type.
///
template<states_like States, transition_like... Transitions>
class transitions final
{
  static_assert(sizeof...(Transitions) > 0, "at least one transition must be available");
  static_assert(
    (States::template is_state_contained<typename std::remove_cvref_t<Transitions>::source_state_type> && ...),
    "source state of every transition must be contained in states"
  );
  static_assert(
    (States::template is_state_contained<typename std::remove_cvref_t<Transitions>::target_state_type> && ...),
    "target state of every transition must be contained in states"
  );

  // Variables
  std::remove_cvref_t<States> states_;
  std::tuple<std::remove_cvref_t<Transitions>...> transitions_;

public: // Typedefs
  using states_type = decltype(states_);
  using transitions_type = decltype(transitions_);
  template<std::size_t I>
  using transition_at = std::tuple_element_t<I, transitions_type>;
  template<std::size_t I>
  using state_at = typename states_type::template state_at<I>;

public: // Constants
  static constexpr std::size_t transition_count = sizeof...(Transitions);

public: // Typedefs
  ///
  /// One row of the transition table with everything resolved to the objects the machine holds.
  ///
  template<state_like SourceState, event_like Event, typename Guard, typename Action, state_like TargetState>
    requires(guard_like<Guard, SourceState, Event, TargetState> && action_like<Action, SourceState, Event, TargetState>)
  struct element final
  {
    static_assert(States::template is_state_contained<SourceState>, "source state must be contained in states");
    static_assert(States::template is_state_contained<TargetState>, "target state must be contained in states");

    // Variables
    std::reference_wrapper<std::remove_cvref_t<SourceState>> source_state;
    std::reference_wrapper<std::remove_cvref_t<Guard>> guard;
    std::reference_wrapper<std::remove_cvref_t<Action>> action;
    std::reference_wrapper<std::remove_cvref_t<TargetState>> target_state;
  };

  template<std::size_t I>
  using element_at = element<
    typename transition_at<I>::source_state_type,
    typename transition_at<I>::event_type,
    typename transition_at<I>::guard_type,
    typename transition_at<I>::action_type,
    typename transition_at<I>::target_state_type>;

public: // Static
  ///
  /// Checks if any transition of the table reacts to an event.
  /// \tparam Event generic event type
  /// \return true, if a transition is triggered by Event, false otherwise
  ///
  template<event_like Event>
  [[nodiscard]] static constexpr auto handles_event() -> bool
  {
    return (std::is_same_v<typename std::remove_cvref_t<Transitions>::event_type, std::remove_cvref_t<Event>> || ...);
  }

public: // Constructor
  constexpr transitions(states_type machine_states, std::remove_cvref_t<Transitions>... rows)
    : states_{std::move(machine_states)}
    , transitions_{std::move(rows)...}
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
  /// Access a row of the transition table.
  /// \tparam I index of the row
  /// \return references to the state, event, guard and action objects of the row
  ///
  template<std::size_t I>
    requires(I < transition_count)
  [[nodiscard]] constexpr auto transition() -> element_at<I>
  {
    using transition_t = transition_at<I>;
    constexpr auto source_index = states_type::template state_index_of<typename transition_t::source_state_type>();
    constexpr auto target_index = states_type::template state_index_of<typename transition_t::target_state_type>();
    auto& row = std::get<I>(transitions_);
    return element_at<I>{
      .source_state = states_.template state<source_index>(),
      .guard = row.guard(),
      .action = row.action(),
      .target_state = states_.template state<target_index>(),
    };
  }
};

template<states_like States, transition_like... Transitions>
transitions(States, Transitions...) -> transitions<std::remove_cvref_t<States>, std::remove_cvref_t<Transitions>...>;
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
