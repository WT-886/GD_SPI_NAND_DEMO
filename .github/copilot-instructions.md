# Project Guidelines

## Context
- Read [README.md](../README.md) for hardware wiring, setup, module ownership,
  test switches, datasheet references, and known limitations.
- Target GD32450Z-EVAL / GD32F450ZKT6, bare-metal C11 with the GD32F4xx library.
- Keep generic NAND operations, Continuous Read, BBM, and test/demo code in
  their existing modules. Extend the device table using the matching datasheet.

## Safety and Evidence
- Building is not a hardware test. Report exactly what was verified.
- Do not flash, run destructive tests, erase NAND, or execute persistent A1h
  mappings without explicit permission for those operations.
- The release configuration enables destructive tests and BBM execution.
  Disabling the full-chip switch alone does not make the firmware read-only.
- Existing chip BBM still applies to driver addresses. Never assume a historical
  A5h table, free-link count, or demo candidate is the current device state.
- Separate datasheet guarantees from measurements and hypotheses. Historical-PBA
  reuse is experimental; A1h does not copy data and consumes finite link entries.

## Change and Validation
- Preserve user changes, public APIs, vendor copyright notices, and licenses.
  Correct stale project comments without rewriting vendor release history.
- Keep manual CMake changes inside the existing mcudeva:user sections.
- After C changes, build the Debug preset with the available VS Code/MCU build
  tools. Check diagnostics and git diff --check; never claim hardware PASS from
  compilation alone. Toolchain/debug paths are machine-specific.
- Keep documentation-only changes in separate docs: commits. When asked to
  commit, provide detailed purpose, changes, validation, and operational risks.
- Develop on debug unless instructed otherwise; do not commit, merge, move tags,
  rewrite history, or push without an explicit request.
- Update README.md when wiring, geometry, defaults, deployment, or risks change.
- Respond in Simplified Chinese unless requested otherwise.