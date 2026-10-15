"""File-based Git worker; no shell, no automatic staging or stash."""
import json
import os
import pathlib
import subprocess
import sys
import time


def git(root, *args, check=True):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
    p = subprocess.run(["git", "-C", str(root), "-c", "core.quotepath=false", "--literal-pathspecs", *args],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=120,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if check and p.returncode:
        raise RuntimeError(p.stderr.decode("utf-8", "replace").strip() or p.stdout.decode("utf-8", "replace").strip())
    return p


def text(root, *args):
    return git(root, *args).stdout.decode("utf-8", "replace").strip()


def changes(root):
    parts = git(root, "status", "--porcelain=v1", "-z", "--untracked-files=all").stdout.split(b"\0")
    rows, i = {}, 0
    while i < len(parts) and parts[i]:
        entry = parts[i].decode("utf-8", "strict")
        state, path = entry[:2], entry[3:]
        rows[path] = {"path": path, "state": state, "unpublished": False}
        i += 1
        if "R" in state or "C" in state:
            # -z rename records put the destination first; include source for a scoped commit.
            old = parts[i].decode("utf-8", "strict")
            rows[path]["rename_from"] = old
            rows[old] = {"path": old, "state": " D", "unpublished": False, "rename_to": path}
            i += 1
    return rows


def visible_in_workspace(path):
    """Hide Python helpers from the asset publishing UI, not from Git safety checks."""
    path = pathlib.PurePosixPath(path.replace("\\", "/").lower())
    return "__pycache__" not in path.parts and path.suffix not in {".py", ".pyw", ".pyc", ".pyo", ".pyd"}


def status(root, references=()):
    branch = text(root, "branch", "--show-current")
    head = text(root, "rev-parse", "HEAD")
    p = git(root, "rev-parse", "--abbrev-ref", "@{upstream}", check=False)
    upstream = p.stdout.decode("utf-8", "replace").strip() if p.returncode == 0 else ""
    rows = changes(root)
    ahead = behind = 0
    if upstream:
        ahead, behind = map(int, text(root, "rev-list", "--left-right", "--count", "HEAD...@{upstream}").split())
        base = text(root, "merge-base", "HEAD", "@{upstream}")
        for raw in git(root, "diff", "--name-only", "-z", base, "HEAD").stdout.split(b"\0"):
            if raw:
                path = raw.decode("utf-8", "strict")
                rows.setdefault(path, {"path": path, "state": "", "unpublished": True})["unpublished"] = True
    else:
        # No upstream: every tracked placement dependency requires initial publication.
        for raw in git(root, "ls-files", "-z").stdout.split(b"\0"):
            if raw:
                path = raw.decode("utf-8", "strict")
                rows.setdefault(path, {"path": path, "state": "", "unpublished": True})["unpublished"] = True
    refs = set(references)
    if upstream:
        different = set(x.decode("utf-8", "strict") for x in git(root, "diff", "--name-only", "-z", "@{upstream}").stdout.split(b"\0") if x)
        remote_blobs = {}
        for raw in git(root, "ls-tree", "-r", "-z", "@{upstream}").stdout.split(b"\0"):
            if raw:
                header, name = raw.split(b"\t", 1)
                remote_blobs[name.decode("utf-8", "strict")] = header.split()[2].decode()
        for path, row in rows.items():
            row["needs_publish"] = path in different if row["state"] != "??" else True
            if row["state"] == "??" and path in refs and path in remote_blobs:
                row["needs_publish"] = text(root, "hash-object", "--path=" + path, "--", path) != remote_blobs[path]
    else:
        for row in rows.values(): row["needs_publish"] = True
    return {"branch": branch, "head": head, "upstream": upstream, "ahead": ahead, "behind": behind,
            "files": [row for path, row in rows.items() if visible_in_workspace(path)],
            "dirty": bool(changes(root))}


def scoped_commit(root, request, job):
    if text(root, "branch", "--show-current") != request["branch"] or text(root, "rev-parse", "HEAD") != request["head"]:
        raise RuntimeError("HEAD changed. Refresh and review the selected files again.")
    available = changes(root)
    selected = set(request.get("files", []))
    for path in list(selected):
        related = available.get(path, {})
        selected.update(related[k] for k in ("rename_from", "rename_to") if k in related)
    if not selected or not request.get("message", "").strip():
        raise RuntimeError("Select changed files and enter a commit message.")
    for path in selected:
        p = pathlib.PurePosixPath(path)
        if p.is_absolute() or ".." in p.parts or ".git" in p.parts or path not in available or "\0" in path:
            raise RuntimeError("Invalid or stale selected path: " + path)
        if "U" in available[path]["state"] or available[path]["state"] in ("AA", "DD"):
            raise RuntimeError("Resolve merge conflicts before committing.")
    # An existing rename must include both old and new paths; never absorb an unrelated staged file.
    pathspec = job / "selected.paths"
    pathspec.write_bytes(b"".join(p.encode("utf-8") + b"\0" for p in sorted(selected)))
    body = job / "message.txt"
    body.write_text(request["message"], encoding="utf-8")
    args = ["--pathspec-from-file=" + str(pathspec), "--pathspec-file-nul"]
    indexed = set(x.decode("utf-8", "strict") for x in git(root, "ls-files", "-z").stdout.split(b"\0") if x)
    to_add = {p for p in selected if p in indexed or (root / p).exists()}
    if to_add:
        add_spec = job / "stage.paths"
        add_spec.write_bytes(b"".join(p.encode("utf-8") + b"\0" for p in sorted(to_add)))
        git(root, "add", "--pathspec-from-file=" + str(add_spec), "--pathspec-file-nul")
    try:
        git(root, "commit", "--only", *args, "-F", str(body))
    except Exception as e:
        raise RuntimeError(str(e) + "\nSelected files may remain staged; no reset was performed.") from e


def other_editors():
    if os.name != "nt":
        p = subprocess.run(["ps", "-eo", "pid=,comm="], capture_output=True, check=True)
        return [line for line in p.stdout.decode().splitlines() if "UnrealEditor" in line]
    import ctypes
    from ctypes import wintypes
    class Entry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
                    ("exe", wintypes.WCHAR * 260)]
    k = ctypes.windll.kernel32
    k.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    k.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = k.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise RuntimeError("Cannot verify other editor processes; pull refused.")
    found = []
    entry = Entry(); entry.size = ctypes.sizeof(entry)
    try:
        more = k.Process32FirstW(handle, ctypes.byref(entry))
        while more:
            if entry.exe.lower() in ("unrealeditor.exe", "unrealeditor-cmd.exe"):
                found.append(entry.pid)
            more = k.Process32NextW(handle, ctypes.byref(entry))
    finally:
        k.CloseHandle(handle)
    return found


