// kwx_ts — source-location probe for the transpiled (.ts) loader path.
// Prints "caught-ok" when a handled error carries a .ts location, then
// throws uncaught so the stderr report can be checked as well.
function fail(message: string): never {
    throw new Error(message);
}

try {
    fail("caught probe");
} catch (error) {
    const stack = String((error as Error).stack);
    console.log(stack.indexOf("stack_trace.ts:5:") >= 0 ? "caught-ok" : "caught-mismatch");
}

function outer(): void {
    fail("stack trace probe");
}

outer();
