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
./fast-downward.py output.sas --search "sat(encoder=r2e(ordering=tsort()))"  # Balyo's R²∃-step encoding
# only compute the action ordering, write it and stop (exit code 12 = "stopped without solution")
./fast-downward.py output.sas --search 'sat(encoder=r2e(ordering=tsort(), write_ordering="order.txt", stop_after_ordering=true))'
# use an ordering from a file
./fast-downward.py output.sas --search 'sat(encoder=r2e(ordering=from_file(filename="order.txt")))'
```
- Options shared by `ksr(...)` and `r2e(...)` (`add_base_encoding_options_to_feature`): `ordering` (default
  `disabling()` for ksr, `tsort()` for r2e), `write_ordering="file"`, `stop_after_ordering`, `fdr_mutexes`,
  `additional_mutexes`, `force_at_least_one_action`. `r2e` additionally has `prune_opponents` (default true).
- Orderings (plugin category `ActionOrdering`): `disabling(threshold=5000000, join_groups_above_threshold=true)`,
  `tsort()`, `input()`, `random(random_seed=-1)`, `from_file(filename="...")`; all take `inverted=true|false`.
  The former `ksr` options `disabling_threshold`/`join_groups_above_threshold` now live on `disabling()`.
- Ordering file format: whitespace-separated (spaces and/or newlines) operator IDs in execution order (first =
  lowest rank). Must be a permutation of 0..|O|−1, otherwise exit 33 (input error).
- `ksr(encoding=0)` ignores `ordering` (sequential, identity order).
- State AMO per FDR variable is **always** encoded (all encodings rely on it for deletes, incl. the init state);
  `fdr_mutexes=false` only drops the ALO constraints.
- `inc(...)` needs an incremental IPASIR solver; with kissat it crashes.
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
| `src/search/sat/base_encoding.{h,cc}` | `BaseEncodingFactory` / `BaseEncoding`: common base of ksr and r2e. Factory: `initialize` = `analyse_task()` → `compute_action_ordering()` → write ordering / stop → `prepare_encoding()`; holds `global_action_ordering`, `statically_true_derived_predicates`, mutex options. Encoding: operator/fact variables, state mutexes, init, goal (`get_derived_goal_var` hook for axioms), plan extraction (sorted by ordering, **replayed and checked**: invalid plan → exit 32), statistics. Also defines `registerClauses`. |
| `src/search/sat/kautz_selman_rintanen.{h,cc}` | KSR encoding (`ksr(...)`): sequential (encoding=0) and Rintanen ∃-step (encoding=2). Supports axioms/derived predicates, conditional effects, FDR + extra (h²) mutexes. |
| `src/search/sat/plugin_kautzselmanrintanen.cc` | `ksr` plugin options. `plugin_sat_encoding.cc` registers the factory category. |
| `src/search/sat/relaxed_relaxed_exists_step.{h,cc}` + `plugin_relaxed_relaxed_exists_step.cc` | R²∃-step encoding (`r2e(...)`). |
| `src/search/sat/exists_step_sigma.{h,cc}` + `plugin_exists_step_sigma.cc` | `sigma(...)`: ∃-step for a fixed ranking with explicit intermediate states (σ_i construction of Cardellini/Giunchiglia/Maratea, "Symbolic pattern planning", AIJ 352 (2026), §3.2, classical part only). Per step and per fact change of a_i (rank order) a var σ_i(f) ↔ σ_{i−1}(f) ∨ a_i (add) / σ_{i−1}(f) ∧ ¬a_i (delete); preconditions a_i → σ_{i−1}(pre); last σ of a fact *is* b^{t+1}, unchanged facts get b^{t+1} ↔ b^t. `prune_deletes=true` (default): with pre x=u only x=u and the effect value change. Same options/orderings as r2e (default `tsort()`); rejects CE/axioms. |
| `src/search/sat/ordering/` | `ActionOrdering` base (`action_ordering.*`, also file read/write) and one class per ordering: `disabling_graph_ordering`, `topological_ordering`, `input_ordering`, `random_ordering`, `file_ordering`; all plugins in `plugin_orderings.cc`. |
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

### Action orderings and KSR ∃-step
- `ActionOrdering::compute(task_proxy, InterferenceInfo, log)` returns `ActionOrderingResult`: `order` (operator IDs
  in execution order, `order[0]` first), `groups` (partition; interference only needs to be handled inside a group,
  each group listed in execution order) and `unconstrained_facts` (facts for which no interference constraints are
  generated). Orderings other than `disabling()` (and every `inverted=true` ordering) return one group = whole order.
- `InterferenceInfo` (needing/deleting operators per fact) is computed by the encoding: KSR uses
  `compute_needs_and_deletes_for_operator` (axiom- and conditional-effect-aware), R²∃ uses
  `compute_basic_interference_info()` and only if `ordering->requires_interference_info()` (only `disabling()`).
- `disabling()`: disabling graph edge `a → b` if `a` deletes a fact `b` needs, unless pre/effects are incompatible.
  Facts with `|deleters|·|needers| > threshold` (or without needers) become `unconstrained_facts`.
  Groups = SCCs (`sccs::compute_maximal_sccs`), order = SCCs in reverse, ops inside an SCC in SCC order.
- KSR ∃-step builds `erasingList`/`requiringList` per fact and **per group** (position = index inside the group)
  and generates Rintanen chains (`generateChain`) per group. With `disabling()` the formula is identical to the
  pre-refactoring version (verified by CNF dumps). Any other ordering yields one group, i.e. chains over the whole order.
- Known pre-existing gap (not changed): for facts above the disabling threshold no ∃-step constraints are encoded
  and the at-most-one group for `join_groups_above_threshold` is built but never encoded — possibly unsound for huge tasks.

## R²∃-step encoding (implemented in `relaxed_relaxed_exists_step.*`)

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
- `prune_opponents=true` (default): if a has pre x=u and eff x=v', it only opposes x=u (other values of x are already
  false right before a executes; the last real destroyer of a fact is always an unpruned opponent). `false` = paper.
- Implementation notes: the factory precomputes `rank`, rank-sorted `supporters[var][val]` and `scope_operators[var]`,
  `effprev` per operator, and per fact the chain trimmed to [first opponent .. last requirer] (facts without an
  opponent before a requirer get no chain). Chain variables are shared between positions where possible: a new `h`
  is created only after a supporter or when an opponent follows a requirer that already used the current `h`.
  ALO+AMO state mutexes come from the base class.

### Ranking methods (thesis §3.4.2, §3.5)
- Random (unique random ranks), Input (operator order of the SAS file), Inverted Input (r := n − r),
  Topological (TSort), Inverted TSort.
- TSort (Fig. 3.3): enabling graph `a → a'` iff eff(a) ∩ pre(a') ≠ ∅. For each a in input order, DFS that first
  recursively ranks all *supporting actions of a* (actions whose effects hit a's preconditions), then assigns
  `r(a) := lastRank++` (post-order; cycles ignored via visited flag). Implement iteratively (recursion depth = #ops).
- Selective encoding (§3.5): if |Δ|/|X| > 10 use the Reinforced encoding, else R²∃; with R²∃ alternate TSort on even
  makespans and Input on odd makespans. Thesis result: TSort best overall, Input clearly best on openstacks.
- The existing disabling-graph order (Rintanen) is a further ranking candidate.

## Status of `r2e`
Done: (1) `write_ordering`/`stop_after_ordering`, (2) `r2e` encoding, (3) Balyo's rankings (`tsort`, `input`,
`random`, `inverted`) next to `disabling`, (4) `from_file`. Sanity-checked on IPC tasks: plans VAL-valid, TSort/Input
match a reference implementation of thesis Fig. 3.3, write→`from_file` round trip yields an identical formula.
Not done: §3.5 selective encoding / alternating rankings per makespan (needs the makespan in `createEncodingInstance`
or two orderings in the factory) and the Reinforced encoding; KSR old-vs-new full-solve comparison on miconic-ADL,
parcprinter, visitall, pegsol.

## Testing tips
- Benchmarks: IPC domains in `~/Workspace/domains/downward-benchmarks` (axiom domains in `~/Workspace/domains/axioms`).
  Translate with `./fast-downward.py --translate --sas-file X.sas dom.pddl prob.pddl`; run the search binary directly
  with `builds/release/bin/downward --search "..." < X.sas`. Use only one CPU core for test runs.
- To check that a refactoring does not change a formula, dump all clauses with an `LD_PRELOAD` shim that wraps
  `kissat_init`/`kissat_add` from `libkissat.so` (wrapping `ipasir_add` does not work: it is defined inside the
  binary in `sat/kissat.cc`) and compare old vs new binary with `length_strategy=constant(plan_length=k)`.

## Conventions
- Code style in `src/search/sat` and the SAT search algorithms: tabs, `sat->...` helpers for clauses,
  `registerClauses("name")` / `variableCounter[...]` for statistics, `DEBUG(...)` for debug-only naming/logging.
- New plugins: follow `plugin_kautzselmanrintanen.cc` (TypedFeature + `make_shared_from_arg_tuples`) and add sources to
  the `sat_search` / `plugin_sat` libraries in `src/search/CMakeLists.txt`.
- Validate plans with VAL when PDDL is available (`./fast-downward.py --validate ...`). Independently,
  `BaseEncoding::extractSolution` replays every extracted plan and exits with an error if it is invalid.
- New encodings: derive from `BaseEncodingFactory`/`BaseEncoding` (implement `analyse_task`,
  `compute_action_ordering`, optionally `prepare_encoding`, `createEncodingInstance`, and `encode`).
