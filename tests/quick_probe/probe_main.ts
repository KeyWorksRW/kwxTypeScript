// Hand-written probe entry for the kwxTypeScript probe lane.
//
// probe.wxui (../../quick/tests/quick_probe/probe.wxui) generates
// gen/probe_mainframe.ts and gen/probe_dlg.ts. Type-check only:
//   deno check --no-lock tests/quick_probe/probe_main.ts
// Probe output never goes to tests/quick.

import {
    initialize,
    initAllImageHandlers,
    run,
    setAppName,
    setTopWindow,
    shutdown,
} from "kwx";
import { createProbeMainFrame } from "./gen/probe_mainframe.ts";
import { createProbeDialog } from "./gen/probe_dlg.ts";
import { exit } from "kwx/platform";

export function main(): number
{
    if (!initialize())
    {
        console.error("Failed to initialize wxWidgets");
        return 1;
    }

    setAppName("Probe");
    // The generated dialog decodes its embedded artwork, which needs the handlers.
    initAllImageHandlers();

    const frame = createProbeMainFrame(null);
    frame.Show();
    setTopWindow(frame.ptr);

    const dialog = createProbeDialog(frame.ptr);
    dialog.ShowModal();

    const exitCode = run();

    shutdown();
    return exitCode;
}

// Match the samples' bootstrap: exit() reports the code from run() as the process status.
exit(main());
