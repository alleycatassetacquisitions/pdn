# GitHub Issue: chore: integrate anti-bloat tooling into CI pipeline

**Labels:** enhancement, process

---

## Summary

As part of the code quality initiative, we need to integrate anti-bloat and duplication detection tooling into our development workflow. This will catch regressions, prevent copy-paste proliferation, and give us baseline metrics for tracking improvement.

## Tools to Integrate

### 1. Bloaty McBloatface — Binary Size Profiling
```bash
bloaty .pio/build/native_cli/program -d compileunits -n 20
bloaty .pio/build/native_cli/program -d symbols -n 30
```
- Tracks where flash/RAM is being consumed
- Catch size regressions per compile unit
- Baseline report should be generated and stored

### 2. CPD (Copy-Paste Detector) — Duplication Detection
```bash
# PMD's CPD tool (requires Java)
cpd --minimum-tokens 50 --language cpp --dir src/ --dir include/
```
- Detects duplicated code blocks across the codebase
- Should flag any new duplication above threshold
- Current baseline: ~1,882 lines of known duplication

### 3. cppcheck — Static Analysis
```bash
pio check -e native --tool cppcheck
```
- Already available via PlatformIO
- Should run on every PR

### 4. include-what-you-use (IWYU) — Header Hygiene
```bash
iwyu -p .pio/build/native/ src/**/*.cpp
```
- Catches unnecessary includes that bloat compile times and binary size
- Run periodically (not every PR — too noisy initially)

## Build Flag Optimization

Consider adding to `platformio.ini` for ESP32 release builds:
```ini
build_flags =
    -Os                      ; Optimize for size (not -O2)
    -ffunction-sections      ; Each function in own section
    -fdata-sections          ; Each global in own section
    -fno-rtti                ; Save ~10-20KB

link_flags =
    -Wl,--gc-sections        ; Strip unreferenced code
```

## Acceptance Criteria

- [ ] Bloaty baseline report generated and committed
- [ ] CPD runs with `--minimum-tokens 50` against `src/` and `include/`
- [ ] cppcheck runs on native build environment
- [ ] Build flags reviewed for ESP32 release target
- [ ] Results documented in `docs/` for fleet agent reference

## References

- `docs/DUPLICATION-ANALYSIS.md` — current duplication audit
- Bloaty: https://github.com/google/bloaty
- PMD CPD: https://pmd.github.io/latest/pmd_userdocs_cpd.html
