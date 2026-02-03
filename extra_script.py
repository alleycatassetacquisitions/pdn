"""
PlatformIO Extra Script for Compilation Database Generation

This script configures the generation of compile_commands.json for use with
static analysis tools like SonarQube, clangd, and other tools that consume
compilation databases.

Usage:
    Generate compile_commands.json:
    > pio run -t compiledb

    Generate for specific environment:
    > pio run -e esp32-s3_release -t compiledb

The compile_commands.json will be saved to the project root directory.
"""

import os

Import("env")

# Include toolchain paths in the compilation database
# This provides full context for static analysis tools like SonarQube
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)

# Save compile_commands.json to project root (default behavior, but explicit)
# SonarQube typically expects it in the project root
env.Replace(COMPILATIONDB_PATH=os.path.join(env.subst("$PROJECT_DIR"), "compile_commands.json"))

print(f"Compilation database will be saved to: {env.subst('$COMPILATIONDB_PATH')}")
