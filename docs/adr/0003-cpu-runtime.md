# ADR 0003: CPU session, DAG, local updates and cooperative resource limits

Status: implemented in P2. Extends P1; no new domain knowledge or wider grammar is claimed.

## Representation and enumeration

Each canonical polynomial has a fixed-coefficient DAG node: operand IDs, operation, depth and complexity. Child IDs always precede parents. The exact-identity index uses fixed arrays, not sample signatures. Only returned hypotheses are formatted as expression strings. Fast matching evaluates polynomial coefficients via Horner; tests compare that result with independent recursive DAG evaluation at multiple probes.

Enumeration owns a cursor (depth, previous-level width, left/right IDs, operation). `SearchControl.maxCombinations` yields a work slice; cancellation tokens and deadlines yield without resetting the cursor. A resumed session must produce the same identities and combination count as uninterrupted enumeration. Termination at configured total combinations/unique nodes/storage is permanent for that session. Configure a different budget to rebuild.

Tie-breaking after node count now uses stable discovery ID, not formatted text as in P1. This avoids formatting all matches solely for ordering. The chosen syntax can differ but identity and predictions must remain consistent. Neither version guarantees the globally smallest syntax tree.

## Local cache

One mutable session per Learner, keyed by the complete SearchOptions, is reused across skills. One fit cache stores skill name, teaching snapshot and **all** consistent IDs (not just those displayed). Identical examples skip numeric matching. An appended lesson checks only the new suffix against the prior consistent set. Replacing an old label or changing skills performs a full fit against the existing DAG; changing options rebuilds the graph. This allows previously rejected rules to recover after correction.

Incomplete or interrupted fits never replace a valid cache. Truncated displays may still cache the full consistent set so future teaching does not lose omitted hypotheses. Loading state creates a new Learner and clears sessions. One instance is single-caller; `const learn` mutates caches and is not safe for concurrent calls. Independent instances can run on separate threads. Externally requested cancellation is supported through stop_token.

## Storage accounting

A custom PMR memory_resource backs both the graph vector and canonical index. It rejects allocations exceeding maxStorageBytes, tracking currently requested bytes and peak requested bytes, including overlapping vector buffers during growth. Nodes/keys have no internal dynamic allocation. A failed index insertion rolls back the appended node, retaining valid IDs and index invariants. Index construction is inside the same budget guard: MSVC allocates an empty-map sentinel while libstdc++ does not; even a zero-byte budget must return memory_limit without an escaping allocation exception.

This is not a process RSS budget. The session object, returned expression strings, fit ID vectors/cache, teaching examples, allocator metadata and stack are outside this resource. Process RSS is reported independently by the Linux benchmark collector. No system isolation or hard realtime guarantee is claimed.

## Time and cancellation

Generation and fit loops check a caller stop_token and optional steady-clock deadline. Tests use an injected clock to expire deterministically and request cancellation during work. Pre-cancelled warm-cache queries also abstain. Formatting returned results, resource initialization, allocations and scheduling are not preempted. Active-question scoring and exam scoring remain bounded but do not accept the generation deadline. The CLI limits therefore describe search/matching, not an end-to-end UI deadline.

## Evaluation and evidence

Frozen reports carry search settings and completeness. Incomplete sessions cannot certify a candidate, even if numeric exam answers all match. Cancellation/limits must not change lessons or manufacture mastery. P2 tests cover cursor resumption, full/incremental equivalence, correction recovery, display-cap recovery, graph invariants, PMR failures, deadlines and CLI restart.

Benchmarks report new combinations, numeric checks, latency distributions, graph-requested bytes and process RSS with environment metadata. Warm versus cold compares two paths within P2, not against a GPU or an earlier release. It does not measure energy. Sanitizer checks disable LeakSanitizer only because the instrumented environment uses ptrace; leak freedom remains unverified.

## Remaining work

No cross-process cursor persistence, incremental grammar expansion, disk cache, background scheduler, SIMD kernels or learned skill composition. No wall-clock latency promise or percentage energy claim follows from this implementation.
