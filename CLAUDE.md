# CLAUDE.md

Fast Downward fork with a SAT-based planner (planning as satisfiability, IPASIR / kissat).
Current work (branch `r2e`): Relaxed Relaxed ∃-step (R²∃) encoding and action-ordering ("ranking") methods.

## Git rules (strict)

- **Never push or commit to any branch other than `r2e`.** Commits/pushes are only allowed on `r2e`.
- Do not commit the stray untracked files in the repo root (`Apptainer.txt`, `formula.cnf`,
  `preprocessed-output.sas`, `sat_axiom_task.{cc,h}`, `output.sas`, `sas_plan`) unless asked.
- Commit only when asked; end commit messages with the attribution line from the system prompt.

## Build & run

```bash
# release build against the custom kissat (what builds/release is configured with)
./build.py --custom-kissat -s/home/gregor/Workspace/software/kissat-p/build/
./build.py --debug --custom-kissat -s/home/gregor/Workspace/software/kissat-p/build/   # debug build (asserts, DEBUG(...) macros, SAT variable names)
# other solver setups: --kissat (stock kissat), --ipasir -l<libname> -s<libdir>
```
- `build.py` flags: `-s<dir>` → `SAT_DIR`, `-l<lib>` → `SAT_LIB`; `--kissat/--custom-kissat/--ipasir` set `USE_KISSAT`/`USE_CUSTOM_KISSAT`.
  Without the custom kissat, `sat/kissat_dummy.cc` stubs the custom functions.
- Plain `cd builds/release && make -j downward` also works once configured.

```bash
./fast-downward.py output.sas --search "sat(encoder=ksr())"                 # one-by-one lengths, fresh solver per length
./fast-downward.py output.sas --search "inc(encoder=ksr(), strategy=goal)"   # incremental, single solver
./fast-downward.py output.sas --search "schedule(encoder=ksr())"             # Rintanen's algorithm B/C style scheduler (threads)
./fast-downward.py dom.pddl prob.pddl --search "sat(encoder=ksr(encoding=0))" # 0 = sequential (at-most-one action), 2 = ∃-step (default)
```
- **Always pass `encoder=...` to `sat(...)`**: its default `label_sat()` does not exist in this repo.
- Length strategies: `one_by_one()`, `by_iteration(...)`, `constant(...)` (`sat/length_strategy.cc`).
- Optional h² preprocessing: `--preprocess` runs `preprocess-h2` (`src/preprocess_h2`) and the search reads
  `preprocessed-output.sas`. It can remove operators/variables, so **operator IDs are always relative to the SAS
  file the search component actually reads.**
- A quick sanity task: `output.sas` in the repo root (414 operators, 71 variables).

## Code map (SAT part)

Compiled (see `src/search/CMakeLists.txt`, libraries `sat_solver`, `sat_search`, `plugin_sat`):

| File | Role |
|---|---|
| `src/search/sat/sat_encoding.h` | Abstract `SATEncoding` (per-solver-instance formula builder) and `SATEncodingFactory` (one per run, holds static precomputation, plugin category `SATEncodingFactory`). |
| `src/search/sat/sat_encoder.{h,cc}` | `sat_capsule`: thin wrapper over IPASIR with clause helpers (`implies`, `impliesOr`, `andImplies`, `atMostOne`, …); counts vars/clauses. Variable names only in debug builds (`DEBUG(sat->registerVariable(...))`). |
| `src/search/sat/kautz_selman_rintanen.{h,cc}` | KSR encoding (`ksr(...)`): sequential (encoding=0) and Rintanen ∃-step (encoding=2). Supports axioms/derived predicates, conditional effects, FDR + extra (h²) mutexes. |
| `src/search/sat/plugin_kautzselmanrintanen.cc` | `ksr` plugin options. `plugin_sat_encoding.cc` registers the factory category. |
| `src/search/sat/length_strategy.{h,cc}` | Makespan schedules + plugins. |
| `src/search/search_algorithms/sat_search.{h,cc}` + `plugin_sat.cc` | `sat(...)`: new solver & encoding instance per makespan. |
| `src/search/search_algorithms/incremental_sat_search.*` + `plugin_incremental_sat.cc` | `inc(...)`: one solver, adds time steps (strategies goal/init/middle). |
| `src/search/search_algorithms/rintanen_search.*` + `plugin_rintanen.cc` | `schedule(...)`: multiple makespans in parallel threads; encoding instances are created concurrently from one shared factory (factory must stay read-only after `initialize`). |
| `src/search/sat/kissat*.{h,cc}`, `ipasir.h`, `no_branching_heuristic.cc` | Solver glue. |

