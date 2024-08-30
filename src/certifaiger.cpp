#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <ranges>
#include <span>
#include <vector>

#include "aiger.hpp"

#ifdef QUIET
#define MSG \
  if (0) std::cout
#else
#define MSG std::cout << "Certifaiger: "
#endif

auto param(int argc, char *argv[]) {
  std::vector<const char *> checks{
      "reset.aag", "transition.aag", "property.aag", "base.aag", "step.aag",
  };
  if (argc > 1 && !strcmp(argv[1], "--version")) {
    std::cout << VERSION << '\n';
    exit(0);
  }
  bool weak = argc > 1 && !strcmp(argv[1], "--weak");
  if (argc < 3 || (argc < 4 && weak)) {
    std::cerr << "Usage: " << argv[0] << " <model> <witness> [";
    for (const char *o : checks)
      std::cerr << " <" << o << ">";
    std::cerr << " ]\n";
    exit(1);
  }
  for (int i = 3 + weak; i < argc; ++i)
    checks[i - (3 + weak)] = argv[i];
  return std::tuple{argv[1 + weak], argv[2 + weak], checks, weak};
}

// The ecoding of the checks requires us to map the literals in one circuit to
// new literals in the combinatorial circuit encoding one of the checks.
void map(std::vector<unsigned> &m, unsigned from, unsigned to) {
  m.at(from) = to;
  m.at(aiger_not(from)) = aiger_not(to);
}
// Creates a map of the apropiate size.
std::vector<unsigned> empty_map(const aiger *aig) {
  std::vector<unsigned> m(size(aig), INVALID_LIT);
  map(m, aiger_false, aiger_false);
  return m;
}
bool mapped(const std::vector<unsigned> &m, unsigned l) {
  return m.at(l) != INVALID_LIT;
}
auto unmapped(const std::vector<unsigned> &m) {
  return std::views::filter([&m](const auto &l) { return !mapped(m, l.lit); });
}
auto map_at(const std::vector<unsigned> &m) {
  return std::views::transform([&m](const auto &l) { return m.at(l); });
}
// Both are mapped to inputs in the *to* circuit.
void map_inputs_and_latches(aiger *to, std::vector<unsigned> &m,
                            const aiger *from) {
  for (unsigned l : inputs(from) | unmapped(m) | lits)
    map(m, l, input(to));
  for (unsigned l : latches(from) | unmapped(m) | lits)
    map(m, l, input(to));
}
void map_ands(aiger *to, std::vector<unsigned> &m, const aiger *from) {
  for (auto [a, x, y] : ands(from)) {
    if (mapped(m, a)) continue;
    assert(mapped(m, x) && mapped(m, y));
    map(m, a, conj(to, m.at(x), m.at(y)));
  }
}
// For two circuits C and C' the variable order is
// I L (I'\K) (L'\K) A A' where K are the shared inputs/latches.
// We also use X to denote I'\I and L'\L.
std::pair<std::vector<unsigned>, std::vector<unsigned>>
map_concatenated_circuits(
    aiger *combined, const aiger *left, const aiger *right,
    const std::vector<std::pair<unsigned, unsigned>> &shared = {},
    unsigned *model_inputs_end = nullptr) {
  std::vector<unsigned> left_m{empty_map(left)};
  map_inputs_and_latches(combined, left_m, left);
  if (model_inputs_end) *model_inputs_end = size(combined);
  std::vector<unsigned> right_m{empty_map(right)};
  for (auto [m, w] : shared)
    map(right_m, w, left_m.at(m));
  map_inputs_and_latches(combined, right_m, right);
  map_ands(combined, left_m, left);
  map_ands(combined, right_m, right);
  return {left_m, right_m};
}

void quantify(aiger *aig, unsigned l, bool existential) {
  static constexpr unsigned EXIST = 0, ALL = 1, MAX_NAME_SIZE = 14;
  aiger_symbol *input = aiger_is_input(aig, l);
  assert(input);
  input->name = static_cast<char *>(malloc(MAX_NAME_SIZE));
  assert(input->name);
  std::snprintf(input->name, MAX_NAME_SIZE, "%d", existential ? EXIST : ALL);
}

