/*
 * kwx_ts — build-time binding generator.
 *
 * Parses the FFI declaration corpus (`wx/kwx_ffi_gen.ts` and `src/kwxApp.ts`)
 * and emits `bindings_gen.h` / `bindings_gen.cpp` into the build directory.
 * Each parsed symbol becomes one QuickJS C function, but no thunk is created at
 * startup: kwx_register_generated_bindings() only mounts a resolver, and a
 * thunk is created the first time the corpus asks for it.
 *
 * This tool runs at build time only; it never ships with the executable and
 * needs no Node, npm, or Python.
 *
 * Usage: kwx_binding_gen <kwx_ffi_gen.ts> <kwxApp.ts> <output_dir>
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

    // A single parsed declaration: its name, its FFI parameter types in order, and
    // its FFI result type.
    struct Symbol
    {
        std::string name;
        std::vector<std::string> parameters;
        std::string result;
        // kwxApp_* entry points are declared with native C types by kwx_host_app.h.
        // Their FFI-typed prototypes must not be emitted as well, or the two
        // declarations conflict in the generated translation unit.
        bool from_host_app = false;
    };

    // Collapse every run of whitespace (including newlines) to a single space so
    // multi-line and single-line entries share one shape.
    std::string NormalizeWhitespace(const std::string& text)
    {
        std::string normalized;
        normalized.reserve(text.size());
        bool in_whitespace = false;
        for (const char character: text)
        {
            const bool is_whitespace =
                (character == ' ' || character == '\t' || character == '\r' || character == '\n');
            if (is_whitespace)
            {
                if (!in_whitespace)
                {
                    normalized.push_back(' ');
                    in_whitespace = true;
                }
            }
            else
            {
                normalized.push_back(character);
                in_whitespace = false;
            }
        }
        return normalized;
    }

    bool ReadFile(const std::string& path, std::string& out_text)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }
        out_text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    std::set<std::string> ParamTypes()
    {
        return { "pointer", "function", "i32", "u32", "u8", "usize", "f32", "f64" };
    }

    bool IsKnownParamType(const std::string& type)
    {
        static const std::set<std::string> types = ParamTypes();
        return types.contains(type);
    }

    bool IsKnownResultType(const std::string& type)
    {
        return type == "void" || IsKnownParamType(type);
    }

    std::vector<std::string> ExtractParameters(const std::string& raw_parameters)
    {
        std::vector<std::string> parameters;
        static const std::regex quoted(R"rx("([^"]*)")rx");
        std::sregex_iterator iterator(raw_parameters.begin(), raw_parameters.end(), quoted);
        const std::sregex_iterator end_iterator;
        for (; iterator != end_iterator; ++iterator)
        {
            parameters.push_back((*iterator)[1].str());
        }
        return parameters;
    }

    // Parse every `<identifier>: { parameters: [ ... ], result: "<type>" }` entry.
    bool ParseSymbols(const std::string& path, std::vector<Symbol>& out_symbols, bool from_host_app)
    {
        std::string text;
        if (!ReadFile(path, text))
        {
            std::cerr << "gen_bindings: cannot read " << path << "\n";
            return false;
        }

        const std::string normalized = NormalizeWhitespace(text);
        // clang-format off
  static const std::regex entry(
      R"rx(([A-Za-z_][A-Za-z0-9_]*)\s*:\s*\{\s*parameters\s*:\s*\[([^\]]*)\]\s*,?\s*result\s*:\s*"([^"]*)"\s*,?\s*\})rx");
        // clang-format on

        std::sregex_iterator iterator(normalized.begin(), normalized.end(), entry);
        const std::sregex_iterator end_iterator;
        for (; iterator != end_iterator; ++iterator)
        {
            Symbol symbol;
            symbol.name = (*iterator)[1].str();
            symbol.parameters = ExtractParameters((*iterator)[2].str());
            symbol.result = (*iterator)[3].str();
            symbol.from_host_app = from_host_app;

            for (const std::string& parameter: symbol.parameters)
            {
                if (!IsKnownParamType(parameter))
                {
                    std::cerr << "gen_bindings: unknown parameter type \"" << parameter << "\" for "
                              << symbol.name << " in " << path << "\n";
                    return false;
                }
            }
            if (!IsKnownResultType(symbol.result))
            {
                std::cerr << "gen_bindings: unknown result type \"" << symbol.result << "\" for "
                          << symbol.name << " in " << path << "\n";
                return false;
            }
            out_symbols.push_back(symbol);
        }

        if (out_symbols.empty())
        {
            std::cerr << "gen_bindings: no symbols parsed from " << path << "\n";
            return false;
        }
        return true;
    }

    const char* ParameterCType(const std::string& type)
    {
        if (type == "pointer" || type == "function")
        {
            return "void *";
        }
        if (type == "i32")
        {
            return "std::int32_t ";
        }
        if (type == "u32")
        {
            return "std::uint32_t ";
        }
        if (type == "u8")
        {
            return "std::uint8_t ";
        }
        if (type == "usize")
        {
            return "std::size_t ";
        }
        if (type == "f32")
        {
            return "float ";
        }
        return "double ";
    }

    // Result type for an emitted prototype. "void" is only valid as a result;
    // the trailing space keeps `void name(...)` well formed.
    const char* PrototypeCType(const std::string& type)
    {
        if (type == "void")
        {
            return "void ";
        }
        return ParameterCType(type);
    }

    const char* ArgumentConverter(const std::string& type)
    {
        if (type == "pointer")
        {
            return "arg_ptr";
        }
        if (type == "function")
        {
            return "arg_fnptr";
        }
        if (type == "i32")
        {
            return "arg_i32";
        }
        if (type == "u32")
        {
            return "arg_u32";
        }
        if (type == "u8")
        {
            return "arg_u8";
        }
        if (type == "usize")
        {
            return "arg_usize";
        }
        if (type == "f32")
        {
            return "arg_f32";
        }
        return "arg_f64";
    }

    bool IsPointerParameter(const std::string& type)
    {
        return type == "pointer" || type == "function";
    }

    bool NeedsScope(const Symbol& symbol)
    {
        for (const std::string& parameter: symbol.parameters)
        {
            if (IsPointerParameter(parameter))
            {
                return true;
            }
        }
        return false;
    }

    std::string CallExpression(const Symbol& symbol)
    {
        std::ostringstream call;
        call << symbol.name << "(";
        for (std::size_t index = 0; index < symbol.parameters.size(); ++index)
        {
            if (index != 0)
            {
                call << ", ";
            }
            if (IsPointerParameter(symbol.parameters[index]))
            {
                // KwxArgPointer bridges the corpus' generic "pointer" to the exact
                // pointer type the kwxFFI prototype declares (object or function).
                call << "KwxArgPointer(p" << index << ")";
            }
            else
            {
                call << "p" << index;
            }
        }
        call << ")";
        return call.str();
    }

    const char* ResultConverter(const std::string& type)
    {
        if (type == "pointer")
        {
            return "ret_ptr";
        }
        if (type == "i32")
        {
            return "ret_i32";
        }
        if (type == "u32")
        {
            return "ret_u32";
        }
        if (type == "u8")
        {
            return "ret_u8";
        }
        if (type == "usize")
        {
            return "ret_usize";
        }
        if (type == "f32")
        {
            return "ret_f32";
        }
        return "ret_f64";
    }

    void EmitThunk(std::ostream& out, const Symbol& symbol)
    {
        const bool uses_scope = NeedsScope(symbol);
        const bool has_parameters = !symbol.parameters.empty();
        const bool returns_value = symbol.result != "void";

        out << "static JSValue js_" << symbol.name << "(";
        if (uses_scope || has_parameters || returns_value)
        {
            out << "JSContext *ctx";
        }
        else
        {
            out << "[[maybe_unused]] JSContext *ctx";
        }
        out << ", [[maybe_unused]] JSValueConst this_val, ";
        if (has_parameters)
        {
            out << "int argc, JSValueConst *argv";
        }
        else
        {
            out << "[[maybe_unused]] int argc, [[maybe_unused]] JSValueConst *argv";
        }
        out << ")\n{\n";

        if (uses_scope)
        {
            out << "    KwxArgScope scope(ctx);\n";
        }

        for (std::size_t index = 0; index < symbol.parameters.size(); ++index)
        {
            out << "    " << ParameterCType(symbol.parameters[index]) << "p" << index
                << (IsPointerParameter(symbol.parameters[index]) ? " = nullptr;\n" : " = 0;\n");
        }

        if (has_parameters)
        {
            out << "    if (argc < " << symbol.parameters.size() << ")\n    {\n"
                << "        return JS_ThrowTypeError(ctx, \"" << symbol.name << " expects "
                << symbol.parameters.size() << " argument(s)\");\n    }\n";

            out << "    if (";
            for (std::size_t index = 0; index < symbol.parameters.size(); ++index)
            {
                if (index != 0)
                {
                    out << " || ";
                }
                out << "!" << ArgumentConverter(symbol.parameters[index]) << "(ctx, argv[" << index
                    << "], &p" << index;
                if (IsPointerParameter(symbol.parameters[index]))
                {
                    out << ", scope";
                }
                out << ")";
            }
            out << ")\n    {\n        return JS_EXCEPTION;\n    }\n";
        }

        if (returns_value)
        {
            out << "    return " << ResultConverter(symbol.result) << "(ctx, "
                << CallExpression(symbol) << ");\n";
        }
        else
        {
            out << "    " << CallExpression(symbol) << ";\n    return JS_UNDEFINED;\n";
        }

        out << "}\n\n";
    }

    bool WriteFile(const std::filesystem::path& path, const std::string& content)
    {
        std::ofstream output(path, std::ios::binary);
        if (!output.is_open())
        {
            std::cerr << "gen_bindings: cannot write " << path.string() << "\n";
            return false;
        }
        output << content;
        return output.good();
    }

    // The kwxFFI declaration headers are a DSL for kwxgen, not a C++ header set:
    // TClassDef/TClassDefExtend expand to nothing, so constructor declarations in
    // the focused headers (kwx_richtext.h, kwx_ribbon_extra.h) have no return
    // type and cannot be compiled. Emitting prototypes from the parsed corpus
    // keeps this translation unit independent of the header syntax, and the
    // corpus types are exactly the ABI the generated thunks marshal.
    void EmitExternPrototypes(std::ostream& out, const std::vector<Symbol>& symbols)
    {
        out << "// kwxFFI prototypes emitted from the parsed corpus (the headers are a "
               "generator DSL).\n"
            << "extern \"C\"\n{\n";

        for (const Symbol& symbol: symbols)
        {
            if (symbol.from_host_app)
            {
                continue;
            }

            if (symbol.name == "kwxConstants_IntTable")
            {
                out << "    const int *kwxConstants_IntTable();\n";
                continue;
            }

            out << "    " << PrototypeCType(symbol.result) << symbol.name << "(";
            if (symbol.parameters.empty())
            {
                out << "void";
            }
            else
            {
                for (std::size_t index = 0; index < symbol.parameters.size(); ++index)
                {
                    if (index != 0)
                    {
                        out << ", ";
                    }
                    out << PrototypeCType(symbol.parameters[index]);
                }
            }
            out << ");\n";
        }

        out << "}\n\n";
    }

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: kwx_binding_gen <kwx_ffi_gen.ts> <kwxApp.ts> <output_dir>\n";
        return 2;
    }

    const std::string ffi_path = argv[1];
    const std::string app_path = argv[2];
    const std::filesystem::path output_dir = argv[3];

    std::vector<Symbol> symbols;
    if (!ParseSymbols(ffi_path, symbols, false) || !ParseSymbols(app_path, symbols, true))
    {
        return 1;
    }

    std::set<std::string> seen;
    for (const Symbol& symbol: symbols)
    {
        if (!seen.insert(symbol.name).second)
        {
            std::cerr << "gen_bindings: duplicate identifier " << symbol.name << "\n";
            return 1;
        }
    }

    // The lookup table is binary-searched at runtime, so emit it sorted by name
    // while keeping thunk definitions in corpus order.
    std::vector<const Symbol*> sorted_symbols;
    sorted_symbols.reserve(symbols.size());
    for (const Symbol& symbol: symbols)
    {
        sorted_symbols.push_back(&symbol);
    }
    std::sort(sorted_symbols.begin(), sorted_symbols.end(),
              [](const Symbol* left, const Symbol* right)
              {
                  return left->name < right->name;
              });

    std::error_code error_code;
    std::filesystem::create_directories(output_dir, error_code);
    if (error_code)
    {
        std::cerr << "gen_bindings: cannot create " << output_dir.string() << ": "
                  << error_code.message() << "\n";
        return 1;
    }

    std::ostringstream header;
    header << "// Generated by kwx_ts/tools/gen_bindings.cpp — do not edit.\n"
           << "#ifndef KWX_TS_BINDINGS_GEN_H\n"
           << "#define KWX_TS_BINDINGS_GEN_H\n\n"
           << "#include \"quickjs.h\"\n\n"
           << "void kwx_register_generated_bindings(JSContext *ctx, JSValue "
              "native_obj);\n\n"
           << "#endif // KWX_TS_BINDINGS_GEN_H\n";

    std::ostringstream source;
    source << "// Generated by kwx_ts/tools/gen_bindings.cpp — do not edit.\n"
           << "#include \"bindings_gen.h\"\n\n"
           << "#include \"kwx_js_marshal.h\"\n"
           << "#include \"kwx_host_app.h\"\n\n"
           << "#include <cstddef>\n"
           << "#include <cstdint>\n"
           << "#include <cstring>\n\n";

    EmitExternPrototypes(source, symbols);

    source << "namespace\n{\n\n";
    for (const Symbol& symbol: symbols)
    {
        EmitThunk(source, symbol);
    }

    source << "struct KwxGeneratedSymbol\n"
           << "{\n"
           << "    const char *name;\n"
           << "    JSCFunction *function;\n"
           << "    int length;\n"
           << "};\n\n"
           << "// Sorted by name so the resolver can binary-search it.\n"
           << "static const KwxGeneratedSymbol kwx_generated_symbols[] = {\n";
    for (const Symbol* symbol: sorted_symbols)
    {
        source << "    {\"" << symbol->name << "\", js_" << symbol->name << ", "
               << symbol->parameters.size() << "},\n";
    }
    source << "};\n\n"
           << "const KwxGeneratedSymbol *KwxFindGeneratedSymbol(const char "
              "*name)\n"
           << "{\n"
           << "    std::size_t low = 0;\n"
           << "    std::size_t high =\n"
           << "        sizeof(kwx_generated_symbols) / "
              "sizeof(kwx_generated_symbols[0]);\n"
           << "    while (low < high)\n"
           << "    {\n"
           << "        const std::size_t middle = low + ((high - low) / 2);\n"
           << "        const int order =\n"
           << "            std::strcmp(kwx_generated_symbols[middle].name, "
              "name);\n"
           << "        if (order == 0)\n"
           << "        {\n"
           << "            return &kwx_generated_symbols[middle];\n"
           << "        }\n"
           << "        if (order < 0)\n"
           << "        {\n"
           << "            low = middle + 1;\n"
           << "        }\n"
           << "        else\n"
           << "        {\n"
           << "            high = middle;\n"
           << "        }\n"
           << "    }\n"
           << "    return nullptr;\n"
           << "}\n\n"
           << "// __kwx_resolve_symbol(name) — create the thunk for one corpus "
              "symbol\n"
           << "// on demand. Returns undefined when the name is not in the corpus.\n"
           << "JSValue KwxJsResolveSymbol(JSContext *ctx,\n"
           << "                           [[maybe_unused]] JSValueConst this_val,\n"
           << "                           int argc, JSValueConst *argv)\n"
           << "{\n"
           << "    if (argc < 1)\n"
           << "    {\n"
           << "        return JS_UNDEFINED;\n"
           << "    }\n"
           << "    const char *name = JS_ToCString(ctx, argv[0]);\n"
           << "    if (name == nullptr)\n"
           << "    {\n"
           << "        return JS_EXCEPTION;\n"
           << "    }\n"
           << "    const KwxGeneratedSymbol *entry = "
              "KwxFindGeneratedSymbol(name);\n"
           << "    JS_FreeCString(ctx, name);\n"
           << "    if (entry == nullptr)\n"
           << "    {\n"
           << "        return JS_UNDEFINED;\n"
           << "    }\n"
           << "    return JS_NewCFunction(ctx, entry->function, entry->name, "
              "entry->length);\n"
           << "}\n"
           << "} // namespace\n\n"
           << "void kwx_register_generated_bindings(JSContext *ctx, JSValue "
              "native_obj)\n"
           << "{\n"
           << "    JS_SetPropertyStr(\n"
           << "        ctx, native_obj, \"__kwx_resolve_symbol\",\n"
           << "        JS_NewCFunction(ctx, KwxJsResolveSymbol, "
              "\"__kwx_resolve_symbol\", 1));\n"
           << "}\n";

    if (!WriteFile(output_dir / "bindings_gen.h", header.str()) ||
        !WriteFile(output_dir / "bindings_gen.cpp", source.str()))
    {
        return 1;
    }

    std::cout << "gen_bindings: " << symbols.size() << " symbols\n";
    return 0;
}
