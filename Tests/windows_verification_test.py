#!/usr/bin/env python3
"""Portable checks of Windows manifests, argument quoting and runtime evidence.
These fixtures do not execute PowerShell, Unreal or a Windows GPU.
"""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Tools'))
from windows_verification import validate_manifest,sha256,unreal_command_line,runtime_evidence,version_tuple,parse_args

class WindowsVerificationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)/'project';self.archive=Path(self.tmp.name)/'archive'
        (self.root/'Config').mkdir(parents=True);self.archive.mkdir()
        self.ini=self.root/'Config/DefaultGame.ini';self.ini.write_text('ProjectVersion=0.30.0\n')
        self.exe=self.archive/'Vroom.exe';self.exe.write_bytes(b'fixture, not a native executable')
        self.manifest=dict(schema_version=1,version='0.30.0',configuration='Development',executable='Vroom.exe',
            executable_sha256=sha256(self.exe),source_files=[dict(path='Config/DefaultGame.ini',sha256=sha256(self.ini))])
    def test_matching_manifest(self):
        exe,errors=validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
        self.assertEqual(exe,self.exe.resolve());self.assertEqual(errors,[])
    def test_stale_or_changed_binary(self):
        self.exe.write_bytes(b'other executable')
        _,errors=validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
        self.assertTrue(any('executable' in e for e in errors))
    def test_changed_source(self):
        self.ini.write_text('ProjectVersion=0.30.0\nChanged=1\n')
        _,errors=validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
        self.assertTrue(any('changed since' in e for e in errors))
    def test_new_source_requires_rebuild(self):
        (self.root/'Source').mkdir();(self.root/'Source/new.cpp').write_text('// added after packaging')
        _,errors=validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
        self.assertTrue(any('added since' in e for e in errors))
    def test_shipping_and_version_mismatch_fail(self):
        self.manifest.update(configuration='Shipping',version='0.29.0')
        _,errors=validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
        self.assertEqual(len(errors),2)
    def test_manifest_path_escape_rejected(self):
        self.manifest['executable']='../Vroom.exe'
        with self.assertRaises(ValueError):validate_manifest(self.manifest,self.root,self.archive,'0.30.0')
    def test_ue_key_value_quoting_has_no_shell(self):
        command=unreal_command_line('C:\\Program Files\\Vroom.exe',['-d3d12','-abslog=C:\\Users\\David Test\\a.log','-Name=a&b'])
        self.assertEqual(command,'"C:\\Program Files\\Vroom.exe" -d3d12 -abslog="C:\\Users\\David Test\\a.log" -Name=a&b')
        with self.assertRaises(ValueError):unreal_command_line('game.exe',['-bad="quote"'])
        with self.assertRaises(ValueError):unreal_command_line('game.exe',['-bad=\n'])
    def test_selected_adapter_not_merely_present_gpu(self):
        text='''LogD3D12RHI: Found D3D12 adapter 0: Intel Graphics (VendorId: 8086, DeviceId: 01
LogD3D12RHI: Found D3D12 adapter 1: NVIDIA GeForce RTX 5090 (VendorId: 10de, DeviceId: 02
LogD3D12RHI: Chosen D3D12 Adapter Id = 0
LogTemp: VROOM: Graphics applied scale=100 AA=4 DLSS=0 saved=0'''
        evidence=runtime_evidence(text)
        self.assertEqual(evidence['selected_gpu'],'Intel Graphics')
        self.assertEqual(evidence['dlss_game_reported_effective_mode'],'native')
        self.assertEqual(evidence['rhi'],'D3D12')
    def test_final_effective_settings_after_dlss_fallback(self):
        text='VROOM: Graphics applied scale=66.7 AA=2 DLSS=2 saved=0\nDLSS activation refused\nVROOM: Graphics applied scale=100 AA=4 DLSS=0 saved=0'
        self.assertEqual(runtime_evidence(text)['dlss_game_reported_effective_mode'],'native')
        self.assertIsNone(runtime_evidence('DLSS plugin found')['dlss_game_reported_effective_mode'])
    def test_explicit_version_and_threshold_arguments(self):
        self.assertEqual(version_tuple('0.30'),version_tuple('0.30.0'))
        with self.assertRaises(ValueError):version_tuple('current')
        args=parse_args(['--expected-version','0.30.0','--target-fps','120','--min-mean-fps','118'])
        self.assertEqual(args.target_fps,120);self.assertEqual(args.min_mean_fps,118)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parse_args(['--expected-version','0.30.0','--frames','120'])

if __name__=='__main__':unittest.main()
