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
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <model> <witness> [";
    for (const char *o : checks)
      std::cerr << " <" << o << ">";
    std::cerr << " ] [--qbf <list of max number of quantifiers>]\n";
    std::cerr << "Specifying just --qbf results in the most relaxed check.\n";
    exit(1);
  }
  std::vector<unsigned> qbf;
  qbf.reserve(5);
  int i = 3;
  for (; i < argc; ++i) {
    if (!strcmp(argv[i], "--qbf")) break;
    checks[i - 3] = argv[i];
  }
  i++;
  if (i == argc) qbf = {2, 2, 2, 2, 2};
  for (; i < argc; ++i) {
    unsigned n;
    if (std::from_chars(argv[i], argv[i] + std::strlen(argv[i]), n).ec !=
        std::errc())
      break;
    qbf.push_back(n);
  }
  qbf.resize(5);
  return std::tuple{argv[1], argv[2], checks, qbf};
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
    const std::vector<std::pair<unsigned, unsigned>> &shared = {}) {
  std::vector<unsigned> left_m{empty_map(left)};
  map_inputs_and_latches(combined, left_m, left);
  std::vector<unsigned> right_m{empty_map(right)};
  for (auto [m, w] : shared)
    map(right_m, w, left_m.at(m));
  map_inputs_and_latches(combined, right_m, right);
  map_ands(combined, left_m, left);
  map_ands(combined, right_m, right);
  return {left_m, right_m};
}

// Adds specified quantifier to the provided input.
void quantify(aiger *aig, unsigned l, unsigned level = 0) {
  assert(level < 3);
  static constexpr unsigned MAX_NAME_SIZE = 14;
  aiger_symbol *input = aiger_is_input(aig, l);
  assert(input);
  if (input->name) free(input->name);
  input->name = static_cast<char *>(malloc(MAX_NAME_SIZE));
  assert(input->name);
  std::snprintf(input->name, MAX_NAME_SIZE, "%d", level);
}

void add_quantifiers(aiger *aig, auto &&universal, auto &&existential) {
  if (universal.empty() && existential.empty()) return;

  char buffer[80];
  std::snprintf(buffer, sizeof(buffer),
                "Quantified universal %zu existential %zu", universal.size(),
                existential.size());
  aiger_add_comment(aig, buffer);

  // quantification does not implicitly default to 0 in qaiger2qcir
  for (unsigned l : inputs(aig) | lits)
    quantify(aig, l, 0);
  for (unsigned l : universal)
    quantify(aig, l, 1);
  for (unsigned l : existential)
    quantify(aig, l, 2);
}

void add_quantifiers(aiger *aig, auto &&universal) {
  add_quantifiers(aig, universal, std::vector<unsigned>{});
}

// Read the mapping of shared latches from the symbol table of the witness
// circuit. If no mapping is found, it is assumed that the first inputs map to
// each other and the first latches map to each other.
auto shared_inputs_latches(const aiger *model, const aiger *witness) {
  std::vector<std::pair<unsigned, unsigned>> shared;
  std::vector<unsigned> extended;
  shared.reserve(model->num_inputs + model->num_latches);
  extended.reserve(witness->num_inputs + witness->num_latches);
  for (unsigned l : inputs(witness) | lits) {
    if (auto m = simulates_input(model, witness, l))
      shared.emplace_back(m->lit, l);
    else if (auto m = simulates_latch(model, witness, l))
      shared.emplace_back(m->lit, l);
    else
      extended.push_back(l);
  }
  for (unsigned l : latches(witness) | lits) {
    if (auto m = simulates_input(model, witness, l))
      shared.emplace_back(m->lit, l);
    else if (auto m = simulates_latch(model, witness, l))
      shared.emplace_back(m->lit, l);
    else
      extended.push_back(l);
  }
  if (shared.empty()) {
    MSG << "No witness mapping found, using default\n";
    const unsigned n = std::min(model->num_inputs, witness->num_inputs);
    const unsigned m = std::min(model->num_latches, witness->num_latches);
    for (unsigned i = 0; i < n; ++i)
      shared.emplace_back(model->inputs[i].lit, witness->inputs[i].lit);
    for (unsigned i = 0; i < m; ++i)
      shared.emplace_back(model->latches[i].lit, witness->latches[i].lit);
    extended.clear();
    for (unsigned i = n; i < witness->num_inputs; ++i)
      extended.push_back(witness->inputs[i].lit);
    for (unsigned i = m; i < witness->num_latches; ++i)
      extended.push_back(witness->latches[i].lit);
  }
  return std::tuple{shared, extended};
}

