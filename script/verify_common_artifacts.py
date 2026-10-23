"""Allow GUI rebuilds to reuse tested, unchanged Go artifacts from this repository."""
import json
import os
import re
import subprocess
import time
import urllib.request
from pathlib import Path


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def job_section(workflow, name):
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [\w-]+:\n|\Z)", workflow)
    if not match:
        raise RuntimeError("Missing workflow job: " + name)
    # Display names and resume conditions do not alter commands or build inputs.
    return "\n".join(line for line in match[1].splitlines() if not line.startswith(("    if: ", "    name: ")))


CPP_REUSE_SUFFIXES = ("windows-amd64", "windows-arm64", "windowslegacy-amd64", "windowslegacy-386",
                      "linux-amd64", "linux-amd64-system-qt", "linux-arm64", "linux-arm64-system-qt",
                      "darwin-amd64", "darwinlegacy-amd64")
CI_ONLY_FILES = {".github/workflows/build.yml", ".github/workflows/publish-verified.yml",
                 "script/verify_common_artifacts.py", "script/test_verify_common_artifacts.py"}


def verify_cpp_artifacts(sha, jobs, artifacts, changed):
    if set(changed) - CI_ONLY_FILES:
        raise RuntimeError("Application sources changed; rebuild desktop artifacts")
    if not any(job["name"] == "verify-common" and job["conclusion"] == "success" for job in jobs):
        raise RuntimeError("The source GUI build must have verified its common artifacts")
    expected = {"Throne-" + sha + "-" + suffix for suffix in CPP_REUSE_SUFFIXES}
    if {artifact["name"] for artifact in artifacts if artifact["name"].startswith("Throne-" + sha + "-")} != expected:
        raise RuntimeError("Source must contain exactly the 10 unchanged desktop artifacts")
    for suffix in CPP_REUSE_SUFFIXES:
        matching = [job for job in jobs if job["name"].startswith("build-cpp (") and job["name"].split(", ")[3:4] == [suffix]]
        if len(matching) != 1 or matching[0]["conclusion"] != "success":
            raise RuntimeError("Desktop build did not pass: " + suffix)
        matching = [artifact for artifact in artifacts if artifact["name"] == "Throne-" + sha + "-" + suffix]
        if len(matching) != 1 or matching[0]["expired"] or matching[0]["size_in_bytes"] <= 0:
            raise RuntimeError("Desktop artifact unavailable: " + suffix)


def verify_release_artifacts(sha, jobs, artifacts, changed):
    if set(changed) - CI_ONLY_FILES:
        raise RuntimeError("Application sources differ from the tested release packages")
    required = {"Pack windows", "Pack linux-amd64", "Pack linux-arm64", "Pack macos", "Pack debug",
                "Verify portable Windows upgrade", "Pack and verify Arch Linux x86_64"}
    for name in required:
        matching = [job for job in jobs if job["name"] == name]
        if len(matching) != 1 or matching[0]["conclusion"] != "success":
            raise RuntimeError("Release check did not pass: " + name)
    cpp = [job for job in jobs if job["name"].startswith("build-cpp (")]
    if not cpp or any(job["conclusion"] != "success" for job in cpp):
        raise RuntimeError("Desktop build did not pass")
    if any(job["conclusion"] not in ("success", "skipped") and job["name"] != "Publish Release" for job in jobs):
        raise RuntimeError("A prerequisite failed")
    deployment = [artifact for artifact in artifacts if artifact["name"] == "Deployment-" + sha]
    if len(deployment) != 1 or deployment[0]["expired"] or deployment[0]["size_in_bytes"] <= 0:
        raise RuntimeError("Verified deployment artifact is unavailable")


def main():
    cpp_mode = bool(os.environ.get("CPP_RUN_ID"))
    release_mode = bool(os.environ.get("RELEASE_RUN_ID"))
    run_id = os.environ["RELEASE_RUN_ID"] if release_mode else (os.environ["CPP_RUN_ID"] if cpp_mode else os.environ["COMMON_RUN_ID"])
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
    if run.get("event") != "workflow_dispatch" or run.get("head_repository", {}).get("full_name") != repo:
        raise RuntimeError("Artifacts must come from a release build dispatched in this repository")
    if release_mode:
        for _ in range(12):
            if run["status"] == "completed":
                break
            time.sleep(5)
            run = api("/actions/runs/" + run_id)
    if run["path"] != ".github/workflows/build.yml" or run["status"] != "completed":
        raise RuntimeError("Select a completed qThrone release build")
    jobs = api("/actions/runs/" + run_id + "/jobs?per_page=100")["jobs"]
    if cpp_mode or release_mode:
        sha = run["head_sha"]
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            raise RuntimeError("Invalid source commit")
        subprocess.run(["git", "fetch", "origin", sha, "--depth=1"], check=True)
        changed = git("diff", "--name-only", sha, "HEAD").splitlines()
        artifacts = api("/actions/runs/" + run_id + "/artifacts?per_page=100")["artifacts"]
        if release_mode:
            verify_release_artifacts(sha, jobs, artifacts, changed)
        else:
            verify_cpp_artifacts(sha, jobs, artifacts, changed)
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(("release_sha=" if release_mode else "cpp_sha=") + sha + "\n")
        print("Verified release packages" if release_mode else "Verified 10 unchanged desktop artifacts; rebuilding macOS ARM64 only")
        return
    for prefix, count in (("build-go (", 9), (("test-qwdtt (", "qWDTT and CSQTT tests ("), 3), ("build-android", 1)):
        required = [job for job in jobs if job["name"].startswith(prefix)]
        if len(required) != count or any(job["conclusion"] != "success" for job in required):
            raise RuntimeError("Required source build checks did not pass: " + str(prefix))
    sha = run["head_sha"]
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise RuntimeError("Invalid source commit")
    subprocess.run(["git", "fetch", "origin", sha, "--depth=1"], check=True)
    changed = git("diff", "--name-only", sha, "HEAD").splitlines()
    protected = ("core/", "qwdtt/", "sstp/", "updater/", "3rdparty/", "script/openflux/", "script/olcrtc/", "script/dnstun/", "script/masterdns/", "script/s3bypass/")
    exact = {"script/build_go.sh", "script/build_openflux.sh", "script/build_olcrtc.sh", "script/build_dnstunnels.sh", "script/build_wdttplus.sh", "script/fetch_pinned_source.py", "transport-sources.lock.json", ".gitmodules", ".gitattributes", ".github/workflows/qwdtt-tests.yml"}
    if any(path.startswith(protected) or path in exact for path in changed):
        raise RuntimeError("Go sources or build inputs changed; rebuild them first")
    if "version.json" in changed:
        # The application version is a Qt build input; Go binaries contain their dependency versions only.
        previous = json.loads(git("show", sha + ":version.json"))
        current = json.loads(Path("version.json").read_text(encoding="utf-8"))
        for metadata in (previous, current):
            version = metadata.pop("version", None)
            if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?(?:-(?:alpha|beta|rc)\.\d+)?", version):
                raise RuntimeError("Invalid application version")
        if previous != current:
            raise RuntimeError("Upstream version metadata changed; rebuild first")
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
