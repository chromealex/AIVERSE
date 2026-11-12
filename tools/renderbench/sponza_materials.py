#!/usr/bin/env python3
"""Create a Sponza recipe with editable library-derived material graphs."""
import argparse
import json
from pathlib import Path

LIBRARY=Path(__file__).resolve().parents[2]/'examples/shader_graph/material_library/shaders'

def create(source):
    source=Path(source).resolve()
    target=source.with_name(source.stem+'-materials.json')
    if target.exists():raise ValueError(f'already exists: {target}')
    scene=json.loads(source.read_text())
    root=source.parent
    graphs=root/'shaders-sponza'
    graphs.mkdir(exist_ok=True)
    def bind(preset, textured=True, tint=None):
        name=preset+('-textured' if textured else '')
        graph=json.loads((LIBRARY/(preset+'.json')).read_text())
        if textured:
            # Keep imported roughness/metallic maps, including their per-pixel variation.
            graph['outputs']={'color':'color'}
        for node in graph['nodes']:
            if node['id']=='tint':node['value']=tint or [1,1,1,1]
        path=graphs/(name+'.json')
        if path.exists() and json.loads(path.read_text())!=graph:
            raise ValueError(f'refusing to replace edited graph: {path}')
        path.write_text(json.dumps(graph,indent=2)+'\n')
        return str(path.relative_to(root))
    stone=bind('sg_limestone',tint=[1,.98,.95,1])
    cloth=bind('sg_cloth_red')
    steel=bind('sg_steel')
    brass=bind('sg_brass')
    wax=bind('sg_wax',False)
    flame=bind('sg_candle_glow',False)
    main=json.loads((root/scene['model']).read_text())
    stone_names={'arch_stone_wall_01','stone_trims_01','brickwall_02','stones_2ndfloor','stone_trims_02','stones_01_tile','column_1stfloor','column_head_1stfloor','door_stoneframe_02','door_stoneframe_01','brickwall_01','floor_01','ornament_lion','column_head_2ndfloor_02','column_brickwall_01','column_head_2ndfloor_03','ornament_01'}
    scene['materialGraphs']={m['name']:stone for m in main['materials'] if m['name'] in stone_names}
    for entry in scene['models']:
        path=entry['path']
        bindings=entry.setdefault('materialGraphs',{})
        if 'curtains' in path.lower():
            data=json.loads((root/path).read_text())
            bindings.update({m['name']:cloth for m in data['materials'] if m['name'].startswith('curtain_')})
        if entry.get('lights'):
            bindings.update(Wax_MAT=wax,Flame_MAT=flame)
        if 'knight' in path.lower():
            for name in entry.get('materials',{}):
                if any(key in name for key in ['Armor','Chestplate','Shoulder','Shield','Sword']):bindings[name]=steel
                elif 'Brass' in name:bindings[name]=brass
                elif 'Cloth' in name:bindings[name]=cloth
    scene['id']+='-shader-graphs'
    scene['materialGraphNote']='Library variants preserve imported textures, alpha, normal, roughness and metal maps. Wax and flame use constant surface outputs. Emission does not create light.'
    target.write_text(json.dumps(scene,indent=2)+'\n')
    return target

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    print(create(parser.parse_args().source))
