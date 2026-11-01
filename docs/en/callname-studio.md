# Callname Editor (Callname Studio)

**Power Pros Wii 2008 Callname Studio** creates commentator names without writing TOML. Open **SETTINGS → TOOLS → CALLNAME EDITOR**. `CallnameStudio.exe` is installed next to the game and shares the Season Editor's appearance and language setting.

1. Select a mod or choose **+ New mod**. New mods are enabled automatically; **Enable / Disable** updates `Config.toml`.
2. Choose **Add**. The first free voice number (7223–10239) is allocated across all mods, including disabled ones. You can change it; a warning identifies clashes. With conflicting active mods the game uses the first by name.
3. Enter **List text**. The preview uses the game's rules: accents removed, unsupported characters dropped, plus the **Search Type** form. Saving requires 1–15 visible game characters.
4. Choose **Normal** and optionally **Excited / Full**. Missing variants use Normal. Files must be 16-bit PCM WAV or MP3, mono/stereo, 8–192 kHz, up to 5 seconds. Selected files are validated and copied to `callnames/` with safe names; **Listen** plays them. Use your own or licensed audio.
5. Choose **Save**. In **PowerUp+ (MODS › EDIT ROSTER)** the voice picker lists **Callname <text>**. Assign voices, export the roster, and enable both the roster and callname mods. Restart the game if it was open.

Select a row to **Edit** or **Delete**. Deletion asks for confirmation and keeps audio files that other entries might use. Cancelling an edit can also leave imported audio files; these are never automatically deleted.

The editor writes only to the selected mod and `Config.toml`. It reads handwritten TOML and preserves unknown fields and comments, although formatting and order can change. Unreadable manifests block editing/automatic voice allocation with an error, avoiding data loss or accidental clashes. Audio paths must stay inside `callnames/`.

## Build and check

```powershell
& tools/callname-studio/build.ps1
& local/build/callname-studio/CallnameStudio.exe --selftest
python tools/callname-studio/test_cli.py local/build/callname-studio/CallnameStudio.exe
```

The editor shares `ui.h`, `config.cpp`, SDL3 and Dear ImGui with the other studios. It links the runtime's `collect.cpp` and `audio.cpp` directly for identical text conversion and audio validation.

Test options (zero-based indices; UTF-8 paths):

```text
--mods <folder> --config <Config.toml> --mod <id>
--create-mod
--add --list <text> --normal <file> [--excited <file>] [--full <file>] [--voice <id>]
--update <index> --list <text> [--normal <file>] [--voice <id>]
--delete <index>
--validate
--lang es|en --edit <index>
```

`--create-mod` creates and enables a mod and can be combined with `--add`. `--add`, `--update`, `--delete` and `--validate` exit without a window. `--edit` opens the dialog for screenshots. CLI operations share the window's import, validation and save code.
