#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sfsm
{

namespace detail
{

// Helper count type trait.
template<typename T, typename... Ts>
struct count_type;
template<typename T>
struct count_type<T> final
{
  static constexpr std::size_t value = 0;
};
template<typename T, typename T1, typename... Ts>
struct count_type<T, T1, Ts...> final
{
  static constexpr std::size_t value = count_type<T, Ts...>::value + (std::is_same_v<T, T1> ? 1 : 0);
};

// Helper type trait to check if template arguments are unique.
template<typename T, typename... Ts>
struct are_unique;
template<typename T>
struct are_unique<T> final
{
  static constexpr bool value = true;
};
template<typename T, typename T1, typename... Ts>
struct are_unique<T, T1, Ts...> final
{
  static constexpr bool value = count_type<T, T1, Ts...>::value == 0 && are_unique<T1, Ts...>::value;
};

// Helper type trait to find the index of a type within a tuple like type.
template<typename T, typename Ti>
struct type_index;
template<typename Ti>
struct type_index<std::tuple<>, Ti> final
{
  static constexpr std::size_t index = 0;
};
template<typename Ti, typename T1, typename... Ts>
struct type_index<std::tuple<T1, Ts...>, Ti> final
{
  static constexpr std::size_t index = std::is_same_v<T1, Ti> ? 0 : (1 + type_index<std::tuple<Ts...>, Ti>::index);
};

template<typename T>
concept copyable_or_movable = std::copyable<T> || std::movable<T>;
template<typename T>
concept copy_or_move_constructible = std::copy_constructible<T> || std::move_constructible<T>;

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

template<typename C, typename S, typename E, typename T>
concept invocable_with_any =
  std::is_invocable_v<C&, S&, const E&, T&> || std::is_invocable_v<C&, S&, const E&> || std::is_invocable_v<C&, S&, T&> ||
  std::is_invocable_v<C&, const E&, T&> || std::is_invocable_v<C&, S&> || std::is_invocable_v<C&, const E&> ||
  std::is_invocable_v<C&, T&> || std::is_invocable_v<C&>;

template<typename C, typename S, typename E, typename T>
using callable_result_t =
  decltype(invoke_callable(std::declval<C&>(), std::declval<S&>(), std::declval<const E&>(), std::declval<T&>()));

} // namespace detail

///
/// Concept describing a state machine state.
/// A state must be either copyable or movable and cannot be a pointer.
///
template<typename S>
concept state_like = detail::copyable_or_movable<S> && !std::is_pointer_v<S>;

///
/// Concept describing a state machine event.
/// An event must be either copyable or movable.
///
template<typename E>
concept event_like = detail::copyable_or_movable<E>;

///
/// Concept describing a state machine callable.
/// A callable declares the arguments it needs in the canonical order source state, event,
/// target state and may leave out any of them.
///
template<typename C, typename S, typename E, typename T>
concept callable_like = detail::copy_or_move_constructible<C> && state_like<S> && event_like<E> && state_like<T> &&
                        !std::is_same_v<S, E> && !std::is_same_v<T, E> && detail::invocable_with_any<C, S, E, T>;

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
/// An action runs when a transition fires and may modify both states, its result is ignored.
///
template<typename A, typename S, typename E, typename T>
concept action_like = callable_like<A, S, E, T>;

} // namespace sfsm
