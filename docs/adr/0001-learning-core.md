# Architecture decision 0001

Status: experimental, not production-ready.

Use a dependency-free native C++20 implementation to make the first learning claim executable in the available environment. This does not settle the long-term Rust versus Java Native Image decision.

Pipeline: teacher examples → bounded expression enumeration → sample-consistency filter → representative executable hypothesis → held-out evaluation.

Persistence stores examples, not assertions of mastery. Prediction always displays the underlying hypothesis. A task can be out of grammar or out of search budget; neither is silently classified as learned.

CPU considerations: bounded candidate count, small contiguous numeric vectors, deduplication by observed behavior, no global neural retraining. Remaining costs: tree enumeration, string construction, ordered-map comparisons and fresh search per query. Profile before changing representations.

The current grammar and search order are inductive priors. The system starts without per-skill examples, not without prior structure. Natural language and raw sensor representation learning are outside v0.1.

ChronoSynth remains separate. Future adapters can import lessons and export evaluated skills with provenance. Narrative updates and keyword coverage must never substitute for independent execution evidence.
