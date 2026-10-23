import copy
import unittest

from verify_common_artifacts import CPP_REUSE_SUFFIXES, verify_cpp_artifacts


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


if __name__ == '__main__':
    unittest.main()
