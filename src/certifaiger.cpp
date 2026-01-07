#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
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
  std::vector<std::vector<unsigned>> Q;
  std::vector<unsigned> N;
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
      unsigned x = aiger_lit2var(a->rhs0);
      unsigned y = aiger_lit2var(a->rhs1);
      if (!--in_degree[x]) stack.push_back(x);
      if (!--in_degree[y]) stack.push_back(y);
    } else if (aiger_symbol *lat = aiger_is_latch(circuit, l)) {
      unsigned r = aiger_lit2var(lat->reset);
      if (lat->reset != lat->lit && !--in_degree[r])
        stack.push_back(r);
    }
  }
  return visited == n;
}

unsigned conj(unsigned x, unsigned y) {
  assert(next_lit % 2 == 0);
  aiger_add_and(check, next_lit, x, y);
  unsigned ret{next_lit};
  next_lit += 2;
  return ret;
}
unsigned disj(unsigned x, unsigned y) {
  return aiger_not(conj(aiger_not(x), aiger_not(y)));
}
unsigned imply(unsigned x, unsigned y) {
  return aiger_not(conj(x, aiger_not(y)));
}
unsigned equivalent(unsigned x, unsigned y) {
  return conj(imply(x, y), imply(y, x));
}

const char *parse_num(const char *c, unsigned &value, const char *msg) {
  auto [end, err] = std::from_chars(c, c + std::strlen(c), value);
  if (err != std::errc())
    std::cerr << "Error in " << c << ": " << msg << "\n", exit(1);
  return end;
}

bool read_mapping_comment(std::vector<std::pair<unsigned, unsigned>> &mapping,
                          const char *keyword) {
  assert(mapping.empty());
  if (!witness->comments) return false;
  bool found{};
  const char *const *p = witness->comments;
  const char *c;
  unsigned num_mapped{}, x{}, y{};
  for (; (c = *p++);)
    if (!strncmp(c, keyword, std::strlen(keyword))) {
      parse_num(c + std::strlen(keyword), num_mapped,
                "requires number of mapped literals");
      found = true;
      break;
    }
  if (!found) return false;
  std::cout << "Found " << keyword << "comment for " << num_mapped
            << " literals\n";
  mapping.reserve(num_mapped);
  for (unsigned i = 0; i < num_mapped; ++i) {
    if (!(c = *p++))
      std::cerr << "Mapping incomplete, expected " << num_mapped << " lines\n",
          exit(1);
    auto end = parse_num(c, x, "Invalid witness gate in mapping");
    parse_num(end + 1, y, "Invalid model gate in mapping");
    mapping.emplace_back(x, y);
  }
  return true;
}

bool read_mapping_symbols(std::vector<std::pair<unsigned, unsigned>> &mapping,
                          const char symbol) {
  assert(mapping.empty());
  bool found{};
  mapping.reserve(witness->num_inputs + witness->num_latches);
  for (unsigned i = 0; i < witness->num_inputs + witness->num_latches; ++i) {
    unsigned x = 2 * (i + 1), y{};
    bool mapped{};
    const char *c = aiger_get_symbol(witness, x);
    if (!c) continue;
    while (*c && !(mapped = *(c++) == symbol)) {}
    if (!mapped) continue;
    found = true;
    while (*c && std::isspace(static_cast<unsigned char>(*c))) ++c;
    parse_num(c, y, "Invalid right-hand side in literal mapping");
    mapping.emplace_back(x, y);
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
      map[c][t][0] = aiger_false;
      map[c][t][1] = aiger_true;
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
      for (unsigned i = 0; i < aig[c]->num_outputs; ++i)
        predicates[c][t].P = conj(predicates[c][t].P,
                                  aiger_not(map[c][t][aig[c]->outputs[i].lit]));

      // Q size matches, additional justice in witness is ignored and fewer are
      // extended with aiger_true
      predicates[c][t].Q.resize(model->num_justice);
      for (unsigned just = 0; just < model->num_justice; ++just) {
        auto &Q = predicates[c][t].Q[just];
        const unsigned num_q_signals =
            model->num_fairness + model->justice[just].size;
        Q.resize(num_q_signals, aiger_true);
        // fairness constraints are shared between all justice properties
        // if the witness defines too few they are assumed to be constant true
        const unsigned fairness_limit =
            std::min(aig[c]->num_fairness, model->num_fairness);
        for (unsigned fair = 0; fair < fairness_limit; ++fair)
          Q[fair] = aiger_not(map[c][t][aig[c]->fairness[fair].lit]);
        // if the witness doesn't define this justice property assume true
        if (just >= aig[c]->num_justice) continue;

        const unsigned justice_limit =
            std::min(aig[c]->justice[just].size, model->justice[just].size);
        for (unsigned lit = 0; lit < justice_limit; ++lit)
          Q[model->num_fairness + lit] =
              aiger_not(map[c][t][aig[c]->justice[just].lits[lit]]);
      }

      predicates[c][t].N.resize(model->num_justice, aiger_true);
      for (unsigned just = 0; just < model->num_justice; ++just) {
        if (just >= aig[c]->num_justice) continue;
        if (aig[c]->justice[just].size == 0) continue;
        // The last literal of the justice property is always N
        // it may or may not also be in Q
        predicates[c][t].N[just] = aiger_not(
            map[c][t]
               [aig[c]->justice[just].lits[aig[c]->justice[just].size - 1]]);
      }
    }
  }

  for (unsigned t = 0; t < times; ++t) {
    assert(predicates[0][t].Q.size() == predicates[1][t].Q.size());
    for (unsigned j = 0; j < predicates[0][t].Q.size(); ++j)
      assert(predicates[0][t].Q[j].size() == predicates[1][t].Q[j].size());
  }

  return predicates;
}