**Not compiled / legacy** (do not edit, do not take as reference for current behaviour): `src/search/sat/sat_search.{h,cc}`,
`src/search/sat/plugin_sat.cc`, `src/search/sat/branching_heuristic.cc` — old monolithic version of the SAT search.

### Encoding interface contract
- `factory->initialize(task_proxy, log)` is called once from the search's `initialize()`; all task analysis
  (axiom SCCs, disabling graph, **action ordering**) happens here.
- `createEncodingInstance(capsule)` per solver. Then `encode(t, t+1)` per step (states are indexed 1..k+1,
  actions of step t live at time t), `encodeInit(1, …)`, `encodeGoal(k+1, …)`, and after SAT
  `extractSolution(1, time_step_order)` which returns `(goal state, visited states, Plan)`.
- Plan extraction sorts the true actions of a step by their index in `factory->global_action_ordering`.

### Action ordering in KSR today (Rintanen ∃-step, `set_up_exists_step`)
- Disabling graph: edge `a → b` if `a` deletes a fact `b` needs (`compute_needs_and_deletes_for_operator`, includes
  conditional-effect conditions and axiom consequences), unless pre/effects are incompatible. Facts with
  `|deleters|·|needers| > disabling_threshold` are skipped ("threshold facts").
- SCCs via `sccs::compute_maximal_sccs`; `global_action_ordering` = SCCs in reverse order, ops inside an SCC in SCC order.
  `global_action_ordering[0]` is executed first in a step (lowest rank). It is a vector of operator IDs.
- Chains (`generateChain`) are built **per fact and per SCC** over erasers `E` and requirers `R` using
  the position inside the SCC. Cross-SCC interference is excluded by construction of the order. Consequence:
  with an arbitrary externally given order this per-SCC scheme is not valid — chains would have to range over the whole order.

## R²∃-step encoding (target of this branch)

References: `docs/Relaxing_the_Relaxed_Exist-Step_Parallel_Planning_Semantics.pdf` (Balyo, ICTAI 2013) and the thesis
`docs/IPTX_2010_1_11320_0_330331_0_84536.pdf` (Balyo 2014, §3.4 encoding, §3.4.2 ranking, §3.5 selective encoding,
§3.7.2 ranking experiments). **Follow the thesis where the two differ** (it is the corrected version; e.g. the paper
uses `t ∈ 1..k−1` for clause (5), the thesis uses states 1..k+1 and `t ∈ 1..k`).

Semantics: a parallel plan [A1..Ak] is valid iff some ordering of each Ai, concatenated, is a valid sequential plan.
Here the ordering is fixed up-front by a unique rank r(a); actions in a step execute in increasing rank.

Variables per step t: action `a^t`, fact `b^t_{x=v}` (t = 1..k+1), chain aux `h^t_{x=v,i}`.

Clauses (thesis numbering):
- 3.2 at-most-one value per variable, all t ∈ 1..k+1. (The thesis R²∃ omits ALO 3.1; adding ALO is sound since
  states are real states — the existing `mutex_encoding_for_timestep` adds both.)
