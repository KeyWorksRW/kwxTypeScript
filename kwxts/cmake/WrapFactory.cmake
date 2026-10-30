# kwx_ts — build-time CommonJS factory wrapper for QuickJS bytecode.
#
# A CommonJS module body references `exports`, `module`, `require`, `__filename`
# and `__dirname` as free variables, so it cannot be compiled as a standalone
# script. This script wraps the body in a function expression whose parameters
# are exactly those names, and publishes the result on globalThis. The host
# evaluates the compiled bytecode and reads the function back (see
# KwxJsEmbeddedFactory in runtime_bindings.cpp).
#
# Invoked from a custom command, not included:
#   cmake -DINPUT=<module.js> -DOUTPUT=<factory.js> -DFACTORY_NAME=<name>
#         -P WrapFactory.cmake

foreach(required_var INPUT OUTPUT FACTORY_NAME)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR
            "WrapFactory.cmake requires -DINPUT, -DOUTPUT and -DFACTORY_NAME")
    endif()
endforeach()

file(READ "${INPUT}" module_source)

# The function header occupies one line, so line numbers inside the module body
# are shifted by one in stack traces. Acceptable: it keeps the generated file a
# single self-contained script that qjsc can compile with -C (classic script).
file(WRITE "${OUTPUT}"
    "globalThis.${FACTORY_NAME} = function (exports, module, require, __filename, __dirname) {\n"
    "${module_source}\n"
    "};\n"
)
