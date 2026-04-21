#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aiger.h"
namespace {

constexpr unsigned INVALID_LIT = std::numeric_limits<unsigned>::max();
constexpr unsigned circuits{2}; // W, M
constexpr unsigned times{3};    // t0, t1, t2
aiger *model, *witness, *check;
std::array<aiger *, circuits> aig;
unsigned next_lit{2};
struct predicates {
  unsigned R{1}, RK{1}, F{1}, FK{1}, C{1}, P{1};
  std::vector<unsigned> Q;
};

bool reencoded(const aiger *circuit) {
  unsigned l{};
  for (unsigned i = 0; i < circuit->num_inputs; ++i)
    if (circuit->inputs[i].lit != 2 * (++l)) return false;
  for (unsigned i = 0; i < circuit->num_latches; ++i)
    if (circuit->latches[i].lit != 2 * (++l)) return false;
  return true;
}

// Parse command-line arguments, initialize aigs
const char *initialize(int argc, char *argv[]) {
  if (argc > 1 && !std::strcmp(argv[1], "--version"))
    std::cout << VERSION << "\n", exit(0);
  if (argc < 3)
    std::cerr << "Usage: " << argv[0] << " model witness [check=check.aig]\n",
        exit(1);
  // for the rest of the logic witness comes before model
  const char *paths[3] = {argv[2], argv[1], argc > 3 ? argv[3] : "check.aig"};
  check = aiger_init();
  aig = {witness = aiger_init(), model = aiger_init()};
  for (unsigned c = 0; c < circuits; ++c) {
    if (const char *err = aiger_open_and_read_from_file(aig[c], paths[c]))
      std::cerr << "Error reading " << (c ? "model '" : "witness '") << paths[c]
                << "': " << err << '\n',
          exit(1);

    if (!reencoded(aig[c]))
      std::cerr << "Error: " << (c ? "model '" : "witness '") << paths[c]
                << "' is not reencoded\n",
          exit(1);
  }
  std::cout << "Certify Model Checking Witnesses in AIGER\n";
  std::cout << VERSION << " " << GITID << "\n";

  return paths[2];
}

void finalize(const char *path) {
  if (!aiger_open_and_write_to_file(check, path))
    std::cerr << "Error writing " << path << "\n", exit(1);
  aiger_reset(check);
  aiger_reset(witness);
  aiger_reset(model);
}

// Checks if the circuit is stratified (no cyclic dependencies in the reset
// definition) using Kahn. In addition to ands, latches have an edge to their
// reset.
bool stratified(const aiger *circuit) {
  const unsigned n = circuit->maxvar + 1;
  std::vector<unsigned> in_degree(n);
  std::vector<unsigned> stack;
  stack.reserve(n);
  for (unsigned i = 0; i < circuit->num_ands; ++i) {
    aiger_and *a = circuit->ands + i;
    in_degree[aiger_lit2var(a->rhs0)]++;
    in_degree[aiger_lit2var(a->rhs1)]++;
  }
  for (unsigned i = 0; i < circuit->num_latches; ++i) {
    aiger_symbol *l = circuit->latches + i;
    if (l->reset != l->lit) in_degree[aiger_lit2var(l->reset)]++;
  }
  for (unsigned i = 0; i < n; ++i)
    if (!in_degree[i]) stack.push_back(i);
  unsigned visited{};
  while (!stack.empty()) {
    unsigned l{aiger_var2lit(stack.back())};
    stack.pop_back();
    visited++;
    if (aiger_and *a = aiger_is_and(circuit, l)) {
      unsigned s = aiger_lit2var(a->rhs0);
      unsigned t = aiger_lit2var(a->rhs1);
      if (!--in_degree[s]) stack.push_back(s);
      if (!--in_degree[t]) stack.push_back(t);
    } else if (aiger_symbol *lat = aiger_is_latch(circuit, l)) {
      unsigned r = aiger_lit2var(lat->reset);
      if (lat->reset != lat->lit && !--in_degree[r]) stack.push_back(r);
    }
  }
  return visited == n;
}

unsigned conj(unsigned s, unsigned t) {
  assert(next_lit % 2 == 0);
  aiger_add_and(check, next_lit, s, t);
  unsigned ret{next_lit};
  next_lit += 2;
  return ret;
}
template <typename... Rest>
unsigned conj(unsigned s, unsigned t, unsigned u, Rest... rest) {
  return conj(conj(s, t), u, rest...);
}
template <typename... Rest>
unsigned disj(unsigned s, unsigned t, Rest... rest) {
  return aiger_not(conj(aiger_not(s), aiger_not(t), aiger_not(rest)...));
}
unsigned imply(unsigned s, unsigned t) {
  return aiger_not(conj(s, aiger_not(t)));
}
unsigned equivalent(unsigned s, unsigned t) {
  return conj(imply(s, t), imply(t, s));
}

std::optional<std::pair<unsigned, std::string_view>>
parse_num(std::string_view c, const char *msg) {
  const auto start = c.find_first_not_of(" \f\n\r\t\v");
  if (start == std::string_view::npos) return std::nullopt;
  c = c.substr(start);
  unsigned value{};
  auto [end, err] = std::from_chars(c.data(), c.data() + c.size(), value);
  if (err != std::errc()) {
    std::cerr << "Ignoring invalid " << msg << c << "\n";
    return std::nullopt;
  }
  const auto offset = static_cast<std::string_view::size_type>(end - c.data());
  return std::pair{value, c.substr(offset)};
}

bool read_mapping_comment(std::vector<std::pair<unsigned, unsigned>> &mapping,
                          std::string_view keyword) {
  assert(mapping.empty());
  if (!witness->comments) return false;
  const char *const *p = witness->comments;
  bool found{};
  unsigned num_mapped{};
  for (const char *c; (c = *p++);) {
    std::string_view line{c};
    if (!line.starts_with(keyword)) continue;
    found = true;
    auto parsed_num =
        parse_num(line.substr(keyword.size()),
                  "mapping comment missing number of mapped literals: ");
    if (!parsed_num) return false;
    num_mapped = parsed_num->first;
    break;
  }
  if (!found) return false;
  std::cout << "Found " << keyword << "comment for " << num_mapped
            << " literals\n";
  mapping.reserve(num_mapped);
  for (unsigned i = 0; i < num_mapped; ++i) {
    const char *c = *p++;
    if (!c) {
      std::cerr << "Ignoring incomplete mapping" << num_mapped << " lines\n";
      return false;
    }
    auto witness_lit = parse_num(c, "mapping due to witness literal");
    if (!witness_lit) return false;
    auto model_lit =
        parse_num(witness_lit->second, "mapping due to model literal");
    if (!model_lit) return false;
    mapping.emplace_back(witness_lit->first, model_lit->first);
  }
  return true;
}

bool read_mapping_symbols(std::vector<std::pair<unsigned, unsigned>> &mapping,
                          const char symbol) {
  assert(mapping.empty());
  bool found{};
  mapping.reserve(witness->num_inputs + witness->num_latches);
  for (unsigned i = 0; i < witness->num_inputs + witness->num_latches; ++i) {
    unsigned s = 2 * (i + 1);
    bool mapped{};
    const char *c = aiger_get_symbol(witness, s);
    if (!c) continue;
    std::string_view name{c};
    const auto pos = name.find(symbol);
    mapped = pos != std::string_view::npos;
    if (!mapped) continue;
    auto literal = parse_num(name.substr(pos + 1), "mapping in symbol table");
    if (!literal) continue;
    found = true;
    mapping.emplace_back(s, literal->first);
  }
  if (!found) return false;
  std::cout << "Found symbol table mapping " << symbol << " for "
            << mapping.size() << " literals\n";
  return true;
}

// There are three ways to get a mapping indicating the shared literals
// and interventions: 1) A MAPPING comment 2) Symbol table 3) Default mapping
std::array<std::vector<std::pair<unsigned, unsigned>>, 2> read_mapping() {
  std::vector<std::pair<unsigned, unsigned>> shared, interventions;
  std::cout << "Reading shared literal mapping\n";
  if (!read_mapping_comment(shared, "MAPPING ") &&
      !read_mapping_symbols(shared, '=')) {
    std::cout << "No shared literals mapping found, using default\n";
    const unsigned mapped_inputs =
        std::min(model->num_inputs, witness->num_inputs);
    const unsigned mapped_latches =
        std::min(model->num_latches, witness->num_latches);
    shared.reserve(mapped_inputs + mapped_latches);
    for (unsigned i = 0; i < mapped_inputs; ++i)
      shared.emplace_back(witness->inputs[i].lit, model->inputs[i].lit);
    for (unsigned i = 0; i < mapped_latches; ++i)
      shared.emplace_back(witness->latches[i].lit, model->latches[i].lit);
  }

  std::cout << "Reading transition literal mapping\n";
  if (!read_mapping_comment(interventions, "INTERVENTION ") &&
      !read_mapping_symbols(interventions, '<')) {
    std::cout << "No intervention mapping found, using default\n";
    interventions.reserve(model->num_latches);
    for (unsigned i = 0; i < witness->num_latches; ++i) {
      aiger_symbol *l = witness->latches + i;
      if (aiger_is_constant(l->next)) continue;
      interventions.emplace_back(l->lit, l->next);
    }
  }

  return {shared, interventions};
}

// Create three copies of the merged witness and model circuits with latches
// turned to inputs. The witness has the lower indices as it is often a
// superset of the model.
// Returns map[circuit][time]
std::array<std::array<std::vector<unsigned>, times>, circuits>
unroll(const std::vector<std::pair<unsigned, unsigned>> &shared) {
  const std::array<unsigned, circuits> size = {2 * (witness->maxvar + 1),
                                               2 * (model->maxvar + 1)};
  std::array<std::array<std::vector<unsigned>, times>, circuits> map;

  for (unsigned c = 0; c < circuits; ++c) {
    for (unsigned t = 0; t < times; ++t) {
      map[c][t].assign(size[c], INVALID_LIT);
      map[c][t][0] = 0;
      map[c][t][1] = 1;
    }
  }

  for (unsigned t = 0; t < times; ++t) {
    for (unsigned c = 0; c < circuits; ++c) {
      if (c) { // map the shared latches already in the witness
        for (auto [w, m] : shared) {
          map[c][t][m] = map[0][t][w];
          map[c][t][aiger_not(m)] = map[0][t][aiger_not(w)];
        }
      }

      for (unsigned l = 0; l < size[c]; l += 2) {
        assert(next_lit % 2 == 0);
        if (map[c][t][l] != INVALID_LIT) continue;
        if (aiger_is_input(aig[c], l) || aiger_is_latch(aig[c], l))
          aiger_add_input(check, next_lit, nullptr);
        else if (aiger_and *a = aiger_is_and(aig[c], l)) {
          assert(map[c][t][a->rhs0] != INVALID_LIT);
          assert(map[c][t][a->rhs1] != INVALID_LIT);
          aiger_add_and(check, next_lit, map[c][t][a->rhs0],
                        map[c][t][a->rhs1]);
        } else
          std::abort();
        map[c][t][l] = next_lit++;
        map[c][t][l + 1] = next_lit++;
      }
    }
  }

  return map;
}

// Encodes the predicates for model and witness at each time step of the
// unrolling into the check circuit.
// Returns predicates[circuit][time]{R, RK, F, FK, C, P, Q, N}
std::array<std::array<predicates, times>, circuits> encode_predicates(
    const std::array<std::array<std::vector<unsigned>, times>, circuits> &map,
    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  std::array<std::array<predicates, times>, circuits> predicates;
  std::array<std::vector<aiger_symbol *>, circuits> K;
  K[0].reserve(shared.size());
  K[1].reserve(shared.size());
  for (auto [w, m] : shared) {
    if (auto *l = aiger_is_latch(witness, w)) K[0].push_back(l);
    if (auto *l = aiger_is_latch(model, m)) K[1].push_back(l);
  }

  for (unsigned c = 0; c < circuits; ++c) {
    for (unsigned t = 0; t < times; ++t) {

      for (auto l : K[c])
        predicates[c][t].RK =
            conj(predicates[c][t].RK,
                 equivalent(map[c][t][l->lit], map[c][t][l->reset]));
      for (unsigned i = 0; i < aig[c]->num_latches; ++i) {
        aiger_symbol *l = aig[c]->latches + i;
        predicates[c][t].R =
            conj(predicates[c][t].R,
                 equivalent(map[c][t][l->lit], map[c][t][l->reset]));
      }

      if (t < times - 1) { // no transitions at last time step
        for (auto l : K[c])
          predicates[c][t].FK =
              conj(predicates[c][t].FK,
                   equivalent(map[c][t][l->next], map[c][t + 1][l->lit]));
        for (unsigned i = 0; i < aig[c]->num_latches; ++i) {
          aiger_symbol *l = aig[c]->latches + i;
          predicates[c][t].F =
              conj(predicates[c][t].F,
                   equivalent(map[c][t][l->next], map[c][t + 1][l->lit]));
        }
      }

      for (unsigned i = 0; i < aig[c]->num_constraints; ++i)
        predicates[c][t].C =
            conj(predicates[c][t].C, map[c][t][aig[c]->constraints[i].lit]);

      for (unsigned i = 0; i < aig[c]->num_bad; ++i)
        predicates[c][t].P =
            conj(predicates[c][t].P, aiger_not(map[c][t][aig[c]->bad[i].lit]));
      // if neither outputs nor justice is defined assume old aiger
      if (!aig[c]->num_bad && !aig[c]->num_justice)
        for (unsigned i = 0; i < aig[c]->num_outputs; ++i)
          predicates[c][t].P = conj(
              predicates[c][t].P, aiger_not(map[c][t][aig[c]->outputs[i].lit]));

      unsigned Q_size = model->num_fairness;
      for (unsigned i = 0; i < model->num_justice; i++)
        Q_size += model->justice[i].size;
      predicates[c][t].Q.reserve(Q_size);
      for (unsigned i = 0; i < model->num_fairness; i++) {
        unsigned lit{1};
        if (i < aig[c]->num_fairness)
          lit = aiger_not(map[c][t][aig[c]->fairness[i].lit]);
        predicates[c][t].Q.push_back(lit);
      }
      for (unsigned i = 0; i < model->num_justice; i++) {
        for (unsigned j = 0; j < model->justice[i].size; j++) {
          unsigned lit{1};
          if (i < aig[c]->num_justice && j < aig[c]->justice[i].size)
            lit = aiger_not(map[c][t][aig[c]->justice[i].lits[j]]);
          predicates[c][t].Q.push_back(lit);
        }
      }
      assert(predicates[c][t].Q.size() == Q_size);
    }
  }

  return predicates;
}

// Encodes the witness rank at time `current` with interventions
// replaced by their corresponding values from time `next`.
unsigned
intervene_Qv(const std::vector<std::pair<unsigned, unsigned>> &interventions,
             const std::vector<unsigned> &current,
             const std::vector<unsigned> &next) {
  std::vector<unsigned> intervention_map(2 * (witness->maxvar + 1),
                                         INVALID_LIT);
  auto map = [&intervention_map](unsigned from, unsigned to) {
    intervention_map.at(from) = to;
    intervention_map.at(aiger_not(from)) = aiger_not(to);
  };
  map(0, 0);

  // Map inputs and latches from current
  for (unsigned i = 0; i < witness->num_inputs; ++i) {
    aiger_symbol *l = witness->inputs + i;
    map(l->lit, current.at(l->lit));
  }
  for (unsigned i = 0; i < witness->num_latches; ++i) {
    aiger_symbol *l = witness->latches + i;
    map(l->lit, current.at(l->lit));
  }

  // Intervene "next" literals to point to current in next state
  for (auto [c, n] : interventions) map(n, next.at(c));

  // Reencode and gates
  for (unsigned i = 0; i < witness->num_ands; ++i) {
    aiger_and *a = witness->ands + i;
    assert(intervention_map[a->rhs0] != INVALID_LIT);
    assert(intervention_map[a->rhs1] != INVALID_LIT);
    if (intervention_map[a->lhs] != INVALID_LIT) continue;
    map(a->lhs, conj(intervention_map[a->rhs0], intervention_map[a->rhs1]));
  }

  unsigned fair{0};
  for (unsigned i = 0; i < witness->num_fairness; i++)
    fair = disj(fair, aiger_not(intervention_map[witness->fairness[i].lit]));

  unsigned Qv{1};
  for (unsigned i = 0; i < witness->num_justice; i++) {
    unsigned rank{fair};
    for (unsigned j = 0; j < witness->justice[i].size; j++)
      rank =
          disj(rank, aiger_not(intervention_map[witness->justice[i].lits[j]]));
    Qv = conj(Qv, rank);
  }

  return Qv;
}

} // namespace

