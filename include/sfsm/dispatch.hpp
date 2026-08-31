#pragma once

///
/// What becomes of an event that is dispatched while a transition is on the way: the two policies
/// a machine is given, and the slot the queued event waits in.
///

#include "sfsm/meta.hpp"
#include "sfsm/transitions.hpp"

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace sfsm
{

///
/// The policy of a machine that does not queue: an event dispatched from a guard, an action or a
/// hook is refused instead of taken over, which costs nothing and leaves the decision with the
/// caller. It is the default of sfsm, it holds nothing, and no variant is formed for it.
///
struct no_queue final
{};

///
/// The policy of a machine that queues: an event dispatched from a guard, an action or a hook is
/// taken over and runs once the transition has completed. The machine holds one event at a time,
/// a second one that arrives while that slot is taken is refused.
///
struct queue_one final
{};

///
/// Concept describing the queue policy a machine is given, which is one of the two above.
/// \tparam QueuePolicy queue policy of the machine
///
template<typename QueuePolicy>
concept queue_policy_like = std::same_as<QueuePolicy, no_queue> || std::same_as<QueuePolicy, queue_one>;

///
/// Tells whether a policy queues, which queue_one does and no_queue does not.
/// \tparam QueuePolicy queue policy of the machine
///
template<queue_policy_like QueuePolicy>
inline constexpr bool queues_event_v = std::same_as<QueuePolicy, queue_one>;

namespace detail
{

///
/// Event a row reacts to, void for a hook.
///
template<row_like Row>
[[nodiscard]] consteval auto row_event_of()
{
  if constexpr(transition_like<Row>)
  {
    return std::type_identity<typename Row::event_type>{};
  }
  else
  {
    return std::type_identity<void>{};
  }
}
template<row_like Row>
using row_event_t = typename decltype(row_event_of<Row>())::type;

///
/// Walks the rows of a table and collects the events of its transitions, every type once.
///
template<typename List, typename... Rows>
struct collect_events final
{
  using type = List;
};
template<typename List, typename Row, typename... Rest>
struct collect_events<List, Row, Rest...> final
{
  using type = typename collect_events<typename list_add<List, row_event_t<Row>>::type, Rest...>::type;
};

///
/// Turns the collected list into the variant. A table always has at least one transition, so the
/// list that arrives here always holds at least one type.
///
template<typename List>
struct list_variant;
template<typename... Ts>
struct list_variant<type_list<Ts...>> final
{
  using type = std::variant<Ts...>;
};

template<typename Transitions>
struct table_event_variant;
template<typename States, typename... Rows>
struct table_event_variant<transitions<States, Rows...>> final
{
  using type = typename list_variant<typename collect_events<type_list<>, Rows...>::type>::type;
};

} // namespace detail

///
/// Variant over the events of a transition table, every event type once. It is what a machine
/// holds the event it queued as, so that event is stored by value with neither type erasure nor
/// an allocation for it.
/// \tparam Transitions transition table of the machine
///
template<transitions_like Transitions>
using event_variant_t = typename detail::table_event_variant<std::remove_cvref_t<Transitions>>::type;

namespace detail
{

///
/// The one event a machine holds while a transition is on the way. It takes an event only while
/// it is empty, so what it holds is never overwritten and never lost, and the event leaves it
/// before it is dispatched, which frees the slot for the transition that then runs.
///
template<transitions_like Transitions, queue_policy_like QueuePolicy>
class event_queue final
{
  // Typedefs
  using event_variant_type = event_variant_t<Transitions>;

  // Variables
  std::optional<event_variant_type> event_{};

public: // Modifiers
  ///
  /// Takes an event over, which is stored as the alternative of its own type.
  /// \param event event to queue
  /// \return true, if the event was taken over, false if the slot is already taken
  ///
  template<typename Event>
    requires(event_like<std::remove_cvref_t<Event>>)
  [[nodiscard]] constexpr auto push(Event&& event) -> bool
  {
    if(event_.has_value())
    {
      return false;
    }
    event_.emplace(std::in_place_type<std::remove_cvref_t<Event>>, std::forward<Event>(event));
    return true;
  }

  ///
  /// Takes the event out, which leaves the slot empty for the dispatch that then runs it.
  /// \return the event that waited, nothing if none did
  ///
  [[nodiscard]] constexpr auto take() -> std::optional<event_variant_type> { return std::exchange(event_, std::nullopt); }

  ///
  /// Drops the event that is still waiting, if there is one.
  ///
  constexpr auto clear() -> void { event_.reset(); }
};

///
/// Storage of a machine that was given no_queue: it answers a nested event on the spot and never
/// remembers one, so it holds nothing and no variant is formed for it.
///
template<transitions_like Transitions>
class event_queue<Transitions, no_queue> final
{};

} // namespace detail
} // namespace sfsm
