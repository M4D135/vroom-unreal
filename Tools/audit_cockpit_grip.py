#!/usr/bin/env python3
"""Conservative all-face audit of the current cockpit rim against both gloves.

No NumPy, rendering, vertex-only clearance claim, or baked rim-clearance metadata.
This adapter supports explicitly reviewed C++ rim constructions. It refuses a
changed rim/pose function, even if old saved proof files still exist. Changes to
unrelated dashboard/camera code do not invalidate the adapter: both hands and
rim share the same rigid camera-independent tilt and steering transform.

python3 Tools/audit_cockpit_grip.py --output Saved/Validation/v034/classic-rim-audit.json
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED_FUNCTION_HASHES = {'void ClassicRim()': '6f79b0ba31f56556b92be5811cd9d0560b334825709ed9b6bf06b56953ce55f9', 'FDrivingHandPose DrivingHandPose(': 'f194295e54e5d710ebfdf234135d57fa571af6bf40bab6ef2f918875e49c7852'}
SUPPORTED_FUNCTION_HASHES['if (FFileHelper::LoadFileToString(HandData'] = '1e98d47b527d386443c0c96b61d5218e684a9fd36916a103f607342b0d8e5176'
CIRCULAR_RIM_FUNCTION_HASH = 'ff6b7367668a046fe88bb35c0c059203560be2428d6d06ac8a707b35cc31836a'


def normalized_function(source, signature):
    start = source.index(signature)
    first = source.index('{', start)
    depth, end = 1, first + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return re.sub(r'\s+', '', re.sub(r'//[^\n]*|/\*.*?\*/', '', source[first:end], flags=re.S))


def source_parameters(source, glove):
    functions = {}
    for signature, expected in SUPPORTED_FUNCTION_HASHES.items():
        try:
            body = normalized_function(source, signature)
        except (ValueError, IndexError) as error:
            raise ValueError('Unsupported cockpit geometry: missing ' + signature) from error
        digest = hashlib.sha256(body.encode()).hexdigest()
        supported = {expected}
        if signature == 'void ClassicRim()':
            supported.add(CIRCULAR_RIM_FUNCTION_HASH)
        if digest not in supported:
            raise ValueError('Unsupported cockpit geometry changed: ' + signature
                             + '; review the source adapter before certifying new geometry')
        functions[signature] = digest
    compact = re.sub(r'\s+', '', re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S))
    for expression in (
        'auto*WheelMesh=Mesh(TEXT("SteeringWheel"),SteeringPivot);',
        'WheelMesh->SetRelativeScale3D(FVector(WheelScale));',
        'SteeringPivot->SetRelativeRotation(WheelRestTilt*Turn);',
        'CockpitMeshes[LeftGlove+I]->SetRelativeTransform(FTransform(Hand.Rotation,Hand.Wrist,FVector(Hand.Scale)));',
    ):
        if expression not in compact:
            raise ValueError('Unsupported mesh attachment/transform: ' + expression)
    number = r'(-?(?:\d+(?:\.\d*)?|\.\d+))'
    def read(pattern, text=source):
        match = re.search(pattern, text)
        if not match:
            raise ValueError('Unsupported cockpit parameter declaration: ' + pattern)
        return float(match[1])
    scale = read(r'constexpr double WheelScale\s*=\s*' + number)
    base_radius = read(r'constexpr double WheelRadius\s*=\s*' + number + r'\s*\*\s*WheelScale')
    rim = normalized_function(source, 'void ClassicRim()')
    pose = normalized_function(source, 'FDrivingHandPose DrivingHandPose(')
    defaults = {key: read(pattern, pose) for key, pattern in {
        'neutral_angle':r'NeutralAngle=' + number,
        'grip_depth':r'GripDepth=' + number,
        'grip_reach':r'GripReach=' + number,
        'hand_scale':r',Scale=' + number,
        'wrist_tilt':r'WristTilt=' + number,
        'depth_offset':r'HandDepthOffset=' + number}.items()}
    for key, json_key in [('grip_depth','grip_depth_cm'),('grip_reach','grip_reach_cm'),
                          ('wrist_tilt','wrist_tilt_deg'),('depth_offset','hand_depth_offset_cm')]:
        defaults[key] = glove.get('driving_grip', {}).get(json_key, defaults[key])
    defaults['depth_offset'] = max(-1., min(1., defaults['depth_offset']))
    if defaults['neutral_angle'] != 0:
        raise ValueError('Adapter requires the supported 9-and-3 pose')
    if functions['void ClassicRim()'] == CIRCULAR_RIM_FUNCTION_HASH:
        # Explicitly reviewed true circle: no thumb recess, ovalisation,
        # variable section or local depth offset is silently accepted.
        geometry = {
            'rim_radius':read(r'Centers.Add\(FVector\([^,]+,' + number + r'\*FMath::Cos', rim)*scale,
            'radial_recess':0., 'depth_recess':0., 'tube_reduction':0., 'recess_top':9.}
    else:
        geometry = {
            'rim_radius':read(r'constdoubleR=' + number, rim)*scale,
            'radial_recess':read(r'constdoubleR=[^;]*-\(' + number + r'/WheelScale\)', rim),
            'depth_recess':read(r'Centers.Add\(FVector\([^;]*-\(' + number + r'/WheelScale\)', rim),
            'tube_reduction':read(r'Radii.Add\([^;]*-\(' + number + r'/WheelScale\)', rim),
            'recess_top':read(r'ZNominal<' + number, rim)}
    parameters = {'wheel_scale':scale, 'contact_radius':base_radius*scale,
                  'segments':int(read(r'Segments=' + number, rim)),
                  'tube_segments':int(read(r'TubeSegments=' + number, rim)),
                  'depth_base':read(r'Centers.Add\(FVector\(' + number, rim)*scale,
                  'tube_radius':read(r'Radii.Add\(' + number, rim)*scale,
                  **geometry, **defaults}
    if not all(math.isfinite(value) for value in parameters.values()) or scale <= 0 or parameters['tube_radius'] <= parameters['tube_reduction']:
        raise ValueError('Nonfinite or nonpositive geometry scale')
    return parameters, functions


def glove_vertices(glove, p, side):
    c, s = math.cos(math.radians(p['wrist_tilt'])), math.sin(math.radians(p['wrist_tilt']))
    scale = p['hand_scale']
    result = []
    for x, y, z in glove['vertices']:
        dx, dy = x-p['grip_depth'], y-p['grip_reach']
        result.append((scale*(c*dx-s*dy)+p['depth_offset'],
                       -side*(-p['contact_radius']+scale*(s*dx+c*dy)), scale*(z+5.4)))
    return result


def rim_segments(p):
    points = []
    for i in range(p['segments']):
        a = 2*math.pi*i/p['segments']
        z = p['contact_radius']*math.sin(a)
        b = math.sin(math.pi*z/p['recess_top'])**2 if 0 < z < p['recess_top'] else 0.
        radius = p['rim_radius']-p['radial_recess']*b
        points.append(((p['depth_base']-p['depth_recess']*b,
                        radius*math.cos(a), radius*math.sin(a)), p['tube_radius']-p['tube_reduction']*b))
    return [(a, b, max(ra, rb)) for (a, ra), (b, rb) in zip(points, points[1:]+points[:1])]

def sub(a,b):return tuple(a[i]-b[i] for i in range(3))
def dot(a,b):return sum(a[i]*b[i] for i in range(3))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def point_segment_sq(p,a,b):
 d=sub(b,a);v=sub(p,a);t=max(0,min(1,dot(v,d)/max(dot(d,d),1e-30)));return sum((v[k]-t*d[k])**2 for k in range(3))
def segment_segment_sq(p,q,a,b):
 u=sub(q,p);v=sub(b,a);w=sub(p,a);aa=dot(u,u);bb=dot(u,v);cc=dot(v,v);dd=dot(u,w);ee=dot(v,w);den=aa*cc-bb*bb
 candidates=[point_segment_sq(p,a,b),point_segment_sq(q,a,b),point_segment_sq(a,p,q),point_segment_sq(b,p,q)]
 if den>1e-20:
  s=(bb*ee-cc*dd)/den;t=(aa*ee-bb*dd)/den
  if 0<=s<=1 and 0<=t<=1:candidates.append(sum((w[k]+s*u[k]-t*v[k])**2 for k in range(3)))
 return min(candidates)
def point_triangle_sq(p,a,b,c):
 ab=sub(b,a);ac=sub(c,a);ap=sub(p,a);d1=dot(ab,ap);d2=dot(ac,ap)
 if d1<=0 and d2<=0:return dot(ap,ap)
 bp=sub(p,b);d3=dot(ab,bp);d4=dot(ac,bp)
 if d3>=0 and d4<=d3:return dot(bp,bp)
 vc=d1*d4-d3*d2
 if vc<=0 and d1>=0 and d3<=0:
  t=d1/(d1-d3);return sum((ap[k]-t*ab[k])**2 for k in range(3))
 cp=sub(p,c);d5=dot(ab,cp);d6=dot(ac,cp)
 if d6>=0 and d5<=d6:return dot(cp,cp)
 vb=d5*d2-d1*d6
 if vb<=0 and d2>=0 and d6<=0:
  t=d2/(d2-d6);return sum((ap[k]-t*ac[k])**2 for k in range(3))
 va=d3*d6-d5*d4
 if va<=0 and d4-d3>=0 and d5-d6>=0:
  t=(d4-d3)/((d4-d3)+(d5-d6));bc=sub(c,b);return sum((bp[k]-t*bc[k])**2 for k in range(3))
 n=cross(ab,ac);return dot(ap,n)**2/max(dot(n,n),1e-30)
def segment_triangle_sq(p,q,a,b,c):
 n=cross(sub(b,a),sub(c,a));d=sub(q,p);den=dot(n,d)
 if abs(den)>1e-15:
  t=dot(n,sub(a,p))/den
  if 0<=t<=1:
   x=tuple(p[k]+t*d[k] for k in range(3))
   if all(dot(cross(sub(v,u),sub(x,u)),n)>=-1e-10 for u,v in [(a,b),(b,c),(c,a)]):return 0.
 return min(point_triangle_sq(p,a,b,c),point_triangle_sq(q,a,b,c),segment_segment_sq(p,q,a,b),segment_segment_sq(p,q,b,c),segment_segment_sq(p,q,c,a))

def surface_clearance(vertices, indices, segments):
    triangles = []
    for i in range(0, len(indices), 3):
        tri = [vertices[indices[i+j]] for j in range(3)]
        if dot(cross(sub(tri[1],tri[0]),sub(tri[2],tri[0])), cross(sub(tri[1],tri[0]),sub(tri[2],tri[0]))) < 1e-20:
            raise ValueError('Degenerate glove triangle')
        triangles.append((tri, tuple(min(q[k] for q in tri) for k in range(3)),
                          tuple(max(q[k] for q in tri) for k in range(3)), i//3))
    best, worst, tested = float('inf'), None, 0
    for si, (a, b, radius) in enumerate(segments):
        lower = tuple(min(a[k],b[k]) for k in range(3))
        upper = tuple(max(a[k],b[k]) for k in range(3))
        for (t,u,w), tmin, tmax, ti in triangles:
            bound = sum(max(0,lower[k]-tmax[k],tmin[k]-upper[k])**2 for k in range(3))
            if bound >= max(0,radius+best)**2:
                continue
            distance = math.sqrt(segment_triangle_sq(a,b,t,u,w))-radius
            tested += 1
            if distance < best:
                best, worst = distance, [si,ti]
    return {'minimum_surface_clearance_cm':best, 'worst_segment_triangle':worst,
            'exact_segment_triangle_pairs_tested':tested, 'glove_triangles':len(triangles),
            'rim_segments':len(segments)}


def audit(source_path=ROOT/'Source/Vroom/Private/VroomCockpit.cpp',
          glove_path=ROOT/'Content/Data/gloves.json', minimum_clearance_cm=.05):
    source_bytes, glove_bytes = source_path.read_bytes(), glove_path.read_bytes()
    source, glove = source_bytes.decode(), json.loads(glove_bytes)
    p, functions = source_parameters(source, glove)
    if len(glove['indices'])%3 or any(i<0 or i>=len(glove['vertices']) for i in glove['indices']):
        raise ValueError('Invalid glove triangle indices')
    if any(len(v)!=3 or not all(math.isfinite(x) for x in v) for v in glove['vertices']):
        raise ValueError('Invalid glove vertex data')
    segments = rim_segments(p)
    hands = {name:surface_clearance(glove_vertices(glove,p,side),glove['indices'],segments)
             for name,side in [('left',-1),('right',1)]}
    return {'schema_version':1, 'auditor_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'status':'PASS' if all(v['minimum_surface_clearance_cm']>=minimum_clearance_cm for v in hands.values()) else 'FAIL',
            'minimum_required_clearance_cm':minimum_clearance_cm, 'parameters':p, 'hands':hands,
            'source':{'path':str(source_path.resolve()),'sha256':hashlib.sha256(source_bytes).hexdigest(),
                      'supported_geometry_function_sha256':functions},
            'glove':{'path':str(glove_path.resolve()),'sha256':hashlib.sha256(glove_bytes).hexdigest()},
            'method':'Exact segment-triangle distance against every glove face; AABB lower bounds prune distant pairs. Subtract each segment capsule radius=max(endpoint radii). Every rendered facet between the reviewed orthonormal rings lies inside this capsule, so positive separation excludes face-interior intersections. Both hands are evaluated separately. Shared rigid tilt and steering preserve separation.',
            'scope':'Current default cockpit hand pose and ClassicRim only; glove self-intersections, command-line pose overrides, spokes, dashboard, sleeves and body are not certified. Visible appearance still needs runtime inspection.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'Source/Vroom/Private/VroomCockpit.cpp')
    parser.add_argument('--glove',type=Path,default=ROOT/'Content/Data/gloves.json')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        report=audit(args.source,args.glove)
    except (ValueError,KeyError,IndexError) as error:
        parser.exit(2, f'Cockpit audit refused: {error}\n')
    encoded=json.dumps(report,indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(encoded)
    print(encoded,end='')
    return 0 if report['status']=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
