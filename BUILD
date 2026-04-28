package(default_visibility = ["//visibility:public"])

# Bundle all artifacts into bazel-bin/ztracing/
genrule(
    name = "ztracing",
    srcs = [
        "//src:ztracing_wasm",
        "//src:shell.html",
        "//src:coi-serviceworker.js",
    ],
    outs = [
        "ztracing/index.html",
        "ztracing/ztracing.js",
        "ztracing/ztracing.wasm",
        "ztracing/coi-serviceworker.js",
    ],
    cmd = """
        # Generate index.html from shell template, pointing to ztracing.js
        sed 's/{{{ SCRIPT }}}/<script src=\"ztracing.js\"><\\/script>/' $(location //src:shell.html) > $(location ztracing/index.html)

        # Copy coi-serviceworker.js
        cp -f $(location //src:coi-serviceworker.js) $(location ztracing/coi-serviceworker.js)

        # Copy artifacts from the wasm_cc_binary target
        for f in $(locations //src:ztracing_wasm); do
            if [[ $$f == */ztracing.js ]]; then
                cp -f $$f $(location ztracing/ztracing.js)
            elif [[ $$f == */ztracing.wasm ]] && [[ $$f != *.debug.wasm ]]; then
                cp -f $$f $(location ztracing/ztracing.wasm)
            fi
        done
    """,
)
