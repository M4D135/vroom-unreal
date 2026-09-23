#!/usr/bin/env python3
"""Portable checks of Windows manifests, argument quoting and runtime evidence.
These fixtures do not execute PowerShell, Unreal or a Windows GPU.
"""
import contextlib
import copy
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Tools'))
import windows_verification as runner
from windows_verification import (validate_manifest,sha256,unreal_command_line,runtime_evidence,
                                  version_tuple,parse_args,gameplay_failures,soak_coverage_failures)

def capture_command_line(executable, arguments, captured):
    # Exercise the real Windows quoting, but let the fake process consume the
    # original arguments: POSIX shlex corrupts unquoted Windows backslashes.
    command = unreal_command_line(executable, arguments)
    captured[command] = [str(executable), *arguments]
    return command

class WindowsVerificationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)/'project';self.archive=Path(self.tmp.name)/'archive'
        (self.root/'Config').mkdir(parents=True);self.archive.mkdir()
        self.ini=self.root/'Config/DefaultGame.ini';self.ini.write_text('ProjectVersion=0.30.0\n')
        self.exe=self.archive/'Vroom.exe';self.exe.write_bytes(b'fixture, not a native executable')
        self.manifest=dict(schema_version=1,version='0.30.0',platform='Win64',configuration='Development',executable='Vroom.exe',
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

    def test_fixture_preserves_windows_and_posix_arguments(self):
        cases = [
            (r'C:\Users\degun\Vroom.exe', r'C:\Users\degun\Temp\neutral.log'),
            (r'C:\Program Files\Vroom.exe', r'C:\Users\David Test\Temp\neutral.log'),
            ('/tmp/vroom/Vroom.exe', '/tmp/vroom/neutral.log'),
            ('/tmp/vroom test/Vroom.exe', '/tmp/vroom test/neutral.log'),
            (r'/tmp/vroom\test/Vroom.exe', r'/tmp/vroom\test/neutral.log'),
        ]
        for executable, log in cases:
            with self.subTest(executable=executable, log=log):
                arguments = ['-d3d12', '-abslog=' + log, '-Name=a&b']
                captured = {}
                command = capture_command_line(executable, arguments, captured)
                self.assertIsInstance(command, str)
                self.assertEqual(command, unreal_command_line(executable, arguments))
                tokens = captured[command]
                self.assertEqual(tokens, [executable, *arguments])
                values = dict(token.split('=', 1) for token in tokens[1:] if '=' in token)
                self.assertEqual(values['-abslog'], log)
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

    def prepare_034(self):
        self.ini.write_text('ProjectVersion=0.34.0\n')
        (self.root/'Vroom.uproject').write_text('{}')
        self.exe=self.archive/'Windows/Vroom/Binaries/Win64/Vroom.exe'
        self.exe.parent.mkdir(parents=True)
        self.exe.write_bytes(b'fixture native game')
        for relative in ('Windows/Vroom.exe','Windows/Vroom/Content/Data/gloves.json',
                         'Windows/Vroom/Content/Paks/Vroom.ucas','Windows/Vroom/Plugins/DLSS/runtime.dll'):
            file=self.archive/relative;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(b'fixture')
        self.manifest.update(schema_version=2,version='0.34.0',package_root='Windows',
                             executable=self.exe.relative_to(self.archive).as_posix(),executable_sha256=sha256(self.exe),
            source_files=[dict(path=p.relative_to(self.root).as_posix(),sha256=sha256(p)) for p in (self.ini,self.root/'Vroom.uproject')],
            package_files=[dict(path=p.relative_to(self.archive).as_posix(),sha256=sha256(p))
                           for p in (self.archive/'Windows').rglob('*') if p.is_file()])

    def test_034_requires_schema_two_and_correct_platform(self):
        self.prepare_034()
        self.assertEqual(validate_manifest(self.manifest,self.root,self.archive,'0.34.0')[1],[])
        for key,value,reason in [('schema_version',1,'schema'),('platform','Mac','platform')]:
            with self.subTest(key=key):
                bad=copy.deepcopy(self.manifest);bad[key]=value
                self.assertTrue(any(reason in error for error in validate_manifest(bad,self.root,self.archive,'0.34.0')[1]))

    def test_package_data_and_plugin_runtime_cannot_change_after_build(self):
        self.prepare_034()
        for relative in ('Windows/Vroom/Content/Data/gloves.json','Windows/Vroom/Content/Paks/Vroom.ucas',
                         'Windows/Vroom/Plugins/DLSS/runtime.dll'):
            with self.subTest(path=relative):
                file=self.archive/relative;before=file.read_bytes();file.write_bytes(b'changed')
                self.assertTrue(any(relative in error for error in validate_manifest(self.manifest,self.root,self.archive,'0.34.0')[1]))
                file.unlink()
                self.assertTrue(any(relative in error for error in validate_manifest(self.manifest,self.root,self.archive,'0.34.0')[1]))
                file.write_bytes(before)
        extra=self.archive/'Windows/extra.dll';extra.write_bytes(b'new')
        self.assertTrue(any('added since' in error for error in validate_manifest(self.manifest,self.root,self.archive,'0.34.0')[1]))

    def test_034_requires_uproject_and_complete_unambiguous_package_manifest(self):
        self.prepare_034()
        for edit in ('missing_uproject','missing_package','duplicate_package','invalid_root','outside_file'):
            with self.subTest(edit=edit):
                bad=copy.deepcopy(self.manifest)
                if edit=='missing_uproject':bad['source_files']=bad['source_files'][:1]
                elif edit=='missing_package':bad.pop('package_files')
                elif edit=='duplicate_package':bad['package_files'].append(bad['package_files'][0])
                elif edit=='invalid_root':bad['package_root']='.'
                else:bad['package_files'].append(dict(path='Vroom.exe',sha256=sha256(self.archive/'Vroom.exe')))
                self.assertTrue(validate_manifest(bad,self.root,self.archive,'0.34.0')[1])
        (self.root/'Vroom.uproject').write_text('{"Plugins":[]}')
        self.assertTrue(any('Vroom.uproject' in error for error in validate_manifest(self.manifest,self.root,self.archive,'0.34.0')[1]))

    def test_new_gameplay_cases_require_their_specific_evidence(self):
        text='VROOM: Version 0.34\nVROOM: Verification PASS\n'
        for name,markers in [('offroad-left',['Offroad restart PASS']),('offroad-right',['Offroad restart PASS']),
                             ('traffic-overtake',['Traffic overtake PASS']),
                             ('drift-expert',['Drift feedback PASS','Expert drift PASS']),
                             ('collision-rub',['Controlled landing quality='])]:
            with self.subTest(name=name):
                self.assertTrue(gameplay_failures(text,name,'0.34.0',[])[0])
                self.assertFalse(gameplay_failures(text+'\n'.join(markers),name,'0.34.0',[])[0])

    def test_start_sequence_and_soak_completion_are_not_generic_pass_markers(self):
        base='VROOM: Version 0.34\nVROOM: Verification PASS\nStart integration PASS\n'
        self.assertTrue(gameplay_failures(base,'start-sequence','0.34.0',[])[0])
        events=''.join(f'VROOM: Start beep signal={i} raceClock={clock}\nVROOM: Start lights raceClock={clock} signal={i}\n'
                       for i,clock in [(0,-2.5),(1,-1),(2,0)])
        self.assertFalse(gameplay_failures(base+events,'start-sequence','0.34.0',[])[0])
        for text in ('VROOM-TOUR track=2 complete=0','VROOM-TOUR track=1 complete=1','VROOM-TOUR track=2 complete=10'):
            self.assertTrue(soak_coverage_failures(text,2))
        self.assertFalse(soak_coverage_failures('VROOM-TOUR track=2 distanceCovered=5000 complete=1\n',2))

    def run_native_fixture(self, failure=None, soak=False):
        """Execute the real runner/gates with only native processes replaced."""
        self.prepare_034()
        (self.archive/'vroom-build.json').write_text(json.dumps(self.manifest))
        csv_dir=self.root/'Saved/Profiling/CSV';csv_dir.mkdir(parents=True)
        args=parse_args(['--project-dir',str(self.root),'--archive-directory',str(self.archive),
                         '--expected-version','0.34.0','--frames','300','--soak' if soak else '--profile'])
        output=io.StringIO();executed=[];captured_commands={}
        def child(command,**kwargs):
            tokens=captured_commands[command];values=dict(token.split('=',1) for token in tokens if '=' in token)
            self.assertIn('-SaveToUserDir',tokens)
            log=Path(values['-abslog']);name=log.stem;executed.append(name)
            text=('VROOM: Version 0.34.0\nVROOM: Verification PASS\n'
                  'LogD3D12RHI: Found D3D12 adapter 0: NVIDIA GeForce RTX 5090 (VendorId: 10de\n'
                  'LogD3D12RHI: Chosen D3D12 Adapter Id = 0\nVROOM: Graphics applied DLSS=0\n')
            text+='\n'.join(runner.REQUIRED_MARKERS.get(name,[]))+'\n'
            if name.startswith('start-'):
                if name=='start-restart':
                    text+='VROOM: Start beep signal=0 raceClock=-2.5\nVROOM: Start lights raceClock=-2.5 signal=0\n'
                    text+='VROOM: Start test restarted during countdown\n'
                text+=''.join(f'VROOM: Start beep signal={i} raceClock={clock}\nVROOM: Start lights raceClock={clock} signal={i}\n'
                              for i,clock in [(0,-2.5),(1,-1),(2,0)])
                if name=='start-pause':text+='Start test resumed after pause\n'
            if '-csvCaptureFrames' in values:
                count=int(values['-csvCaptureFrames']);rows=['16.6667,1,12\n']*count
                if failure=='hitch':rows[-1]='157,1,12\n'
                (csv_dir/(name+'.csv')).write_text('FrameTime,GameThreadTime,GPUTime\n'+''.join(rows)
                    +'[systemresolution.resx],3840,[systemresolution.resy],2160\n')
                text+=''.join(f'VROOM-MOTION dt=0.016667 s={i*.5} yaw=0\n' for i in range(count))
                if '-VroomTrack' in values:
                    text+=f'VROOM-TOUR track={values["-VroomTrack"]} complete={int(failure!="coverage")}\n'
                if failure=='artifact':(self.archive/'Windows/Vroom/Content/Data/gloves.json').write_bytes(b'modified after validation began')
            log.write_text(text)
            if '-VroomCapture' in values:
                Path(values['-VroomCapture']).write_bytes(b'\x89PNG\r\n\x1a\n'+b'\0'*8+struct.pack('>II',3840,2160))
            return SimpleNamespace(returncode=0)
        with mock.patch.object(runner,'parse_args',return_value=args), \
             mock.patch.object(runner,'os',SimpleNamespace(name='nt',environ={'LOCALAPPDATA':str(self.root/'local')})), \
             mock.patch.object(runner,'unreal_command_line',side_effect=lambda executable,arguments:
                               capture_command_line(executable,arguments,captured_commands)), \
             mock.patch.object(runner.subprocess,'run',side_effect=child),contextlib.redirect_stdout(output):
            if failure:
                with self.assertRaises(RuntimeError):runner.main([])
            else:runner.main([])
        report=json.loads(next((self.root/'Saved/Validation').rglob('results.json')).read_text())
        if failure:
            self.assertFalse(report['completed']);self.assertTrue(report['failures'])
        else:
            self.assertTrue(report['completed']);self.assertTrue(all(row['status']=='PASS' for row in report['cases']))
        return report,output.getvalue(),executed

    def test_runner_exercises_034_cases_and_marks_real_profile_pass(self):
        report,output,names=self.run_native_fixture()
        for required in ('offroad-left','offroad-right','pit-service','traffic-overtake','drift-expert',
                         'start-sequence','start-restart','start-pause','start-mute','collision-rub','collision-rear'):
            self.assertIn(required,names)
        self.assertEqual(len(names),33)
        self.assertEqual(output.count('performance: PASS'),1)

    def test_failed_profile_never_prints_pass(self):
        report,output,_=self.run_native_fixture('hitch')
        self.assertEqual(report['cases'][-1]['status'],'FAIL')
        self.assertNotIn('performance: PASS',output)
        self.assertEqual(report['performance'][-1]['report']['frame_time']['max_ms'],157)

    def test_incomplete_soak_never_prints_pass(self):
        report,output,_=self.run_native_fixture('coverage',soak=True)
        self.assertEqual(report['cases'][-1]['status'],'FAIL')
        self.assertEqual(report['failures'][-1]['name'],'soak-track-1-coverage')
        self.assertNotIn('soak-track-1: PASS',output)

    def test_package_changed_during_checks_cannot_complete(self):
        report,_,_=self.run_native_fixture('artifact')
        self.assertEqual(report['failures'][-1]['name'],'artifact-unchanged')

if __name__=='__main__':unittest.main()
