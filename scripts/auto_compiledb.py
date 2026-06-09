# PlatformIO extra_script: keeps compile_commands.json fresh for clangd.
#
# Why: PlatformIO only writes compile_commands.json when you pass -t compiledb,
# so on every normal `pio run` / `pio test` the file goes stale relative to
# files added or renamed since the last manual refresh. Clangd then reports
# diagnostics against a phantom snapshot of the tree.
#
# What it does:
#   - For env:native_cli: registers the SCons CompilationDatabase builder as a
#     default target, so every native_cli build refreshes
#     $PROJECT_DIR/compile_commands.json inline (~0.5s overhead). This env is
#     the canonical indexing source because it includes cli-main.cpp and the
#     cli/* sources that other native envs exclude.
#   - For every other env (esp32, native_cli_test, native, native_perf, etc):
#     spawns `pio run -e native_cli -t compiledb` in the background at script
#     LOAD time (not as a post-build action). This way the refresh fires even
#     on no-op `pio test` invocations where SCons doesn't rebuild anything;
#     AddPostAction is tied to target rebuilds and would silently skip those.
#     Xtensa flags and headers aren't useful to clangd on a Linux dev machine,
#     so we never let ESP32 entries reach the root file.

Import("env")  # noqa: F821  (PlatformIO injects this)

import subprocess
import sys


_CANONICAL_ENV = "native_cli"


def _register_inline_compiledb(env):
    env.Tool("compilation_db")
    cdb = env.CompilationDatabase("$PROJECT_DIR/compile_commands.json")
    env.AlwaysBuild(cdb)
    env.Default(cdb)


def _spawn_canonical_refresh(env):
    project_dir = env.subst("$PROJECT_DIR")
    cmd = [sys.executable, "-m", "platformio", "run",
           "-e", _CANONICAL_ENV, "-t", "compiledb", "-s"]
    try:
        subprocess.Popen(
            cmd,
            cwd=project_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except OSError:
        pass


env_name = env.subst("$PIOENV")

if env_name == _CANONICAL_ENV:
    _register_inline_compiledb(env)
else:
    _spawn_canonical_refresh(env)
