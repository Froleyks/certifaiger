#include <array>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#include "aiger.h"
constexpr unsigned INVALID_LIT = std::numeric_limits<unsigned>::max();

#ifdef QUIET
#define MSG \
  if (0) std::cout
#else
#define MSG std::cout << "Certifaiger: "
#endif

constexpr unsigned circuits = 2; // W, M
constexpr unsigned times = 3;    // t0, t1, t2
aiger *model, *witness, *check;
std::array<aiger *, circuits> aig;
unsigned maxvar{2};
struct predicates {
  unsigned R{1}, RK{1}, F{1}, FK{1}, C{1}, P{1}, Q{1};
};

bool reencoded(const aiger *aig) {
  unsigned l{};
  for (unsigned i = 0; i < aig->num_inputs; ++i)
    if (aig->inputs[i].lit != 2 * (++l)) return false;
  for (unsigned i = 0; i < aig->num_latches; ++i)
    if (aig->latches[i].lit != 2 * (++l)) return false;
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
  for (int c = 0; c < circuits; ++c) {
    if (const char *err = aiger_open_and_read_from_file(aig[c], paths[c]))
      std::cerr << "Error reading " << (c ? "model '" : "witness '") << paths[c]
                << "': " << err << '\n',
          exit(1);

    if (!reencoded(aig[c]))
      std::cerr << "Error: " << (c ? "model '" : "witness '") << paths[c]
                << "' is not reencoded\n",
          exit(1);

    if (aig[c]->num_fairness) std::cerr << "Fairness not supported\n", exit(1);
    for (size_t i = 0; i < aig[c]->num_justice; ++i) {
      if (aig[c]->justice[i].size > 1)
        std::cerr << "Justice properties with more than one fairness "
                     "constraint not supported\n",
            exit(1);
    }
  }
  MSG << "Certify Model Checking Witnesses in AIGER\n";
  MSG << VERSION << " " << GITID << "\n";

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
bool stratified(const aiger *aig) {
  const unsigned n = aig->maxvar + 1;
  std::vector<unsigned> in_degree(n);
  std::vector<unsigned> stack;
  stack.reserve(n);
  for (int i = 0; i < aig->num_ands; ++i) {
    aiger_and *a = aig->ands + i;
    in_degree[a->rhs0 >> 1]++;
    in_degree[a->rhs1 >> 1]++;
  }
  for (int i = 0; i < aig->num_latches; ++i) {
    aiger_symbol *l = aig->latches + i;
    if (l->reset != l->lit) in_degree[l->reset >> 1]++;
  }
  for (unsigned i = 0; i < n; ++i)
    if (!in_degree[i]) stack.push_back(i << 1);
  unsigned visited{};
  while (!stack.empty()) {
    unsigned l{stack.back()};
    stack.pop_back();
    visited++;
    if (aiger_and *a = aiger_is_and(aig, l)) {
      if (!--in_degree[a->rhs0 >> 1]) stack.push_back(a->rhs0);
      if (!--in_degree[a->rhs1 >> 1]) stack.push_back(a->rhs1);
    } else if (aiger_symbol *lat = aiger_is_latch(aig, l)) {
      if (lat->reset != lat->lit && !--in_degree[lat->reset >> 1])
        stack.push_back(lat->reset);
    }
  }
  return visited == n;
}

unsigned gate(unsigned x, unsigned y) {
  assert(maxvar % 2 == 0);
  aiger_add_and(check, maxvar, x, y);
  maxvar += 2;
  return maxvar - 2;
}
unsigned imply(unsigned x, unsigned y) {
  return aiger_not(gate(x, aiger_not(y)));
}
unsigned equivalent(unsigned x, unsigned y) {
  return gate(imply(x, y), imply(y, x));
}

const char *parse_num(const char *c, unsigned &value, const char *msg) {
  auto [end, err] = std::from_chars(c, c + strlen(c), value);
  if (err != std::errc())
    std::cerr << "Error in " << c << ": " << msg << "\n", exit(1);
  return end;
}

// There are three ways to get a mapping indicating the shared literals, in
// order: 1) A MAPPING comment 2) Symbol table 3) Default mapping
std::vector<std::pair<unsigned, unsigned>> read_shared() {
  std::vector<std::pair<unsigned, unsigned>> shared;
  bool found_mapping{};
  unsigned num_mapped{}, w{}, m{};
  const char *const *p = witness->comments;
  const char *c;
  for (; (c = *p++);)
    if (!strncmp(c, "MAPPING ", 8)) {
      parse_num(c + 8, num_mapped, "MAPPING requires number of mapped gates");
      found_mapping = true;
      break;
    }
  if (found_mapping) {
    shared.reserve(num_mapped);
    for (int i = 0; i < num_mapped; ++i) {
      if (!(c = *p++))
        std::cerr << "Mapping incomplete, expected " << num_mapped
                  << " lines\n",
            exit(1);
      auto end = parse_num(c, w, "Invalid witness gate in mapping");
      parse_num(end + 1, m, "Invalid model gate in mapping");
      shared.emplace_back(m, w);
    }
    MSG << "Found mapping for " << num_mapped << " literals\n";
    return shared;
  }

  for (unsigned i = 0; i < witness->num_inputs + witness->num_latches; ++i) {
    const unsigned l = 2 * (i + 1);
    const char *c = aiger_get_symbol(witness, l);
    if (!c || !strlen(c) || c[0] != '=') continue;
    parse_num(c + 2, m, "Invalid model gate in mapping");
    found_mapping = true;
    shared.emplace_back(m, l);
  }
  if (found_mapping) return shared;

  MSG << "No witness mapping found, using default\n";
  const unsigned mapped_inputs =
      std::min(model->num_inputs, witness->num_inputs);
  const unsigned mapped_latches =
      std::min(model->num_latches, witness->num_latches);
  for (unsigned i = 0; i < mapped_inputs; ++i)
    shared.emplace_back(model->inputs[i].lit, witness->inputs[i].lit);
  for (unsigned i = 0; i < mapped_latches; ++i)
    shared.emplace_back(model->latches[i].lit, witness->latches[i].lit);
  return shared;
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
        for (auto [m, w] : shared) {
          map[c][t][m] = map[0][t][w];
          map[c][t][aiger_not(m)] = map[0][t][aiger_not(w)];
        }
      }

      for (unsigned l = 0; l < size[c]; l += 2) {
        if (map[c][t][l] != INVALID_LIT) continue;
        if (aiger_is_input(aig[c], l) || aiger_is_latch(aig[c], l))
          aiger_add_input(check, maxvar, nullptr);
        else if (aiger_and *a = aiger_is_and(aig[c], l)) {
          assert(map[c][t][a->rhs0] != INVALID_LIT);
          assert(map[c][t][a->rhs1] != INVALID_LIT);
          aiger_add_and(check, maxvar, map[c][t][a->rhs0], map[c][t][a->rhs1]);
        } else
          assert(false);
        map[c][t][l] = maxvar++;
        map[c][t][l + 1] = maxvar++;
      }
    }
  }

  return map;
}

