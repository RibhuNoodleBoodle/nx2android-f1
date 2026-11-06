import json, subprocess, sys, zipfile
from pathlib import Path
import pytest

from porter import pipeline
from porter.upstream import BuildError


def _dummy_elf() -> bytes:
    return b'\x7fELF\x02\x01\x01\x00' + b'\x00' * 56


def test_driver_extra_so_from_adrenotools_zip(tmp_path):
    zf = tmp_path / 'turnip_v25.zip'
    with zipfile.ZipFile(zf, 'w') as z:
        z.writestr('meta.json', json.dumps({
            'schemaVersion': 1,
            'name': 'Mesa Turnip',
            'driverVersion': '25.1.0-R1',
            'libraryName': 'vulkan.ad06xx.so',
        }))
        z.writestr('vulkan.ad06xx.so', _dummy_elf() + b'MAIN_ICD')
        z.writestr('sub/libcompanion.so', _dummy_elf() + b'COMPANION')

    p = pipeline.Port.__new__(pipeline.Port)
    p.o = pipeline.Options(folder=tmp_path, driver=zf)
    p.aw = tmp_path / 'android'
    p.aw.mkdir()

    extra = p.driver_extra_so()
    main_so = p.aw / 'driver/libcustom_vulkan.so'
    comp_so = p.aw / 'driver/libcompanion.so'
    assert extra == ['--extra-so', str(main_so), '--extra-so', str(comp_so)]
    assert main_so.read_bytes().endswith(b'MAIN_ICD')
    assert comp_so.read_bytes().endswith(b'COMPANION')


def test_driver_extra_so_from_raw_so(tmp_path):
    raw = tmp_path / 'libvulkan_freedreno.so'
    raw.write_bytes(_dummy_elf() + b'RAW_SO')

    p = pipeline.Port.__new__(pipeline.Port)
    p.o = pipeline.Options(folder=tmp_path, driver=raw)
    p.aw = tmp_path / 'android'
    p.aw.mkdir()

    extra = p.driver_extra_so()
    main_so = p.aw / 'driver/libcustom_vulkan.so'
    assert extra == ['--extra-so', str(main_so)]
    assert main_so.read_bytes().endswith(b'RAW_SO')


def test_prepare_custom_driver_mrpurple_adpkg(tmp_path):
    for fname in ('turnip_mrpurple_T30-toasted.adpkg.zip', 'turnip_mrpurple_T30-toasted.adpkg'):
        zf = tmp_path / fname
        with zipfile.ZipFile(zf, 'w') as z:
            z.writestr('meta.json', json.dumps({
                'schemaVersion': 1,
                'name': 'Turnip Adreno Driver T30 (@Mr_Purple_666)',
                'description': 'Compiled from Mesa main Git + Optimization commits',
                'author': 'Mr. Purple',
                'packageVersion': 'T30',
                'vendor': 'Mesa',
                'driverVersion': '26.3.0-T30-1.4.359',
                'minApi': 30,
                'libraryName': 'vulkan.purple.so',
            }))
            z.writestr('vulkan.purple.so', _dummy_elf() + b'PURPLE_T30_ICD')

        out_dir = tmp_path / f'staged_{fname}'
        staged = pipeline.prepare_custom_driver(zf, out_dir)
        assert staged == [out_dir / 'libcustom_vulkan.so']
        assert staged[0].read_bytes().endswith(b'PURPLE_T30_ICD')


def test_driver_extra_so_rejects_non_elf(tmp_path):
    bad = tmp_path / 'not_an_elf.so'
    bad.write_bytes(b'not an elf file')

    p = pipeline.Port.__new__(pipeline.Port)
    p.o = pipeline.Options(folder=tmp_path, driver=bad)
    p.aw = tmp_path / 'android'
    p.aw.mkdir()

    with pytest.raises(BuildError, match='not an ELF'):
        p.driver_extra_so()


def test_cli_port_accepts_driver_flag():
    r = subprocess.run([sys.executable, '-m', 'porter', 'port', '--help'], capture_output=True, text=True, check=True)
    assert '--driver' in r.stdout


def test_driver_extra_so_fallback_to_repo_driver(tmp_path):
    p = pipeline.Port.__new__(pipeline.Port)
    p.o = pipeline.Options(folder=tmp_path, driver=None)
    p.aw = tmp_path / 'android'
    p.aw.mkdir()

    extra = p.driver_extra_so()
    main_so = p.aw / 'driver/libcustom_vulkan.so'
    assert extra == ['--extra-so', str(main_so)]
    assert main_so.is_file()
    assert main_so.read_bytes()[:4] == b'\x7fELF'


def test_driver_extra_so_mandatory_when_missing(tmp_path, monkeypatch):
    p = pipeline.Port.__new__(pipeline.Port)
    p.o = pipeline.Options(folder=tmp_path, driver=None)
    p.aw = tmp_path / 'android'
    p.aw.mkdir()

    fake_dm = tmp_path / 'fake_dm'
    monkeypatch.setattr(pipeline, 'DM', fake_dm)
    with pytest.raises(BuildError, match='Turnip Vulkan driver is mandatory'):
        p.driver_extra_so()
