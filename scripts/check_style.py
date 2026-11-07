#!/usr/bin/env python3
"""Style checks on a diff, run by hooks/pre-commit.

Uses clang-format, clang-tidy and a compile_commands.json at the repo root
(`pio run -e native_cli -t compiledb`) when they are there. On Windows,
native_cli usually builds with MSYS MinGW g++; LLVM clang-tidy then needs that toolchain's system
include paths (injected automatically — see clang_tidy_driver_extras). Line
rules and clang-format apply to the lines added in the diff; clang-tidy runs on
the translation units behind the changed files, with its diagnostics restricted
to those same lines.
The diff is the staged index by default. `--base <ref>` reads `<ref>...HEAD`
instead, which is what CI needs: a checkout stages nothing, so the default would
find no files and pass without checking anything.
Findings are advisory by default: the script reports and exits 0, so a missing
tool or an unusable compile database costs you the checks it feeds rather than
the commit. `--strict`, or CHECK_STYLE_STRICT in the environment,
exits 1 on a finding, skipped check, or tooling error — for CI, where the
toolchain is installed deliberately and a finding is worth failing on.
A legacy violation elsewhere in a touched file is not reported at all
(clang-format may still reflow the statement around an added line), so legacy
gets fixed on touch rather than all at once. Exceptions: new and
renamed files must be kebab-case, new headers need #pragma once, and the
own-header rule fires whenever the include block is touched.
"""
import argparse
import difflib
import json
import os
import re
import shutil
import subprocess
import sys

COMPILEDB_HINT = "generate it with: pio run -e native_cli -t compiledb"
GLOBS = ("*.cpp", "*.hpp")

violations = []
notices = []
format_diffs = []
# Tooling/DB skips that fail --strict (unlike notices for files without a TU).
skipped_checks = []

diff_revs = ["--cached"]
content_rev = ""  # prefix for `git show <rev>:path`; "" means the index


def git(*args):
    # Decoded leniently: one stray Latin-1 byte in a comment must surface as
    # a diagnosis, not a traceback.
    # check=True so a bad --base ref raises instead of returning empty diff output.
    return subprocess.run(["git", *args], capture_output=True, check=True,
                          encoding="utf-8", errors="replace").stdout


def flag(path, ln, msg):
    violations.append((path, ln, msg))


def changed_files():
    """{new path: (status letter, old path)}; old differs only for renames."""
    fields = git("diff", *diff_revs, "--name-status", "-z", "-M",
                 "--diff-filter=ACMR", "--", *GLOBS).split("\0")
    files, i = {}, 0
    while i < len(fields) and fields[i]:
        status = fields[i][0]
        if status in "RC":
            files[fields[i + 2]] = (status, fields[i + 1])
            i += 3
        else:
            files[fields[i + 1]] = (status, fields[i + 1])
            i += 2
    return files


def added_lines(path, old_path):
    """(lineno, text) pairs for lines added in the diff of one file.

    Both rename sides are passed so -M still pairs them. Only hunk lines are
    read, never the file headers, so diff.noprefix, diff.mnemonicPrefix,
    color or quotePath settings in the user's git config cannot hide a file.
    """
    out = git("diff", *diff_revs, "-U0", "-M", "--no-color", "--no-ext-diff",
              "--", *sorted({path, old_path}))
    added, ln, in_hunk = [], 0, False
    for raw in out.split("\n"):
        if raw.startswith("@@ "):
            ln = int(re.match(r"@@ -\S+ \+(\d+)", raw).group(1))
            in_hunk = True
        elif in_hunk and raw.startswith("+"):
            added.append((ln, raw[1:]))
            ln += 1
    return added


def file_content(path):
    return git("show", f"{content_rev}:{path}")


def ranges_of(added):
    """Consecutive line numbers folded into inclusive [start, end] pairs."""
    ranges = []
    for ln, _ in added:
        if ranges and ln == ranges[-1][1] + 1:
            ranges[-1][1] = ln
        else:
            ranges.append([ln, ln])
    return ranges


LITERAL = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\[^\']+|[^\'\\])\'')
RAW_STRING = re.compile(r'R"([^(]*)\(.*?\)\1"')