// Read the mapping of shared latches from the symbol table of the witness
// circuit. If no mapping is found, it is assumed that the entire model is
// embedded at the beginning of the witness.
auto shared_inputs_latches(const aiger *model, const aiger *witness) {
  std::vector<std::pair<unsigned, unsigned>> shared;
  shared.reserve(model->num_inputs + model->num_latches);
  for (unsigned l : inputs(witness) | lits) {
    if (auto m = simulates_input(model, witness, l))
      shared.emplace_back(m->lit, l);
    else if (auto m = simulates_latch(model, witness, l))
      shared.emplace_back(m->lit, l);
  }
  for (unsigned l : latches(witness) | lits) {
    if (auto m = simulates_input(model, witness, l))
      shared.emplace_back(m->lit, l);
    else if (auto m = simulates_latch(model, witness, l))
      shared.emplace_back(m->lit, l);
  }
  if (shared.empty()) {
    MSG << "No witness mapping found, using default\n";
    for (unsigned l : inputs(model) | lits)
      shared.emplace_back(l, l);
    for (unsigned l : latches(model) | lits)
      shared.emplace_back(l, l);
  }
  return shared;
}

// Checks if the circuit is stratified (no cyclic dependencies in the reset
// definition). Assumes the latches are in reversd topological order.
bool stratified_reset(aiger *aig) {
  std::vector<unsigned> s;
  std::vector<unsigned> visted(size(aig));
  unsigned time{1};
  auto visit = [&visted, &time](unsigned l) {
    visted.at(l) = time;
    visted.at(aiger_not(l)) = time;
  };
  visit(aiger_false);
  for (unsigned l : inputs(aig) | lits)
    visit(l);
  for (unsigned l : latches(aig) | uninitialized | lits)
    visit(l);
  for (auto [l, r] : latches(aig) | resets) {
    time++;
    s.push_back(r);
    while (!s.empty()) {
      unsigned l = s.back();
      s.pop_back();
      if (visted.at(l) < time) continue;
      if (visted.at(l) == time) return false;
      visit(l);
      aiger_and *a = aiger_is_and(aig, l);
      if (a) {
        s.push_back(a->rhs0);
        s.push_back(a->rhs1);
        continue;
      }
      return false;
    }
    visit(l);
  }
  return true;
}

bool shared_constraint(
    aiger *aig, const std::vector<std::pair<unsigned, unsigned>> &shared_lits) {
  std::vector<char> shared(size(aig));
  auto share = [&shared](unsigned l) {
    shared.at(l) = true;
    shared.at(aiger_not(l)) = true;
  };
  share(aiger_false);
  for (auto [_, w] : shared_lits)
    share(w);
  for (auto a : ands(aig))
    if (shared[a.rhs0] && shared[a.rhs1]) share(a.lhs);
  for (unsigned l : constraints(aig) | lits)
    if (!shared[l]) return false;
  return true;
}

void check_reset(aiger *check, const aiger *model, const aiger *witness,
                 const std::vector<std::pair<unsigned, unsigned>> &shared,
                 bool quantified) {
  unsigned model_inputs_end{};
  auto [model_m, witness_m] = map_concatenated_circuits(
      check, model, witness, shared, &model_inputs_end);
  std::vector<unsigned> model_latch_reset, witness_latch_reset;
  const char *bad_description;
  if (quantified) { // exist(X): R ^ C => R' ^ C'
    MSG << "Generating QBF reset check\n";
    for (unsigned l : inputs(check) | lits)
      quantify(check, l, l < model_inputs_end);
    model_latch_reset.reserve(model->num_latches);
    witness_latch_reset.reserve(witness->num_latches);
    for (auto [l, r] : latches(model) | resets)
      model_latch_reset.push_back(eq(check, model_m.at(l), model_m.at(r)));
    for (auto [l, r] : latches(witness) | resets)
      witness_latch_reset.push_back(
          eq(check, witness_m.at(l), witness_m.at(r)));
    bad_description = "exist(I,L) forall(X) R ^ C ^ -(R' ^ C')";
  } else { // R{K} ^ C => R'{K} ^ C'
    model_latch_reset.reserve(shared.size());
    witness_latch_reset.reserve(shared.size());
    for (auto [model_l, witness_l] : shared) {
      if (is_latch(model, model_l))
        model_latch_reset.push_back(
            eq(check, model_m.at(model_l), model_m.at(reset(model, model_l))));
      if (is_latch(witness, witness_l))
        witness_latch_reset.push_back(
            eq(check, witness_m.at(witness_l),
               witness_m.at(reset(witness, witness_l))));
    }
    bad_description = "R{K} ^ C ^ -(R'{K} ^ C')";
  }

  unsigned model_reset = conj(check, model_latch_reset);
  unsigned witness_reset = conj(check, witness_latch_reset);
  unsigned model_constrained =
      conj(check, constraints(model) | lits | map_at(model_m));
  unsigned witness_constrained =
      conj(check, constraints(witness) | lits | map_at(witness_m));
  unsigned conclusion = conj(check, witness_reset, witness_constrained);
  unsigned bad =
      conj(check, {model_reset, model_constrained, aiger_not(conclusion)});
  aiger_add_output(check, bad, bad_description);
}

