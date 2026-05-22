#!/usr/bin/env python3
import json
import pathlib
import shlex
import subprocess
import sys


CLANG_TIDY_UNSUPPORTED_ARGS = {
    "-fno-canonical-system-headers",
}


def run(command):
    return subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE)


def source_from_args(args):
    try:
        index = args.index("-c")
    except ValueError:
        return None
    if index + 1 >= len(args):
        return None
    return args[index + 1]


def is_project_source(path):
    return (
        (path.startswith("src/") or path.startswith("tests/"))
        and path.endswith((".c", ".cc", ".cpp", ".cxx", ".m", ".mm"))
    )


def rewrite_path(path, workspace, execroot_link):
    if path.startswith("external/") or path.startswith("bazel-out/"):
        return str(execroot_link / path)
    if is_project_source(path):
        return str(workspace / path)
    return path


def rewrite_arg(arg, workspace, execroot_link):
    for prefix in ("-I", "-F"):
        if arg.startswith(prefix) and len(arg) > len(prefix):
            return prefix + rewrite_path(arg[len(prefix) :], workspace, execroot_link)
    for prefix in ("-isystem", "-iquote", "-idirafter", "-iframework", "-MF"):
        if arg.startswith(prefix) and len(arg) > len(prefix):
            return prefix + rewrite_path(arg[len(prefix) :], workspace, execroot_link)
    return rewrite_path(arg, workspace, execroot_link)


def rewrite_args(args, workspace):
    execroot_link = workspace / f"bazel-{workspace.name}"
    rewritten = []
    for arg in args:
        if arg in CLANG_TIDY_UNSUPPORTED_ARGS:
            continue
        rewritten.append(rewrite_arg(arg, workspace, execroot_link))
    return rewritten


def main():
    targets = sys.argv[1:] or ["//src:yac", "//tests:all_tests"]
    workspace = pathlib.Path.cwd()
    run(["bazel", "build", "//src:yac", "//tests/..."])
    expression = 'mnemonic("CppCompile", deps({}))'.format(" + ".join(targets))
    output = run(["bazel", "aquery", expression, "--output=jsonproto"]).stdout
    actions = json.loads(output).get("actions", [])

    entries = []
    seen = set()
    for action in actions:
        args = action.get("arguments", [])
        source = source_from_args(args)
        if source is None or not is_project_source(source):
            continue
        if source in seen:
            continue
        seen.add(source)
        args = rewrite_args(args, workspace)
        entries.append(
            {
                "directory": str(workspace),
                "command": shlex.join(args),
                "file": str(workspace / source),
            }
        )

    entries.sort(key=lambda entry: entry["file"])
    with (workspace / "compile_commands.json").open("w", encoding="utf-8") as handle:
        json.dump(entries, handle, indent=2)
        handle.write("\n")
    print(f"Wrote compile_commands.json with {len(entries)} entries")


if __name__ == "__main__":
    main()
