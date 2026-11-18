# PlatformIO extra_script: native_cli builds write compile_commands.json; every
# other env spawns `pio run -e native_cli -t compiledb` at load time
# (post-actions skip no-op builds), so Xtensa entries never reach clangd.

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
