# SPDX-License-Identifier: GPL-2.0-or-later
"""kr-port: which game dump the port supports (replaces bbport's Bloodborne check)."""
import json
from pathlib import Path

SUPPORTED_TITLES = ('CUSA02716',)
SUPPORTED_VERSION = '01.02'
# sha256 of the prepared memory image of eboot v01.02; None until first verified run.
SUPPORTED_IMAGE = None


def problem(game, image_sha256):
    """None if supported, else (kind, detail)."""
    if SUPPORTED_IMAGE and image_sha256 != SUPPORTED_IMAGE:
        return ('image', image_sha256)
    return None


def explain(kind, detail):
    return (f'eboot.bin is not the supported v{SUPPORTED_VERSION} executable of '
            f'{"/".join(SUPPORTED_TITLES)} (image sha256 {detail}).')
