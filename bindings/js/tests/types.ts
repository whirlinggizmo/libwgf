// The binding's declarations, checked by TypeScript (tsc --noEmit --strict; tools/
// check_js_binding.py): what a program should be able to write compiles, and each line
// marked as an expected error is a mistake the types must catch. Checked, never run.
import * as wgf from '../wgf.js';

export function frame(): void {
    // handles are typed by kind: a texture is not a node, and 0 is none of any kind
    const texture: wgf.wgf_texture_t = wgf.wgf_texture_create('sprites/logo.png');
    const sprite: wgf.wgf_node_t = wgf.wgf_sprite_create(texture);
    const moved: boolean = wgf.wgf_node_set_position(sprite, 1, 2, 3);
    const parentless: wgf.wgf_entity_t = wgf.wgf_entity_create(0);
    const kind: string = wgf.wgf_handle_get_kind_name(sprite);

    // a vector comes back as an object, or into one the caller keeps, or into an array
    const where: wgf.wgf_vec3_t = wgf.wgf_node_get_position(sprite);
    const kept = { x: 0, y: 0, z: 0 };
    const again: wgf.wgf_vec3_t = wgf.wgf_node_get_position(sprite, kept);
    const flat: Float32Array = wgf.wgf_node_get_position(sprite, new Float32Array(3));
    const sum: number = where.x + again.y + flat[2];

    // bulk calls take arrays or typed arrays, and fill the ones C writes
    const entities = [parentless];
    const positions = new Float32Array(3);
    const filled: number = wgf.wgf_entity_get_positions(entities, positions);
    const set: boolean = wgf.wgf_entity_set_positions(new Uint32Array(1), [1, 2, 3]);

    // enums are their values; a color is a number
    const status: wgf.wgf_resource_status_t = wgf.wgf_resource_get_status(texture);
    const ready: boolean = status === wgf.WGF_RESOURCE_STATUS_READY;
    wgf.wgf_render_set_clear_color(wgf.wgf_color_get(wgf.WGF_COLOR_STOCK_RAYWHITE));
    const bytes: Uint8Array = wgf.wgf_fs_task_get_data(wgf.wgf_fs_read('save.txt'));

    // @ts-expect-error a texture is not a node
    wgf.wgf_node_set_position(texture, 1, 2, 3);
    // @ts-expect-error a plain number is not a handle (only 0, none, is)
    wgf.wgf_node_set_position(42, 1, 2, 3);
    // @ts-expect-error a string is not a handle
    wgf.wgf_node_set_position('sprite', 1, 2, 3);
    // @ts-expect-error too few arguments
    wgf.wgf_node_set_position(sprite, 1, 2);
    // @ts-expect-error 42 is not a wgf_resource_status_t
    const wrong: wgf.wgf_resource_status_t = 42;
    // @ts-expect-error a vector has no w
    where.w;
    // @ts-expect-error an array of strings is not one of entities
    wgf.wgf_entity_get_positions(['a'], positions);

    void [moved, kind, sum, filled, set, ready, bytes, wrong];
}

// the run takes JS functions, any of them null
const running: boolean = wgf.wgf_app_run(null, null, frame, null);
void running;
