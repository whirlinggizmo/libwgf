// The binding's declarations, checked by TypeScript (tsc --noEmit --strict; tools/
// check_js_binding.py): what a program should be able to write compiles, and each line
// marked as an expected error is a mistake the types must catch. Checked, never run.
import * as wgf from '../wgf.js';
import { Actor, Prefab, Scene, Texture, Sprite, World, Presentation, KeyboardKey, Keyboard } from '../wgf-typed.js';

export function frame(): void {
    // handles are typed by kind: a texture is not an actor, and 0 is none of any kind
    const texture: wgf.wgf_texture_t = wgf.wgf_texture_create('sprites/logo.png');
    const sprite: wgf.wgf_actor_t = wgf.wgf_sprite_create(texture);
    const moved: boolean = wgf.wgf_actor_set_position(sprite, 1, 2, 3);
    const parentless: wgf.wgf_actor_t = wgf.wgf_actor_create();
    const kind: string = wgf.wgf_handle_get_kind_name(sprite);

    // a vector comes back as an object, or into one the caller keeps, or into an array
    const where: wgf.wgf_vec3_t = wgf.wgf_actor_get_position(sprite);
    const kept = { x: 0, y: 0, z: 0 };
    const again: wgf.wgf_vec3_t = wgf.wgf_actor_get_position(sprite, kept);
    const flat: Float32Array = wgf.wgf_actor_get_position(sprite, new Float32Array(3));
    const sum: number = where.x + again.y + flat[2];

    // bulk calls take arrays or typed arrays, and fill the ones C writes
    const actors = [parentless];
    const positions = new Float32Array(3);
    const filled: number = wgf.wgf_actor_get_positions(actors, positions);
    const set: boolean = wgf.wgf_actor_set_positions(new Uint32Array(1), [1, 2, 3]);

    // enums are their values; a color is a number
    const status: wgf.wgf_resource_status_t = wgf.wgf_resource_get_status(texture);
    const ready: boolean = status === wgf.WGF_RESOURCE_STATUS_READY;
    wgf.wgf_render_set_clear_color(wgf.wgf_color_get(wgf.WGF_COLOR_STOCK_RAYWHITE));
    const bytes: Uint8Array = wgf.wgf_fs_task_get_data(wgf.wgf_fs_read('save.txt'));

    // @ts-expect-error a texture is not an actor
    wgf.wgf_actor_set_position(texture, 1, 2, 3);
    // @ts-expect-error a plain number is not a handle (only 0, none, is)
    wgf.wgf_actor_set_position(42, 1, 2, 3);
    // @ts-expect-error a string is not a handle
    wgf.wgf_actor_set_position('sprite', 1, 2, 3);
    // @ts-expect-error too few arguments
    wgf.wgf_actor_set_position(sprite, 1, 2);
    // @ts-expect-error 42 is not a wgf_resource_status_t
    const wrong: wgf.wgf_resource_status_t = 42;
    // @ts-expect-error a vector has no w
    where.w;
    // @ts-expect-error an array of strings is not one of actors
    wgf.wgf_actor_get_positions(['a'], positions);

    void [moved, kind, sum, filled, set, ready, bytes, wrong];
}

// the run takes JS functions, any of them null
const running: boolean = wgf.wgf_app_run(null, null, frame, null);
void running;

// the typed layer: the raw calls under the typed API's names, the handle first, its
// handle types the raw binding's branded ones
export function typed(): void {
    const texture: Texture = Texture.create('sprites/logo.png');
    const sprite: Actor = Sprite.create(texture);
    const moved: boolean = Actor.setPosition(sprite, 1, 2, 3);
    const alive: boolean = Actor.isAlive(sprite);
    const scene: Scene = Scene.create('scenes/field.scene');
    const rock: Prefab = Scene.prefab(scene, 'rock');
    const spawned: Actor = Prefab.spawnAt(rock, sprite, 1, 2, 0, 0.5);
    const once: Actor = Scene.spawnPrefab(scene, 'rock', sprite);
    const placed: Actor = Scene.spawnPrefab(scene, 'rock', sprite, 1, 2, 0, 0.5);
    const found: number = Actor.findWithBehavior('Rock', [spawned, once, placed]);
    const events: number = World.takeEvents(new Int32Array(64));
    Presentation.setBarColor(0xff000000);
    const pressed: boolean = Keyboard.isPressed(KeyboardKey.SPACE);
    // @ts-expect-error an actor is not a prefab
    Prefab.spawn(sprite, sprite);
    // @ts-expect-error a prefab is not an actor
    Actor.setPosition(rock, 1, 2, 3);
    void moved; void alive; void found; void events; void pressed;
}
