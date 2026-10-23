import copy
import unittest

from verify_common_artifacts import CPP_REUSE_SUFFIXES, verify_cpp_artifacts, verify_pack_artifacts, verify_release_artifacts


class DesktopResumeTests(unittest.TestCase):
    def setUp(self):
        self.sha = 'a' * 40
        self.jobs = [{'name': 'verify-common', 'conclusion': 'success'}] + [
            {'name': 'build-cpp (runner, qt, arch, ' + suffix + ', ' + suffix.replace('-system-qt', '') + ', cache)', 'conclusion': 'success'}
            for suffix in CPP_REUSE_SUFFIXES]
        self.artifacts = [{'name': 'Throne-' + self.sha + '-' + suffix, 'expired': False, 'size_in_bytes': 100}
                          for suffix in CPP_REUSE_SUFFIXES]

    def test_valid_completed_binaries_can_be_reused(self):
        verify_cpp_artifacts(self.sha, self.jobs, self.artifacts, ['.github/workflows/build.yml'])

    def test_application_or_version_changes_require_rebuild(self):
        for path in ['src/ui/mainwindow.cpp', 'version.json', 'transport-sources.lock.json']:
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                verify_cpp_artifacts(self.sha, self.jobs, self.artifacts, [path])

    def test_failed_or_unverified_jobs_cannot_be_reused(self):
        for index in [0, 1]:
            jobs = copy.deepcopy(self.jobs)
            jobs[index]['conclusion'] = 'failure'
            with self.subTest(index=index), self.assertRaises(RuntimeError):
                verify_cpp_artifacts(self.sha, jobs, self.artifacts, [])

    def test_missing_expired_empty_or_foreign_artifacts_are_rejected(self):
        for field, value in [('name', 'foreign-artifact'), ('expired', True), ('size_in_bytes', 0)]:
            artifacts = copy.deepcopy(self.artifacts)
            artifacts[0][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                verify_cpp_artifacts(self.sha, self.jobs, artifacts, [])
        with self.assertRaises(RuntimeError):
            verify_cpp_artifacts(self.sha, self.jobs, self.artifacts[1:], [])
        with self.assertRaises(RuntimeError):
            verify_cpp_artifacts(self.sha, self.jobs, self.artifacts + [{'name': 'Throne-' + self.sha + '-darwin-arm64'}], [])


class StablePublishTests(unittest.TestCase):
    def setUp(self):
        self.sha = 'b' * 40
        names = ['Pack windows', 'Pack linux-amd64', 'Pack linux-arm64', 'Pack macos', 'Pack debug',
                 'Verify portable Windows upgrade', 'Pack and verify Arch Linux x86_64',
                 'build-cpp (macos-15, qt, arm64, darwin-arm64, common)']
        self.jobs = [{'name': name, 'conclusion': 'success'} for name in names]
        self.jobs.append({'name': 'Publish Release', 'conclusion': 'failure'})
        self.artifacts = [{'name': 'Deployment-' + self.sha, 'expired': False, 'size_in_bytes': 100}]

    def test_verified_packages_publish_without_rebuilding(self):
        verify_release_artifacts(self.sha, self.jobs, self.artifacts, ['.github/workflows/publish-verified.yml'])

    def test_failed_checks_block_publication(self):
        for index in range(len(self.jobs) - 1):
            jobs = copy.deepcopy(self.jobs)
            jobs[index]['conclusion'] = 'failure'
            with self.subTest(index=index), self.assertRaises(RuntimeError):
                verify_release_artifacts(self.sha, jobs, self.artifacts, [])

    def test_application_changes_or_missing_packages_block_publication(self):
        with self.assertRaises(RuntimeError):
            verify_release_artifacts(self.sha, self.jobs, self.artifacts, ['version.json'])
        with self.assertRaises(RuntimeError):
            verify_release_artifacts(self.sha, self.jobs, [], [])

    def test_verified_pack_resume_can_publish_without_rebuilding(self):
        jobs = [job for job in self.jobs if job['name'] not in ('Pack windows', 'Pack linux-amd64', 'Pack linux-arm64') and not job['name'].startswith('build-cpp (')]
        jobs.append({'name': 'verify-pack', 'conclusion': 'success'})
        verify_release_artifacts(self.sha, jobs, self.artifacts, [])
        jobs[-1]['conclusion'] = 'failure'
        with self.assertRaises(RuntimeError):
            verify_release_artifacts(self.sha, jobs, self.artifacts, [])


class PackageResumeTests(unittest.TestCase):
    def setUp(self):
        self.sha = 'c' * 40
        self.jobs = [{'name': 'verify-common', 'conclusion': 'success'},
                     {'name': 'build-cpp (macos-15, qt, arm64, darwin-arm64, common)', 'conclusion': 'success'}]
        self.artifacts = [{'name': 'Throne-' + self.sha + '-darwin-arm64', 'expired': False, 'size_in_bytes': 10}]
        for target in ('windows', 'linux-amd64', 'linux-arm64'):
            self.jobs.append({'name': 'Pack ' + target, 'conclusion': 'success'})
            self.artifacts.append({'name': 'Pack-' + self.sha + '-' + target, 'expired': False, 'size_in_bytes': 10})

    def test_verified_packages_and_arm_binary_are_reused(self):
        verify_pack_artifacts(self.sha, self.jobs, self.artifacts, ['.github/workflows/build.yml'])

    def test_missing_or_failed_packages_cannot_be_reused(self):
        with self.assertRaises(RuntimeError):
            verify_pack_artifacts(self.sha, self.jobs, self.artifacts[:-1], [])
        self.jobs[-1]['conclusion'] = 'failure'
        with self.assertRaises(RuntimeError):
            verify_pack_artifacts(self.sha, self.jobs, self.artifacts, [])


if __name__ == '__main__':
    unittest.main()
