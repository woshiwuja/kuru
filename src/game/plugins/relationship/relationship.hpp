#include "entt/entt.hpp"
struct Relationship {
  entt::entity parent{entt::null}, first{entt::null}, prev{entt::null},
      next{entt::null};
};
inline void attach(entt::registry &reg, entt::entity child, entt::entity parent) {
  reg.get_or_emplace<Relationship>(child);
  reg.get_or_emplace<Relationship>(
      parent);
  auto &c = reg.get<Relationship>(child);
  auto &p = reg.get<Relationship>(parent);
  assert(c.parent == entt::null);
  c.parent = parent;
  c.next = p.first;
  if (p.first != entt::null)
    reg.get<Relationship>(p.first).prev = child;
  p.first = child;
}
inline void detach(entt::registry &reg, entt::entity child) {
    auto &c = reg.get<Relationship>(child);
    if (c.prev != entt::null) reg.get<Relationship>(c.prev).next = c.next;
    else if (c.parent != entt::null) reg.get<Relationship>(c.parent).first = c.next;
    if (c.next != entt::null) reg.get<Relationship>(c.next).prev = c.prev;
    c.parent = c.prev = c.next = entt::null;
}
template <typename F>
void each_child(entt::registry &reg, entt::entity p, F func) {
    for (auto c = reg.get<Relationship>(p).first; c != entt::null;) {
        auto next = reg.get<Relationship>(c).next;
        func(c);
        c = next;
    }
}
template <typename T, typename F>
void walk(entt::registry &reg, entt::entity e, T type, F &func) {
    auto p = reg.get<T>(e);
    func(e, type);
    if (reg.try_get<Relationship>(e))
        each_child(reg, e, [&](auto c) { walk(reg, c, type, func); });
}

template <typename F, typename T>
void walk_all(entt::registry &reg, F f) {
    for (auto e : reg.view<T>()) {
        auto *r = reg.try_get<Relationship>(e);
        if (!r || r->parent == entt::null) walk(reg, e, T{}, f);
    }
}