// Encodes the predicates for model and witness at each time step of the
// unrolling into the check circuit.
// Returns predicates[circuit][time]{R, RK, F, FK, C, P, Q}
std::array<std::array<predicates, times>, circuits> encode_predicates(
    const std::array<std::array<std::vector<unsigned>, times>, circuits> &map,
    const std::vector<std::pair<unsigned, unsigned>> &shared) {
  std::array<std::array<predicates, times>, circuits> predicates;
  std::array<std::vector<aiger_symbol *>, circuits> K;
  K[0].reserve(shared.size());
  K[1].reserve(shared.size());
  for (auto [m, w] : shared) {
    if (auto *l = aiger_is_latch(witness, w)) K[0].push_back(l);
    if (auto *l = aiger_is_latch(model, m)) K[1].push_back(l);
  }

  for (unsigned c = 0; c < circuits; ++c) {
    for (unsigned t = 0; t < times; ++t) {

      for (auto l : K[c])
        predicates[c][t].RK =
            gate(predicates[c][t].RK,
                 equivalent(map[c][t][l->lit], map[c][t][l->reset]));
      for (unsigned i = 0; i < aig[c]->num_latches; ++i) {
        aiger_symbol *l = aig[c]->latches + i;
        predicates[c][t].R =
            gate(predicates[c][t].R,
                 equivalent(map[c][t][l->lit], map[c][t][l->reset]));
      }

      if (t < times - 1) { // no transitions at last time step
        for (auto l : K[c])
          predicates[c][t].FK =
              gate(predicates[c][t].FK,
                   equivalent(map[c][t][l->next], map[c][t + 1][l->lit]));
        for (unsigned i = 0; i < aig[c]->num_latches; ++i) {
          aiger_symbol *l = aig[c]->latches + i;
          predicates[c][t].F =
              gate(predicates[c][t].F,
                   equivalent(map[c][t][l->next], map[c][t + 1][l->lit]));
        }
      }

      for (unsigned i = 0; i < aig[c]->num_constraints; ++i)
        predicates[c][t].C =
            gate(predicates[c][t].C, map[c][t][aig[c]->constraints[i].lit]);

      for (unsigned i = 0; i < aig[c]->num_bad; ++i)
        predicates[c][t].P =
            gate(predicates[c][t].P, aiger_not(map[c][t][aig[c]->bad[i].lit]));
      for (unsigned i = 0; i < aig[c]->num_outputs; ++i)
        predicates[c][t].P = gate(predicates[c][t].P,
                                  aiger_not(map[c][t][aig[c]->outputs[i].lit]));

      // TODO fairness and justice
      for (size_t i = 0; i < aig[c]->num_justice; ++i) {
        assert(aig[c]->justice[i].size == 1);
        predicates[c][t].Q =
            gate(predicates[c][t].Q,
                 aiger_not(map[c][t][aig[c]->justice[i].lits[0]]));
      }
    }
  }

  return predicates;
}