int main(int argc, char *argv[]) {
  auto check_path = initialize(argc, argv);
  if (!stratified(witness))
    std::cerr << "Witness resets not stratified\n", exit(1);
  const auto [shared, interventions] = read_mapping();
  const auto map = unroll(shared);
  const auto [W, M] = encode_predicates(map, shared);

  { // Reset: R[K] ∧ C → R'[K] ∧ C'
    unsigned reset_antecedent = conj(M[0].RK, M[0].C);
    unsigned reset_consequent = conj(W[0].RK, W[0].C);
    unsigned reset = imply(reset_antecedent, reset_consequent);
    aiger_add_output(check, aiger_not(reset), "Reset");
  }
  { // Transition: Fst[K] ∧ Cs ∧ Ct ∧ C's → F'st[K] ∧ C't
    unsigned transition_antecedent = conj(M[0].FK, M[0].C, M[1].C, W[0].C);
    unsigned transition_consequent = conj(W[0].FK, W[1].C);
    unsigned transition = imply(transition_antecedent, transition_consequent);
    aiger_add_output(check, aiger_not(transition), "Transition");
  }
  { // Safety: P' ∧ C ∧ C' → P
    unsigned safety_antecedent = conj(M[0].C, W[0].C, W[0].P);
    unsigned safety_consequent = M[0].P;
    unsigned safety = imply(safety_antecedent, safety_consequent);
    aiger_add_output(check, aiger_not(safety), "Safety");
  }
  { // Liveness: ∧i∈{s,t}(Ci ∧ C'i ∧ P'i) ∧ F'_st[L'] → ∧q∈Q(q'st → qst)
    unsigned live_guard{1};
    for (unsigned i = 0; i < 2; ++i)
      live_guard = conj(live_guard, M[i].C, W[i].C, W[i].P);
    unsigned live_antecedent = conj(live_guard, W[0].F);
    unsigned live_consequent{1};
    assert(W[0].Q.size() == M[0].Q.size());
    for (unsigned i = 0; i < W[0].Q.size(); i++)
      live_consequent = conj(live_consequent, imply(W[0].Q[i], M[0].Q[i]));
    unsigned live = imply(live_antecedent, live_consequent);
    aiger_add_output(check, aiger_not(live), "Liveness");
  }

  { // Base: R'[L'] ∧ C'→ P'
    unsigned base_antecedent = conj(W[0].R, W[0].C);
    unsigned base_consequent = W[0].P;
    unsigned base = imply(base_antecedent, base_consequent);
    aiger_add_output(check, aiger_not(base), "Base");
  }
  { // Inductive: F'st[L'] ∧ C's ∧ C't ∧ P's → P't
    unsigned inductive_antecedent = conj(W[0].F, W[0].C, W[1].C, W[0].P);
    unsigned inductive_consequent = W[1].P;
    unsigned inductive = imply(inductive_antecedent, inductive_consequent);
    aiger_add_output(check, aiger_not(inductive), "Inductive");
  }
  const unsigned Qst = intervene_Qv(interventions, map[0][0], map[0][1]);
  const unsigned Qsu = intervene_Qv(interventions, map[0][0], map[0][2]);
  const unsigned Qtu = intervene_Qv(interventions, map[0][1], map[0][2]);
  const unsigned Qts = intervene_Qv(interventions, map[0][1], map[0][0]);
  { // Decrease: ∧i∈{s,t}(C'i ∧ P'i) ∧ F'st[L'] → Q'ts
    unsigned decrease_guard{1};
    for (unsigned i = 0; i < 2; ++i)
      decrease_guard = conj(decrease_guard, W[i].C, W[i].P);
    unsigned decrease_antecedent = conj(decrease_guard, W[0].F);
    unsigned decrease_consequent = Qts;
    unsigned decrease = imply(decrease_antecedent, decrease_consequent);
    aiger_add_output(check, aiger_not(decrease), "Decrease");
  }
  { // Closure: ∧i∈{s,t,u}(C'i ∧ P'i) ∧ F'st[L'] ∧ Q'su → Q'tu
    unsigned closure_guard{1};
    for (unsigned i = 0; i < 3; ++i)
      closure_guard = conj(closure_guard, W[i].C, W[i].P);
    unsigned closure_antecedent = conj(closure_guard, W[0].F, Qsu);
    unsigned closure_consequent = Qtu;
    unsigned closure = imply(closure_antecedent, closure_consequent);
    aiger_add_output(check, aiger_not(closure), "Closure");
  }
  { // Consistent: ∧i∈{s,t,u}(C'i ∧ P'i) ∧ F'st[L'] ∧ F'tu[L']
    // ∧ Q'st ∧ Q'tu → ∧q∈Q(q'st → q'tu)
    unsigned consistent_guard{1};
    for (unsigned i = 0; i < 3; ++i)
      consistent_guard = conj(consistent_guard, W[i].C, W[i].P);
    unsigned consistent_antecedent =
        conj(consistent_guard, W[0].F, W[1].F, Qst, Qtu);
    unsigned consistent_consequent{1};
    assert(W[0].Q.size() == W[1].Q.size());
    for (unsigned i = 0; i < W[0].Q.size(); i++)
      consistent_consequent =
          conj(consistent_consequent, imply(W[0].Q[i], W[1].Q[i]));
    unsigned consistent = imply(consistent_antecedent, consistent_consequent);
    aiger_add_output(check, aiger_not(consistent), "Consistent");
  }

  finalize(check_path);
}