// Oracles extend the witness format by enabling more flexible reset,
// transition, and step checks. The only model checking technique requiring
// this extension we have found so far are uniqueness constraints in the
// context of k-induction.
std::vector<unsigned> oracle_inputs(const aiger *witness) {
  std::vector<unsigned> oracles;
  for (const auto &input : inputs(witness))
    if (input.name && std::strncmp(input.name, "oracle", 6) == 0)
      oracles.push_back(input.lit);
  return oracles;
}

// Returns the set of latches influenced by the set of gates as flags.
std::vector<char> out_cone(aiger *aig, const std::vector<unsigned> &gates) {
  std::vector<char> m(size(aig));
  auto mark = [&m](unsigned l, bool v = true) {
    m.at(l) = v;
    m.at(aiger_not(l)) = v;
  };
  for (unsigned l : gates)
    mark(l, true);
  for (auto [a, l, r] : ands(aig))
    mark(a, m[l] | m[r]);
  return m;
}

// Checks if the circuit is stratified (no cyclic dependencies in the reset
// definition) using Kahn. In addition to ands, latches have an edge to their
// reset.
bool stratified_reset(aiger *aig) {
  const unsigned n = aig->maxvar + 1;
  std::vector<unsigned> in_degree(n);
  std::vector<unsigned> stack;
  stack.reserve(n);
  auto inc = [&in_degree](unsigned l) { in_degree.at(l >> 1)++; };
  auto dec = [&in_degree, &stack](unsigned l) {
    if (!--in_degree.at(l >> 1)) stack.push_back(l);
  };
  for (auto [_, l, r] : ands(aig))
    inc(l), inc(r);
  for (auto [_, r] : latches(aig) | initialized | resets)
    inc(r);
  for (unsigned i = 0; i < n; ++i)
    if (!in_degree[i]) stack.push_back(i << 1);
  unsigned visited = 0;
  while (!stack.empty()) {
    visited++;
    unsigned l{stack.back()};
    stack.pop_back();
    if (aiger_and *a = aiger_is_and(aig, l))
      dec(a->rhs0), dec(a->rhs1);
    else if (aiger_symbol *a = aiger_is_latch(aig, l))
      dec(a->reset);
  }
  return visited == n;
}

// QBF checks are very expensive. This function determines which checks
// actually require quantification using conservative heuristics.
void reduce_quantifiers(
    aiger *witness, const std::vector<std::pair<unsigned, unsigned>> &shared,
    const std::vector<unsigned> &extended, const std::vector<unsigned> &oracles,
    std::vector<unsigned> &quantifiers) {
  if (oracles.size() && !std::reduce(quantifiers.begin(), quantifiers.end()))
    MSG << "Warning: The witness contains oracles, but --qbf option is not "
           "used\n";

  auto seconds = std::views::transform([](const auto &p) { return p.second; });
  auto intersects = [](const auto &gates, const auto &flags) {
    return std::ranges::any_of(gates,
                               [&flags](const auto &l) { return flags[l]; });
  };
  auto extended_cone = out_cone(witness, extended);
  auto oracle_cone = out_cone(witness, oracles);

  bool not_stratified = !stratified_reset(witness);
  bool extended_constraint =
      intersects(constraints(witness) | lits, extended_cone);
  bool extended_property =
      intersects(std::vector{output(witness)}, extended_cone);
  bool oracle_constraint = //
      intersects(constraints(witness) | lits, oracle_cone);
  bool oracle_property = //
      intersects(std::vector{output(witness)}, oracle_cone);
  bool oracle_reset =
      intersects(latches(witness) | resets | seconds, oracle_cone);
  bool oracle_transition =
      intersects(latches(witness) | nexts | seconds, oracle_cone);

  unsigned reset_alternations =        //
      oracle_constraint | oracle_reset //
          ? 2
          : extended_constraint | not_stratified;
  unsigned transition_alternations =        //
      oracle_constraint | oracle_transition //
          ? 2
          : extended_constraint;
  unsigned property_alternations = extended_constraint | extended_property;
  unsigned base_alternations = 0;
  unsigned step_alternations = oracle_property | oracle_constraint;

  if (not_stratified && !quantifiers[0]) {
    std::cerr << "Non stratified reset in witness requires quantified reset "
                 "check. Specify --qbf 1\n";
    exit(1);
  }

  quantifiers[0] = std::min(quantifiers[0], reset_alternations);
  quantifiers[1] = std::min(quantifiers[1], transition_alternations);
  quantifiers[2] = std::min(quantifiers[2], property_alternations);
  quantifiers[3] = std::min(quantifiers[3], base_alternations);
  quantifiers[4] = std::min(quantifiers[4], step_alternations);
}

