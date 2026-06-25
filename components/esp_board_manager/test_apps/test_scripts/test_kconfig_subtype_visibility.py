from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from gen_bmgr_config_codes import BoardConfigGenerator


def test_device_subtype_symbols_use_depends_on(tmp_path: Path) -> None:
    project_dir = tmp_path / 'project'
    project_dir.mkdir()
    (project_dir / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.16)\nproject(test_project)\n',
        encoding='utf-8',
    )

    bmgr_root = Path(__file__).resolve().parents[2]
    generator = BoardConfigGenerator(bmgr_root, project_dir=str(project_dir))

    kconfig = generator.generate_nested_kconfig_entry(
        'display_lcd',
        'devices/dev_display_lcd',
        True,
        'n',
        ['rgb_3wire_spi', 'spi'],
    )

    assert 'if ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT' not in kconfig
    assert 'config ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT' in kconfig
    assert 'config ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT' in kconfig
    assert 'depends on ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT' in kconfig


def test_rgb_3wire_spi_subtype_omits_depends_on(tmp_path: Path) -> None:
    project_dir = tmp_path / 'project'
    project_dir.mkdir()
    (project_dir / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.16)\nproject(test_project)\n',
        encoding='utf-8',
    )

    bmgr_root = Path(__file__).resolve().parents[2]
    generator = BoardConfigGenerator(bmgr_root, project_dir=str(project_dir))

    kconfig = generator.generate_nested_kconfig_entry(
        'display_lcd',
        'devices/dev_display_lcd',
        True,
        'n',
        ['rgb_3wire_spi', 'spi'],
    )

    rgb_block = kconfig.split('config ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_3WIRE_SPI_SUPPORT', 1)[1]
    rgb_block = rgb_block.split('config ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT', 1)[0]
    assert 'depends on ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT' not in rgb_block

    spi_block = kconfig.split('config ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT', 1)[1]
    assert 'depends on ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT' in spi_block
