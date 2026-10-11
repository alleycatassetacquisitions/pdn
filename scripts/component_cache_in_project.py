# PlatformIO pre-build hook for the device envs: make dependencies.lock come out
# the same on every machine.

Import("env")  # noqa: F821

import os

# u8g2 has a git submodule, and the component manager hashes the submodule's
# .git pointer file into the lock. Git writes that pointer from the temporary
# checkout to the component cache; outside the project the two share only "/",
# so the pointer holds the cache's absolute path and the lock verifies only on
# the machine that wrote it. With both under .pio the pointer is the same
# relative path everywhere. The component manager reads its cache location only
# from the environment, and its checkout directory from TMPDIR.
buildRoot = os.path.join(env.subst("$PROJECT_DIR"), ".pio")  # noqa: F821
tempDir = os.path.join(buildRoot, "tmp")
os.makedirs(tempDir, exist_ok=True)
os.environ["IDF_COMPONENT_CACHE_PATH"] = os.path.join(buildRoot, "component-cache")
os.environ["TMPDIR"] = tempDir
