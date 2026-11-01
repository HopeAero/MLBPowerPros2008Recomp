# Creating two-way players in My Life

`tools/patches/wiicompiled-two-way-player.patch` keeps the pitcher main position while a new career is initialized.
The day's role no longer replaces it in calls from the finalizer `0x80A20FB8`. The in-game test showed that
`[0x81048D84]` can also be zero during creation, so that flag alone does not protect initialization.

## Independent selection

With the two-way rule on, after choosing Pitcher and a second position:

1. The original panel asks for two pitching advantages.
2. The same panel repeats with two batting advantages.
3. The disadvantages panel asks for one pitching and then one batting disadvantage.

B from the first choice of the second round goes back to the first round. The three ids of each side are kept
separately. The original header still stores the pitching set; the extra choices only need to live in memory until
their effects are applied to the final record. The resulting abilities and stats use the game's normal save.
The original creation summary keeps its three pitching rows. The second-round lists are batting ones; the original
summary does not show all six choices at once and can keep labels from the last round while navigating. The ids of
each set stay separate when their effects are applied.

The advantage enter/update wrappers (`0x80977244`, `0x809772CC`) and disadvantage wrappers (`0x80A53244`,
`0x80A532A8`) reuse the translated functions, their lists, navigation, combination checks and sounds. They are
registered with `REGISTER_NATIVE_FUNCTION_AS` to keep the translated bodies they call. Confirming the first round
resets the panel for the second; the parent gets the confirmation when the second round ends.

## Applying the effects

`0x80A21160`, at the end of initialization, also applies the batting choices through the original routines
`0x80A219F4` (growth limits) and `0x80A233E8` (stats and abilities). The pitching stats word at record +0x78, the
position aptitudes at +0x5C..+0x67 and the pitching limits (indices 7..9 of both tables and the assignment at index 20)
are preserved. The initial growth is then computed with both sides present.

No generated code, disc file or user save is changed by hand.

## Verification

- `python tools/test_two_way_creation.py`: real position during creation, even with the flag at zero, and career /
  practice roles kept.
- `python tools/test_two_way_perks.py`: two rounds, B, separate sets, pitching stats/aptitudes/limits kept, rule off.
  It compiles the patch hooks against synthetic memory, without game content.
- Local `WiiCompiled` build completed.
- Checked in game with a new high-school career on the Red Sox, position P and second position RF: two advantage
  lists and two disadvantage lists; B from the batting round restores the pitching selection. The final record keeps
  148 km/h, control 105 and stamina 55, plus power 115 and contact 6. A fresh NAND under `local/tmp/two-way-creation/`
  was used; screenshots and traces stay under `local/`. No match was played and reloading was not tested.
- After that test, the disabled selection is cleared when switching rounds and the right advantage set is passed to
  the original disadvantage check. Both cases are covered by the synthetic test.
