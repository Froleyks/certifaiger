#pragma once
#include <cassert>
#include <iostream>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

// Wrapper around the aiger library.
extern "C" {
#include "aiger.h"
}

static constexpr unsigned INVALID_LIT = std::numeric_limits<unsigned>::max();

// I don't want to un-const the input circuits, so I'm going to use a const_cast
bool is_input(const aiger *aig, unsigned l) {
  return aiger_is_input(const_cast<aiger *>(aig), l);
}
bool is_latch(const aiger *aig, unsigned l) {
  return aiger_is_latch(const_cast<aiger *>(aig), l);
}
unsigned simulates_lit(const aiger *aig, unsigned l) {
  assert(is_input(aig, l) || is_latch(aig, l));
  const char *name = aiger_get_symbol(const_cast<aiger *>(aig), l);
  if (!name || name[0] != '=') return INVALID_LIT;
  unsigned simulated_l;
  [[maybe_unused]] auto [_, err] =
      std::from_chars(name + 2, name + strlen(name), simulated_l);
  assert(err == std::errc());
  return simulated_l;
}
const aiger_symbol *simulates_input(const aiger *model, const aiger *witness,
                                    unsigned l) {
  const unsigned simulated_l = simulates_lit(witness, l);
  if (simulated_l == INVALID_LIT) return nullptr;
  return aiger_is_input(const_cast<aiger *>(model), simulated_l);
}
const aiger_symbol *simulates_latch(const aiger *model, const aiger *witness,
                                    unsigned l) {
  const unsigned simulated_l = simulates_lit(witness, l);
  if (simulated_l == INVALID_LIT) return nullptr;
  return aiger_is_latch(const_cast<aiger *>(model), simulated_l);
}
unsigned reset(const aiger *aig, unsigned l) {
  assert(is_latch(aig, l));
  return aiger_is_latch(const_cast<aiger *>(aig), l)->reset;
}
unsigned next(const aiger *aig, unsigned l) {
  assert(is_latch(aig, l));
  return aiger_is_latch(const_cast<aiger *>(aig), l)->next;
}
unsigned size(const aiger *aig) { return (aig->maxvar + 1) * 2; }

unsigned input(aiger *aig) {
  const unsigned new_var{size(aig)};
  aiger_add_input(aig, new_var, nullptr);
  assert(size(aig) != new_var);
  return new_var;
};

unsigned conj(aiger *aig, unsigned x, unsigned y) {
  const unsigned new_var{size(aig)};
  aiger_add_and(aig, new_var, x, y);
  return new_var;
};

unsigned eq(aiger *aig, unsigned x, unsigned y) {
  return aiger_not(conj(aig, aiger_not(conj(aig, x, y)),
                        aiger_not(conj(aig, aiger_not(x), aiger_not(y)))));
}

// consumes v
unsigned conj(aiger *aig, std::vector<unsigned> &v) {
  if (v.empty()) return 1;
  const auto begin = v.begin();
  auto end = v.cend();
  bool odd = (begin - end) % 2;
  end -= odd;
  while (end - begin > 1) {
    auto j = begin, i = j;
    while (i != end)
      *j++ = conj(aig, *i++, *i++);
    if (odd) *j++ = *end;
    odd = (begin - j) % 2;
    end = j - odd;
  }
  const unsigned res = *begin;
  v.clear();
  return res;
}
unsigned conj(aiger *aig, std::vector<unsigned> &&v) {
  std::vector<unsigned> tmp(v.begin(), v.end());
  return conj(aig, tmp);
}
unsigned conj(aiger *aig, auto range) {
  std::vector<unsigned> v(range.begin(), range.end());
  return conj(aig, v);
}

std::span<aiger_symbol> inputs(const aiger *aig) {
  return {aig->inputs, aig->num_inputs};
}
std::span<aiger_symbol> latches(const aiger *aig) {
  return {aig->latches, aig->num_latches};
}
std::span<aiger_and> ands(const aiger *aig) {
  return {aig->ands, aig->num_ands};
}
std::span<aiger_symbol> constraints(const aiger *aig) {
  return {aig->constraints, aig->num_constraints};
}
unsigned output(const aiger *aig) {
  if (aig->num_bad)
    return aig->bad[0].lit;
  else if (aig->num_outputs)
    return aig->outputs[0].lit;
  else
    return aiger_false;
}

auto lits = std::views::transform([](const auto &l) { return l.lit; });
auto nexts = std::views::transform([](const auto &l) {
  return std::pair{l.lit, l.next};
});
auto resets = std::views::transform([](const auto &l) {
  return std::pair{l.lit, l.reset};
});
auto initialized =
    std::views::filter([](const auto &l) { return l.reset != l.lit; });
auto uninitialized =
    std::views::filter([](const auto &l) { return l.reset == l.lit; });

bool inputs_latches_reencoded(aiger *aig) {
  unsigned v{2};
  for (unsigned l : inputs(aig) | lits) {
    if (l != v) return false;
    v += 2;
  }
  for (unsigned l : latches(aig) | lits) {
    if (l != v) return false;
    v += 2;
  }
  return true;
}

// Read only circuit for model and witness.
struct InAIG {
  aiger *aig;
  InAIG(const char *path) : aig(aiger_init()) {
    const char *err = aiger_open_and_read_from_file(aig, path);
    if (err) {
      std::cerr << "certifaiger: parse error reading " << path << ": " << err
                << "\n";
      exit(1);
    }
    if (!inputs_latches_reencoded(aig)) {
      std::cerr << "certifaiger: inputs and latches have to be reencoded even "
                   "in ASCII format "
                << path << "\n";
      exit(2);
    }
    if (aig->num_justice + aig->num_fairness)
      std::cout
          << "certifaiger: WARNING justice and fairness are not supported "
          << path << "\n";
    if (aig->num_bad + aig->num_outputs > 1)
      std::cout << "certifaiger: WARNING Multiple properties. Using "
                << (aig->num_bad ? "bad" : "output") << "0 of " << path << "\n";
  }
  ~InAIG() { aiger_reset(aig); }
  aiger *operator*() const { return aig; }
};

// Wrapper for combinatorial circuits meant to be checked for validity via SAT.
struct OutAIG {
  aiger *aig; // combinatorial
  const char *path;
  OutAIG(const char *path) : aig(aiger_init()), path(path) {}
  ~OutAIG() {
    assert(!aig->num_latches);
    aiger_open_and_write_to_file(aig, path);
    aiger_reset(aig);
  }
  aiger *operator*() const { return aig; }
};
