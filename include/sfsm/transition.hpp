#pragma once

#include "sfsm/meta.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace sfsm
{

///
/// Helper type describing a state machine transition. Note that source and
/// target state belong to the states of a machine is checked by transitions.
///
template<state_like SourceState, event_like Event, typename Guard, typename Action, state_like TargetState>
  requires(guard_like<Guard, SourceState, Event, TargetState> && action_like<Action, SourceState, Event, TargetState>)
class transition final
{
  // Variables
  std::remove_cvref_t<Guard> guard_;
  std::remove_cvref_t<Action> action_;

public: // Typedefs
  using source_state_type = std::remove_cvref_t<SourceState>;
  using event_type = std::remove_cvref_t<Event>;
  using guard_type = decltype(guard_);
  using action_type = decltype(action_);
  using target_state_type = std::remove_cvref_t<TargetState>;

public: // Constructor
  constexpr transition(guard_type guard, action_type action)
    : guard_{std::move(guard)}
    , action_{std::move(action)}
  {
  }

public: // Accessors
  constexpr auto guard() -> std::reference_wrapper<guard_type> { return std::ref(guard_); }
  constexpr auto action() -> std::reference_wrapper<action_type> { return std::ref(action_); }
};

// clang-format off
template<typename T>
struct is_transition final : public std::false_type {};
template<typename... T>
struct is_transition<transition<T...>> final : public std::true_type {};
template<typename T>
inline constexpr auto is_transition_v = is_transition<T>::value;
template<typename T>
concept transition_like = is_transition_v<T>;
// clang-format on

///
/// Guard letting every transition fire and action doing nothing, the defaults of a transition.
///
inline constexpr auto always = []() { return true; };
inline constexpr auto noop = []() {};
using always_type = std::remove_cvref_t<decltype(always)>;
using noop_type = std::remove_cvref_t<decltype(noop)>;

///
/// Creates a transition of a state machine. Source state, event and target state cannot be
/// deduced from the arguments and have to be named, in the order the table reads.
/// \tparam SourceState state the transition starts in
/// \tparam Event event the transition reacts to
/// \tparam TargetState state the transition ends in
/// \param guard decides whether the transition may fire
/// \param action runs when the transition fires, before the state changes
/// \return transition usable as a row of a transition table
///
template<
  state_like SourceState,
  event_like Event,
  state_like TargetState,
  typename Guard = always_type,
  typename Action = noop_type>
[[nodiscard]] constexpr auto make_transition(Guard guard = {}, Action action = {}) -> transition<
  std::remove_cvref_t<SourceState>,
  std::remove_cvref_t<Event>,
  std::remove_cvref_t<Guard>,
  std::remove_cvref_t<Action>,
  std::remove_cvref_t<TargetState>>
{
  return {std::move(guard), std::move(action)};
}

} // namespace sfsm
