#!/usr/bin/env python3
"""Style gate on the staged diff, run by hooks/pre-commit.

Needs clang-format, clang-tidy and a compile_commands.json at the repo root
(`pio run -e native_cli -t compiledb`). Line rules and clang-format apply to
the lines added in the staged diff; clang-tidy runs on the translation units
behind the staged files, with its diagnostics restricted to those same lines.
A legacy violation elsewhere in a touched file does not block the commit
(clang-format may still reflow the statement around an added line), so legacy
gets fixed on touch rather than all at once. Exceptions: new and
renamed files must be kebab-case, new headers need #pragma once, and the
own-header rule fires whenever the include block is touched.
"""
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


def git(*args):
    # Decoded leniently: one stray Latin-1 byte in a comment must surface as
    # a diagnosis, not a traceback.
    return subprocess.run(["git", *args], capture_output=True,
                          encoding="utf-8", errors="replace").stdout


def flag(path, ln, msg):
    violations.append((path, ln, msg))


def staged_files():
    """{new path: (status letter, old path)}; old differs only for renames."""
    fields = git("diff", "--cached", "--name-status", "-z", "-M",
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
    """(lineno, text) pairs for lines added in the staged diff of one file.

    Both rename sides are passed so -M still pairs them. Only hunk lines are
    read, never the file headers, so diff.noprefix, diff.mnemonicPrefix,
    color or quotePath settings in the user's git config cannot hide a file.
    """
    out = git("diff", "--cached", "-U0", "-M", "--no-color", "--no-ext-diff",
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


def staged_content(path):
    return git("show", f":{path}")


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
    """A difference is reported as a unified diff against the staged content."""
    ranges = ranges_of(added)
    if not ranges:
        return
    proc = subprocess.run(
        ["clang-format", "--style=file", f"--assume-filename={path}",
         *[f"--lines={a}:{b}" for a, b in ranges]],
        input=content, capture_output=True, text=True)
    if proc.returncode != 0:
        flag(path, ranges[0][0], f"clang-format failed: {proc.stderr.strip()}")
        return
    if proc.stdout != content:
        format_diffs.append("".join(difflib.unified_diff(
            content.splitlines(True), proc.stdout.splitlines(True),
            f"a/{path}", f"b/{path}")))
        flag(path, ranges[0][0],
             "clang-format: reformat the lines in the diff above"
             " (e.g. `git clang-format --staged`, then re-add)")


def load_compiledb(repo_root):
    """Repo-relative TU paths from compile_commands.json, or None when absent.
    A DB from another checkout would match no staged file and skip every TU
    with a notice blaming the wrong cause, so it is rejected whole."""
    db_path = os.path.join(repo_root, "compile_commands.json")
    if not os.path.exists(db_path):
        return None
    with open(db_path, encoding="utf-8") as f:
        entries = json.load(f)
    paths = set()
    for e in entries:
        # Forward slashes to match git's paths on Windows too.
        rel = os.path.relpath(os.path.join(e["directory"], e["file"]), repo_root).replace(os.sep, "/")
        if rel.startswith(".."):
            flag("compile_commands.json", 1,
                 f"generated in another checkout ({e['directory']}); {COMPILEDB_HINT}")
            return set()
        paths.add(rel)
    return paths


def includes_header(tu, header):
    # The DB can name a TU moved or deleted since the last build.
    if not os.path.isfile(tu):
        return False
    with open(tu, encoding="utf-8", errors="replace") as f:
        for m in re.finditer(r'#include\s*"([^"]+)"', f.read()):
            if header == m.group(1) or header.endswith("/" + m.group(1)):
                return True
    return False


def tidy_targets(files, db):
    """Staged .cpp present in the DB, plus one TU per staged .hpp: the
    same-stem TU that includes it, else the first TU that does. Files with
    neither are reported, not tidied."""
    targets, skipped = set(), []
    for path in files:
        if path.endswith(".cpp"):
            (targets.add if path in db else skipped.append)(path)
            continue
        stem = os.path.basename(path)[:-4] + ".cpp"
        including = [t for t in sorted(db) if includes_header(t, path)]
        same = [t for t in including if os.path.basename(t) == stem]
        chosen = same or including[:1]
        if chosen:
            targets.add(chosen[0])
        else:
            skipped.append(path)
    return sorted(targets), skipped


def run_scoped_tidy(files, added_by_path, repo_root):
    """--line-filter drops check findings outside the staged added lines;
    compiler errors are not filtered, so a TU that fails to parse fails the
    commit."""
    db = load_compiledb(repo_root)
    if db is None:
        notices.append(f"compile_commands.json missing, clang-tidy skipped; {COMPILEDB_HINT}")
        return
    if not db:
        return
    line_filter = [{"name": path, "lines": ranges_of(added)}
                   for path, added in added_by_path.items() if added]
    if not line_filter:
        return
    targets, skipped = tidy_targets(files, db)
    for path in skipped:
        notices.append(f"{path}: not in compile_commands.json, clang-tidy skipped; {COMPILEDB_HINT}")
    if not targets:
        return
    proc = subprocess.run(
        ["clang-tidy", "-p", repo_root, "--quiet",
         f"--line-filter={json.dumps(line_filter)}", *targets],
        capture_output=True, text=True)
    output = proc.stdout.strip() or (proc.stderr.strip() if proc.returncode else "")
    if output:
        print(output)
    if proc.returncode != 0:
        flag(targets[0], 1, "clang-tidy diagnostics above")


def check_unstaged_edits(files):
    """clang-tidy reads the working tree while every other check reads the
    index; a staged file with further unstaged edits would be judged on the
    wrong content."""
    dirty = git("diff", "--name-only", "-z", "--", *files).split("\0")
    for path in dirty:
        if path in files:
            flag(path, 1, "has unstaged changes; stage or stash them so clang-tidy sees the staged content")


def main():
    repo_root = git("rev-parse", "--show-toplevel").strip()
    os.chdir(repo_root)
    # During a merge the index vs HEAD shows the whole incoming side as added.
    if os.path.exists(git("rev-parse", "--git-path", "MERGE_HEAD").strip()):
        return 0
    files = staged_files()
    if not files:
        return 0
    missing = [t for t in ("clang-format", "clang-tidy") if shutil.which(t) is None]
    if missing:
        print(f"check_style: {', '.join(missing)} not found on PATH; install LLVM/clang tools")
        return 1
    added_by_path = {}
    for path, (status, old_path) in files.items():
        content = staged_content(path)
        added = added_lines(path, old_path)
        added_by_path[path] = added
        if status in "ACR":
            check_new_file(path, content, status)
        check_own_header_first(path, content, added)
        check_doxygen_and_access_order(path, content, added)
        check_added_lines(path, added, strip_file(content))
        check_format(path, content, added)
    check_unstaged_edits(files)
    run_scoped_tidy(files, added_by_path, repo_root)
    for d in format_diffs:
        print(d, end="" if d.endswith("\n") else "\n")
    for path, ln, msg in sorted(violations):
        print(f"{path}:{ln}: {msg}")
    for n in notices:
        print(n, file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
