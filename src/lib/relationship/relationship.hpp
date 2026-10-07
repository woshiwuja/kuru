#pragma once
#include "entt/entt.hpp"
struct Relationship {
  entt::entity parent{entt::null}, first{entt::null}, prev{entt::null},
      next{entt::null};
};

inline void attach(entt::registry &reg, entt::entity child,
                   entt::entity parent) {
  reg.get_or_emplace<Relationship>(child);
  reg.get_or_emplace<Relationship>(parent);
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
  if (c.prev != entt::null)
    reg.get<Relationship>(c.prev).next = c.next;
  else if (c.parent != entt::null)
    reg.get<Relationship>(c.parent).first = c.next;
  if (c.next != entt::null)
    reg.get<Relationship>(c.next).prev = c.prev;
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

inline void unlinkDestroyed(entt::registry &reg, entt::entity e) {
  const auto link = [&reg](entt::entity x) -> Relationship * {
    if (x == entt::null || !reg.valid(x)) {
      return nullptr;
    }
    return reg.try_get<Relationship>(x);
  };
  const Relationship c = reg.get<Relationship>(e);
  if (auto *prev = link(c.prev)) {
    prev->next = c.next;
  } else if (auto *parent = link(c.parent)) {
    if (parent->first == e) {
      parent->first = c.next;
    }
  }
  if (auto *next = link(c.next)) {
    next->prev = c.prev;
  }
  for (entt::entity child = c.first; child != entt::null;) {
    auto *r = link(child);
    if (r == nullptr) {
      break;
    }
    const entt::entity next = r->next;
    r->parent = r->prev = r->next = entt::null;
    child = next;
  }
}