def strip_code(text, in_block):
    """Returns (code, in_block) so a block comment carries across lines."""
    out, i = [], 0
    while i < len(text):
        if in_block:
            end = text.find("*/", i)
            if end < 0:
                return "".join(out), True
            i, in_block = end + 2, False
            continue
        if text.startswith("//", i):
            break
        if text.startswith("/*", i):
            i, in_block = i + 2, True
            continue
        if text[i] in "\"'":
            m = (RAW_STRING.match(text, i - 1) if i and text[i - 1] == "R" else None) \
                or LITERAL.match(text, i)
            if m:
                out.append('""' if text[i] == '"' else "' '")
                i = m.end()
                continue
        out.append(text[i])
        i += 1
    return "".join(out), in_block


def strip_file(content):
    stripped, in_block = [], False
    for line in content.split("\n"):
        code, in_block = strip_code(line, in_block)
        stripped.append(code)
    return stripped


def check_new_file(path, content, status):
    base = os.path.basename(path)
    if not re.fullmatch(r"[a-z0-9-]+\.(hpp|cpp)", base):
        flag(path, 1, "filename must be kebab-case")
    if status == "A" and path.endswith(".hpp"):
        if "#pragma once" not in "\n".join(content.split("\n")[:5]):
            flag(path, 1, "header missing #pragma once")


def check_own_header_first(path, content, added):
    """The header may sit beside the .cpp or anywhere else in the tree."""
    if not path.endswith(".cpp"):
        return
    own_header = os.path.basename(path)[:-4] + ".hpp"
    if not git("ls-files", "--", own_header, f"*/{own_header}").strip():
        return
    includes = [(i, l) for i, l in enumerate(content.split("\n"), 1)
                if l.lstrip().startswith("#include")]
    if not includes:
        return
    added_nums = {ln for ln, _ in added}
    if not any(i in added_nums for i, _ in includes):
        return
    if not re.search(r'["/]' + re.escape(own_header) + '"', includes[0][1]):
        flag(path, includes[0][0],
             f'own header "{own_header}" must be the first include')


ACCESS_RANK = {"public": 0, "protected": 1, "private": 2}
CONTROL_KEYWORDS = re.compile(r"(if|for|while|switch|return|using|typedef|else|do|static_assert)\b")


def is_method_declaration(decl):
    """Heuristic for a public method declaration on a comment-stripped line.
    Data members carry '(' too: initializers (`= sizeof(x)`), function
    pointers, a '(' inside template arguments or array bounds, `{};`
    initializers."""
    if "(" not in decl or CONTROL_KEYWORDS.match(decl):
        return False
    if re.search(r"\boperator\b", decl):
        return True
    paren, eq = decl.find("("), decl.find("=")
    if 0 <= eq < paren:
        return False
    before = decl[:paren]
    if "(*" in decl[:paren + 2] or before.count("<") > before.count(">") \
            or before.count("[") > before.count("]"):
        return False
    if re.search(r"\{\};\s*$", decl):
        return False
    # MOCK_METHOD mirrors a documented interface; mocks are written bare.
    if decl.startswith(("MOCK_METHOD", "MOCK_CONST_METHOD")):
        return False
    return True


def has_doxygen_block(lines, i):
    """True when a /** ... */ block ends on the nearest line above line i
    (1-based) that is neither blank nor a template<> line."""
    j = i - 2
    while j >= 0 and (not lines[j].strip()
                      or re.match(r"^\s*(template\s*<|\[\[.*\]\]\s*$)", lines[j])):
        j -= 1
    if j < 0 or not lines[j].rstrip().endswith("*/"):
        return False
    while j >= 0 and "/*" not in lines[j]:
        j -= 1
    return j >= 0 and "/**" in lines[j]