void check_transition(aiger *check, const aiger *model, const aiger *witness,
                      const std::vector<std::pair<unsigned, unsigned>> &shared,
                      bool quantified) {
  std::vector<unsigned> current_model_m{empty_map(model)},
      next_model_m{empty_map(model)}, current_witness_m{empty_map(witness)},
      next_witness_m{empty_map(witness)};
  map_inputs_and_latches(check, current_model_m, model);
  for (auto [m, w] : shared)
    map(current_witness_m, w, current_model_m.at(m));
  map_inputs_and_latches(check, current_witness_m, witness);
  map_inputs_and_latches(check, next_model_m, model);
  for (auto [m, w] : shared)
    map(next_witness_m, w, next_model_m.at(m));
  const unsigned next_model_inputs_end = size(check);
  map_inputs_and_latches(check, next_witness_m, witness);
  map_ands(check, current_model_m, model);
  map_ands(check, current_witness_m, witness);
  map_ands(check, next_model_m, model);
  map_ands(check, next_witness_m, witness);
  std::vector<unsigned> model_latch_transition, witness_latch_tarnsition;
  const char *bad_description;
  if (quantified) { // exist(X1): F ^ C0 ^ C1 ^ C0' => F' ^ C1'
    MSG << "Generating QBF transition check\n";
    for (unsigned l : inputs(check) | lits)
      quantify(check, l, l < next_model_inputs_end);
    model_latch_transition.reserve(model->num_latches);
    witness_latch_tarnsition.reserve(witness->num_latches);
    for (auto [l, n] : latches(model) | nexts)
      model_latch_transition.push_back(
          eq(check, current_model_m.at(n), next_model_m.at(l)));
    for (auto [l, n] : latches(witness) | nexts)
      witness_latch_tarnsition.push_back(
          eq(check, current_witness_m.at(n), next_witness_m.at(l)));
    bad_description =
        "exist(I0,L0,I1,L1,X0) forall(X1) F ^ C0 ^ C1 ^ C0' ^ -(F' ^ C1')";
  } else { // F{K} ^ C0 ^ C1 ^ C0' => F'{K} ^ C1'
    model_latch_transition.reserve(shared.size());
    witness_latch_tarnsition.reserve(shared.size());
    for (auto [model_l, witness_l] : shared) {
      assert(next_model_m.at(model_l) == next_witness_m.at(witness_l));
      if (is_latch(model, model_l))
        model_latch_transition.push_back(
            eq(check, current_model_m.at(next(model, model_l)),
               next_model_m.at(model_l)));
      if (is_latch(witness, witness_l))
        witness_latch_tarnsition.push_back(
            eq(check, current_witness_m.at(next(witness, witness_l)),
               next_witness_m.at(witness_l)));
    }
    bad_description = "F{K} ^ C0 ^ C1 ^ C0' ^ -(F'{K} ^ C1')";
  }

  unsigned model_tarnsition = conj(check, model_latch_transition);
  unsigned witness_transition = conj(check, witness_latch_tarnsition);
  unsigned current_model_constrained =
      conj(check, constraints(model) | lits | map_at(current_model_m));
  unsigned current_witness_constrained =
      conj(check, constraints(witness) | lits | map_at(current_witness_m));
  unsigned next_model_constrained =
      conj(check, constraints(model) | lits | map_at(next_model_m));
  unsigned next_witness_constrained =
      conj(check, constraints(witness) | lits | map_at(next_witness_m));
  unsigned conclusion =
      conj(check, witness_transition, next_witness_constrained);
  unsigned bad =
      conj(check,
           {model_tarnsition, current_model_constrained, next_model_constrained,
            current_witness_constrained, aiger_not(conclusion)});
  aiger_add_output(check, bad, bad_description);
}

