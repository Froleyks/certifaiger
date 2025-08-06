#include <array>
#include <cassert>
#include <charconv>
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

bool reencoded(const aiger *aig) {
  unsigned l{};
  for (unsigned i = 0; i < aig->num_inputs; ++i)
    if (aig->inputs[i].lit != 2 * (++l)) return false;
  for (unsigned i = 0; i < aig->num_latches; ++i)
    if (aig->latches[i].lit != 2 * (++l)) return false;
  return true;
}

aiger *model, *witness, *check;
unsigned maxvar{2};
// Parse command-line arguments, initialize aigs
const char *initialize(int argc, char *argv[]) {
  if (argc > 1 && !std::strcmp(argv[1], "--version"))
    std::cout << VERSION << "\n", exit(0);
  if (argc < 3)
    std::cerr << "Usage: " << argv[0] << " model witness [check=check.aig]\n",
        exit(1);
  const char *paths[3] = {argv[1], argv[2], argc > 3 ? argv[3] : "check.aig"};
  check = aiger_init();
  aiger *aigs[2] = {model = aiger_init(), witness = aiger_init()};
  for (int i = 0; i < 2; ++i) {
    if (const char *err = aiger_open_and_read_from_file(aigs[i], paths[i]))
      std::cerr << "Error reading " << (i ? "witness '" : "model '") << paths[i]
                << "': " << err << '\n',
          exit(1);
    if (!reencoded(aigs[i]))
      std::cerr << "Error: " << (i ? "witness '" : "model '") << paths[i]
                << "' is not reencoded\n",
          exit(1);
  }
  MSG << "Certify Model Checking Witnesses in AIGER\n";
  MSG << VERSION << " " << GITID << "\n";
  return paths[2];
}