// Encodes the witness liveness predicate Q' at time ~current~ while replacing
// next literals with the state literal in copy ~next~.
unsigned
WQ_intervention(const std::array<std::vector<unsigned>, times> &witness_map,
                unsigned current, unsigned next) {
  std::vector<unsigned> intervention_map(2 * (witness->maxvar + 1),
                                         INVALID_LIT);
  auto map = [&intervention_map](unsigned from, unsigned to) {
    assert(intervention_map[from] == INVALID_LIT);
    intervention_map[from] = to;
    intervention_map[aiger_not(from)] = aiger_not(to);
  };
  map(aiger_false, aiger_false);

  // Map inputs and latches from current
  for (size_t i = 0; i < witness->num_inputs; ++i) {
    aiger_symbol *l = witness->inputs + i;
    map(l->lit, witness_map[current][l->lit]);
  }
  for (size_t i = 0; i < witness->num_latches; ++i) {
    aiger_symbol *l = witness->latches + i;
    map(l->lit, witness_map[current][l->lit]);
  }

  // Intervene next literals to point to next state
  for (size_t i = 0; i < witness->num_latches; ++i) {
    aiger_symbol *l = witness->latches + i;
    unsigned n = l->next;
    if (n < 2) continue; // no intervention on constants
    if (intervention_map[n] != INVALID_LIT) continue;
    map(n, witness_map[next][l->lit]);
  }

  // Recompute ands
  for (int i = 0; i < witness->num_ands; ++i) {
    aiger_and *a = witness->ands + i;
    assert(intervention_map[a->rhs0] != INVALID_LIT);
    assert(intervention_map[a->rhs1] != INVALID_LIT);
    if (intervention_map[a->lhs] != INVALID_LIT) continue;
    map(a->lhs, gate(intervention_map[a->rhs0], intervention_map[a->rhs1]));
  }
  unsigned Q{1};
  for (size_t i = 0; i < witness->num_justice; ++i)
    Q = gate(Q, aiger_not(intervention_map[witness->justice[i].lits[0]]));
  return Q;
}

