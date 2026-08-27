#pragma once

#include "sfsm/meta.hpp"

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace sfsm
{

///
/// Role a callable plays in a machine. The role is part of the type, so a guard cannot be passed
/// where an action is meant and the order the callables are written in does not matter.
///
enum class callable_role
{
  guard,
  action,
  entry,
  exit,
};

///
/// Tells which half of the enum a role belongs to. A guard and an action are the parts of one
/// transition, an entry and an exit hook belong to a state and are shared by every transition that
/// reaches it, so the two halves are not interchangeable.
///
template<callable_role Role>
inline constexpr bool is_part_role = Role == callable_role::guard || Role == callable_role::action;
template<callable_role Role>
inline constexpr bool is_hook_role = Role == callable_role::entry || Role == callable_role::exit;

namespace detail
{

///
/// Invokes a guard or an action with the arguments it declares. Arguments follow the canonical
/// order source state, event, target state, every subsequence of it is accepted.
///
template<typename C, typename S, typename E, typename T>
constexpr auto invoke_callable(C& callable, S& source, const E& event, T& target) -> decltype(auto)
{
  if constexpr(std::is_invocable_v<C&, S&, const E&, T&>)
  {
    return std::invoke(callable, source, event, target);
  }
  else if constexpr(std::is_invocable_v<C&, S&, const E&>)
  {
    return std::invoke(callable, source, event);
  }
  else if constexpr(std::is_invocable_v<C&, S&, T&>)
  {
    return std::invoke(callable, source, target);
  }
  else if constexpr(std::is_invocable_v<C&, const E&, T&>)
  {
    return std::invoke(callable, event, target);
  }
  else if constexpr(std::is_invocable_v<C&, S&>)
  {
    return std::invoke(callable, source);
  }
  else if constexpr(std::is_invocable_v<C&, const E&>)
  {
    return std::invoke(callable, event);
  }
  else if constexpr(std::is_invocable_v<C&, T&>)
  {
    return std::invoke(callable, target);
  }
  else
  {
    return std::invoke(callable);
  }
}

///
/// Invokes an entry or exit callable with the arguments it declares, its state or nothing.
///
template<typename C, typename S>
constexpr auto invoke_state_callable(C& callable, S& state) -> decltype(auto)
{
  if constexpr(std::is_invocable_v<C&, S&>)
  {
    return std::invoke(callable, state);
  }
  else
  {
    return std::invoke(callable);
  }
}

template<typename C, typename S>
concept invocable_with_state_or_nothing = std::is_invocable_v<C&, S&> || std::is_invocable_v<C&>;

template<typename C, typename S, typename E, typename T>
concept invocable_with_any =
  std::is_invocable_v<C&, S&, const E&, T&> || std::is_invocable_v<C&, S&, const E&> || std::is_invocable_v<C&, S&, T&> ||
  std::is_invocable_v<C&, const E&, T&> || std::is_invocable_v<C&, S&> || std::is_invocable_v<C&, const E&> ||
  std::is_invocable_v<C&, T&> || std::is_invocable_v<C&>;

template<typename C, typename S, typename E, typename T>
using callable_result_t =
  decltype(invoke_callable(std::declval<C&>(), std::declval<S&>(), std::declval<const E&>(), std::declval<T&>()));

///
/// A callable together with the role it plays. Entry and exit callables also carry the state they
/// belong to, guards and actions leave it void. Only guard, action, on_entry and on_exit create
/// these, so the type never has to be named.
/// \tparam Role role the callable plays
/// \tparam Callable the wrapped callable
/// \tparam State state an entry or exit callable belongs to, void for guards and actions
///
template<callable_role Role, plain_type Callable, typename State = void>
class marked_callable final
{
private: // Variables
  Callable callable_;

public: // Typedefs
  using callable_type = Callable;
  using state_type = State;

public: // Constants
  static constexpr callable_role role = Role;

public: // Constructor
  constexpr explicit marked_callable(callable_type callable)
    : callable_{std::move(callable)}
  {
  }

public: // Accessors
  constexpr auto callable() -> std::reference_wrapper<callable_type> { return std::ref(callable_); }
  constexpr auto take() && -> callable_type { return std::move(callable_); }
};

// clang-format off
template<typename T>
struct is_marked_callable final : public std::false_type {};
template<callable_role Role, typename Callable, typename State>
struct is_marked_callable<marked_callable<Role, Callable, State>> final : public std::true_type {};
template<typename T>
inline constexpr auto is_marked_callable_v = is_marked_callable<T>::value;
template<typename T, callable_role Role>
concept marked_as = is_marked_callable_v<T> && (T::role == Role);

// Helper variable template matching a row against a hook role and a state, both given as arguments.
template<callable_role Role, typename State, typename Row>
  requires(is_hook_role<Role>)
inline constexpr bool is_hook_for = false;
template<callable_role Role, typename State, typename Callable>
  requires(is_hook_role<Role>)
inline constexpr bool is_hook_for<Role, State, marked_callable<Role, Callable, State>> = true;
// clang-format on

} // namespace detail

///
/// Concept describing a state machine callable.
/// A callable declares the arguments it needs in the canonical order source state, event,
/// target state and may leave out any of them.
///
template<typename C, typename S, typename E, typename T>
concept callable_like =
  detail::plain_type<C> && detail::copy_or_move_constructible<C> && state_like<S> && event_like<E> && state_like<T> &&
  !std::is_same_v<S, E> && !std::is_same_v<T, E> && detail::invocable_with_any<C, S, E, T>;

///
/// Concept describing the callable of an entry or exit hook.
/// A hook belongs to a state and is invoked with that state or with nothing, the event that
/// caused the change is not part of it because a hook is shared by every transition of its state.
///
template<typename C, typename S>
concept hook_callable_like = detail::plain_type<C> && detail::copy_or_move_constructible<C> && state_like<S> &&
                             detail::invocable_with_state_or_nothing<C, S>;

///
/// Concept describing a state machine guard.
/// A guard decides whether a transition may fire, so it must return bool. Guards of transitions
/// that do not fire run as well, so both states are handed over as const.
///
template<typename G, typename S, typename E, typename T>
concept guard_like = callable_like<G, S, E, T> && detail::invocable_with_any<G, const S, E, const T> &&
                     std::same_as<detail::callable_result_t<G, const S, E, const T>, bool>;

///
/// Concept describing a state machine action.
/// An action runs when a transition fires and may modify both states. It has nothing to answer,
/// so it must return void, which keeps a callable meant as a guard from being taken for one.
///
template<typename A, typename S, typename E, typename T>
concept action_like = callable_like<A, S, E, T> && std::same_as<detail::callable_result_t<A, S, E, T>, void>;

// clang-format off
///
/// Concepts describing a callable that was marked with its role, which is what a transition and a
/// machine are written from. A transition is written from its parts, a guard and an action, and a
/// machine from its rows, of which the hooks are the ones that are a callable.
///
template<typename T>
concept marked_guard_like = detail::marked_as<T, callable_role::guard>;
template<typename T>
concept marked_action_like = detail::marked_as<T, callable_role::action>;
template<typename T>
concept entry_hook_like = detail::marked_as<T, callable_role::entry>;
template<typename T>
concept exit_hook_like = detail::marked_as<T, callable_role::exit>;
template<typename T>
concept hook_like = entry_hook_like<T> || exit_hook_like<T>;
template<typename T>
concept transition_part_like = marked_guard_like<T> || marked_action_like<T>;
// clang-format on

///
/// Guard letting every transition fire and action doing nothing, the defaults of a transition.
///
inline constexpr auto always = []() { return true; };
inline constexpr auto never = []() { return false; };
inline constexpr auto noop = []() {};
using always_type = std::remove_cvref_t<decltype(always)>;
using never_type = std::remove_cvref_t<decltype(never)>;
using noop_type = std::remove_cvref_t<decltype(noop)>;

///
/// Concept describing a callback type that can be used as a fallback.
///
template<typename T>
concept fallback_callable_like =
  std::is_same_v<always_type, std::remove_cvref_t<T>> || std::is_same_v<noop_type, std::remove_cvref_t<T>> ||
  std::is_same_v<never_type, std::remove_cvref_t<T>>;

///
/// Marks a callable as the guard of a transition, which decides whether the transition may fire.
/// \param callable guard of the transition
/// \return marked callable usable as an argument of make_transition
///
template<detail::plain_type Callable>
[[nodiscard]] constexpr auto guard(Callable callable) -> detail::marked_callable<callable_role::guard, Callable>
{
  return detail::marked_callable<callable_role::guard, Callable>{std::move(callable)};
}

///
/// Marks a callable as the action of a transition, which runs when the transition fires.
/// \param callable action of the transition
/// \return marked callable usable as an argument of make_transition
///
template<detail::plain_type Callable>
[[nodiscard]] constexpr auto action(Callable callable) -> detail::marked_callable<callable_role::action, Callable>
{
  return detail::marked_callable<callable_role::action, Callable>{std::move(callable)};
}

///
/// Marks a callable as the entry hook of a state, which runs whenever a transition enters it.
/// The callable takes its state or nothing, a machine holds at most one entry hook per state.
/// \tparam State state the hook belongs to
/// \param callable entry hook of the state
/// \return marked callable usable as a row of a machine
///
template<state_like State, detail::plain_type Callable>
  requires(hook_callable_like<Callable, State>)
[[nodiscard]] constexpr auto on_entry(Callable callable) -> detail::marked_callable<callable_role::entry, Callable, State>
{
  return detail::marked_callable<callable_role::entry, Callable, State>{std::move(callable)};
}

///
/// Marks a callable as the exit hook of a state, which runs whenever a transition leaves it.
/// The callable takes its state or nothing, a machine holds at most one exit hook per state.
/// \tparam State state the hook belongs to
/// \param callable exit hook of the state
/// \return marked callable usable as a row of a machine
///
template<state_like State, detail::plain_type Callable>
  requires(hook_callable_like<Callable, State>)
[[nodiscard]] constexpr auto on_exit(Callable callable) -> detail::marked_callable<callable_role::exit, Callable, State>
{
  return detail::marked_callable<callable_role::exit, Callable, State>{std::move(callable)};
}

} // namespace sfsm