// Encodes the witness liveness predicate N' at time ~current~ while replacing
// intervened literals with their corresponding values from time ~next~.
unsigned
intervene_N(const std::vector<std::pair<unsigned, unsigned>> &interventions,
            const std::vector<unsigned> &current,
            const std::vector<unsigned> &next, unsigned justice_index) {
  if (justice_index >= witness->num_justice) return aiger_true;
  std::vector<unsigned> intervention_map(2 * (witness->maxvar + 1),
                                         INVALID_LIT);
  auto map = [&intervention_map](unsigned from, unsigned to) {
    intervention_map.at(from) = to;
    intervention_map.at(aiger_not(from)) = aiger_not(to);
  };
  map(aiger_false, aiger_false);

  // Map inputs and latches from current
  for (unsigned i = 0; i < witness->num_inputs; ++i) {
    aiger_symbol *l = witness->inputs + i;
    map(l->lit, current[l->lit]);
  }
  for (unsigned i = 0; i < witness->num_latches; ++i) {
    aiger_symbol *l = witness->latches + i;
    map(l->lit, current[l->lit]);
  }

  // Intervene "next" literals to point to current in next state
  for (auto [c, n] : interventions) map(n, next[c]);

  // Reencode and gates
  for (unsigned i = 0; i < witness->num_ands; ++i) {
    aiger_and *a = witness->ands + i;
    assert(intervention_map[a->rhs0] != INVALID_LIT);
    assert(intervention_map[a->rhs1] != INVALID_LIT);
    if (intervention_map[a->lhs] != INVALID_LIT) continue;
    map(a->lhs, conj(intervention_map[a->rhs0], intervention_map[a->rhs1]));
  }

  assert(witness->justice[justice_index].size > 0);
  return aiger_not(
      intervention_map[witness->justice[justice_index]
                           .lits[witness->justice[justice_index].size - 1]]);
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
  { // Transition: Fxy[K] ∧ Cx ∧ Cy ∧ C'x → F'xy[K] ∧ C'y
    unsigned transition_antecedent =
        conj(conj(conj(M[0].FK, M[0].C), M[1].C), W[0].C);
    unsigned transition_consequent = conj(W[0].FK, W[1].C);
    unsigned transition = imply(transition_antecedent, transition_consequent);
    aiger_add_output(check, aiger_not(transition), "Transition");
  }
  { // Base: R'[L'] ∧ C'→ P'
    unsigned base_antecedent = conj(W[0].R, W[0].C);
    unsigned base_consequent = W[0].P;
    unsigned base = imply(base_antecedent, base_consequent);
    aiger_add_output(check, aiger_not(base), "Base");
  }
  { // Inductive: F'xy[L'] ∧ C'x ∧ C'y ∧ P'x → P'y
    unsigned inductive_antecedent =
        conj(conj(conj(W[0].F, W[0].C), W[1].C), W[0].P);
    unsigned inductive_consequent = W[1].P;
    unsigned inductive = imply(inductive_antecedent, inductive_consequent);
    aiger_add_output(check, aiger_not(inductive), "Inductive");
  }
  { // Safe: C ∧ C' ∧ P' → P
    unsigned safe_antecedent = conj(conj(M[0].C, W[0].C), W[0].P);
    unsigned safe_consequent = M[0].P;
    unsigned safe = imply(safe_antecedent, safe_consequent);
    aiger_add_output(check, aiger_not(safe), "Safe");
  }

  for (unsigned j = 0; j < model->num_justice; ++j) {
    const std::vector<unsigned> &MQx = M[0].Q[j];
    const std::vector<unsigned> &WQx = W[0].Q[j];
    const std::vector<unsigned> &WQy = W[1].Q[j];
    assert(MQx.size() <= std::numeric_limits<unsigned>::max());
    const unsigned Q_size = static_cast<unsigned>(MQx.size());
    assert(WQx.size() == Q_size && WQy.size() == Q_size);
    const unsigned WNxy = intervene_N(interventions, map[0][0], map[0][1], j);
    const unsigned WNxz = intervene_N(interventions, map[0][0], map[0][2], j);
    const unsigned WNyx = intervene_N(interventions, map[0][1], map[0][0], j);
    auto index = [j](const char *name) {
      return std::string(name) + (j ? "_" + std::to_string(j) : "");
    };

    { // Decrease: (∧i∈{x,y} C'i ∧ P'i) ∧ F'xy[L'] → N'xy
      unsigned decrease_guard{aiger_true};
      for (unsigned i = 0; i < 2; ++i)
        decrease_guard = conj(decrease_guard, conj(W[i].C, W[i].P));
      unsigned decrease_antecedent = conj(decrease_guard, W[0].F);
      unsigned decrease_consequent = WNxy;
      unsigned decrease = imply(decrease_antecedent, decrease_consequent);
      aiger_add_output(check, aiger_not(decrease), index("Decrease").c_str());
    }
    { // Closure: (∧i∈{x,y,z} C'i ∧ P'i) ∧ N'xy ∧ F'yz[L'] → N'xz
      unsigned closure_guard{aiger_true};
      for (unsigned i = 0; i < 3; ++i)
        closure_guard = conj(closure_guard, conj(W[i].C, W[i].P));
      unsigned closure_antecedent = conj(conj(closure_guard, WNxy), W[1].F);
      unsigned closure_consequent = WNxz;
      unsigned closure = imply(closure_antecedent, closure_consequent);
      aiger_add_output(check, aiger_not(closure), index("Closure").c_str());
    }
    { // Cover: (∧i∈{x,y} C'i ∧ P'i) ∧ F'xy[L'] ∧ N'yx → (∨q∈Q q'x)
      unsigned cover_guard{aiger_true};
      for (unsigned i = 0; i < 2; ++i)
        cover_guard = conj(cover_guard, conj(W[i].C, W[i].P));
      unsigned cover_antecedent = conj(conj(cover_guard, W[0].F), WNyx);
      unsigned cover_consequent{aiger_false};
      for (const auto q : WQx) cover_consequent = disj(cover_consequent, q);
      unsigned cover = imply(cover_antecedent, cover_consequent);
      aiger_add_output(check, aiger_not(cover), index("Cover").c_str());
    }
    { // Consistent: (∧i∈{x,y} C'i ∧ P'i) ∧ F'xy[L'] ∧ N'yx → (∧q∈Q q'x → q'y)
      unsigned consistent_guard{aiger_true};
      for (unsigned i = 0; i < 2; ++i)
        consistent_guard = conj(consistent_guard, conj(W[i].C, W[i].P));
      unsigned consistent_antecedent =
          conj(conj(consistent_guard, W[0].F), WNyx);
      unsigned consistent_consequent{aiger_true};
      for (unsigned i = 0; i < Q_size; ++i)
        consistent_consequent =
            conj(consistent_consequent, imply(WQx[i], WQy[i]));
      unsigned consistent = imply(consistent_antecedent, consistent_consequent);
      aiger_add_output(check, aiger_not(consistent),
                       index("Consistent").c_str());
    }
    { // Live: (∧i∈{x,y} Ci ∧ C'i ∧ P'i) ∧ F'xy[L'] ∧ N'yx → (∧q∈Q q'x → qx)
      unsigned live_guard{aiger_true};
      for (unsigned i = 0; i < 2; ++i)
        live_guard = conj(live_guard, conj(conj(M[i].C, W[i].C), W[i].P));
      unsigned live_antecedent = conj(conj(live_guard, W[0].F), WNyx);
      unsigned live_consequent{aiger_true};
      for (unsigned i = 0; i < Q_size; ++i)
        live_consequent = conj(live_consequent, imply(WQx[i], MQx[i]));
      unsigned live = imply(live_antecedent, live_consequent);
      aiger_add_output(check, aiger_not(live), index("Live").c_str());
    }
  }

  finalize(check_path);
}