int main(int argc, char *argv[]) {
  auto check_path = initialize(argc, argv);
  if (!stratified(witness))
    std::cerr << "Witness resets not stratified\n", exit(1);
  const auto shared = read_shared();
  const auto map = unroll(shared);
  const auto [W, M] = encode_predicates(map, shared);
  unsigned WQxy = WQ_intervention(map[0], 0, 1);
  unsigned WQxz = WQ_intervention(map[0], 0, 2);
  unsigned WQxx = WQ_intervention(map[0], 0, 0);

  // Reset: R[K] ∧ C → R'[K] ∧ C'
  unsigned reset_antecedent = gate(M[0].RK, M[0].C);
  unsigned reset_consequent = gate(W[0].RK, W[0].C);
  unsigned reset = imply(reset_antecedent, reset_consequent);
  aiger_add_output(check, aiger_not(reset), "Reset");

  // Transition: Fxy[K] ∧ Cx ∧ Cy ∧ C'x → F'xy[K] ∧ C'y
  unsigned transition_antecedent =
      gate(gate(gate(M[0].FK, M[0].C), M[1].C), W[0].C);
  unsigned transition_consequent = gate(W[0].FK, W[1].C);
  unsigned transition = imply(transition_antecedent, transition_consequent);
  aiger_add_output(check, aiger_not(transition), "Transition");

  // Base: R'[L'] ∧ C'→ P'
  unsigned base_antecedent = gate(W[0].R, W[0].C);
  unsigned base_consequent = W[0].P;
  unsigned base = imply(base_antecedent, base_consequent);
  aiger_add_output(check, aiger_not(base), "Base");

  // Inductive: F'xy[L'] ∧ C'x ∧ C'y ∧ P'x → P'y
  unsigned inductive_antecedent =
      gate(gate(gate(W[0].F, W[0].C), W[1].C), W[0].P);
  unsigned inductive_consequent = W[1].P;
  unsigned inductive = imply(inductive_antecedent, inductive_consequent);
  aiger_add_output(check, aiger_not(inductive), "Inductive");

  // Safety: C ∧ C' ∧ P' → P
  unsigned safety_antecedent = gate(gate(M[0].C, W[0].C), W[0].P);
  unsigned safety_consequent = M[0].P;
  unsigned safety = imply(safety_antecedent, safety_consequent);
  aiger_add_output(check, aiger_not(safety), "Safety");

  // Decrease: (∧i∈{x,y} C'i ∧  P'i) ∧ Q'
  unsigned decrease_guard{aiger_true};
  for (unsigned i = 0; i < 2; ++i)
    decrease_guard = gate(decrease_guard, gate(W[i].C, W[i].P));
  unsigned decrease_antecedent = decrease_guard;
  unsigned decrease_consequent = W[0].Q;
  unsigned decrease = imply(decrease_antecedent, decrease_consequent);
  aiger_add_output(check, aiger_not(decrease), "Decrease");

  // Transitive: (∧i∈{x,y,z} C'i ∧  P'i) ∧ Q'xy ∧ F'yz[L'] → Q'xz
  unsigned transitive_guard{aiger_true};
  for (unsigned i = 0; i < 3; ++i)
    transitive_guard = gate(transitive_guard, gate(W[i].C, W[i].P));
  unsigned transitive_antecedent = gate(gate(transitive_guard, WQxy), W[1].F);
  unsigned transitive_consequent = WQxz;
  unsigned transitive = imply(transitive_antecedent, transitive_consequent);
  aiger_add_output(check, aiger_not(transitive), "Transitive");

  // Liveness: C ∧ C' ∧ P' ∧ Q'xx → Q
  unsigned liveness_antecedent = gate(gate(gate(M[0].C, W[0].C), W[0].P), WQxx);
  unsigned liveness_consequent = M[0].Q;
  unsigned liveness = imply(liveness_antecedent, liveness_consequent);
  aiger_add_output(check, aiger_not(liveness), "Liveness");

  finalize(check_path);
}
