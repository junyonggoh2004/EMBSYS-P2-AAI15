"""The documentation holds together: every link resolves, every checklist task
in the README has its tests listed in TESTING.md, and every test file is in
TESTING.md. Run from the repository root."""
import os
import re
import subprocess
import unittest
from urllib.parse import unquote

LINK = re.compile(r"\]\(([^)\s]+)\)")


def tracked_markdown():
    out = subprocess.run(["git", "ls-files", "*.md"], capture_output=True, text=True, check=True).stdout
    return [f for f in out.split() if "third_party" not in f and os.path.exists(f)]


def lines_outside_code(path):
    fence = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.lstrip("> ").startswith("```"):
                fence = not fence
                continue
            if not fence:
                yield line


def slug(heading):
    heading = re.sub(r"<[^>]+>", "", heading).strip().lower()
    return re.sub(r"[^\w\- ]", "", heading).replace(" ", "-")


def anchors(path):
    found = set()
    for line in lines_outside_code(path):
        m = re.match(r"^#{1,6} (.*)$", line.rstrip("\n"))
        if m:
            found.add(slug(m.group(1)))
        found.update(re.findall(r'<a name="([^"]+)"', line))
    return found


def readme_task_ids():
    """Task IDs from the checklist tables in README section 2."""
    with open("README.md", encoding="utf-8") as f:
        text = f.read()
    start = text.index('<a name="2-development-checklists"></a>')
    end = text.index('<a name="3-set-up-and-test-from-scratch"></a>')
    ids, table = set(), None
    for line in text[start:end].split("\n"):
        if line.startswith("| # |"):
            table = line
            continue
        if not line.startswith("|"):
            table = None
            continue
        if table and ("Task" in table or "Status" in table):
            m = re.match(r"^\| *(D\d-\d+|\d+(?:\.\d+)?) *\|", line)
            if m:
                ids.add(m.group(1))
    return ids


def testing_section(number):
    with open("TESTING.md", encoding="utf-8") as f:
        text = f.read()
    start = text.index(f"## {number}.")
    nxt = text.find("\n## ", start + 4)
    return text[start:nxt if nxt >= 0 else len(text)]


class DocsTest(unittest.TestCase):
    def test_every_relative_link_resolves(self):
        broken = []
        for path in tracked_markdown():
            base = os.path.dirname(path)
            for line in lines_outside_code(path):
                for m in LINK.finditer(line):
                    url = m.group(1)
                    if re.match(r"^(https?:|mailto:|#)", url) or "://" in url:
                        continue
                    target = unquote(url.split("#")[0])
                    if target and not os.path.exists(os.path.normpath(os.path.join(base, target))):
                        broken.append(f"{path}: {url}")
        self.assertEqual(broken, [])

    def test_every_readme_anchor_exists(self):
        known = anchors("README.md")
        missing = []
        for path in tracked_markdown():
            base = os.path.dirname(path)
            for line in lines_outside_code(path):
                for m in LINK.finditer(line):
                    url = m.group(1)
                    if "#" not in url or "://" in url:
                        continue
                    target, anchor = url.split("#", 1)
                    resolved = os.path.normpath(os.path.join(base, unquote(target))) if target else path
                    if resolved == "README.md" and anchor not in known:
                        missing.append(f"{path}: {url}")
        self.assertEqual(missing, [])

    def test_every_checklist_task_has_tests(self):
        claims = readme_task_ids()
        self.assertGreater(len(claims), 80)
        listed = set(re.findall(r"^\| *(D\d-\d+|\d+(?:\.\d+)?) *\|", testing_section(3), re.M))
        self.assertEqual(sorted(claims - listed), [])

    def test_every_test_id_is_defined(self):
        defined = set(re.findall(r"^\| *((?:LT|BT)-\d+) *\|", testing_section(1) + testing_section(2), re.M))
        used = set(re.findall(r"\b((?:LT|BT)-\d+)\b", testing_section(3)))
        self.assertEqual(sorted(used - defined), [])

    def test_every_test_file_is_listed(self):
        with open("TESTING.md", encoding="utf-8") as f:
            testing = f.read()
        files = [f for f in os.listdir("tests") if re.match(r"test_.*\.(c|py)$", f)]
        self.assertEqual(sorted(f for f in files if f not in testing), [])


if __name__ == "__main__":
    unittest.main()