- 3.8 init at t=1; 3.9 goal at t=k+1.
- 3.5 frame: `b^{t+1}_{x=v} → b^t_{x=v} ∨ ⋁{a^t | (x=v) ∈ eff(a)}`.
- 3.21 precondition: `a^t_i → b^t_{x=v} ∨ ⋁{a^t_j | r(a_j) < r(a_i), (x=v) ∈ eff(a_j)}` for every (x=v) ∈ pre(a_i).
- 3.22 effect/prevail propagation: `a^t_i → b^{t+1}_{x=v} ∨ ⋁{a^t_j | r(a_j) > r(a_i), x ∈ scope(a_j)}`
  for every (x=v) ∈ effprev(a_i) = effects ∪ prevail conditions (preconditions on vars without effect);
  scope(a) = variables of effprev(a).
- 3.23 interference chain, for every fact x=v, over N_{x=v} = rank-sorted list of all supporters
  S (x=v ∈ eff), opponents O (x=v' ∈ eff, v'≠v) and requirers R (x=v ∈ pre); prev(i) = predecessor in N:
  - `h_prev(i) → h_i` for i ∈ R ∪ O
  - `h_prev(i) → ¬a_i` for i ∈ R
  - `a_i → h_i` for i ∈ O
  - `h_prev(i) → h_i ∨ a_i` for i ∈ S (a supporter can break the chain)
  - Drop clauses that mention `h_prev` of the first element. An action can be in both R and O (non-prevail precondition).
  - Paper notes the chain can be shrunk by removing aux vars (e.g. no h before the first opponent, none after the last requirer).
- No completeness guarantee for a fixed k (depends on the ranking), but any sequential plan of length k is found at makespan ≤ k.
- Paper/thesis do not cover conditional effects or axioms — R²∃ must reject such tasks (exit `SEARCH_UNSUPPORTED`)
  unless/until we extend the encoding.
- Possible sound optimisation (verify before using): if a has pre x=u and eff x=v', it only effectively opposes x=u
  (other values of x are false right before a executes) — mirrors the delete pruning already done in KSR.

### Ranking methods (thesis §3.4.2, §3.5)
- Random (unique random ranks), Input (operator order of the SAS file), Inverted Input (r := n − r),
  Topological (TSort), Inverted TSort.
- TSort (Fig. 3.3): enabling graph `a → a'` iff eff(a) ∩ pre(a') ≠ ∅. For each a in input order, DFS that first
  recursively ranks all *supporting actions of a* (actions whose effects hit a's preconditions), then assigns
  `r(a) := lastRank++` (post-order; cycles ignored via visited flag). Implement iteratively (recursion depth = #ops).
- Selective encoding (§3.5): if |Δ|/|X| > 10 use the Reinforced encoding, else R²∃; with R²∃ alternate TSort on even
  makespans and Input on odd makespans. Thesis result: TSort best overall, Input clearly best on openstacks.
- The existing disabling-graph order (Rintanen) is a further ranking candidate.

## Planned work on `r2e` (todo)
1. Option to stop after the action ordering is computed and write it to a file (filename configurable).
2. R²∃-step encoding as a new `SATEncodingFactory`.
3. Balyo's ranking methods next to the existing disabling-graph ordering.
4. Option to read an ordering from a file: whitespace-separated operator IDs (IDs relative to the SAS file the
   search reads), listed in execution order (first = lowest rank); must be a permutation of 0..|O|−1.

## Conventions
- Code style in `src/search/sat` and the SAT search algorithms: tabs, `sat->...` helpers for clauses,
  `registerClauses("name")` / `variableCounter[...]` for statistics, `DEBUG(...)` for debug-only naming/logging.
- New plugins: follow `plugin_kautzselmanrintanen.cc` (TypedFeature + `make_shared_from_arg_tuples`) and add sources to
  the `sat_search` / `plugin_sat` libraries in `src/search/CMakeLists.txt`.
- Validate plans with VAL when PDDL is available (`./fast-downward.py --validate ...`). For SAS-only inputs,
  `extractSolution` replays the plan (`get_unregistered_successor`) but the comparison against the SAT state is
  commented out; new encodings should check applicability of each step and the goal (at least under `NDEBUG` off).
