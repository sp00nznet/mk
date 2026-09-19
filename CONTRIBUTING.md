# Contributing

Contributions toward the static C recompilation are welcome. Correctness changes should be small,
reproducible, and measured against the original ROM as an execution oracle.

## Never commit proprietary game data

Do not commit ROMs, firmware, save-states, extracted assets, memory snapshots, traces containing
bulk ROM data, or generated build products. Contributors must obtain and keep their own legal ROM
copy locally. The repository ignore rules cover common file types, but always inspect
`git status` and the staged diff before committing.

## Recompiler changes

- Add or update focused cases in `tools/recomp/test_autogen.py` for opcode, addressing-mode,
  M/X-width, CFG, timing, and call-frame behavior.
- Regenerate `src/recomp/smk_autogen.c` from the checked-in profile rather than editing generated
  bodies manually.
- Distinguish “generates successfully” from “oracle-validated and safe to enable.”
- Gate default intercept-set changes on the deterministic 1,400-frame snapshot comparison.
- Document any ignored memory region and prove that it is dead scratch rather than live state.
- Keep experimental timing features opt-in until their acceptance gate passes.

## Submodule changes

If a parent change depends on `ext/snesrecomp`, publish the submodule commit first. Then update the
parent gitlink and verify that a fresh recursive checkout can resolve it. Do the same for nested
LakeSnes changes if any are introduced.

## Minimum checks

```bash
python -m unittest tools.recomp.test_autogen -v
cmake --build build --config Release --target smk_launcher
git diff --check
```

For timing or runtime changes, also run the relevant ROM-oracle snapshot gate described in
[`docs/contribution_status.md`](docs/contribution_status.md).
