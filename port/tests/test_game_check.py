# SPDX-License-Identifier: GPL-2.0-or-later
"""kr-port: the supported-game check (scripts/game_check.py)."""
from paths import ROOT  # noqa: F401 (puts scripts/ on sys.path)
import unittest
from unittest import mock

import game_check


class GameCheckTests(unittest.TestCase):
    def test_supported_title_and_version(self):
        self.assertEqual(game_check.SUPPORTED_TITLES, ('CUSA02716',))
        self.assertEqual(game_check.SUPPORTED_VERSION, '01.02')

    def test_any_image_passes_until_one_is_recorded(self):
        with mock.patch.object(game_check, 'SUPPORTED_IMAGE', None):
            self.assertIsNone(game_check.problem('game', 'ab' * 32))

    def test_recorded_image_is_enforced(self):
        with mock.patch.object(game_check, 'SUPPORTED_IMAGE', 'aa' * 32):
            self.assertIsNone(game_check.problem('game', 'aa' * 32))
            self.assertEqual(game_check.problem('game', 'bb' * 32), ('image', 'bb' * 32))

    def test_explanation_names_the_supported_game(self):
        text = game_check.explain('image', 'bb' * 32)
        self.assertIn('CUSA02716', text)
        self.assertIn('v01.02', text)
        self.assertIn('bb' * 32, text)


if __name__ == '__main__':
    unittest.main()
