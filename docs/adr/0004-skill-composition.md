# ADR 0004: Accepted skills, bounded program reuse and versioned replay

Status: implemented in P3. This is program abstraction/reuse inside the existing arithmetic domain, not unrestricted concept creation.

## Two states

Learner holds mutable teaching drafts and P2 caches. SkillLibrary holds independently evaluated immutable versions and a small active-name map. `ask` queries a draft; `run` executes an accepted version. Draft teaching never changes an accepted function. Admission requires a frozen/current draft, a complete unique hypothesis for learned or inferred-composition candidates, independent held-out execution and replay of other active skills. An intentional redefinition may replace the same active name; that old name's former exam is not a veto on the new definition.

Pending candidates have a per-process library-instance identity plus activation epoch and draft revision/snapshot. Teaching changes, library activation/rollback, reload or moves invalidate them. Version IDs are local to a library archive, not globally meaningful or cryptographically authenticated.

## Composition

Enumerate pipelines of 1..3 active skills in deterministic name/version order, excluding the target active name. Default caps are 512 tested programs and 64 distinct polynomial candidates; the C++ API permits at most 4,096 programs and 256 candidates. The P2 control token/deadline and per-call slice also constrain planning, but P3 composition planning itself is not resumable. Larger libraries may exceed the budget before the full search is examined; no candidate may then be published. Retrieval-guided source selection is future work.

Each pipeline pins concrete dependency versions. Derive a bounded polynomial by symbolic composition and fit teaching inputs by actually calling the version graph. Group exact polynomial identities, retaining the first fitting pipeline. This is algebraic grouping, not equivalence of every intermediate domain precondition. Therefore the independent exam also executes the pinned pipeline, including domain checks. Exam results never select another pipeline.

A primitive costs one call; a pipeline costs one plus the sum of its dependency costs. Limit cost to 64, nesting to eight, steps per routine to three, polynomial degree to 16 and coefficients to ±1,000,000. The shared numeric input/output domain is enforced at every skill invocation. Dependency versions must already exist, so new IDs cannot introduce cycles.

The demo learns shift and square separately, then infers shift→square→square from new target examples. The resulting shifted quartic is outside the default two-level primitive grammar, but reusable accepted skills make it expressible. This is the concrete new capability demonstrated by P3.

## Program abstraction

Expand active version graphs to pinned leaf calls, mine contiguous 2..3-call sequences used by at least two different active skill names, and rank by support then reference order. This is a reusable procedure proposal. The user chooses a mined candidate, supplies target lessons and an independent exam, and accepts it as a new skill. Support measures repeated use, not independent evidence that a scientific statement is true. The new abstraction can itself be a future composition source.

## Versioning and replay

Keep up to 256 immutable versions and 64 active names. Updating a dependency's active name does not update existing routines. Rollback switches only an active pointer; it does not revert draft observations or rewrite dependent versions. `replay` executes stored acceptance cases against active pinned programs under a shared call budget and optional cancellation/deadline. It is a regression/retention check, not parameter learning or fresh held-out evidence. Admission replays other active skills before activation.

## Persistence

`CPUAI_LIBRARY1` stores version history, source teaching snapshots, search settings, semantic coefficients, pinned references, acceptance cases and active pointers. `CPUAI1` draft files remain separate. Restore is staged in a new library and validates bounded counts (4 MiB archive, 128 teaching/test cases per version), increasing IDs, earlier dependencies, composed coefficients, learned primitives via re-inference, disjoint exams, actual execution and active replay. Display syntax is reconstructed. Source metadata for composition uses its origin/pinned program; primitive SearchOptions do not describe the composition enumerator.

The archive is an inspectable local record, not signed proof. Public example exams are regression fixtures and can be contaminated by human inspection. Current-grammar validation can reject an archive after a future grammar/scoring change; future migrations must be explicit. Direct file writes do not provide crash-atomic persistence. Stored tests provide historical scoped evidence rather than universal mastery.

## CPU and remaining scope

Pure native CPU execution. Planning is bounded and uses small exact polynomials and existing routines; it does not retrain a global network. Skill-history/pattern-mining storage is outside P2's DAG PMR budget, though counts and lengths are bounded. One caller per library/learner instance; cancellation tokens may be signalled externally. No cross-device synchronization, online neural consolidation, multimodal representation learning or unrestricted theorem discovery is claimed.
