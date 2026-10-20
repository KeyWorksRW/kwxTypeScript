// kwx_ts — platform surface for FFI access.
//
// This is the only module that talks to the kwx_ts host bridge
// (`globalThis.kwxHost`). Generated bindings import it through the bare
// specifier "kwx/platform"; the runtime resolves that specifier to the embedded
// kwx_platform asset, never to a file on disk.
//
// Importing this module under a plain JavaScript runtime (Deno, Node, a
// browser) throws an error that names the missing runtime immediately, rather
// than failing later inside a binding call.

/** An opaque native pointer: a non-zero address, or null for a null pointer. */
export type Pointer = bigint | null;

/** Native parameter and result types used by the generated binding corpus. */
export type NativeType =
    | "pointer"
    | "function"
    | "i32"
    | "u32"
    | "u8"
    | "usize"
    | "f32"
    | "f64";

/** One symbol declaration inside a dlopen() table. */
export interface SymbolDeclaration {
    readonly parameters: readonly NativeType[];
    readonly result: NativeType | "void";
}

/** The complete symbol table passed to dlopen(). */
export interface SymbolDefinitions {
    readonly [symbolName: string]: SymbolDeclaration;
}

/** The JavaScript value a native type is marshalled to. */
// usize is 64-bit on every supported platform, so it marshals as a bigint.
type NativeTypeToJs<T extends NativeType | "void"> =
    T extends "pointer" | "function" ? Pointer
        : T extends "void" ? void
            : T extends "usize" ? bigint
                : number;

/** The JavaScript argument tuple for one declaration. */
type NativeParameters<T extends readonly NativeType[]> = {
    [TIndex in keyof T]: T[TIndex] extends NativeType
        ? NativeTypeToJs<T[TIndex]>
        : never;
};

/** One callable native symbol. */
type ForeignFunction<TDeclaration extends SymbolDeclaration> = (
    ...parameters: NativeParameters<TDeclaration["parameters"]>
) => NativeTypeToJs<TDeclaration["result"]>;

/** The callable symbols produced from a dlopen() declaration table. */
export type NativeSymbols<TSymbols extends SymbolDefinitions> = {
    readonly [TKey in keyof TSymbols]: ForeignFunction<TSymbols[TKey]>;
};

/** A loaded library: a lazily resolved symbol table and a close() no-op. */
export interface Library<TSymbols extends SymbolDefinitions = SymbolDefinitions> {
    readonly symbols: NativeSymbols<TSymbols>;
    close(): void;
}

/** Callback signature declaration for a native event closure. */
export interface CallbackDefinition {
    readonly parameters: readonly NativeType[];
    readonly result: NativeType | "void";
}

/** Host platform name. */
export type PlatformName = "windows" | "darwin" | "linux";

type NativeSymbolFunction = (...parameters: unknown[]) => unknown;

interface KwxHost {
    readonly __kwx_platform_os: string;
    __kwx_get_args(): string[];
    __kwx_set_exit_code(code: number): void;
    __kwx_resolve_symbol(name: string): NativeSymbolFunction | undefined;
    __kwx_ptr_of(value: ArrayBufferView | ArrayBuffer): Pointer;
    __kwx_get_cstring(pointer: Pointer): string;
    __kwx_get_array_buffer(pointer: Pointer, byteLength: number): ArrayBuffer;
    __kwx_callback_create(handler: (...parameters: Pointer[]) => void): number;
    __kwx_callback_pointer(slot: number): Pointer;
    __kwx_callback_close(slot: number): void;
}

// globalThis has no declared kwxHost property; the double assertion is the
// narrowest way to read a host-injected global without adding an ambient
// declaration that would leak into every user program.
function requireHost(): KwxHost {
    const host = (globalThis as unknown as { kwxHost?: KwxHost }).kwxHost;
    if (!host) {
        throw new Error(
            "kwx/platform requires the kwx_ts runtime " +
                "(globalThis.kwxHost is not available)",
        );
    }
    return host;
}

const HOST = requireHost();

// argv is [program, entry, ...userArgs]; `args` excludes both leading entries.
const ARGUMENT_OFFSET = 2;

/** Host platform name. */
export const platform: PlatformName = HOST.__kwx_platform_os as PlatformName;

