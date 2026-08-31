#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>

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

// Helper type list types are collected in. A list is what a walk over types starts from and adds
// to, and it is not the type the result is asked for: a variant, for one, must not be instantiated
// without alternatives, while an empty list is a list.
template<typename... Ts>
struct type_list final
{};

// Helper type trait to append a type to a list, unless the list already holds it or the type is
// void, which is what something with no type to contribute answers with.
template<typename List, typename T>
struct list_add;
template<typename... Ts>
struct list_add<type_list<Ts...>, void> final
{
  using type = type_list<Ts...>;
};
template<typename... Ts, typename T>
struct list_add<type_list<Ts...>, T> final
{
  using type = std::conditional_t<(std::is_same_v<T, Ts> || ...), type_list<Ts...>, type_list<Ts..., T>>;
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
template<typename T>
concept plain_type = std::same_as<T, std::remove_cvref_t<T>>;

} // namespace detail

///
/// Concept describing a state machine state.
/// A state is stored by value, so it must be an unqualified type that is either copyable or
/// movable, and it cannot be a pointer.
///
template<typename S>
concept state_like = detail::plain_type<S> && detail::copyable_or_movable<S> && !std::is_pointer_v<S>;

///
/// Concept describing a state machine event.
/// An event must be an unqualified type that is either copyable or movable.
///
template<typename E>
concept event_like = detail::plain_type<E> && detail::copyable_or_movable<E>;

} // namespace sfsm
