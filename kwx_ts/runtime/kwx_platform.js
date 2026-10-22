var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __defNormalProp = (obj, key, value) => key in obj ? __defProp(obj, key, { enumerable: true, configurable: true, writable: true, value }) : obj[key] = value;
var __export = (target, all) => {
  for (var name in all)
    __defProp(target, name, { get: all[name], enumerable: true });
};
var __copyProps = (to, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to, key) && key !== except)
        __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to;
};
var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);
var __publicField = (obj, key, value) => __defNormalProp(obj, typeof key !== "symbol" ? key + "" : key, value);

// ../../src/kwx_platform.ts
var kwx_platform_exports = {};
__export(kwx_platform_exports, {
  Callback: () => Callback,
  args: () => args,
  arrayBufferView: () => arrayBufferView,
  dlopen: () => dlopen,
  exit: () => exit,
  platform: () => platform,
  pointerOf: () => pointerOf,
  readCString: () => readCString
});
module.exports = __toCommonJS(kwx_platform_exports);
function requireHost() {
  const host = globalThis.kwxHost;
  if (!host) {
    throw new Error(
      "kwx/platform requires the kwx_ts runtime (globalThis.kwxHost is not available)"
    );
  }
  return host;
}
var HOST = requireHost();
var ARGUMENT_OFFSET = 2;
var platform = HOST.__kwx_platform_os;
var args = HOST.__kwx_get_args().slice(ARGUMENT_OFFSET);
function dlopen(name, symbols) {
  const hasSymbol = (key) => Object.prototype.hasOwnProperty.call(symbols, key);
  const resolveSymbol = (key) => {
    if (!hasSymbol(key)) {
      return void 0;
    }
    const symbol = HOST.__kwx_resolve_symbol(key);
    if (typeof symbol !== "function") {
      throw new Error(
        `kwx/platform: dlopen("${name}") cannot resolve symbol "${key}"`
      );
    }
    return symbol;
  };
  const symbolTable = new Proxy(
    /* @__PURE__ */ Object.create(null),
    {
      // The proxy target is intentionally empty: all keys live in the
      // declaration table, so the handlers never read it.
      get(target, key) {
        void target;
        if (typeof key !== "string" || !hasSymbol(key)) {
          return void 0;
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
          return void 0;
        }
        return { enumerable: true, configurable: true };
      }
    }
  );
  return {
    symbols: symbolTable,
    close() {
    }
  };
}
function pointerOf(value) {
  return HOST.__kwx_ptr_of(value);
}
function readCString(pointer) {
  return HOST.__kwx_get_cstring(pointer);
}
function arrayBufferView(pointer, byteLength) {
  return HOST.__kwx_get_array_buffer(pointer, byteLength);
}
function exit(code = 0) {
  HOST.__kwx_set_exit_code(code);
  const sentinel = new Error(`kwx_ts: exit(${code})`);
  sentinel.__kwx_exit = true;
  throw sentinel;
}
var Callback = class {
  constructor(definition, handler) {
    __publicField(this, "definition");
    __publicField(this, "pointer");
    __publicField(this, "slot");
    __publicField(this, "closed", false);
    this.definition = definition;
    this.slot = HOST.__kwx_callback_create(handler);
    if (this.slot < 0) {
      throw new Error("kwx/platform: Callback: no free native callback slot");
    }
    this.pointer = HOST.__kwx_callback_pointer(this.slot);
  }
  close() {
    if (!this.closed) {
      HOST.__kwx_callback_close(this.slot);
      this.closed = true;
    }
  }
};