def wait_for_editor(pid):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes
            k = ctypes.windll.kernel32
            k.OpenProcess.restype = wintypes.HANDLE
            k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            k.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = k.OpenProcess(0x100000, False, pid)
            if not handle:
                return
            code = k.WaitForSingleObject(handle, 0)
            k.CloseHandle(handle)
            if code == 0:
                return
        else:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
        time.sleep(.25)
    raise RuntimeError("Editor did not exit; pull was not started.")


def perform(request, job):
    root = pathlib.Path(request["root"]).resolve()
    if pathlib.Path(text(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise RuntimeError("Project directory must be the repository root.")
    op = request["op"]
    if op == "fetch":
        git(root, "fetch", "--prune")
    elif op == "commit":
        scoped_commit(root, request, job)
    elif op == "push":
        branch = text(root, "branch", "--show-current")
        if not branch or branch != request["branch"] or text(root, "rev-parse", "HEAD") != request["head"]:
            raise RuntimeError("Branch or HEAD changed. Refresh before pushing.")
        git(root, "check-ref-format", "--branch", branch)
        p = git(root, "config", "--get", "branch." + branch + ".remote", check=False)
        remote = p.stdout.decode().strip() if p.returncode == 0 else "origin"
        if not remote or remote.startswith("-") or remote == ".":
            raise RuntimeError("Invalid push remote.")
        p = git(root, "config", "--get", "branch." + branch + ".merge", check=False)
        target = p.stdout.decode().strip() if p.returncode == 0 else "refs/heads/" + branch
        if not target.startswith("refs/heads/"):
            raise RuntimeError("Push target must be a branch.")
        git(root, "push", "--set-upstream", "--", remote, "HEAD:" + target)
    elif op == "pull":
        # Editor exits normally before this worker changes any working-tree file.
        wait_for_editor(int(request["editor_pid"]))
        try:
            if other_editors():
                raise RuntimeError("Another local editor is still running; pull refused to protect loaded assets.")
            if text(root, "branch", "--show-current") != request["branch"]:
                raise RuntimeError("Branch changed; pull refused.")
            if changes(root) or text(root, "rev-parse", "HEAD") != request["head"]:
                raise RuntimeError("Working tree or HEAD changed. Pull refused; no stash/reset performed.")
            git(root, "fetch", "--prune")
            incoming = text(root, "rev-parse", "@{upstream}")
            old_files = set(x for x in git(root, "ls-tree", "-r", "--name-only", "-z", "HEAD").stdout.split(b"\0") if x)
            new_files = set(x for x in git(root, "ls-tree", "-r", "--name-only", "-z", incoming).stdout.split(b"\0") if x)
            for raw in new_files - old_files:
                path = root / raw.decode("utf-8", "strict")
                if os.path.lexists(path):
                    raise RuntimeError("Incoming tracked file overlaps a local ignored/untracked file: " + str(path))
            if other_editors() or changes(root) or text(root, "rev-parse", "HEAD") != request["head"]:
                raise RuntimeError("Editor, working tree or HEAD changed during fetch. Pull refused.")
            git(root, "merge", "--ff-only", incoming)
            changed = text(root, "diff", "--name-only", request["head"], "HEAD").splitlines()
            if any(p.startswith(("Source/", "Plugins/")) or p.endswith((".uproject", ".uplugin")) for p in changed):
                target = request["editor_target"]
                build = pathlib.Path(request["engine_root"]) / "Build/BatchFiles/Build.bat"
                p = subprocess.run([str(build), target, "Win64", "Development", "-Project=" + request["project"], "-WaitMutex"],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
                (job / "build.log").write_bytes(p.stdout)
                if p.returncode:
                    raise RuntimeError("Pull succeeded, but editor build failed. See build.log; editor remains closed.")
        except Exception:
            # Failed pull leaves source intact; reopening after a failed build would load stale native classes.
            if text(root, "rev-parse", "HEAD") == request["head"] and not changes(root):
                subprocess.Popen([request["editor"], request["project"], request["map"]])
            raise
        subprocess.Popen([request["editor"], request["project"], request["map"]])
    elif op != "status":
        raise RuntimeError("Unknown Git operation.")
    return status(root, request.get("references", []))


def main(request_path):
    path = pathlib.Path(request_path)
    try:
        request = json.loads(path.read_text(encoding="utf-8-sig"))
        result = {"ok": True, "message": request["op"] + " completed", **perform(request, path.parent)}
    except Exception as e:
        result = {"ok": False, "message": str(e)}
    temp = path.parent / "result.tmp"
    temp.write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
    temp.replace(path.parent / "result.json")


if __name__ == "__main__":
    main(sys.argv[1])
