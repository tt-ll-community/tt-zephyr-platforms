#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import pyluwen
import sys
import time

from pathlib import Path
from twister_harness import DeviceAdapter

# Import tt_boot_fs utilities
sys.path.append(str(Path(__file__).parents[3] / "scripts"))

import tt_boot_fs

from pcie_utils import rescan_pcie

logger = logging.getLogger(__name__)

# Constant memory addresses we can read from SMC
ARC_POSTCODE_STATUS = 0x80030060
# Boot status register
ARC_BOOT_STATUS = 0x80030408


def read_boot_status():
    """
    Helper to read the PCIe status register
    """
    chips = pyluwen.detect_chips()
    if len(chips) == 0:
        raise RuntimeError("PCIe card was not detected on this system")
    chip = chips[0]
    try:
        status = chip.axi_read32(ARC_POSTCODE_STATUS)
    except Exception:
        print("Warning- SMC firmware requires a reset. Rescanning PCIe bus")
        rescan_pcie()
        status = chip.axi_read32(ARC_POSTCODE_STATUS)
    assert (status & 0xFFFF0000) == 0xC0DE0000, "SMC firmware postcode is invalid"
    # Check post code status of firmware
    assert (status & 0xFFFF) >= 0x1D, "SMC firmware boot failed"
    return chip.axi_read32(ARC_BOOT_STATUS)


def test_recovery_cmfw(unlaunched_dut: DeviceAdapter):
    """
    Tests flashing a bad base CMFW, and makes sure the SMC boots the recovery
    CMFW. Verifies the recovery CMFW is running, and then flashes a working CMFW
    back to the card
    """
    # Get the build directory of the DUT
    build_dir = unlaunched_dut.device_config.build_dir
    # Get the path to base tt_boot_fs.bin
    boot_fs = build_dir / "tt_boot_fs.bin"
    patched_fs = build_dir / "tt_boot_fs_patched.bin"
    assert boot_fs.exists(), f"tt_boot_fs.bin not found at {boot_fs}"
    with open(boot_fs, "rb") as f:
        bootfs_data = f.read()
    fs = tt_boot_fs.BootFs.from_binary(bootfs_data)
    # Write copy of tt_boot_fs to new file
    with open(patched_fs, "wb") as f:
        f.write(bootfs_data)
    # Get offset of base CMFW
    cmfw_offset = fs.entries["cmfw"].spi_addr
    # Write bad data to base CMFW
    with open(patched_fs, "r+b") as f:
        f.seek(cmfw_offset)
        f.write(b"BAD DATA")
    # Make bundle from damaged CMFW
    tt_boot_fs.mkbundle(patched_fs, build_dir / "tt_boot_fs_patched.bundle", "P100-1")
    # Flash the damaged CMFW
    unlaunched_dut.command = [
        unlaunched_dut.west,
        "flash",
        "--build-dir",
        str(build_dir),
        "--runner",
        "tt_flash",
        "--force",
        "--skip-rebuild",
        "--file",
        str(build_dir / "tt_boot_fs_patched.bundle"),
    ]
    unlaunched_dut._flash_and_run()
    time.sleep(1)
    assert (read_boot_status() & 0x78) == 0x8, "Recovery firmware should be active"

    # Flash the good CMFW back. Note- this requires an up to date version of tt-flash
    unlaunched_dut.command = [
        unlaunched_dut.west,
        "flash",
        "--build-dir",
        str(build_dir),
        "--runner",
        "tt_flash",
        "--force",
        "--skip-rebuild",
    ]
    unlaunched_dut._flash_and_run()
    time.sleep(1)
    assert (
        read_boot_status() & 0x78
    ) == 0x0, "Recovery firmware should no longer be active"