void check_reset(aiger *check, const aiger *model, const aiger *witness,
                 const std::vector<std::pair<unsigned, unsigned>> &shared,
                 const std::vector<unsigned> &extended,
                 const std::vector<unsigned> &oracle) {
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  add_quantifiers(check, extended | map_at(witness_m),
                  oracle | map_at(witness_m));
  std::vector<unsigned> model_latch_reset, witness_latch_reset;
  const char *bad_description;
  bool intersection_only = extended.empty();
  if (intersection_only) { // R{K} ∧ C ⇒ R’{K} ∧ C’
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
  } else { // R ∧ C ⇒ ∃X∀O.(R’ ∧ C’)
    MSG << "Generating QBF reset check\n";
    model_latch_reset.reserve(model->num_latches);
    witness_latch_reset.reserve(witness->num_latches);
    for (auto [l, r] : latches(model) | resets)
      model_latch_reset.push_back(eq(check, model_m.at(l), model_m.at(r)));
    for (auto [l, r] : latches(witness) | resets)
      witness_latch_reset.push_back(
          eq(check, witness_m.at(l), witness_m.at(r)));
    bad_description = "exist(I,L) forall(X) exists(O) R ^ C ^ -(R' ^ C')";
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
                      const std::vector<unsigned> &extended,
                      const std::vector<unsigned> &oracle) {
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
  add_quantifiers(check, extended | map_at(next_witness_m),
                  oracle | map_at(next_witness_m));
  std::vector<unsigned> model_latch_transition, witness_latch_tarnsition;
  const char *bad_description;
  bool intersection_only = extended.empty();
  if (intersection_only) { // F{K} ∧ C0 ∧ C1 ∧ C0’ ⇒ F’{K} ∧ C1’
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
  } else { // F ∧ C0 ∧ C1 ∧ C0’ ⇒ ∃X1∀O1.(F’ ∧ C1’)
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
    bad_description = "exist(I0,L0,I1,L1,X0) forall(X1) exist(O1) F ^ C0 ^ C1 "
                      "^ C0' ^ -(F' ^ C1')";
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
                    const std::vector<std::pair<unsigned, unsigned>> &shared,
                    const std::vector<unsigned> &extended) {
  // C ∧ ¬P ⇒ ∃X.(C’ ∧ ¬P’)
  auto [model_m, witness_m] =
      map_concatenated_circuits(check, model, witness, shared);
  add_quantifiers(check, extended | map_at(witness_m));
  unsigned model_constrained =
      conj(check, constraints(model) | lits | map_at(model_m));
  unsigned witness_constrained =
      conj(check, constraints(witness) | lits | map_at(witness_m));
  unsigned bad = conj(check, {model_m.at(output(model)), model_constrained,
                              witness_constrained,
                              aiger_not(witness_m.at(output(witness)))});
  aiger_add_output(check, bad, "C ^ -P ^ C' ^ P'");
}

void check_base(aiger *check, const aiger *witness) {
  // R’ ∧ C’ ⇒ P’
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

void check_step(aiger *check, const aiger *witness,
                const std::vector<unsigned> &oracle) {
  // ∀O1 P0' ^ F' ^ C0' ^ C1' ⇒ P1'
  auto [current_m, next_m] = map_concatenated_circuits(check, witness, witness);
  add_quantifiers(check, oracle | map_at(current_m));
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
  auto [model_path, witness_path, checks, quantifiers] = param(argc, argv);
  MSG << "Certify Model Checking Witnesses in AIGER\n";
  MSG << VERSION " " GITID "\n";
  InAIG model(model_path);
  InAIG witness(witness_path);
  auto [shared, extended] = shared_inputs_latches(*model, *witness);
  auto oracles = oracle_inputs(*witness);
  reduce_quantifiers(*witness, shared, extended, oracles, quantifiers);

  const std::vector<unsigned> none;
  check_reset(*OutAIG(checks[0]), *model, *witness, shared,
              quantifiers[0] > 0 ? extended : none,
              quantifiers[0] > 1 ? oracles : none);
  check_transition(*OutAIG(checks[1]), *model, *witness, shared,
                   quantifiers[1] > 0 ? extended : none,
                   quantifiers[1] > 1 ? oracles : none);
  check_property(*OutAIG(checks[2]), *model, *witness, shared,
                 quantifiers[2] ? extended : none);
  check_base(*OutAIG(checks[3]), *witness);
  check_step(*OutAIG(checks[4]), *witness, quantifiers[4] ? oracles : none);
}
