#pragma once

#include "sfsm/meta.hpp"

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sfsm
{

///
/// Helper type containing all states of a state machine.
///
template<state_like... States>
class states
{
  static_assert(sizeof...(States) > 0, "at least one state must be available");
  static_assert(detail::are_unique<States...>::value, "all states must be unique");

  // Typedefs
  using state_tuple_type = std::tuple<States...>;

  // Variables
  state_tuple_type states_;

public: // Typedefs
  template<std::size_t I>
  using state_at = std::tuple_element_t<I, state_tuple_type>;

public: // Constants
  static constexpr std::size_t state_count = sizeof...(States);
  template<state_like State>
  static constexpr auto is_state_contained = detail::count_type<State, States...>::value == 1;

public: // Static
  ///
  /// Index a state is stored under.
  /// \tparam State state of the state machine
  /// \return index of State
  ///
  template<state_like State>
  [[nodiscard]] static constexpr auto state_index_of() -> std::size_t
    requires(is_state_contained<State>)
  {
    return detail::type_index<state_tuple_type, State>::index;
  }

public: // Constructor
  constexpr states(States... objects)
    : states_{std::move(objects)...}
  {
  }

public: // Accessors
  template<std::size_t I>
  [[nodiscard]] constexpr auto state() -> std::reference_wrapper<state_at<I>>
    requires(I < state_count)
  {
    return std::ref(std::get<I>(states_));
  }

  template<std::size_t I>
  [[nodiscard]] constexpr auto state() const -> std::reference_wrapper<const state_at<I>>
    requires(I < state_count)
  {
    return std::cref(std::get<I>(states_));
  }
};

template<state_like... States>
states(States...) -> states<States...>;
// clang-format off
template<typename S>
struct is_states final : public std::false_type {};
template<typename... S>
struct is_states<states<S...>> final : public std::true_type {};
template<typename S>
inline constexpr auto is_states_v = is_states<S>::value;
template<typename S>
concept states_like = is_states_v<S>;
// clang-format on

} // namespace sfsm
