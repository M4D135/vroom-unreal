#!/usr/bin/env python3
"""Repose only the thumb of the existing white driving glove from Valve source.

The source must match the checksum documented by bake_valve_gloves.py. Input
UVs, triangle indices, and every non-thumb vertex are preserved exactly. The
historical vertex-only rim-fit metadata is retained but marked obsolete for
the new thumb. This tool does not certify runtime clearance: run the separate
all-face cockpit audit and inspect neutral/steering renders before adoption.

Requires Python 3 + NumPy. Example (paths selected by the caller):
  python3 Tools/repose_glove_thumb.py --source /path/to/left.glb \
      --input /path/to/original-gloves.json --output /path/to/candidate.json \
      --metacarpal-y 30 --metacarpal-z 30
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import numpy as np
import bake_valve_gloves as baker

ROOT=Path(__file__).resolve().parents[1]
EXPECTED_SOURCE_SHA256='135189e6a2caab7d5fa0182feda4c2c7b1a9485bc5a7b27b0fe7b5b5ed7e33eb'


def make_generator(source, input_path):
    """Create a sequential pose generator against an explicit immutable input.

    The baker hook is restored in finally; do not call one generator from
    multiple threads. Independent processes are safe.
    """
    SOURCE=Path(source)
    input_path=Path(input_path)
    input_payload=input_path.read_bytes()
    BASE=json.loads(input_payload)
    INPUT_SHA256=hashlib.sha256(input_payload).hexdigest()
    DOCUMENT,BINARY,SOURCE_SHA=baker.read_glb(SOURCE)
    baker.require(SOURCE_SHA == EXPECTED_SOURCE_SHA256, 'Unexpected Valve source checksum')
    node=next(n for n in DOCUMENT['nodes'] if 'mesh' in n and 'skin' in n)
    attrs=DOCUMENT['meshes'][node['mesh']]['primitives'][0]['attributes']
    joints=baker.accessor(DOCUMENT,BINARY,attrs['JOINTS_0']).astype(int)
    weights=baker.accessor(DOCUMENT,BINARY,attrs['WEIGHTS_0'])
    names=[DOCUMENT['nodes'][i].get('name','') for i in DOCUMENT['skins'][node['skin']]['joints']]
    thumbslots=[i for i,n in enumerate(names) if n.startswith('Thumb_')]
    otherslots=[i for i,n in enumerate(names) if n.startswith(('Index_','Middle_','Ring_','Little_')) and 'Metacarpal' not in n]
    PROTECTED=np.any(np.isin(joints,otherslots)&(weights>0),axis=1)
    MASK=np.any(np.isin(joints,thumbslots)&(weights>0),axis=1)&~PROTECTED
    POSITIONS=np.asarray(BASE['vertices']);NORMALS=np.asarray(BASE['normals'])
    FACES=np.asarray(BASE['indices']).reshape(-1,3)
    baker.require(BASE['indices'] == baker.accessor(DOCUMENT,BINARY,DOCUMENT['meshes'][node['mesh']]['primitives'][0]['indices']).astype(int).tolist(), 'Input triangle order differs from source')
    baker.require(BASE['uvs'] == baker.accessor(DOCUMENT,BINARY,attrs['TEXCOORD_0']).astype(float).tolist(), 'Input UVs differ from source')
    baker.require(len(POSITIONS) == len(weights) and POSITIONS.shape == NORMALS.shape, 'Input vertex/normal count differs from source')
    baker.require(np.isfinite(POSITIONS).all() and np.isfinite(NORMALS).all(), 'Nonfinite input geometry')


    def generate(proximal=0.,distal=25.,y=0.,z=0.,normals=True,
                 metacarpal_y=0.,metacarpal_z=0.,metacarpal_x=0.):
        """Positive proximal/distal flexion is local -X; y,z are local +Y/+Z.

        Proximal: authoredRest * Rx(-proximal) * Ry(y) * Rz(z).
        Metacarpal: authoredRest * Rx(metacarpal_x) * Ry(metacarpal_y) * Rz(metacarpal_z).
        Distal remains authoredRest * Rx(-distal). Only source-thumb vertices
        replace current ones; the inherited per-vertex thumb rim-fit is discarded.
        ``normals=False`` accelerates candidate geometry audits. Regenerate the
        chosen candidate with normals=True before rendering/exporting.
        """
        assert all(math.isfinite(v) for v in (proximal,distal,y,z,metacarpal_y,metacarpal_z,metacarpal_x))
        def opposition(yd,zd):
            ay,az=math.radians(yd),math.radians(zd)
            cy,sy,cz,sz=math.cos(ay),math.sin(ay),math.cos(az),math.sin(az)
            ry=np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])
            rz=np.array([[cz,-sz,0],[sz,cz,0],[0,0,1]])
            return ry@rz
        ax=math.radians(metacarpal_x)
        cx,sx=math.cos(ax),math.sin(ax)
        rx=np.array([[1,0,0],[0,cx,-sx],[0,sx,cx]])
        rotations={'Thumb_Proximal_L':opposition(y,z),
                   'Thumb_Metacarpal_L':rx@opposition(metacarpal_y,metacarpal_z)}
        original=baker.node_matrix
        original_globals=baker.global_matrices
        def transformed(n,curl_degrees=0.):
            matrix=original(n,curl_degrees)
            if n.get('name') in rotations:
                scale=np.diag(n.get('scale',[1,1,1]))
                matrix[:3,:3]=matrix[:3,:3]@np.linalg.inv(scale)@rotations[n['name']]@scale
            return matrix
        def posed_globals(document,curls):
            # The bake verifies its bind/rest pose first. Add opposition only to
            # the subsequent articulated pose, never to the bind verification.
            if not curls:return original_globals(document,curls)
            try:
                baker.node_matrix=transformed
                return original_globals(document,curls)
            finally:
                baker.node_matrix=original
        try:
            baker.global_matrices=posed_globals
            baked=baker.bake(SOURCE,thumb_curl=(proximal,distal))
        finally:
            baker.node_matrix=original
            baker.global_matrices=original_globals
        baker.require(baked['source']['sha256']==SOURCE_SHA, 'Source changed during generation')
        out=copy.deepcopy(BASE);P=POSITIONS.copy()
        P[MASK]=np.asarray(baked['vertices'])[MASK]
        for i in np.flatnonzero(MASK):
            out['vertices'][int(i)]=np.round(P[i],7).tolist()
        assert all(out['vertices'][i]==BASE['vertices'][i] for i in np.flatnonzero(~MASK))
        if normals:
            changed=np.linalg.norm(P-POSITIONS,axis=1)>1e-9
            adjacent=set(FACES[np.any(changed[FACES],axis=1)].reshape(-1).tolist())
            sums=[]
            for vertices in (POSITIONS,P):
                tri=vertices[FACES];outward=np.cross(tri[:,2]-tri[:,0],tri[:,1]-tri[:,0])
                assert np.min(np.linalg.norm(outward,axis=1))>1e-8
                accum=np.zeros_like(vertices)
                for corner in range(3):np.add.at(accum,FACES[:,corner],outward)
                sums.append(accum)
            N=NORMALS.copy()
            for i in adjacent:
                if PROTECTED[i]:continue
                a,b=sums[0][i],sums[1][i];a/=np.linalg.norm(a);b/=np.linalg.norm(b)
                axis=np.cross(a,b);co=float(np.dot(a,b))
                assert co>-.999
                n=N[i];n=n+np.cross(axis,n)+np.cross(axis,np.cross(axis,n))/(1+co)
                N[i]=n/np.linalg.norm(n)
            for i in adjacent:
                if not PROTECTED[i]:out['normals'][i]=np.round(N[i],9).tolist()
        assert all(out['normals'][i]==BASE['normals'][i] for i in np.flatnonzero(PROTECTED))
        assert out['uvs']==BASE['uvs'] and out['indices']==BASE['indices']
        def elements_sha(values,mask):
            payload=json.dumps([values[int(i)] for i in np.flatnonzero(mask)],
                               separators=(',',':'),allow_nan=False).encode()
            return hashlib.sha256(payload).hexdigest()
        out['validation'].update({'vertex_count':len(P),'triangle_count':len(FACES),
            'thumb_source_pose':{'proximal_x_flexion':proximal,'distal_x_flexion':distal,
              'proximal_y_degrees':y,'proximal_z_degrees':z,'source_sha256':SOURCE_SHA,
              'metacarpal_y_degrees':metacarpal_y,'metacarpal_z_degrees':metacarpal_z,
              'metacarpal_x_degrees':metacarpal_x,
              'source_unfitted':True,'other_finger_vertices_unchanged':True,
              'input_sha256':INPUT_SHA256,
              'preserved_non_thumb_vertex_count':int(np.count_nonzero(~MASK)),
              'preserved_non_thumb_vertices_sha256':elements_sha(out['vertices'],~MASK),
              'protected_other_finger_vertex_count':int(np.count_nonzero(PROTECTED)),
              'protected_other_finger_normals_sha256':elements_sha(out['normals'],PROTECTED),
              'normals_updated':normals,'clearance_status':'NOT_EVALUATED_BY_GENERATOR',
              'audit_tool':'Tools/audit_cockpit_grip.py'}})
        for key in ('rim_clearance','grip_tightening'):
            if key in out['validation']:
                out['validation'][key]['historical_only']=True
                out['validation'][key]['applies_to_current_thumb']=False
        return out

    return generate


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source',type=Path,default=baker.DEFAULT_SOURCE)
    parser.add_argument('--input',type=Path,default=ROOT/'Content/Data/gloves.json')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--proximal',type=float,default=0.)
    parser.add_argument('--distal',type=float,default=25.)
    parser.add_argument('--proximal-y',type=float,default=0.)
    parser.add_argument('--proximal-z',type=float,default=0.)
    parser.add_argument('--metacarpal-y',type=float,default=0.)
    parser.add_argument('--metacarpal-z',type=float,default=0.)
    parser.add_argument('--metacarpal-x',type=float,default=0.)
    args=parser.parse_args()
    if args.input.resolve()==args.output.resolve():
        parser.error('--output must differ from --input so the original grip remains available')
    angles=(args.proximal,args.distal,args.proximal_y,args.proximal_z,args.metacarpal_y,args.metacarpal_z,args.metacarpal_x)
    if not all(math.isfinite(v) and abs(v)<=90 for v in angles):
        parser.error('all joint angles must be finite and within +/-90 degrees')
    generate=make_generator(args.source,args.input)
    result=generate(args.proximal,args.distal,args.proximal_y,args.proximal_z,
                    metacarpal_y=args.metacarpal_y,metacarpal_z=args.metacarpal_z,
                    metacarpal_x=args.metacarpal_x)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,separators=(',',':'),allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({'output':str(args.output),**result['validation']['thumb_source_pose']},indent=2))


if __name__=='__main__':
    main()
