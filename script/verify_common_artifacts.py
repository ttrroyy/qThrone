"""Allow GUI rebuilds to reuse tested, unchanged Go artifacts from this repository."""
import json
import os
import re
import subprocess
import urllib.request
from pathlib import Path


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def job_section(workflow, name):
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [\w-]+:\n|\Z)", workflow)
    if not match:
        raise RuntimeError("Missing workflow job: " + name)
    # Only the new job-level resume condition may differ; commands and pins may not.
    return "\n".join(line for line in match[1].splitlines() if not line.startswith("    if: "))


def main():
    run_id = os.environ["COMMON_RUN_ID"]
    if not run_id.isdigit():
        raise RuntimeError("Common artifact run ID must be numeric")
    repo = os.environ["GITHUB_REPOSITORY"]

    def api(path):
        request = urllib.request.Request(
            "https://api.github.com/repos/" + repo + path,
            headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                     "Accept": "application/vnd.github+json", "User-Agent": "qThrone-build-validation"})
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    run = api("/actions/runs/" + run_id)
    if run["path"] != ".github/workflows/build.yml" or run["status"] != "completed":
        raise RuntimeError("Select a completed qThrone release build")
    jobs = api("/actions/runs/" + run_id + "/jobs?per_page=100")["jobs"]
    for prefix, count in (("build-go (", 9), ("test-qwdtt (", 3), ("build-android", 1)):
        required = [job for job in jobs if job["name"].startswith(prefix)]
        if len(required) != count or any(job["conclusion"] != "success" for job in required):
            raise RuntimeError("Required source build checks did not pass: " + prefix)
    sha = run["head_sha"]
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise RuntimeError("Invalid source commit")
    subprocess.run(["git", "fetch", "origin", sha, "--depth=1"], check=True)
    changed = git("diff", "--name-only", sha, "HEAD").splitlines()
    protected = ("core/", "qwdtt/", "updater/", "3rdparty/")
    exact = {"script/build_go.sh", "version.json", ".gitmodules", ".gitattributes", ".github/workflows/qwdtt-tests.yml"}
    if any(path.startswith(protected) or path in exact for path in changed):
        raise RuntimeError("Go sources or build inputs changed; rebuild them first")
    old = git("show", sha + ":.github/workflows/build.yml")
    new = Path(".github/workflows/build.yml").read_text(encoding="utf-8")
    for name in ("test-qwdtt", "build-go", "build-android"):
        if job_section(old, name) != job_section(new, name):
            raise RuntimeError("Go build/test workflow changed: " + name)
    artifacts = api("/actions/runs/" + run_id + "/artifacts?per_page=100")["artifacts"]
    common = [artifact for artifact in artifacts if artifact["name"].startswith("Throne-" + sha + "-Common-")]
    if len(common) != 9 or any(artifact["expired"] or artifact["size_in_bytes"] <= 0 for artifact in common):
        raise RuntimeError("Complete Go artifact set is not available")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write("common_sha=" + sha + "\n")
    print("Verified 9 Go artifacts and 13 prerequisite checks at " + sha)


if __name__ == "__main__":
    main()