void check_property(aiger *check, const aiger *model, const aiger *witness,
                    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // -P ^ C ^ C' => -P'
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  unsigned model_constrained =
      conj(check, constraints(model) | lits | map_at(model_m));
  unsigned witness_constrained =
      conj(check, constraints(witness) | lits | map_at(witness_m));
  unsigned bad = conj(check, {model_m.at(output(model)), model_constrained,
                              witness_constrained,
                              aiger_not(witness_m.at(output(witness)))});
  aiger_add_output(check, bad, "-P ^ C ^ C' ^ P'");
}

void check_base(aiger *check, const aiger *witness) {
  // R' ^ C' => P'
  std::vector<unsigned> m{empty_map(witness)};
  map_inputs_and_latches(check, m, witness);
  map_ands(check, m, witness);
  std::vector<unsigned> latch_reset;
  latch_reset.reserve(witness->num_latches);
  for (auto [l, r] : latches(witness) | resets)
    latch_reset.push_back(eq(check, m.at(l), m.at(r)));
  unsigned witness_reset = conj(check, latch_reset);
  unsigned witness_constrained =
      conj(check, constraints(witness) | lits | map_at(m));
  unsigned bad =
      conj(check, {witness_reset, witness_constrained, m.at(output(witness))});
  aiger_add_output(check, bad, "R' ^ C' ^ -P'");
}

void check_step(aiger *check, const aiger *witness) {
  // P0' ^ F' ^ C0' ^ C1' => P1'
  auto [current_m, next_m] = map_concatenated_circuits(check, witness, witness);
  std::vector<unsigned> latch_transition;
  latch_transition.reserve(witness->num_latches);
  for (auto [l, n] : latches(witness) | nexts)
    latch_transition.push_back(eq(check, current_m.at(n), next_m.at(l)));
  unsigned witness_transition = conj(check, latch_transition);
  unsigned current_constrained =
      conj(check, constraints(witness) | lits | map_at(current_m));
  unsigned next_constrained =
      conj(check, constraints(witness) | lits | map_at(next_m));
  unsigned bad = conj(check, {aiger_not(current_m.at(output(witness))),
                              witness_transition, current_constrained,
                              next_constrained, next_m.at(output(witness))});
  aiger_add_output(check, bad, "P0' ^ F' ^ C0' ^ C1' ^ -P1'");
}

int main(int argc, char *argv[]) {
  auto [model_path, witness_path, checks, weak] = param(argc, argv);
  MSG << "Certify Model Checking Witnesses in AIGER\n";
  MSG << VERSION " " GITID "\n";
  InAIG model(model_path);
  InAIG witness(witness_path);
  std::vector<std::pair<unsigned, unsigned>> shared =
      shared_inputs_latches(*model, *witness);

  const bool QBF_trans = weak && !shared_constraint(*witness, shared);
  const bool QBF_reset = QBF_trans || !stratified_reset(*witness);
  check_reset(*OutAIG(checks[0]), *model, *witness, shared, QBF_reset);
  check_transition(*OutAIG(checks[1]), *model, *witness, shared, QBF_trans);
  check_property(*OutAIG(checks[2]), *model, *witness, shared);
  check_base(*OutAIG(checks[3]), *witness);
  check_step(*OutAIG(checks[4]), *witness);
  return (QBF_trans << 2) | (QBF_reset << 1);
}
