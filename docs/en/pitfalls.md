# Environment pitfalls

*Español: [../trampas.md](../trampas.md)*

Silent or misleading failures of the working environment, one dated line per pitfall. The handoff for each piece of work is in [../traspaso.md](../traspaso.md) (Spanish only).

- **2026-11-07 · Poisoned `pipeline_cache.db`:** a failed boot (e.g. a match benchmark with Arrange teams whose data was never loaded) can leave rows in `local/wiicompiled-mlb/build/UserData/Cache/pipeline_cache.db` that abort **every** later boot on the pipeline thread (`aurora::gx::build_shader` → `Module::fatal`, seen with lldb), even though the code is fine. Fix: delete the new rows (`DELETE FROM pipeline_cache WHERE rowid > <last good>`) or restore a copy. Copy the file before a risky boot.
- **2026-11-07 · Arrange save v2/v3 downgrade risk:** with this patch, any save made with the marks option OR the uniforms option enabled migrates the `MLBARRMK` block to version 3 at `0xD2E00`. An old v2 runtime that reads that save sees the block as Invalid at `0xD3000` and, if it saves with changes, overwrites part of the v3 (the uniform choices are lost). Acceptable while v2 is not widely distributed; once upgraded, do not go back to an older runtime with those saves.
