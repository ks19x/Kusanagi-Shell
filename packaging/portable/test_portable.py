#!/usr/bin/env python3
"""Check relocation, safe registration and isolation from the host environment."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PortableTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kusanagi portable ')
        self.base = Path(self.temp.name)
        self.bundle = self.base / 'bundle with spaces'
        (self.bundle / 'usr/bin').mkdir(parents=True)
        (self.bundle / 'usr/libexec').mkdir()
        for name in ('kusanagi', 'install'):
            shutil.copy2(ROOT / 'packaging/portable' / name, self.bundle / name)
        for name in ('kusanagi', 'kusanagi-shell'):
            path = self.bundle / 'usr/bin' / name
            path.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            path.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.base / 'home'), XDG_BIN_HOME=str(self.base / 'commands'))

    def tearDown(self):
        self.temp.cleanup()

    def test_relocated_launcher_preserves_arguments(self):
        moved = self.base / 'moved bundle'
        self.bundle.rename(moved)
        output = subprocess.check_output([moved / 'kusanagi', 'settings', 'a b'], text=True)
        self.assertEqual(output, 'settings\na b\n')

    def test_install_is_idempotent_and_refuses_conflicts(self):
        command = [self.bundle / 'install']
        subprocess.run(command, env=self.env, check=True, capture_output=True)
        subprocess.run(command, env=self.env, check=True, capture_output=True)
        link = Path(self.env['XDG_BIN_HOME']) / 'kusanagi'
        self.assertEqual(link.resolve(), self.bundle / 'usr/bin/kusanagi')
        link.unlink()
        link.write_text('existing user file')
        result = subprocess.run(command, env=self.env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(link.read_text(), 'existing user file')

    def test_shell_wrapper_does_not_export_library_path(self):
        shutil.copy2(ROOT / 'packaging/portable/launch-shell', self.bundle / 'usr/bin/kusanagi-shell')
        child = self.bundle / 'usr/libexec/kusanagi-shell'
        child.write_text('#!/bin/sh\nprintenv\n')
        child.chmod(0o755)
        env = dict(self.env, PIPEWIRE_MODULE_DIR='/host/modules')
        env.pop('LD_LIBRARY_PATH', None)
        output = subprocess.check_output([self.bundle / 'usr/bin/kusanagi-shell'], env=env, text=True)
        values = dict(line.split('=', 1) for line in output.splitlines())
        self.assertEqual(values['KUSANAGI_HOST_PIPEWIRE_MODULE_DIR'], '/host/modules')
        self.assertEqual(values['PIPEWIRE_MODULE_DIR'], str(self.bundle / 'usr/lib/pipewire-0.3'))
        self.assertNotIn('LD_LIBRARY_PATH', values)

    def test_native_child_environment_restores_host_values(self):
        source = self.base / 'test.cpp'
        source.write_text(r'''
#include "core/process/portable_environment.h"
#include <cassert>
#include <cstring>
int main() {
  setenv("PIPEWIRE_MODULE_DIR", "/ordinary/host", 1);
  process::portable::restoreHostEnvironment();
  assert(std::strcmp(getenv("PIPEWIRE_MODULE_DIR"), "/ordinary/host") == 0);
  setenv("KUSANAGI_PORTABLE", "1", 1);
  setenv("KUSANAGI_HOST_PIPEWIRE_MODULE_DIR", "/host/modules", 1);
  setenv("PIPEWIRE_MODULE_DIR", "/bundle/modules", 1);
  unsetenv("KUSANAGI_HOST_SPA_PLUGIN_DIR");
  setenv("SPA_PLUGIN_DIR", "/bundle/spa", 1);
  assert(process::portable::omitFromHostEnvironment("SPA_PLUGIN_DIR"));
  assert(!process::portable::omitFromHostEnvironment("PIPEWIRE_MODULE_DIR"));
  assert(!process::portable::omitFromHostEnvironment("HOME"));
  process::portable::restoreHostEnvironment();
  assert(std::strcmp(getenv("PIPEWIRE_MODULE_DIR"), "/host/modules") == 0);
  assert(getenv("SPA_PLUGIN_DIR") == nullptr);
  assert(getenv("KUSANAGI_HOST_PIPEWIRE_MODULE_DIR") == nullptr);
  assert(getenv("KUSANAGI_PORTABLE") == nullptr);
}
''')
        binary = self.base / 'test'
        subprocess.run(['g++', '-std=c++20', '-I', str(ROOT / 'native/src'), str(source), '-o', str(binary)], check=True)
        subprocess.run([binary], check=True)


if __name__ == '__main__':
    unittest.main()
