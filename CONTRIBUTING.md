# Development

Use C++20. Run `make test` or the CMake/CTest workflow before submitting changes.
No third-party dependency is needed for the initial core. Discuss new dependencies in an ADR.

Learning changes need tests of held-out behavior and failure/ambiguity handling, not just successful storage or textual self-reports. Keep evaluation answers separate from training data.

Record CPU, OS, compiler, flags and workload with performance claims. No claims of energy improvement without an energy measurement method.

Do not commit private teaching data, recordings, credentials, generated state or build outputs. The project has no open-source license yet; owner must decide before public release.