def check_doxygen_and_access_order(path, content, added):
    """Added public method declarations need a /** */ block above them; an
    added access specifier must keep public -> protected -> private order."""
    if not path.endswith(".hpp"):
        return
    lines = content.split("\n")
    stripped = strip_file(content)
    added_nums = {ln for ln, _ in added}
    # Stack of [indent, access, last_rank]: a nested struct must not lose the
    # enclosing class's public section.
    scopes = []
    for i, code in enumerate(stripped, 1):
        m = re.match(r"^(\s*)(class|struct)\s+\w+", code)
        if m and not code.rstrip().endswith(";"):
            scopes.append([len(m.group(1)), "public" if m.group(2) == "struct" else "private", -1])
            continue
        if not scopes:
            continue
        class_indent = scopes[-1][0]
        if re.match(r"^\s{%d}\}" % class_indent, code):
            scopes.pop()
            continue
        m = re.match(r"^\s*(public|protected|private)\s*:", code)
        if m:
            scopes[-1][1] = m.group(1)
            if ACCESS_RANK[m.group(1)] < scopes[-1][2] and i in added_nums:
                flag(path, i, "access sections must be ordered public -> protected -> private")
            scopes[-1][2] = ACCESS_RANK[m.group(1)]
            continue
        if scopes[-1][1] != "public" or i not in added_nums:
            continue
        m = re.match(r"^\s{%d}(?:\[\[[^\]]*\]\]\s*)?([A-Za-z_~].*)$" % (class_indent + 4), code)
        if m and is_method_declaration(m.group(1)) and not has_doxygen_block(lines, i):
            flag(path, i, "public method declaration needs a /** */ Doxygen block")


AUTO_DECL = re.compile(r"\b(?:const\s+)?auto\s*[*&]{0,2}\s*(\w+|\[[^\]]*\])\s*=\s*(.+)")
# RHS forms where the type is visible or auto is the idiom: new, static_cast,
# lambdas, iterator factories. Structured bindings are excused on the LHS.
AUTO_OK_RHS = re.compile(r"new\b|static_cast<|^\s*\[|\.(begin|end|find|cbegin|cend)\s*\(")
# Also catches a legacy `_` member used on an added line: rename on touch.
TRAILING_UNDERSCORE = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z]\w*_)(?!\w)")
CONSTEXPR_DECL = re.compile(r"\bconstexpr\b[^=;()]*?\b([a-zA-Z_]\w*)\s*[={]")
UNSCOPED_ENUM = re.compile(r"^\s*enum\s+(?!class\b|struct\b)(?:\w+\s*)?[{:]")
SMART_POINTER = re.compile(r"\b(unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared)\b")
EXCEPTION = re.compile(r"\bthrow\b(?!\s*\()|\btry\s*\{|\bcatch\s*\(")


def check_added_lines(path, added, stripped):
    """Line rules on the comment-stripped form of each added line; `stripped`
    is the whole file, so a block comment opened above the hunk is known."""
    comment_run = []

    def flush_run():
        if len(comment_run) >= 3 and sum(bool(re.search(r"[;{}]\s*$", t)) for _, t in comment_run) >= 2:
            flag(path, comment_run[0][0], "commented-out code block — delete it")
        comment_run.clear()

    prev_ln = None
    for ln, text in added:
        if prev_ln is not None and ln != prev_ln + 1:
            flush_run()
        prev_ln = ln
        code = stripped[ln - 1] if ln <= len(stripped) else ""
        if text.strip().startswith("//"):
            comment_run.append((ln, text.strip()))
        else:
            flush_run()
        if not code.strip():
            continue
        if UNSCOPED_ENUM.match(code):
            flag(path, ln, "unscoped enum — use enum class, or namespace + constexpr int for registries")
        if SMART_POINTER.search(code):
            flag(path, ln, "smart pointers are not used — raw new/delete per the guide")
        # The guide allows throw only for a catastrophic failure state; the
        # marker names which one.
        if EXCEPTION.search(code) and "catastrophic:" not in text:
            flag(path, ln, "exceptions are not used outside catastrophic failure paths"
                           " — mark the site with a `// catastrophic: <why>` comment")
        if re.search(r"\bNULL\b", code):
            flag(path, ln, "NULL — use nullptr")
        m = TRAILING_UNDERSCORE.search(code)
        if m:
            flag(path, ln, f"trailing underscore on '{m.group(1)}' — plain camelCase per the guide")
        m = CONSTEXPR_DECL.search(code)
        if m and m.group(1) != "operator":
            names = [m.group(1)] + re.findall(r",\s*([a-zA-Z_]\w*)\s*[={]", code[m.end():])
            for name in names:
                if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
                    flag(path, ln, f"constexpr '{name}' must be SCREAMING_SNAKE_CASE")
        m = AUTO_DECL.search(code)
        if m and not m.group(1).startswith("[") and not AUTO_OK_RHS.search(m.group(2)):
            flag(path, ln, "auto hides the type — spell it out (allowed: new, static_cast, lambdas, iterators)")
        if path.endswith(".hpp") and re.search(r"#define\s+\w*TAG\b", code):
            flag(path, ln, "log TAG #define belongs in the .cpp")
    flush_run()


