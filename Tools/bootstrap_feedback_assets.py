"""Small unlit race-feedback materials. Executed by the Unreal asset bootstrap."""
import runpy
from pathlib import Path
import unreal


def main():
    h = runpy.run_path(str(Path(__file__).with_name('bootstrap_assets.py')), run_name='feedback_helpers')
    library = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    for name, rgb, alpha, smoke in (
        ('M_F1Ghost', (.06, .8, 1.0), .24, False),
        ('M_TyreTrace', (.012, .014, .016), 1, False),
        ('M_DriftSmoke', (.62, .66, .69), 1, True),
        ('M_DriftSpark', (1, 1, 1), 1, True),
    ):
        h['create_material'](name, rgb, 1, glass_opacity=(alpha, alpha))
        material = assets.load_asset('/Game/Materials/' + name)
        library.delete_all_material_expressions(material)
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        h['output'](h['color'](material, rgb), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if name == 'M_DriftSpark':
            material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
            vertex_color = h['expression'](material, unreal.MaterialExpressionVertexColor)
            h['output'](vertex_color, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if name == 'M_F1Ghost':
            opacity = h['scalar'](material, alpha)
        else:
            vertex = h['expression'](material, unreal.MaterialExpressionVertexColor)
            opacity = h['expression'](material, unreal.MaterialExpressionMultiply)
            h['require'](library.connect_material_expressions(vertex, 'A', opacity, 'A'), 'Vertex alpha to opacity')
            h['connect'](h['scalar'](material, 1), opacity, 'B')
            if smoke:
                uv = h['expression'](material, unreal.MaterialExpressionTextureCoordinate)
                center = h['expression'](material, unreal.MaterialExpressionConstant2Vector, r=.5, g=.5)
                distance = h['expression'](material, unreal.MaterialExpressionDistance)
                h['connect'](uv, distance, 'A'); h['connect'](center, distance, 'B')
                scale = h['expression'](material, unreal.MaterialExpressionMultiply)
                h['connect'](distance, scale, 'A'); h['connect'](h['scalar'](material, 2), scale, 'B')
                inverse = h['expression'](material, unreal.MaterialExpressionOneMinus)
                h['connect'](scale, inverse)
                clamp = h['expression'](material, unreal.MaterialExpressionClamp)
                h['connect'](inverse, clamp)
                fade = h['expression'](material, unreal.MaterialExpressionMultiply)
                h['connect'](clamp, fade, 'A'); h['connect'](opacity, fade, 'B')
                opacity = fade
        h['output'](opacity, unreal.MaterialProperty.MP_OPACITY)
        errors = library.recompile_material(material)
        if errors:
            raise RuntimeError(str(errors))
        h['require'](assets.save_asset('/Game/Materials/' + name, only_if_is_dirty=False), name)


if __name__ == '__main__':
    main()
