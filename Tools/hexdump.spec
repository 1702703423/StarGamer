# -*- mode: python ; coding: utf-8 -*-
# Build:  cd Tools && python -m PyInstaller hexdump.spec
#         then copy dist/hexdump.exe to Tools/hexdump-py.exe
# console=True keeps the CLI working from a terminal; the GUI is started
# through --gui (or by passing a single file path).


a = Analysis(
    ['hexdump.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='hexdump',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)