"""Convert pinned WebXR Touch Plus GLBs to the runtime's textured triangle format."""
import io
import json
from pathlib import Path
import struct
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1] / 'runtime/assets/quest_touch_plus'
for hand in ['left', 'right']:
    raw = (root / f'{hand}.glb').read_bytes()
    json_size = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20 + json_size])
    binary = raw[28 + json_size:]
    nodes = doc['nodes']
    by_name = {n['name']: i for i, n in enumerate(nodes)}
    def matrix(node):
        if 'matrix' in node:
            return np.array(node['matrix']).reshape(4, 4).T
        x, y, z, w = node.get('rotation', [0, 0, 0, 1])
        out = np.eye(4)
        out[:3, :3] = np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]) @ np.diag(node.get('scale', [1,1,1]))
        out[:3, 3] = node.get('translation', [0,0,0])
        return out
    def worlds(override=None):
        out = {}
        def visit(index, parent):
            node = nodes[index]
            out[index] = parent @ matrix(override if override and index == by_name[override['name']] else node)
            for child in node.get('children', []): visit(child, out[index])
        for index in doc['scenes'][doc.get('scene',0)]['nodes']: visit(index, np.eye(4))
        return out
    def accessor(index):
        a = doc['accessors'][index]; v = doc['bufferViews'][a['bufferView']]
        dtype = {5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
        components = {'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']]
        offset = v.get('byteOffset',0) + a.get('byteOffset',0)
        return np.ndarray((a['count'],components), dtype=dtype, buffer=binary, offset=offset,
            strides=(v.get('byteStride', np.dtype(dtype).itemsize*components), np.dtype(dtype).itemsize)).copy()
    components = ['xr_standard_thumbstick', 'a_button' if hand=='right' else 'x_button',
        'b_button' if hand=='right' else 'y_button', 'xr_standard_trigger', 'xr_standard_squeeze',
        'xr_standard_thumbstick_xaxis', 'xr_standard_thumbstick_xaxis',
        'xr_standard_thumbstick_yaxis', 'xr_standard_thumbstick_yaxis']
    neutral = worlds()
    active = []
    for i, component in enumerate(components):
        value = component + '_pressed_value'
        # OpenXR thumbstick +Y is up; WebXR/gamepad +Y is down.
        target = nodes[by_name[component + ('_pressed_min' if i in [5,8] else '_pressed_max')]]
        override = dict(nodes[by_name[value]])
        for prop in ['rotation','translation','scale']: override[prop] = target.get(prop, {'rotation':[0,0,0,1],'translation':[0,0,0],'scale':[1,1,1]}[prop])
        active.append(worlds(override))
    records = []
    for index, node in enumerate(nodes):
        if 'mesh' not in node: continue
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            assert primitive.get('mode',4)==4
            attrs = primitive['attributes']; positions = accessor(attrs['POSITION'])
            normal = accessor(attrs['NORMAL']); uv = accessor(attrs['TEXCOORD_0'])
            homogeneous = np.concatenate([positions, np.ones((len(positions),1))],axis=1)
            base = (homogeneous @ neutral[index].T)[:,:3]
            world_normals = normal @ np.linalg.inv(neutral[index][:3,:3])
            world_normals /= np.linalg.norm(world_normals,axis=1)[:,None]
            shade = .8 + .2 * np.clip(world_normals[:,1]*.5+.5,0,1)
            deltas = [(homogeneous @ state[index].T)[:,:3]-base for state in active]
            indices = accessor(primitive['indices']).ravel()
            for vertex in indices:
                records.append([*base[vertex], *([shade[vertex]]*3), *uv[vertex], *np.concatenate([delta[vertex] for delta in deltas])])
    anchor_nodes = [c+'_pressed_value' for c in components[:5]]
    anchors = np.array([neutral[by_name[n]][:3,3] for n in anchor_nodes],dtype='<f4')
    with (root/f'{hand}.wccontroller').open('wb') as stream:
        stream.write(struct.pack('<4sII',b'WCC1',len(records),9)); stream.write(anchors.tobytes())
        stream.write(np.array(records,dtype='<f4').tobytes())
    image_view = doc['bufferViews'][doc['images'][0]['bufferView']]
    offset = image_view.get('byteOffset',0)
    image = Image.open(io.BytesIO(binary[offset:offset+image_view['byteLength']])).convert('RGBA')
    (root/f'{hand}.rgba').write_bytes(struct.pack('<II',*image.size)+image.tobytes())
    assert len(records)%3==0
    print(hand, len(records)//3, 'triangles', image.size, 'bounds',np.min(np.array(records)[:,:3],axis=0),np.max(np.array(records)[:,:3],axis=0))
