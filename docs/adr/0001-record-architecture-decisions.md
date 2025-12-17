# ADR 0001: Record architecture decisions

## Status

Accepted

## Context

This repository is early-stage and is expected to evolve quickly. The project has strong constraints (determinism, replayability, wire-level fidelity, UDP realities) and will likely require design decisions that should remain understandable months later.

Without a lightweight decision log:
- design intent is lost,
- refactors become harder to justify,
- and downstream consumers may be surprised by behavior changes.

## Decision

We will use Architecture Decision Records (ADRs) to record significant decisions and their rationale.

ADRs will:
- live in `docs/adr/`,
- be numbered with 4 digits (zero-padded),
- be written in Markdown,
- and be updated via new ADRs (prefer “superseded” over rewriting history).

## Alternatives considered

- Keep decisions only in PR/issue discussions
  - Rejected: hard to discover later and not versioned as a coherent narrative.
- Maintain a single “architecture decisions” doc
  - Rejected: quickly becomes an unstructured dumping ground with unclear chronology.

## Consequences

- Adds a small process overhead when making significant changes.
- Improves long-term maintainability and onboarding speed.

## Notes / References

- Template: `docs/adr/0000-adr-template.md`


