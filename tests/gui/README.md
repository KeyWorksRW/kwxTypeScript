# GUI comparison tests

Pixel-level regression tests for the TypeScript port, driven by
[wxShot](https://github.com/KeyWorksRW/wxShot). Each sample launches the
reference C++ binary (`wxSamples`) and the TypeScript runner (`kwxts.exe`) on
the same machine, captures both windows, and compares them.

The agent-facing workflow is described in the `gui-comparison` guide.

## Running one sample locally

From the repository root:

```powershell
c:\rwCode\wxShot\bin\Release\wxshot.exe test `
    --manifest tests\gui\samples.json `
    --mode reference `
    --reference-root ..\..\wxSamples\bin\Release `
    --only about `
    --report tests\gui\artifacts\gui-report.json
```

`--reference-root` supplies the directory that `@ref` resolves against, so the
manifest stays machine independent: `reference.exe` is `@ref/about.exe`.

`@ref` may also come from `WXSHOT_REFERENCE_ROOT`, which is how the reusable
workflow passes it in CI.

## Artifact layout

`--report` and `--artifacts` write one directory per sample:

```
tests/gui/artifacts/
├── gui-report.json
└── about/
    ├── reference.png    # the C++ rendering
    ├── candidate.png    # the TypeScript port
    ├── diff.png         # reference dimmed to 40 %, differing pixels solid red
    └── compose.png      # reference | candidate | diff, scaled to fit
```

`compose.png` is the first image to look at: both panels show the same sample,
so a layout difference is visible without opening the other files. `diff.png`
then localises the difference.

`tests/gui/artifacts/` is git-ignored — artifacts are build output, never
committed.

## Baseline policy

- **No baselines are committed.** CI runs `mode: reference`, so the reference
  binary and the port are both captured on the runner and compared live. A
  developer-machine PNG would encode that machine's DPI, theme and font
  metrics.
- The `baseline` field in `samples.json` exists because the manifest schema
  requires it in reference mode, and `--update` writes to it. It is not a
  committed image.
- `--update` records the **reference** capture only — never the candidate. An
  agent must not run `--update`.

## Thresholds

Defaults are 2 % differing pixels (`maxDiffRatio: 0.02`), a per-channel
`pixelTolerance` of 8, and `minSsim: 0.98`. Raising a threshold requires a
comment in the manifest entry explaining why that sample cannot match more
closely.

## Adding the next sample

1. Confirm a matching C++ sample exists in `wxSamples/src/**` and that it
   builds to `wxSamples/bin/Release/<sample>.exe`.
2. Port it under `samples/<sample>/` as `<sample>.kwxts`.
3. Add an entry to `samples.json` under `samples`:

   ```json
   "<sample>": {
     "mode": "reference",
     "reference": { "exe": "@ref/<sample>.exe", "cwd": "@ref", "args": [] },
     "candidate": {
       "exe": "bin/Release/kwxts.exe",
       "cwd": ".",
       "args": ["samples/<sample>/<sample>.kwxts"]
     },
     "capture": { "windowTitleRegex": "<the top-level window title>" },
     "baseline": "baselines/<sample>.png"
   }
   ```

4. `windowTitleRegex` must match a **top-level** window that exists without
   interaction — wxShot has no input simulation, so a modal dialog opened from
   a menu cannot be captured. Confirm the title with:

   ```powershell
   c:\rwCode\wxShot\bin\Release\wxshot.exe list --process kwxts.exe --json
   ```

## Known limitation: the `about` sample

`wxSamples` `about.exe` and the TypeScript port both open a 500x313 frame whose
only content is a menu bar; the HTML page lives in the **modal** About dialog,
opened from File → About. wxShot's CLI has no keystroke or click injection, so
neither side can reach that dialog on its own:

- `windowTitleRegex: "About"` never matches, because the dialog is modal and is
  not created until the menu item is chosen. The run fails with exit 6
  (`TIMEOUT`, "no window matching 'About' appeared within 20000 ms").
- Matching the frame title instead (`wxHtmlWindow testing application`) makes
  both sides launch, but the reference frame captures as a single flat colour
  and is rejected with `BLANK_CAPTURE` in client mode; with `client: false` the
  two frames compare at `diffRatio` 0.41 — partly because the reference runs at
  DPI 120 and the port at DPI 96 (it declares no per-monitor DPI awareness),
  which also shifts the menu bar height and therefore the content below it.

Neither adjustment is a legitimate fix, and no threshold is relaxed to hide it,
so `tests/gui/samples.json` keeps the plan's documented values and this sample
is reported as failing rather than adjusted. The real fixes sit outside this
phase: input injection (`--send-keys`/`--click`) in wxShot so a menu-driven
modal can be reached, and a per-monitor-v2 DPI manifest for `kwxts.exe` so both
sides render at the same scale.
