#!/usr/bin/env python3
"""Exercise manual activation rollback with mocked services/signals and temporary files only."""
import importlib.util
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('session_test', Path(__file__).with_name('session-test.py'))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class ActivationChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='lordos-session-unit-')
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        (base / 'bundle/runs').mkdir(parents=True)
        values = dict(BUNDLE=base / 'bundle', DROPIN=base / 'dropins/ours.conf', USER='unit-user')
        for name, value in values.items():
            p = patch.object(mod, name, value)
            p.start()
            self.addCleanup(p.stop)
        for target, options in [
            ('os.geteuid', dict(return_value=0)),
            ('pwd.getpwnam', dict(return_value=SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid()))),
            ('os.fchown', {}), ('os.chown', {}), ('os.dup2', {}), ('signal.signal', {}),
        ]:
            p = patch.object(getattr(mod, target.split('.')[0]), target.split('.')[1], **options)
            p.start()
            self.addCleanup(p.stop)
        for name in ('verify', 'close_user_session', 'run'):
            p = patch.object(mod, name)
            setattr(self, name, p.start())
            self.addCleanup(p.stop)
        p = patch.dict(os.environ, SUDO_USER='unit-user')
        p.start()
        self.addCleanup(p.stop)

    def test_activation_restores_only_own_override(self):
        mod.DROPIN.parent.mkdir()
        other = mod.DROPIN.parent / 'local-development.conf'
        other.write_text('keep this')
        mod.switch(False)
        self.assertEqual(mod.DROPIN.read_text(), mod.CONF)
        self.assertEqual(other.read_text(), 'keep this')
        # Distinct log filename for the second action already has a distinct prefix.
        self.verify.side_effect = RuntimeError('binary no longer available')
        mod.switch(True)
        self.assertFalse(mod.DROPIN.exists())
        self.assertEqual(other.read_text(), 'keep this')

    def test_unrelated_override_is_refused_before_service_change(self):
        mod.DROPIN.parent.mkdir()
        mod.DROPIN.write_text('unrelated')
        with self.assertRaises(RuntimeError):
            mod.switch(True)
        self.run.assert_not_called()
        self.assertEqual(mod.DROPIN.read_text(), 'unrelated')

    def test_failed_activation_removes_only_own_override(self):
        self.run.side_effect = [None, RuntimeError('reload failed'), None, None]
        with self.assertRaises(RuntimeError):
            mod.switch(False)
        self.assertFalse(mod.DROPIN.exists())
        self.assertEqual(self.run.call_args.args, ('systemctl', 'start', 'lordos.service'))


if __name__ == '__main__':
    unittest.main()
