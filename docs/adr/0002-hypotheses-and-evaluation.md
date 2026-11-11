# ADR 0002: Bounded hypotheses and independent evaluation

Status: implemented in P1. Supersedes ADR 0001's sample-signature deduplication and first-match behavior.

## Problem

The P0 learner discarded x² after seeing x on inputs 0 and 1. It then could not represent uncertainty. Fitting sample outputs is not semantic identity, and self-generated checks cannot establish new-task performance.

## Search

Use exact canonical integer coefficient vectors to identify polynomials in the existing +,-,* grammar. Keep different polynomials even when they agree on training data. Retain the first representative for an identity; rank surviving representatives by node count and then text, without claiming global minimal syntax.

Enumerate all configured levels before selecting consistent candidates. Default depth changes from three to two to keep this full bounded search small (the demo evaluates 2,346 combinations and retains 396 unique expressions). This is a transparent prior, not automatic discovery of a representation. Higher depths can be requested through the C++ API under explicit limits.

Coefficients are bounded to ±1,000,000 and degree to 16; intermediate products accumulate safely in int64 under those operand limits. Use long-double Horner evaluation and an explicit relative tolerance for numeric matching. These guarantees apply to valid grammar objects, not a formal proof of real-world arithmetic accuracy.

Search budget and unique-cap exhaustion differ from no fit. Candidate truncation differs from uniqueness. Both incomplete conditions make prediction abstain; existing candidates may still suggest a question, labeled partial.

## Active teaching

Partition retained candidates' predictions on the fixed probe grid [-3,-2,-1,0,1,2,3,4], excluding taught inputs. Choose the input minimizing the largest bucket, breaking ties by more buckets and then grid order. This has no probability model, confidence percentage or global optimality claim. No question means no distinction on this grid, not mastery.

## Evaluation

Freeze one candidate and its teaching snapshot before loading an independent named exam. Do not evaluate all candidates and choose the one that scores best. Do not add exam cases as lessons. Require 3..128 unique test inputs disjoint from teaching inputs. A passing result is historical evidence only for that candidate and suite; other retained candidates can still disagree. Incomplete search cannot yield passed_on_suite even with 100% numeric accuracy.

Reports bind the candidate, search settings, revision, teaching snapshot and suite ID plus all inputs/answers. isCurrent compares revision and snapshot. State files retain CPUAI1 compatibility and store no report or certification. Cross-process/cross-device report identity, signed certificates and immutable content IDs are out of scope.

The included public square-v1 fixture checks regressions. It is not a secret benchmark; developers/teachers can contaminate it by adapting teaching to its labels. Users need a separately held-out suite for independent research claims.

## Remaining costs

Queries re-enumerate. Coefficient vectors, strings and ordered maps allocate per candidate. There is no hard runtime deadline, cancellation, energy measurement or cache. Address these in P2 after profiling. CPU-only does not imply efficiency compared with another implementation.