def check_format(path, content, added):
    """A difference is reported as a unified diff against the checked content."""
    ranges = ranges_of(added)
    if not ranges:
        return
    proc = subprocess.run(
        ["clang-format", "--style=file", f"--assume-filename={path}",
         *[f"--lines={a}:{b}" for a, b in ranges]],
        input=content, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        flag(path, ranges[0][0], f"clang-format failed: {proc.stderr.strip()}")
        return
    if proc.stdout != content:
        format_diffs.append("".join(difflib.unified_diff(
            content.splitlines(True), proc.stdout.splitlines(True),
            f"a/{path}", f"b/{path}")))
        flag(path, ranges[0][0],
             "clang-format: reformat the lines in the diff above"
             " (`git clang-format` on the range, or --staged then re-add)")


def tu_path_in_repo(entry, repo_root):
    """Repo-relative path for one compile_commands entry (forward slashes)."""
    return os.path.relpath(os.path.join(entry["directory"], entry["file"]),
                           repo_root).replace(os.sep, "/")


def load_compiledb(repo_root):
    """{repo-relative TU path: compile_commands entry}, or {} if unusable."""
    db_path = os.path.join(repo_root, "compile_commands.json")
    if not os.path.exists(db_path):
        skipped_checks.append(f"compile_commands.json missing, clang-tidy "
                              f"skipped; {COMPILEDB_HINT}")
        return {}
    try:
        with open(db_path, encoding="utf-8") as f:
            entries = json.load(f)
    except (json.JSONDecodeError, OSError) as exc:
        skipped_checks.append(f"compile_commands.json is unreadable "
                              f"({exc}), clang-tidy skipped; {COMPILEDB_HINT}")
        return {}
    db = {}
    for e in entries:
        rel = tu_path_in_repo(e, repo_root)
        if rel.startswith(".."):
            skipped_checks.append("compile_commands.json was generated in another "
                                  f"checkout ({e['directory']}), clang-tidy "
                                  f"skipped; {COMPILEDB_HINT}")
            return {}
        db[rel] = e
    if not db:
        skipped_checks.append(f"compile_commands.json lists no units, clang-tidy skipped; {COMPILEDB_HINT}")
    return db


MINGW_DRIVER = re.compile(r"^g(\+\+|cc)(-\d+)?\.exe$", re.I)
INCLUDE_FLAG = re.compile(r'-I\s*"([^"]+)"|-I\s*(\S+)')
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
INCLUDE_DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
_mingw_system_includes_cache = {}


def compiler_from_compile_command(entry):
    """First token of a compile command — the driver executable."""
    if "arguments" in entry:
        args = entry["arguments"]
        return args[0] if args else None
    command = entry.get("command", "").strip()
    if not command:
        return None
    if command.startswith('"'):
        end = command.find('"', 1)
        return command[1:end] if end > 0 else None
    return command.split(None, 1)[0]


def is_mingw_gcc_driver(compiler_path):
    if not compiler_path:
        return False
    return bool(MINGW_DRIVER.match(os.path.basename(compiler_path)))


def mingw_system_include_paths(compiler_path):
    """System #include <...> directories from a MinGW g++/gcc driver (-v)."""
    key = os.path.normcase(os.path.normpath(compiler_path))
    if key in _mingw_system_includes_cache:
        return _mingw_system_includes_cache[key]
    proc = subprocess.run(
        [compiler_path, "-E", "-x", "c++", "-", "-v"],
        input="", capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=120)
    text = proc.stderr
    start = text.find("#include <...> search starts here:")
    end = text.find("End of search list.", start)
    paths = []
    if start >= 0 and end > start:
        for line in text[start:end].splitlines()[1:]:
            path = line.strip()
            if path and os.path.isdir(path):
                paths.append(path)
    _mingw_system_includes_cache[key] = paths
    return paths


def clang_tidy_driver_extras(db, targets):
    """Extra clang-tidy flags so LLVM clang can parse MinGW compile commands."""
    if sys.platform != "win32" or not targets:
        return []
    compilers = set()
    for tu in targets:
        entry = db.get(tu)
        if not entry:
            continue
        driver = compiler_from_compile_command(entry)
        if is_mingw_gcc_driver(driver):
            compilers.add(os.path.normpath(driver))
    if not compilers:
        return []
    extras = ["--extra-arg-before=--target=x86_64-w64-mingw32"]
    for driver in sorted(compilers):
        for inc in mingw_system_include_paths(driver):
            inc = inc.replace("\\", "/")
            extras.append(f"--extra-arg-before=-isystem{inc}")
    return extras


def include_search_dirs(db, repo_root):
    """Repo-relative -I directories from compile_commands.json, in driver order."""
    dirs = []
    for entry in db.values():
        if "arguments" in entry:
            command = " ".join(entry["arguments"])
        else:
            command = entry.get("command", "")
        for quoted, bare in INCLUDE_FLAG.findall(command):
            raw = quoted or bare
            rel = os.path.relpath(os.path.join(entry["directory"], raw),
                                  repo_root).replace(os.sep, "/")
            if rel not in dirs:
                dirs.append(rel)
    return dirs


def resolve_include(tu, text, search_dirs):
    """Repo-relative path resolved for `#include "text"` from `tu` (tu's dir, then search_dirs), or None."""
    for directory in (os.path.dirname(tu), *search_dirs):
        candidate = os.path.normpath(os.path.join(directory, text))
        if os.path.isfile(candidate):
            return candidate.replace(os.sep, "/")
    return None


def includes_header(tu, header, search_dirs):
    # The DB can name a TU moved or deleted since the last build.
    if not os.path.isfile(tu):
        return False
    with open(tu, encoding="utf-8", errors="replace") as f:
        text = BLOCK_COMMENT.sub("", f.read())
        return any(resolve_include(tu, inc, search_dirs) == header
                   for inc in INCLUDE_DIRECTIVE.findall(text))


def tidy_targets(files, db, search_dirs):
    """Changed .cpp present in the DB, plus one TU per changed .hpp: the
    same-stem TU that includes it, else the first TU that does. Files with
    neither are reported, not tidied."""
    targets, skipped = set(), []
    for path in files:
        if path.endswith(".cpp"):
            (targets.add if path in db else skipped.append)(path)
            continue
        stem = os.path.basename(path)[:-4] + ".cpp"
        including = [t for t in sorted(db) if includes_header(t, path, search_dirs)]
        same = [t for t in including if os.path.basename(t) == stem]
        chosen = same or including[:1]
        if chosen:
            targets.add(chosen[0])
        else:
            skipped.append(path)
    return sorted(targets), skipped


def run_scoped_tidy(files, added_by_path, repo_root):
    """--line-filter drops check findings outside the added lines;
    compiler errors are not filtered, so a TU that fails to parse is reported
    whole."""
    db = load_compiledb(repo_root)
    if not db:
        return
    line_filter = [{"name": path, "lines": ranges_of(added)}
                   for path, added in added_by_path.items() if added]
    if not line_filter:
        return
    targets, skipped = tidy_targets(files, db, include_search_dirs(db, repo_root))
    if skipped:
        # Expected for test/, FDN, ESP32-only and header-only files: the
        # native_cli database never has a unit that includes them.
        notices.append("clang-tidy skipped, no translation unit in compile_commands.json: "
                       + ", ".join(skipped))
    if not targets:
        return
    check_uncommitted_edits(files)
    tidy_cmd = [
        "clang-tidy", "-p", repo_root, "--quiet",
        f"--line-filter={json.dumps(line_filter)}",
        *clang_tidy_driver_extras(db, targets),
        *targets,
    ]
    proc = subprocess.run(tidy_cmd, capture_output=True,
                          encoding="utf-8", errors="replace")
    output = proc.stdout.strip() or (proc.stderr.strip() if proc.returncode else "")
    if output:
        print(output)
    if proc.returncode != 0:
        violations.append((None, None, "clang-tidy reported the diagnostics above"))


def check_uncommitted_edits(files):
    """clang-tidy reads the working tree while other checks read git (index or HEAD), so uncommitted edits skew results."""
    revs = [content_rev] if content_rev else []
    dirty = git("diff", "--name-only", "-z", *revs, "--", *files).split("\0")
    for path in dirty:
        if path in files:
            flag(path, 1, "has uncommitted changes, so clang-tidy judged it on different content than the other checks; commit or stash them")


def run_checks(strict):
    repo_root = git("rev-parse", "--show-toplevel").strip()
    os.chdir(repo_root)
    # During a merge the index vs HEAD shows the whole incoming side as added.
    if not content_rev and os.path.exists(
            git("rev-parse", "--git-path", "MERGE_HEAD").strip()):
        print("check_style: merge in progress, checks skipped")
        return
    files = changed_files()
    if content_rev:
        print(f"check_style: {len(files)} file(s) in range")
    if not files:
        return
    have_format = shutil.which("clang-format") is not None
    have_tidy = shutil.which("clang-tidy") is not None
    for tool, present in (("clang-format", have_format), ("clang-tidy", have_tidy)):
        if not present:
            skipped_checks.append(f"{tool} not found on PATH, its checks skipped; install LLVM/clang tools")
    added_by_path = {}
    for path, (status, old_path) in files.items():
        content = file_content(path)
        added = added_lines(path, old_path)
        added_by_path[path] = added
        if status in "ACR":
            check_new_file(path, content, status)
        check_own_header_first(path, content, added)
        check_doxygen_and_access_order(path, content, added)
        check_added_lines(path, added, strip_file(content))
        if have_format:
            check_format(path, content, added)
    if have_tidy:
        run_scoped_tidy(files, added_by_path, repo_root)
    for d in format_diffs:
        print(d, end="" if d.endswith("\n") else "\n")
    for path, ln, msg in sorted(violations,
                                key=lambda v: (v[0] is None, v[0] or "", v[1] or 0)):
        print(f"{path}:{ln}: {msg}" if path else f"check_style: {msg}")
    for n in notices + skipped_checks:
        print(n)
    if violations and not strict:
        print("check_style: findings above are advisory, the commit is not blocked")


def parse_args(argv):
    ap = argparse.ArgumentParser(
        prog="check_style.py", description="Style checks on a diff.",
        allow_abbrev=False)
    ap.add_argument("--base", metavar="REF",
                    help="check REF...HEAD instead of the staged index. In CI "
                         "pass HEAD^1: on a pull_request the checked-out merge "
                         "commit's first parent is the live base tip.")
    ap.add_argument("--strict", action="store_true",
                    help="exit 1 on a finding, or on a check that could not run")
    return ap.parse_args(argv)


def main():
    global diff_revs, content_rev
    args = parse_args(sys.argv[1:])
    strict = args.strict or os.environ.get("CHECK_STYLE_STRICT", "") not in ("", "0")
    if args.base is not None:
        if not args.base.strip():
            sys.exit("check_style: --base needs a ref, got an empty string")
        diff_revs = [f"{args.base}...HEAD"]
        content_rev = "HEAD"
    try:
        run_checks(strict)
    except Exception as exc:
        print(f"check_style: checks incomplete, {type(exc).__name__}: {exc}")
        return 1 if strict else 0
    return 1 if (strict and (violations or skipped_checks)) else 0


if __name__ == "__main__":
    sys.exit(main())