void finalize(const char *path) {
  aiger_open_and_write_to_file(check, path);
  aiger_reset(check);
  aiger_reset(witness);
  aiger_reset(model);
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
std::vector<std::pair<unsigned, unsigned>> get_shared() {
  std::vector<std::pair<unsigned, unsigned>> shared;
  bool found_mapping{};
  unsigned num_mapped{}, w{}, m{};
  const char *const *p = witness->comments;
  const char *c;
  for (; (c = *p++);)
    if (!strncmp(c, "MAPPING", 7)) {
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

// The literals are ordered in map W0, M0, W1, M1 and each of those I, L, A
std::array<std::array<std::vector<unsigned>, 2>, 2>
encode_unrolling(const std::vector<std::pair<unsigned, unsigned>> &shared) {
  std::array<std::array<std::vector<unsigned>, 2>, 2> map;
  std::vector<const aiger *> circuits{witness, model};
  for (unsigned t = 0; t < 2; ++t)
    for (unsigned c = 0; c < 2; ++c) {
      map[t][c].assign((circuits[c]->maxvar + 1) * 2, INVALID_LIT);
      map[t][c][0] = aiger_false;
      map[t][c][1] = aiger_true;
    }
  for (unsigned i = 2; i < map[0][0].size(); ++i)
    map[0][0][i] = maxvar++;
  const unsigned witness_end = maxvar - 1;
  for (auto [m, w] : shared) {
    map[0][1][m] = map[0][0][w];
    map[0][1][aiger_not(m)] = map[0][0][aiger_not(w)];
  }
  for (unsigned i = 0; i < map[0][1].size(); ++i)
    if (map[0][1][i] == INVALID_LIT) map[0][1][i] = maxvar++;
  const unsigned offset = maxvar - 2;
  for (unsigned c = 0; c < 2; ++c)
    for (unsigned i = 2; i < map[0][c].size(); ++i)
      map[1][c][i] = map[0][c][i] + offset;
  maxvar += offset;

  for (unsigned c = 0; c < 2; ++c) {
    for (unsigned i = 2;
         i <= 2 * (circuits[c]->num_inputs + circuits[c]->num_latches);
         i += 2) {
      if (c == 1 && map[0][c][i] < witness_end) continue;
      aiger_add_input(check, map[0][c][i], nullptr);
      aiger_add_input(check, map[1][c][i], nullptr);
    }
  }
  for (unsigned t = 0; t < 2; ++t)
    for (unsigned c = 0; c < 2; ++c)
      for (int i = 0; i < circuits[c]->num_ands; ++i) {
        auto [a, x, y] = circuits[c]->ands[i];
        aiger_add_and(check, map[t][c][a], map[t][c][x], map[t][c][y]);
      }

  std::array<std::vector<unsigned>, 2> witness_map, model_map;
  for (unsigned t = 0; t < 2; ++t) {
    witness_map[t] = std::move(map[t][0]);
    model_map[t] = std::move(map[t][1]);
  }
  return {witness_map, model_map};
}

std::array<unsigned, 13>
encode_components(const std::vector<std::pair<unsigned, unsigned>> &shared,
                  const std::array<std::vector<unsigned>, 2> &witness_map,
                  const std::array<std::vector<unsigned>, 2> &model_map) {
  std::vector<aiger_symbol *> K, KP;
  K.reserve(shared.size());
  KP.reserve(shared.size());
  for (auto [m, w] : shared) {
    if (auto *l = aiger_is_latch(model, m)) K.push_back(l);
    if (auto *l = aiger_is_latch(witness, w)) KP.push_back(l);
  }

  unsigned R0K{1}, R0KP{1}, R0P{1};
  for (auto l : K)
    R0K = gate(R0K, equivalent(model_map[0][l->lit], model_map[0][l->reset]));
  for (auto l : KP)
    R0KP = gate(R0KP,
                equivalent(witness_map[0][l->lit], witness_map[0][l->reset]));
  for (unsigned i = 0; i < witness->num_latches; ++i) {
    aiger_symbol *l = witness->latches + i;
    R0P =
        gate(R0P, equivalent(witness_map[0][l->lit], witness_map[0][l->reset]));
  }

  unsigned F01K{1}, F01KP{1}, F01P{1};
  for (auto l : K)
    F01K = gate(F01K, equivalent(model_map[0][l->next], model_map[1][l->lit]));
  for (auto l : KP)
    F01KP = gate(F01KP,
                 equivalent(witness_map[0][l->next], witness_map[1][l->lit]));
  for (unsigned j = 0; j < witness->num_latches; ++j) {
    aiger_symbol *l = witness->latches + j;
    F01P =
        gate(F01P, equivalent(witness_map[0][l->next], witness_map[1][l->lit]));
  }

  unsigned C0{1}, C0P{1}, C1{1}, C1P{1};
  for (unsigned j = 0; j < witness->num_constraints; ++j) {
    C0P = gate(C0P, witness_map[0][witness->constraints[j].lit]);
    C1P = gate(C1P, witness_map[1][witness->constraints[j].lit]);
  }
  for (unsigned j = 0; j < model->num_constraints; ++j) {
    C0 = gate(C0, model_map[0][model->constraints[j].lit]);
    C1 = gate(C1, model_map[1][model->constraints[j].lit]);
  }

  unsigned P0{1}, P0P{1}, P1P{1};
  for (unsigned j = 0; j < model->num_bad; ++j)
    P0 = gate(P0, aiger_not(model_map[0][model->bad[j].lit]));
  for (unsigned j = 0; j < witness->num_bad; ++j) {
    P0P = gate(P0P, aiger_not(witness_map[0][witness->bad[j].lit]));
    P1P = gate(P1P, aiger_not(witness_map[1][witness->bad[j].lit]));
  }
  for (unsigned j = 0; j < model->num_outputs; ++j)
    P0 = gate(P0, aiger_not(model_map[0][model->outputs[j].lit]));
  for (unsigned j = 0; j < witness->num_outputs; ++j) {
    P0P = gate(P0P, aiger_not(witness_map[0][witness->outputs[j].lit]));
    P1P = gate(P1P, aiger_not(witness_map[1][witness->outputs[j].lit]));
  }
  return std::array{R0K, R0KP, R0P, F01K, F01KP, F01P, C0,
                    C0P, C1,   C1P, P0,   P0P,   P1P};
}

int main(int argc, char *argv[]) {
  auto check_path = initialize(argc, argv);
  auto shared = get_shared();
  auto [witness_map, model_map] = encode_unrolling(shared);

  auto [R0K, R0KP, R0P, F01K, F01KP, F01P, C0, C0P, C1, C1P, P0, P0P, P1P] =
      encode_components(shared, witness_map, model_map);

  // Reset: R0K ∧ C0 → R0KP ∧ C0P
  unsigned guard_reset = gate(R0K, C0);
  unsigned target_reset = gate(R0KP, C0P);
  unsigned reset = imply(guard_reset, target_reset);
  aiger_add_output(check, aiger_not(reset), "reset");

  // Transition: F01K ∧ C0 ∧ C1 ∧ C0P → F01KP ∧ C1P
  unsigned guard_trans = gate(gate(gate(F01K, C0), C1), C0P);
  unsigned target_trans = gate(F01KP, C1P);
  unsigned transition = imply(guard_trans, target_trans);
  aiger_add_output(check, aiger_not(transition), "transition");

  // Property: (C0 ∧ C0P ∧ P0P) → P0
  unsigned guard_prop = gate(gate(C0, C0P), P0P);
  unsigned property = imply(guard_prop, P0);
  aiger_add_output(check, aiger_not(property), "property");

  // Base: R0P ∧ C0P → P0P
  unsigned guard_base = gate(R0P, C0P);
  unsigned base = imply(guard_base, P0P);
  aiger_add_output(check, aiger_not(base), "base");

  // Step: P0P ∧ F01P ∧ C0P ∧ C1P → P1P
  unsigned guard_step = gate(gate(gate(P0P, F01P), C0P), C1P);
  unsigned step = imply(guard_step, P1P);
  aiger_add_output(check, aiger_not(step), "step");

  finalize(check_path);
}
