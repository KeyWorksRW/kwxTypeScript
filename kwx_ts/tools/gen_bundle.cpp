/*
 * kwx_ts — build-time runtime bundler.
 *
 * Resolves the CommonJS module graph of the TypeScript corpus and emits one
 * classic script that the runtime embeds as the "kwx" virtual module (and,
 * for the single-module case, as "kwx/platform"). Each module is lowered by
 * the same SWC transformer the runtime uses, so the bundle has exactly the
 * semantics the runtime applies to user modules.
 *
 * Generating the bundle at build time is what keeps the executable current:
 * the corpus used to be a committed esbuild snapshot that could only be
 * regenerated with Node and npm, so the generated modules could change without
 * the embedded bindings following.
 *
 * This tool runs at build time only; it never ships with the executable and
 * needs no Node, npm, or Python. It links kwx_ts/third_party/swc_transformer,
 * which CMake already builds for the runtime.
 *
 * Usage:
 *   kwx_bundle_gen <source_root> <output_file> <entry_key>
 *       [--dir <directory>]... [--file <path>]...
 *
 * <entry_key> and every module key are paths relative to <source_root> with
 * forward slashes, e.g. "wx/mod.ts". A relative require() resolves inside the
 * bundle; every other specifier (notably "kwx/platform") is handed to the
 * host's require, which maps it to another embedded asset.
 */

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    // C ABI of the Rust transformer (kwx_ts/third_party/swc_transformer).
    extern "C" char* kwx_swc_transform(const char* source, const char* file_name,
                                       std::size_t* out_len);
    extern "C" const char* kwx_swc_last_error(void);
    extern "C" void kwx_swc_free(char* buffer);

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            throw std::runtime_error("cannot open '" + path.string() + "'");
        }
        return std::string { std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>() };
    }

    // Module key: path relative to the source root, always with '/' separators
    // so the generated resolution code is identical on every platform.
    std::string ModuleKey(const std::filesystem::path& root, const std::filesystem::path& file)
    {
        std::string key = std::filesystem::relative(file, root).generic_string();
        if (key.empty())
        {
            throw std::runtime_error("'" + file.string() + "' is outside the source root '" +
                                     root.string() + "'");
        }
        return key;
    }

    std::string Transpile(const std::string& source, const std::string& file_name)
    {
        std::size_t length = 0;
        char* code = kwx_swc_transform(source.c_str(), file_name.c_str(), &length);
        if (code == nullptr)
        {
            const char* detail = kwx_swc_last_error();
            throw std::runtime_error((detail != nullptr && detail[0] != '\0') ?
                                         std::string(detail) :
                                         "the TypeScript transformer failed for '" + file_name +
                                             "'");
        }

        std::string translated(code, length);
        kwx_swc_free(code);

        // The transformer appends an inline source map for the disk loader's
        // stack traces. An embedded bundle has no per-module parse step and the
        // host registers no map for it, so the trailer is dead weight.
        const std::string marker = "\n//# sourceMappingURL=";
        const std::size_t marker_position = translated.rfind(marker);
        if (marker_position != std::string::npos)
        {
            translated.erase(marker_position);
        }
        return translated;
    }

    // Collects the specifiers of the require() calls the CommonJS transform
    // emitted. Only used to verify that every intra-bundle dependency is
    // present, so an incomplete build fails here instead of at the first run.
    void CollectRequires(const std::string& code, std::vector<std::string>& specifiers)
    {
        const std::string token = "require(";
        std::size_t search_from = 0;
        while (true)
        {
            const std::size_t position = code.find(token, search_from);
            if (position == std::string::npos)
            {
                return;
            }
            const std::size_t cursor = position + token.size();
            search_from = cursor;
            if (cursor >= code.size())
            {
                return;
            }

            const char quote = code[cursor];
            if (quote != '"' && quote != '\'')
            {
                continue;
            }
            const std::size_t end = code.find(quote, cursor + 1);
            if (end == std::string::npos)
            {
                return;
            }
            specifiers.push_back(code.substr(cursor + 1, end - cursor - 1));
            search_from = end + 1;
        }
    }

    bool IsRelative(const std::string& specifier)
    {
        return !specifier.empty() && specifier.front() == '.';
    }

    // Resolves `specifier` relative to the directory of `from_module`, matching
    // what the generated __kwx_resolve() does at run time.
    std::string ResolveKey(const std::string& from_module, const std::string& specifier)
    {
        const std::size_t slash = from_module.rfind('/');
        const std::string directory =
            (slash == std::string::npos) ? "" : from_module.substr(0, slash);
        const std::string combined = directory.empty() ? specifier : directory + "/" + specifier;

        std::vector<std::string> segments;
        std::size_t start = 0;
        while (start <= combined.size())
        {
            const std::size_t end = combined.find('/', start);
            const std::string segment = combined.substr(
                start, (end == std::string::npos) ? std::string::npos : end - start);
            if (segment == "..")
            {
                if (!segments.empty())
                {
                    segments.pop_back();
                }
            }
            else if (!segment.empty() && segment != ".")
            {
                segments.push_back(segment);
            }

            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }

        std::string key;
        for (const std::string& segment: segments)
        {
            if (!key.empty())
            {
                key.push_back('/');
            }
            key += segment;
        }
        return key;
    }

    void WriteQuoted(std::ostream& output, const std::string& text)
    {
        output << '"';
        for (const char character: text)
        {
            if (character == '\\' || character == '"')
            {
                output << '\\';
            }
            output << character;
        }
        output << '"';
    }

    void WritePrelude(std::ostream& output, std::size_t module_count)
    {
        output << "// Generated by kwx_bundle_gen. DO NOT EDIT.\n"
                  "// Bundled modules: "
               << module_count
               << "\n"
                  "\"use strict\";\n"
                  "\n"
                  "// Bare specifiers (notably \"kwx/platform\") stay with the host loader: the\n"
                  "// corpus bundle must not resolve them to a bundle that is still executing.\n"
                  "var __kwx_host_require = require;\n"
                  "\n"
                  "var __kwx_modules = Object.create(null);\n"
                  "var __kwx_cache = Object.create(null);\n"
                  "\n"
                  "function __kwx_dirname(module_key) {\n"
                  "    var slash = module_key.lastIndexOf(\"/\");\n"
                  "    return (slash < 0) ? \"\" : module_key.slice(0, slash);\n"
                  "}\n"
                  "\n"
                  "function __kwx_resolve(from_module, specifier) {\n"
                  "    var parts = (__kwx_dirname(from_module) + \"/\" + specifier).split(\"/\");\n"
                  "    var resolved = [];\n"
                  "    for (var index = 0; index < parts.length; ++index) {\n"
                  "        var part = parts[index];\n"
                  "        if (part === \"\" || part === \".\") {\n"
                  "            continue;\n"
                  "        }\n"
                  "        if (part === \"..\") {\n"
                  "            if (resolved.length > 0) {\n"
                  "                resolved.pop();\n"
                  "            }\n"
                  "            continue;\n"
                  "        }\n"
                  "        resolved.push(part);\n"
                  "    }\n"
                  "    return resolved.join(\"/\");\n"
                  "}\n"
                  "\n"
                  "function __kwx_make_require(from_module) {\n"
                  "    return function (specifier) {\n"
                  "        if (specifier.charAt(0) === \".\") {\n"
                  "            return __kwx_load(__kwx_resolve(from_module, specifier));\n"
                  "        }\n"
                  "        return __kwx_host_require(specifier);\n"
                  "    };\n"
                  "}\n"
                  "\n"
                  "function __kwx_load(module_key) {\n"
                  "    var cached = __kwx_cache[module_key];\n"
                  "    if (cached !== undefined) {\n"
                  "        return cached.exports;\n"
                  "    }\n"
                  "    var factory = __kwx_modules[module_key];\n"
                  "    if (factory === undefined) {\n"
                  "        throw new Error(\"kwx_ts: cannot resolve bundled module '\" + "
                  "module_key + \"'\");\n"
                  "    }\n"
                  "    var module = { exports: {} };\n"
                  "    __kwx_cache[module_key] = module;\n"
                  "    factory(module.exports, module, __kwx_make_require(module_key),\n"
                  "        module_key, __kwx_dirname(module_key));\n"
                  "    return module.exports;\n"
                  "}\n"
                  "\n";
    }

    void WriteModule(std::ostream& output, const std::string& key, const std::string& code)
    {
        output << "__kwx_modules[";
        WriteQuoted(output, key);
        output << "] = function (exports, module, require, __filename, __dirname) {\n";
        output << code;
        if (!code.empty() && code.back() != '\n')
        {
            output << '\n';
        }
        output << "};\n\n";
    }

}  // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4)
        {
            std::cerr << "usage: kwx_bundle_gen <source_root> <output_file> <entry_key>\n"
                         "           [--dir <directory>]... [--file <path>]...\n";
            return 2;
        }

        const std::filesystem::path root = std::filesystem::absolute(argv[1]);
        const std::filesystem::path output_path = std::filesystem::absolute(argv[2]);
        const std::string entry_key = argv[3];

        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> files;
        for (int index = 4; index < argc; ++index)
        {
            const std::string option = argv[index];
            if (index + 1 >= argc)
            {
                throw std::runtime_error("missing value after '" + option + "'");
            }
            if (option == "--dir")
            {
                directories.emplace_back(argv[++index]);
            }
            else if (option == "--file")
            {
                files.emplace_back(argv[++index]);
            }
            else
            {
                throw std::runtime_error("unknown option '" + option + "'");
            }
        }

        // Sorted by key, so the generated file is byte-identical for an
        // identical corpus regardless of directory iteration order.
        std::map<std::string, std::filesystem::path> modules;
        for (const std::filesystem::path& directory: directories)
        {
            if (!std::filesystem::is_directory(directory))
            {
                throw std::runtime_error("'" + directory.string() + "' is not a directory");
            }
            for (const std::filesystem::directory_entry& entry:
                 std::filesystem::directory_iterator(directory))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".ts")
                {
                    modules[ModuleKey(root, entry.path())] = entry.path();
                }
            }
        }
        for (const std::filesystem::path& file: files)
        {
            if (!std::filesystem::is_regular_file(file))
            {
                throw std::runtime_error("'" + file.string() + "' does not exist");
            }
            modules[ModuleKey(root, file)] = file;
        }

        if (modules.find(entry_key) == modules.end())
        {
            throw std::runtime_error("entry module '" + entry_key + "' was not collected");
        }
        if (modules.empty())
        {
            throw std::runtime_error("no modules were collected");
        }

        std::map<std::string, std::string> translated;
        for (const auto& [key, path]: modules)
        {
            translated[key] = Transpile(ReadFile(path), key);
        }

        for (const auto& [key, code]: translated)
        {
            std::vector<std::string> specifiers;
            CollectRequires(code, specifiers);
            for (const std::string& specifier: specifiers)
            {
                if (!IsRelative(specifier))
                {
                    continue;
                }
                const std::string resolved = ResolveKey(key, specifier);
                if (translated.find(resolved) == translated.end())
                {
                    throw std::runtime_error("'" + key + "' requires '" + specifier +
                                             "', which resolves to '" + resolved +
                                             "' and is not part of the bundle");
                }
            }
        }

        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("cannot write '" + output_path.string() + "'");
        }

        WritePrelude(output, translated.size());
        for (const auto& [key, code]: translated)
        {
            WriteModule(output, key, code);
        }
        output << "module.exports = __kwx_load(";
        WriteQuoted(output, entry_key);
        output << ");\n";

        output.close();
        if (!output)
        {
            throw std::runtime_error("failed while writing '" + output_path.string() + "'");
        }

        std::cout << "kwx_bundle_gen: " << translated.size() << " modules -> "
                  << output_path.string() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "kwx_bundle_gen: " << error.what() << '\n';
        return 1;
    }
}
