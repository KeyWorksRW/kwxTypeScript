// Thin entry point — imports demo.ts as a library module.
//
// Run with:
//   deno run --allow-ffi --allow-read --allow-env --allow-write examples/demo_main.ts

import { main } from "./demo.ts";
Deno.exit(main());
