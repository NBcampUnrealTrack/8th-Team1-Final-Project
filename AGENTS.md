<!-- revvy:shared-memory:start -->
Shared memory for ALL agents (Claude/Codex/Kimi) lives at `.revvy/shared-memory.md`.
Read it at session start. When you make a durable decision, adopt a convention,
or leave work half-done, append a dated one-line entry there so the next agent
(possibly a different model) picks up where you left off.
<!-- revvy:shared-memory:end -->

## Functional performance acceptance

Before every functional implementation or change, read and apply
`C:/Users/yunse/.codex/skills/feature-performance-gate/SKILL.md`.
Severe FPS drops, excessive CPU/GPU memory, or assigning GPU-suitable heavy work
entirely to the CPU each make the feature **FAIL**. All three must pass measured
validation at the intended scale; missing evidence is **UNVERIFIED**, never PASS.
Use Unreal Insights repeatedly through the development and validation cycle.
