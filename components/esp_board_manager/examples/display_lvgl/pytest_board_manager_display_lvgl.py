# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import os

from pytest_embedded import Dut

@pytest.mark.esp32s3
def test_bmgr_display_lvgl_str_detect(dut: Dut)-> None:
    dut.expect(r'Example Finished\. Exiting app_main\.\.\.', timeout=60)
