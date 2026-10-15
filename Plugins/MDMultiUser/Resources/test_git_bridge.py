import importlib.util
import json
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("bridge", pathlib.Path(__file__).with_name("git_bridge.py"))
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class GitBridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = pathlib.Path(self.temp.name)
        self.remote, self.root = self.base / "remote.git", self.base / "work"
        subprocess.run(["git", "init", "--bare", str(self.remote)], check=True, capture_output=True)
        subprocess.run(["git", "clone", str(self.remote), str(self.root)], check=True, capture_output=True)
        bridge.git(self.root, "config", "user.name", "Fixture")
        bridge.git(self.root, "config", "user.email", "fixture@example.invalid")
        for name in ["placed.uasset", "reference.uasset", "unrelated.txt", "a[1].txt", "a1.txt"]:
            (self.root / name).write_text("base")
        (self.root / ".gitignore").write_text("Docs/Presentation/\n")
        bridge.git(self.root, "add", ".")
        bridge.git(self.root, "commit", "-m", "base")
        bridge.git(self.root, "push", "--set-upstream", "origin", "HEAD")
        self.job = self.base / "job"
        self.job.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def request(self, op, **kwargs):
        state = bridge.status(self.root)
        return {"root": str(self.root), "op": op, "head": state["head"], "branch": state["branch"], **kwargs}

    def test_selected_commit_preserves_unrelated_index_and_literal_paths(self):
        for name in ["placed.uasset", "reference.uasset", "unrelated.txt", "a[1].txt", "a1.txt"]:
            (self.root / name).write_text("changed")
        bridge.git(self.root, "add", "unrelated.txt")
        bridge.perform(self.request("commit", files=["placed.uasset", "reference.uasset", "a[1].txt"], message="선택 참조 커밋"), self.job)
        committed = set(bridge.text(self.root, "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD").splitlines())
        self.assertEqual(committed, {"placed.uasset", "reference.uasset", "a[1].txt"})
        self.assertEqual(bridge.text(self.root, "diff", "--cached", "--name-only"), "unrelated.txt")
        self.assertEqual((self.root / "a1.txt").read_text(), "changed")

    def test_ignored_private_files_and_unpublished_dependencies(self):
        p = self.root / "Docs/Presentation/private.html"
        p.parent.mkdir(parents=True)
        p.write_text("private")
        (self.root / "reference.uasset").write_text("new reference")
        bridge.git(self.root, "add", "reference.uasset")
        bridge.git(self.root, "commit", "-m", "reference")
        rows = {r["path"]: r for r in bridge.status(self.root)["files"]}
        self.assertTrue(rows["reference.uasset"]["unpublished"])
        self.assertNotIn("Docs/Presentation/private.html", rows)
        self.assertEqual(bridge.status(self.root)["ahead"], 1)

    def test_new_unicode_asset_scoped_commit_and_push(self):
        (self.root / "배선반 에셋.uasset").write_text("new")
        state = bridge.perform(self.request("commit", files=["배선반 에셋.uasset"], message="new asset"), self.job)
        self.assertEqual(state["ahead"], 1)
        state = bridge.perform(self.request("push"), self.job)
        self.assertEqual(state["ahead"], 0)

    def test_python_helpers_hidden_without_changing_git_tracking(self):
        names = ["Tools/helper.py", "Tools/window.PYW", "Tools/helper.pyc", "Tools/helper.pyo",
                 "Tools/native.pyd", "Tools/__pycache__/cache.bin"]
        for name in names:
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("helper")
        bridge.git(self.root, "add", "Tools/helper.py")
        bridge.git(self.root, "commit", "-m", "unpublished helper")
        (self.root / "Tools/helper.py").write_text("edited helper")
        (self.root / "placed.uasset").write_text("asset edit")
        state = bridge.status(self.root, names + ["placed.uasset"])
        self.assertEqual({r["path"] for r in state["files"]}, {"placed.uasset"})
        self.assertTrue(state["dirty"])
        self.assertTrue(set(names).issubset(bridge.changes(self.root)))
        self.assertEqual(bridge.text(self.root, "ls-files", "Tools/helper.py"), "Tools/helper.py")
        self.assertFalse(bridge.visible_in_workspace("Tools\\__PYCACHE__\\cache.bin"))
        self.assertTrue(bridge.visible_in_workspace("Content/PythonPanel.uasset"))

    def test_hidden_python_change_still_blocks_pull(self):
        file = self.root / "helper.py"
        file.write_text("local helper")
        request = self.pull_request()
        self.assertEqual(bridge.status(self.root)["files"], [])
        self.assertTrue(bridge.status(self.root)["dirty"])
        with mock.patch.object(bridge, "wait_for_editor"), mock.patch.object(bridge, "other_editors", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "changed"):
                bridge.perform(request, self.job)
        self.assertEqual(file.read_text(), "local helper")

    def test_stale_head_and_outside_paths_refused(self):
        (self.root / "placed.uasset").write_text("changed")
        bad = self.request("commit", files=["../outside"], message="bad")
        with self.assertRaises(RuntimeError):
            bridge.perform(bad, self.job)
        bad = self.request("commit", files=["placed.uasset"], message="bad")
        bad["head"] = "0" * 40
        with self.assertRaises(RuntimeError):
            bridge.perform(bad, self.job)
        self.assertEqual(bridge.text(self.root, "diff", "--cached", "--name-only"), "")

    def test_rename_pair_is_scoped(self):
        bridge.git(self.root, "mv", "placed.uasset", "renamed.uasset")
        (self.root / "unrelated.txt").write_text("other")
        bridge.git(self.root, "add", "unrelated.txt")
        bridge.perform(self.request("commit", files=["renamed.uasset"], message="rename"), self.job)
        self.assertEqual(bridge.text(self.root, "diff", "--cached", "--name-only"), "unrelated.txt")
        self.assertFalse((self.root / "placed.uasset").exists())

    def peer(self, path, content):
        peer = self.base / "peer"
        subprocess.run(["git", "clone", str(self.remote), str(peer)], check=True, capture_output=True)
        bridge.git(peer, "config", "user.name", "Peer")
        bridge.git(peer, "config", "user.email", "peer@example.invalid")
        file = peer / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content)
        bridge.git(peer, "add", "-f", path)
        bridge.git(peer, "commit", "-m", "peer")
        bridge.git(peer, "push")

    def pull_request(self):
        return self.request("pull", editor_pid=1, editor="fixture-editor", project="fixture.uproject",
                            map="/Game/Fixture", engine_root="fixture-engine", editor_target="FixtureEditor")

    def test_pull_fast_forward_and_editor_reopen(self):
        self.peer("placed.uasset", "remote update")
        original = subprocess.Popen
        def launch(args, *a, **kw):
            if args[0] == "fixture-editor": return mock.Mock()
            return original(args, *a, **kw)
        with mock.patch.object(bridge, "wait_for_editor"), mock.patch.object(bridge, "other_editors", return_value=[]), mock.patch.object(bridge.subprocess, "Popen", side_effect=launch) as calls:
            state = bridge.perform(self.pull_request(), self.job)
            self.assertEqual(state["behind"], 0)
            self.assertEqual((self.root / "placed.uasset").read_text(), "remote update")
            self.assertTrue(any(c.args[0][0] == "fixture-editor" for c in calls.call_args_list))

    def test_pull_preserves_ignored_local_file(self):
        p = self.root / "Docs/Presentation/private.html"
        p.parent.mkdir(parents=True); p.write_text("local private")
        self.peer("Docs/Presentation/private.html", "remote")
        before = bridge.text(self.root, "rev-parse", "HEAD")
        original = subprocess.Popen
        def launch(args, *a, **kw):
            return mock.Mock() if args[0] == "fixture-editor" else original(args, *a, **kw)
        with mock.patch.object(bridge, "wait_for_editor"), mock.patch.object(bridge, "other_editors", return_value=[]), mock.patch.object(bridge.subprocess, "Popen", side_effect=launch):
            with self.assertRaisesRegex(RuntimeError, "overlaps"):
                bridge.perform(self.pull_request(), self.job)
        self.assertEqual(p.read_text(), "local private")
        self.assertEqual(bridge.text(self.root, "rev-parse", "HEAD"), before)

    def test_pull_refuses_other_local_editor(self):
        original = subprocess.Popen
        def launch(args, *a, **kw):
            return mock.Mock() if args[0] == "fixture-editor" else original(args, *a, **kw)
        with mock.patch.object(bridge, "wait_for_editor"), mock.patch.object(bridge, "other_editors", return_value=[123]), mock.patch.object(bridge.subprocess, "Popen", side_effect=launch):
            request = self.pull_request()
            with self.assertRaisesRegex(RuntimeError, "Another local editor"):
                bridge.perform(request, self.job)

    def test_current_reference_already_matches_published_version(self):
        (self.root / "reference.uasset").write_text("local committed change")
        bridge.git(self.root, "add", "reference.uasset")
        bridge.git(self.root, "commit", "-m", "local")
        (self.root / "reference.uasset").write_text("base")
        rows = {r["path"]: r for r in bridge.status(self.root, ["reference.uasset"])["files"]}
        self.assertFalse(rows["reference.uasset"]["needs_publish"])

    def test_untracked_reference_already_present_on_remote(self):
        self.peer("new_reference.uasset", "same content")
        (self.root / "new_reference.uasset").write_text("same content")
        bridge.git(self.root, "fetch")
        rows = {r["path"]: r for r in bridge.status(self.root, ["new_reference.uasset"])["files"]}
        self.assertFalse(rows["new_reference.uasset"]["needs_publish"])


if __name__ == "__main__":
    unittest.main()
