#pragma once

#include "sfsm/callable.hpp"
#include "sfsm/meta.hpp"

#include <cstddef>
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
  Guard guard_;
  Action action_;

public: // Typedefs
  using source_state_type = SourceState;
  using event_type = Event;
  using guard_type = Guard;
  using action_type = Action;
  using target_state_type = TargetState;

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

namespace detail
{

///
/// Picks the callable of a role out of the parts of a transition. The parts are walked from left
/// to right, the callable of the first one that carries the role is taken out of its wrapper.
/// Only that part is moved from, the others are left untouched for the other role to pick.
///
/// This overload ends the walk: no part carried the role, so the fallback is what is used, which
/// is how a transition written without a guard or without an action gets its default.
///
template<callable_role Role, fallback_callable_like Fallback>
  requires(is_part_role<Role>)
constexpr auto pick_callable(Fallback fallback) -> Fallback
{
  return fallback;
}

///
/// \see pick_callable, this overload looks at the leftmost part and hands the rest on.
///
template<callable_role Role, fallback_callable_like Fallback, transition_part_like Part, transition_part_like... Rest>
  requires(is_part_role<Role>)
constexpr auto pick_callable(Fallback fallback, Part& part, Rest&... rest)
{
  if constexpr(marked_as<Part, Role>)
  {
    return std::move(part).take();
  }
  else
  {
    return pick_callable<Role>(std::move(fallback), rest...);
  }
}

///
/// Type pick_callable ends up with, which is the type the transition stores for that role.
///
template<callable_role Role, typename Fallback, typename... Parts>
  requires(is_part_role<Role>)
using picked_callable_t = decltype(pick_callable<Role>(std::declval<Fallback>(), std::declval<Parts&>()...));

///
/// Number of parts of a transition that carry a role.
///
template<callable_role Role, typename... Parts>
  requires(is_part_role<Role>)
inline constexpr std::size_t role_count = ((marked_as<Parts, Role> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});

///
/// Concept describing the parts of one transition: a role may be written down once or not at all,
/// so neither two guards nor two actions are a transition. That the parts are a guard or an action
/// in the first place is what transition_part_like says, this only counts them.
///
template<typename... Parts>
concept at_most_one_guard_and_action =
  role_count<callable_role::guard, Parts...> <= 1 && role_count<callable_role::action, Parts...> <= 1;

} // namespace detail

///
/// Creates a transition of a state machine using Source state, event and target state type information.
/// Guard and action differ in type, so they may be written in any order and either of them may be left out.
/// \tparam SourceState state the transition starts in
/// \tparam Event event the transition reacts to
/// \tparam TargetState state the transition ends in
/// \param ...parts guard and action of the transition, at most one of each
/// \return transition usable as a row of a transition table
///
template<state_like SourceState, event_like Event, state_like TargetState, transition_part_like... Parts>
  requires(detail::at_most_one_guard_and_action<Parts...>)
[[nodiscard]] constexpr auto make_transition(Parts... parts) -> transition<
  SourceState,
  Event,
  detail::picked_callable_t<callable_role::guard, always_type, Parts...>,
  detail::picked_callable_t<callable_role::action, noop_type, Parts...>,
  TargetState>
{
  return {
    detail::pick_callable<callable_role::guard>(always, parts...),
    detail::pick_callable<callable_role::action>(noop, parts...),
  };
}

} // namespace sfsm
