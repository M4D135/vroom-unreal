import ctypes, json, os, subprocess, sys, time
from ctypes import wintypes as W
from pathlib import Path
project, archive, out = map(Path, sys.argv[1:4])
sys.path.insert(0, str(project/'Tools'))
from windows_verification import validate_manifest, gameplay_failures, runtime_evidence, unreal_command_line
from verify_mac import REQUIRED_MARKERS
assert os.name == 'nt'
out.mkdir(parents=True, exist_ok=False)
manifest = json.loads((archive/'vroom-build.json').read_text(encoding='utf-8-sig'))
exe, errors = validate_manifest(manifest, project, archive, '0.35.0')
assert not errors, errors
kernel, user = ctypes.windll.kernel32, ctypes.windll.user32
session = W.DWORD()
assert kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session))
assert session.value == kernel.WTSGetActiveConsoleSessionId() and session.value != 0
CALLBACK = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.EnumWindows.argtypes = [CALLBACK, W.LPARAM]
user.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
user.IsWindowVisible.argtypes = [W.HWND]
user.ShowWindow.argtypes = [W.HWND, ctypes.c_int]
user.SetForegroundWindow.argtypes = [W.HWND]
user.GetForegroundWindow.restype = W.HWND
report = dict(version='0.35.0', platform='Windows', scope='targeted-gameplay', completed=False,
    performance_retested=False, session=session.value, executable_sha256=manifest['executable_sha256'], cases=[], failures=[])
def save(): (out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
cases = [
 ('drift-straight',['-VroomDriftTest','-VroomDriftScenario=straight','-VroomStartDistance=350','-VroomStartSpeed=40'],5,1.5),
 ('drift-keyboard',['-VroomDriftTest','-VroomDriftScenario=keyboard','-VroomStartSpeed=40'],5,2.45),
 ('drift-expert',['-VroomDriftTest','-VroomDriftScenario=expert','-VroomStartSpeed=40'],5,2.7),
 ('pit-service',['-VroomPitTest','-VroomChaseCamera'],9,8),
 ('collision-crash',['-VroomCollisionTest=crash','-VroomChaseCamera'],8,.45),
 ('collision-rub',['-VroomCollisionTest=rub','-VroomChaseCamera'],5,.25),
 ('traffic-overtake',['-VroomTrafficTest'],15,3),
 ('start-sequence',['-VroomStartTest'],6,2),
 ('start-pause',['-VroomStartTest','-VroomStartScenario=pause'],8,4),
 ('start-mute',['-VroomStartTest','-VroomStartScenario=mute'],8,4),
 ('neutral',['-VroomAutoDemo','-VroomStartDistance=350','-VroomStartSpeed=0','-VroomPreviewSteering=0'],5,3),
]
save()
for name, flags, seconds, capture_at in cases:
 log, png = out/(name+'.log'), out/(name+'.png')
 args=['-NoSplash','-d3d12','-SaveToUserDir','-windowed','-ForceRes','-ResX=1920','-ResY=1080',
   '-VroomVerify','-VroomGraphicsPreset=3',f'-VroomExitAfter={seconds}',f'-abslog={log}',f'-VroomCapture={png}',f'-VroomCaptureAfter={capture_at}',*flags]
 row=dict(name=name,offscreen=False,status='RUNNING',arguments=args);report['cases'].append(row);save()
 try:
  with (out/(name+'-stdout.log')).open('w') as stream:
   p=subprocess.Popen(unreal_command_line(exe,args),executable=str(exe),cwd=exe.parent,stdout=stream,stderr=subprocess.STDOUT)
   row['pid']=p.pid; row['visible_window']=False; row['foreground']=False
   try:
    deadline=time.monotonic()+60
    while p.poll() is None and time.monotonic()<deadline:
     windows=[]
     @CALLBACK
     def visit(hwnd,_):
      owner=W.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
      if owner.value==p.pid and user.IsWindowVisible(hwnd): windows.append(hwnd)
      return True
     user.EnumWindows(visit,0)
     if windows:
      hwnd=windows[0];user.ShowWindow(hwnd,9);user.SetForegroundWindow(hwnd)
      row['visible_window']=True;row['foreground']=user.GetForegroundWindow()==hwnd;break
     time.sleep(.1)
    row['exit']=p.wait(timeout=seconds+120)
   finally:
    if p.poll() is None: p.terminate();p.wait(timeout=10)
  text=log.read_text(encoding='utf-8-sig',errors='replace') if log.exists() else ''
  reasons, checks = gameplay_failures(text,name,'0.35.0',flags)
  runtime = runtime_evidence(text);row.update(checks=checks,runtime=runtime,capture=str(png))
  if row['exit']!=0: reasons.append('Nonzero native exit')
  if not row['visible_window']: reasons.append('No visible game window')
  if not png.is_file(): reasons.append('Missing capture')
  if runtime.get('rhi')!='D3D12' or 'RTX 5060' not in (runtime.get('selected_gpu') or ''): reasons.append('Wrong RHI/GPU')
  reasons.extend(line for line in text.splitlines() if 'VROOM:' in line and ' FAIL' in line)
  row['errors']=reasons;row['status']='FAIL' if reasons else 'PASS'
  if reasons: report['failures'].append(dict(name=name,reasons=reasons))
 except Exception as exc:
  row.update(status='FAIL',error=str(exc));report['failures'].append(dict(name=name,error=str(exc)))
 save()
 _, errors = validate_manifest(manifest,project,archive,'0.35.0')
 if errors: report['failures'].append(dict(name='package-integrity',reasons=errors));save();break
report['completed']=len(report['cases'])==len(cases) and not report['failures'];save()
sys.exit(0 if report['completed'] else 1)
