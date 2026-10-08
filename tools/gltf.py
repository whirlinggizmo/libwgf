"""Writing glTF 2.0 files: meshes of triangles, materials, an image, nodes, and lights
(KHR_lights_punctual), as a .glb or a .gltf with its .bin beside it, the same bytes for the
same calls every time. tools/gen_model.py's (the examples' model); a module the other tools
import. Standard library only."""
import json
import math
import struct
import zlib

ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
FLOAT, UNSIGNED_SHORT, UNSIGNED_INT = 5126, 5123, 5125


def png(width, height, pixels):
    """A PNG of `pixels` (rows top first, each pixel an (r, g, b, a) of 0..255)."""
    raw = b''.join(b'\x00' + b''.join(bytes(p) for p in row) for row in pixels)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def quaternion(axis, angle):
    """The rotation of `angle` radians about the unit `axis`, as glTF's (x, y, z, w)."""
    s = math.sin(angle / 2)
    return [round(axis[0] * s, 7), round(axis[1] * s, 7), round(axis[2] * s, 7), round(math.cos(angle / 2), 7)]


class Gltf:
    """A glTF being written: add its parts, then take it as bytes with glb() or gltf()."""

    def __init__(self, generator):
        self.data = bytearray()
        self.doc = {'asset': {'version': '2.0', 'generator': generator}, 'scene': 0, 'scenes': [{'nodes': []}],
                    'nodes': [], 'meshes': [], 'materials': [], 'accessors': [], 'bufferViews': []}
        self.used = []

    def _use(self, extension):
        if extension not in self.used:
            self.used.append(extension)

    def _view(self, payload, target=None):
        while len(self.data) % 4:
            self.data.append(0)
        view = {'buffer': 0, 'byteOffset': len(self.data), 'byteLength': len(payload)}
        if target is not None:
            view['target'] = target
        self.data += payload
        self.doc['bufferViews'].append(view)
        return len(self.doc['bufferViews']) - 1

    def _accessor(self, values, kind, width, target, bounds=False):
        flat = [v for item in values for v in item] if width > 1 else list(values)
        if kind == FLOAT:
            payload = struct.pack(f'<{len(flat)}f', *flat)
        else:
            payload = struct.pack(f'<{len(flat)}{"H" if kind == UNSIGNED_SHORT else "I"}', *flat)
        accessor = {'bufferView': self._view(payload, target), 'componentType': kind, 'count': len(values),
                    'type': {1: 'SCALAR', 2: 'VEC2', 3: 'VEC3', 4: 'VEC4'}[width]}
        if bounds:  # glTF requires POSITION's; as float32 rounds them, so the box holds the corners
            packed = [struct.unpack('<f', struct.pack('<f', v))[0] for v in flat]
            accessor['min'] = [min(packed[i::width]) for i in range(width)]
            accessor['max'] = [max(packed[i::width]) for i in range(width)]
        self.doc['accessors'].append(accessor)
        return len(self.doc['accessors']) - 1

    def add_image(self, png_bytes, nearest=False):
        """A texture of a PNG kept in the buffer; its index, for a material."""
        self.doc.setdefault('images', []).append({'bufferView': self._view(png_bytes), 'mimeType': 'image/png'})
        sampler = {'magFilter': 9728, 'minFilter': 9984} if nearest else {}
        self.doc.setdefault('samplers', []).append(sampler)
        self.doc.setdefault('textures', []).append({'source': len(self.doc['images']) - 1,
                                                    'sampler': len(self.doc['samplers']) - 1})
        return len(self.doc['textures']) - 1

    def add_material(self, name, color=(1, 1, 1, 1), metallic=0.0, roughness=0.5, texture=None, blend=False,
                     emissive=None, emissive_strength=None, double_sided=False):
        """A metallic-roughness material (colors linear); its index."""
        pbr = {'baseColorFactor': list(color), 'metallicFactor': metallic, 'roughnessFactor': roughness}
        if texture is not None:
            pbr['baseColorTexture'] = {'index': texture}
        material = {'name': name, 'pbrMetallicRoughness': pbr}
        if blend:
            material['alphaMode'] = 'BLEND'
        if double_sided:
            material['doubleSided'] = True
        if emissive is not None:
            material['emissiveFactor'] = list(emissive)
        if emissive_strength is not None:
            self._use('KHR_materials_emissive_strength')
            material['extensions'] = {'KHR_materials_emissive_strength': {'emissiveStrength': emissive_strength}}
        self.doc['materials'].append(material)
        return len(self.doc['materials']) - 1

    def add_mesh(self, name, primitives):
        """A mesh of `primitives`, each (positions, normals, uvs, indices, material), the first
        three lists of tuples; its index."""
        out = []
        for positions, normals, uvs, indices, material in primitives:
            attributes = {'POSITION': self._accessor(positions, FLOAT, 3, ARRAY_BUFFER, bounds=True),
                          'NORMAL': self._accessor(normals, FLOAT, 3, ARRAY_BUFFER)}
            if uvs:
                attributes['TEXCOORD_0'] = self._accessor(uvs, FLOAT, 2, ARRAY_BUFFER)
            kind = UNSIGNED_SHORT if len(positions) < 65536 else UNSIGNED_INT
            out.append({'attributes': attributes, 'indices': self._accessor(indices, kind, 1, ELEMENT_ARRAY_BUFFER),
                        'material': material})
        self.doc['meshes'].append({'name': name, 'primitives': out})
        return len(self.doc['meshes']) - 1

    def add_light(self, kind, color=(1, 1, 1), intensity=1.0, range_=None, cone=None):
        """A KHR_lights_punctual light ('directional', 'point', 'spot'; a spot's cone as
        (inner, outer) radians); its index, for a node."""
        self._use('KHR_lights_punctual')
        light = {'type': kind, 'color': list(color), 'intensity': intensity}
        if range_ is not None:
            light['range'] = range_
        if cone is not None:
            light['spot'] = {'innerConeAngle': cone[0], 'outerConeAngle': cone[1]}
        lights = self.doc.setdefault('extensions', {}).setdefault('KHR_lights_punctual', {'lights': []})['lights']
        lights.append(light)
        return len(lights) - 1

    def add_node(self, name, mesh=None, translation=None, rotation=None, scale=None, light=None, parent=None):
        """A node, under `parent` (a node's index) or at the scene's top; its index."""
        node = {'name': name}
        if mesh is not None:
            node['mesh'] = mesh
        if translation is not None:
            node['translation'] = list(translation)
        if rotation is not None:
            node['rotation'] = list(rotation)
        if scale is not None:
            node['scale'] = list(scale)
        if light is not None:
            node['extensions'] = {'KHR_lights_punctual': {'light': light}}
        self.doc['nodes'].append(node)
        index = len(self.doc['nodes']) - 1
        if parent is None:
            self.doc['scenes'][0]['nodes'].append(index)
        else:
            self.doc['nodes'][parent].setdefault('children', []).append(index)
        return index

    def _json(self, uri):
        doc = dict(self.doc)
        if self.used:
            doc['extensionsUsed'] = list(self.used)
        doc['buffers'] = [{'byteLength': len(self.data), **({'uri': uri} if uri else {})}]
        return json.dumps(doc, separators=(',', ':')).encode('utf-8')

    def glb(self):
        """The file as a .glb: its JSON and its buffer in one."""
        text = self._json(None)
        text += b' ' * (-len(text) % 4)
        data = bytes(self.data) + b'\x00' * (-len(self.data) % 4)
        total = 12 + 8 + len(text) + 8 + len(data)
        return (b'glTF' + struct.pack('<II', 2, total) + struct.pack('<I', len(text)) + b'JSON' + text +
                struct.pack('<I', len(data)) + b'BIN\x00' + data)

    def gltf(self, bin_name):
        """The file as a .gltf naming `bin_name` beside it, and that file's bytes."""
        return self._json(bin_name), bytes(self.data)


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('gltf.py: a module the other tools import: there is nothing to run')
