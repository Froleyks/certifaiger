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
      "reset.aag",    "constraint.aag", "transition.aag",
      "property.aag", "base.aag",       "step.aag",
  };
  if (argc > 1 && !strcmp(argv[1], "--version")) {
    std::cout << VERSION << '\n';
    exit(0);
  }
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <model> <witness> [";
    for (const char *o : checks)
      std::cerr << " <" << o << ">";
    std::cerr << " ]\n";
    exit(1);
  }
  for (int i = 3; i < argc; ++i)
    checks[i - 3] = argv[i];
  return std::tuple{argv[1], argv[2], checks};
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
bool stratified(aiger *aig) {
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

void check_reset_exists(
    aiger *check, const aiger *model, const aiger *witness,
    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // exists(L'\L): R => R'
  MSG << "Non-stratified reset definition\n";
  MSG << "Generating quantified circuit\n";
  unsigned model_inputs_end{};
  auto [model_m, witness_m] = map_concatenated_circuits(
      check, model, witness, shared, &model_inputs_end);
  static constexpr unsigned EXIST = 0, ALL = 1, MAX_NAME_SIZE = 14;
  for (auto &input : inputs(check)) {
    unsigned l = input.lit;
    const bool in_model = l < model_inputs_end;
    input.name = static_cast<char *>(malloc(MAX_NAME_SIZE));
    assert(input.name);
    std::snprintf(input.name, MAX_NAME_SIZE, "%d %c%d", in_model ? EXIST : ALL,
                  in_model ? 'm' : 'w', l);
  }
  std::vector<unsigned> model_latch_is_reset;
  model_latch_is_reset.reserve(model->num_latches);
  for (auto [l, r] : latches(model) | resets)
    model_latch_is_reset.push_back(eq(check, model_m.at(l), model_m.at(r)));
  std::vector<unsigned> witness_latch_is_reset;
  witness_latch_is_reset.reserve(witness->num_latches);
  for (auto [l, r] : latches(witness) | resets)
    witness_latch_is_reset.push_back(
        eq(check, witness_m.at(l), witness_m.at(r)));
  unsigned model_is_reset = conj(check, model_latch_is_reset);
  unsigned witness_is_reset = conj(check, witness_latch_is_reset);
  unsigned bad = conj(check, model_is_reset, aiger_not(witness_is_reset));
  aiger_add_output(check, bad, "exist(L) forall(L'\\L): R ^ -R'");
}

void check_reset(aiger *check, const aiger *model, const aiger *witness,
                 const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // R|K => R'|K
  MSG << "Stratified reset definition\n";
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  std::vector<unsigned> model_latch_is_reset;
  model_latch_is_reset.reserve(shared.size());
  std::vector<unsigned> witness_latch_is_reset;
  witness_latch_is_reset.reserve(shared.size());
  for (auto [model_l, witness_l] : shared) {
    if (is_latch(model, model_l))
      model_latch_is_reset.push_back(
          eq(check, model_m.at(model_l), model_m.at(reset(model, model_l))));
    if (is_latch(witness, witness_l))
      witness_latch_is_reset.push_back(
          eq(check, witness_m.at(witness_l),
             witness_m.at(reset(witness, witness_l))));
  }
  unsigned model_is_reset = conj(check, model_latch_is_reset);
  unsigned witness_is_reset = conj(check, witness_latch_is_reset);
  unsigned bad = conj(check, model_is_reset, aiger_not(witness_is_reset));
  aiger_add_output(check, bad, "R|K ^ -R'|K");
}

void check_constraint(
    aiger *check, const aiger *model, const aiger *witness,
    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // C => C'
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  unsigned model_constraint =
      conj(check, constraints(model) | lits | map_at(model_m));
  unsigned witness_constraint =
      conj(check, constraints(witness) | lits | map_at(witness_m));
  unsigned bad = conj(check, model_constraint, aiger_not(witness_constraint));
  aiger_add_output(check, bad, "C ^ -C'");
}

void check_transition(
    aiger *check, const aiger *model, const aiger *witness,
    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // F|K => F'|K
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  std::vector<unsigned> model_latch_is_next;
  model_latch_is_next.reserve(shared.size());
  std::vector<unsigned> witness_latch_is_next;
  witness_latch_is_next.reserve(shared.size());
  std::vector<unsigned> nexts(shared.size());
  std::ranges::generate(nexts, [check]() { return input(check); });
  for (unsigned i = 0; i < shared.size(); ++i) {
    auto [model_l, witness_l] = shared[i];
    if (is_latch(model, model_l))
      model_latch_is_next.push_back(
          eq(check, model_m.at(next(model, model_l)), nexts[i]));
    if (is_latch(witness, witness_l))
      witness_latch_is_next.push_back(
          eq(check, witness_m.at(next(witness, witness_l)), nexts[i]));
  }
  unsigned model_is_next = conj(check, model_latch_is_next);
  unsigned witness_is_next = conj(check, witness_latch_is_next);
  unsigned bad = conj(check, model_is_next, aiger_not(witness_is_next));
  aiger_add_output(check, bad, "F|K ^ -F'|K");
}

void check_property(aiger *check, const aiger *model, const aiger *witness,
                    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  // P' => P
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  unsigned bad = conj(check, aiger_not(witness_m.at(output(witness))),
                      model_m.at(output(model)));
  aiger_add_output(check, bad, "P' ^ -P");
}

void check_base(aiger *check, const aiger *witness) {
  // R' ^ C' => P'
  std::vector<unsigned> m{empty_map(witness)};
  map_inputs_and_latches(check, m, witness);
  map_ands(check, m, witness);
  std::vector<unsigned> latch_is_reset;
  latch_is_reset.reserve(witness->num_latches);
  for (auto [l, r] : latches(witness) | resets)
    latch_is_reset.push_back(eq(check, m.at(l), m.at(r)));
  unsigned aig_is_reset = conj(check, latch_is_reset);
  unsigned constraint = conj(check, constraints(witness) | lits | map_at(m));
  unsigned bad = conj(check, {aig_is_reset, constraint, m.at(output(witness))});
  aiger_add_output(check, bad, "R' ^ C' ^ -P'");
}

void check_step(aiger *check, const aiger *witness) {
  // P0' ^ F' ^ C0' ^ C'1 => P1'
  auto [current, next] = map_concatenated_circuits(check, witness, witness);
  std::vector<unsigned> latch_is_next;
  latch_is_next.reserve(witness->num_latches);
  for (auto [l, n] : latches(witness) | nexts)
    latch_is_next.push_back(eq(check, current.at(n), next.at(l)));
  unsigned aig_is_next = conj(check, latch_is_next);
  unsigned current_constraint =
      conj(check, constraints(witness) | lits | map_at(current));
  unsigned next_constraint =
      conj(check, constraints(witness) | lits | map_at(next));
  unsigned bad = conj(check, {aiger_not(current.at(output(witness))),
                              aig_is_next, current_constraint, next_constraint,
                              next.at(output(witness))});
  aiger_add_output(check, bad, "P0' ^ F' ^ C0' ^ C'1 ^ -P1'");
}

int main(int argc, char *argv[]) {
  auto [model_path, witness_path, checks] = param(argc, argv);
  MSG << "Certify Model Checking Witnesses in AIGER\n";
  MSG << VERSION " " GITID "\n";
  InAIG model(model_path);
  InAIG witness(witness_path);
  std::vector<std::pair<unsigned, unsigned>> shared =
      shared_inputs_latches(*model, *witness);

  const bool circuits_stratified = stratified(*model) && stratified(*witness);
  if (circuits_stratified)
    check_reset(*OutAIG(checks[0]), *model, *witness, shared);
  else
    check_reset_exists(*OutAIG(checks[0]), *model, *witness, shared);
  check_constraint(*OutAIG(checks[1]), *model, *witness, shared);
  check_transition(*OutAIG(checks[2]), *model, *witness, shared);
  check_property(*OutAIG(checks[3]), *model, *witness, shared);
  check_base(*OutAIG(checks[4]), *witness);
  check_step(*OutAIG(checks[5]), *witness);
  if (!circuits_stratified) return 15;
}