/** Command-line arguments after the entry module. */
export const args: string[] = HOST.__kwx_get_args().slice(ARGUMENT_OFFSET);

/**
 * Returns a lazily resolved symbol table for `name`.
 *
 * kwx_ts never opens a shared library: every symbol resolves from the
 * statically linked kwxFFI surface, and a thunk is created on first use.
 * `name` is retained for diagnostics only.
 */
export function dlopen<TSymbols extends SymbolDefinitions>(
    name: string,
    symbols: TSymbols,
): Library<TSymbols> {
    const hasSymbol = (key: string): boolean =>
        Object.prototype.hasOwnProperty.call(symbols, key);

    const resolveSymbol = (key: string): NativeSymbolFunction | undefined => {
        if (!hasSymbol(key)) {
            return undefined;
        }
        const symbol = HOST.__kwx_resolve_symbol(key);
        if (typeof symbol !== "function") {
            throw new Error(
                `kwx/platform: dlopen("${name}") cannot resolve symbol "${key}"`,
            );
        }
        return symbol;
    };

    // Enumeration must not create one thunk per declared symbol, so a property
    // descriptor is returned without resolving; only `get` resolves.
    const symbolTable = new Proxy(
        Object.create(null) as Record<string, NativeSymbolFunction>,
        {
            // The proxy target is intentionally empty: all keys live in the
            // declaration table, so the handlers never read it.
            get(target, key) {
                void target;
                if (typeof key !== "string" || !hasSymbol(key)) {
                    return undefined;
                }
                return resolveSymbol(key);
            },
            has(target, key) {
                void target;
                return typeof key === "string" && hasSymbol(key);
            },
            ownKeys(target) {
                void target;
                return Object.keys(symbols);
            },
            getOwnPropertyDescriptor(target, key) {
                void target;
                if (typeof key !== "string" || !hasSymbol(key)) {
                    return undefined;
                }
                return { enumerable: true, configurable: true };
            },
        },
    );

    // Every declared key is present with the signature implied by its
    // declaration; the proxy supplies those values at call time.
    return {
        symbols: symbolTable as unknown as NativeSymbols<TSymbols>,
        close() {},
    };
}

/** Data address of a typed array or ArrayBuffer, as a Pointer. */
export function pointerOf(value: ArrayBufferView | ArrayBuffer): Pointer {
    return HOST.__kwx_ptr_of(value);
}

/** Reads a NUL-terminated UTF-8 C string. */
export function readCString(pointer: Pointer): string {
    return HOST.__kwx_get_cstring(pointer);
}

/** Non-owning ArrayBuffer view over `byteLength` bytes at `pointer`. */
export function arrayBufferView(
    pointer: Pointer,
    byteLength: number,
): ArrayBuffer {
    return HOST.__kwx_get_array_buffer(pointer, byteLength);
}

/**
 * Terminates the program with `code`.
 *
 * The host recognises the sentinel exception and returns `code` from the
 * process. Throwing instead of calling exit directly lets the module stack
 * unwind and the wx event loop shut down cleanly.
 */
export function exit(code = 0): never {
    HOST.__kwx_set_exit_code(code);
    const sentinel = new Error(`kwx_ts: exit(${code})`) as Error & {
        __kwx_exit?: boolean;
    };
    sentinel.__kwx_exit = true;
    throw sentinel;
}

/**
 * A native function pointer backed by a JavaScript handler.
 *
 * kwx_ts provides a fixed pool of 64 callback slots, so a closed instance
 * should be released as soon as its native closure is no longer needed.
 */
export class Callback {
    readonly definition: CallbackDefinition;
    readonly pointer: Pointer;

    private readonly slot: number;
    private closed = false;

    constructor(
        definition: CallbackDefinition,
        handler: (...parameters: Pointer[]) => void,
    ) {
        this.definition = definition;
        this.slot = HOST.__kwx_callback_create(handler);
        if (this.slot < 0) {
            throw new Error("kwx/platform: Callback: no free native callback slot");
        }
        this.pointer = HOST.__kwx_callback_pointer(this.slot);
    }

    close(): void {
        if (!this.closed) {
            HOST.__kwx_callback_close(this.slot);
            this.closed = true;
        }
    }
}
