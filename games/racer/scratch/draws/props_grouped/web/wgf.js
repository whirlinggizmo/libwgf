export const BUILT_VERSION = Object.freeze({ "major": 0, "minor": 1, "patch": 0, "headers": "b81f7f9335b9c667" });
let host = notAttached();
let attached = false;
function notAttached() {
    return new Proxy({}, {
        get(_, name) {
            throw new Error(`wgf: ${String(name)} was reached before attach(host): a call at load time `
                + `runs before the host exists; make libwgf objects in init`);
        },
    });
}
export function attach(module) {
    host = module;
    attached = true;
    resultPointer = 0;
    resultBytes = 0;
    slots.length = 0;
    slotBytes.length = 0;
}
export function isAttached() {
    return attached;
}
function cstr(s) {
    if (s === null || s === undefined) return 0;
    const length = host["lengthBytesUTF8"](s) + 1;
    const pointer = host["stackAlloc"](length);
    host["stringToUTF8"](s, pointer, length);
    return pointer;
}
function str(pointer) {
    return pointer === 0 ? "" : host["UTF8ToString"](pointer);
}
let resultPointer = 0;
let resultBytes = 0;
function result(bytes) {
    if (bytes > resultBytes) {
        if (resultPointer !== 0) host["_free"](resultPointer);
        resultBytes = Math.max(bytes, 64);
        resultPointer = host["_malloc"](resultBytes);
    }
    return resultPointer;
}
function vector(pointer, count, into) {
    const heap = host["HEAPF32"];
    const at = pointer >> 2;
    if (into === null || into === undefined) {
        return count === 2 ? { "x": heap[at], "y": heap[at + 1] }
            : count === 3 ? { "x": heap[at], "y": heap[at + 1], "z": heap[at + 2] }
            : { "x": heap[at], "y": heap[at + 1], "z": heap[at + 2], "w": heap[at + 3] };
    }
    if (Array.isArray(into) || ArrayBuffer.isView(into)) {
        for (let i = 0; i < count; i++) into[i] = heap[at + i];
        return into;
    }
    into["x"] = heap[at];
    into["y"] = heap[at + 1];
    if (count > 2) into["z"] = heap[at + 2];
    if (count > 3) into["w"] = heap[at + 3];
    return into;
}
const slots = [];
const slotBytes = [];
function slot(index, bytes) {
    while (slots.length <= index) {
        slots.push(0);
        slotBytes.push(0);
    }
    if (bytes > slotBytes[index]) {
        if (slots[index] !== 0) host["_free"](slots[index]);
        slotBytes[index] = Math.max(bytes, 256);
        slots[index] = host["_malloc"](slotBytes[index]);
    }
    return slots[index];
}
function lengthOf(values) {
    return values === null || values === undefined ? 0
        : values instanceof ArrayBuffer ? values.byteLength : values.length;
}
function arrayIn(values, index, heapName) {
    if (values === null || values === undefined) return 0;
    const pointer = slot(index, values.length * 4);
    const heap = host[heapName];
    const at = pointer >> 2;
    if (ArrayBuffer.isView(values)) {
        heap.set(values, at);
    } else {
        for (let i = 0; i < values.length; i++) heap[at + i] = values[i];
    }
    return pointer;
}
function arrayOut(values, index) {
    return values === null || values === undefined ? 0 : slot(index, values.length * 4);
}
function arrayBack(pointer, values, heapName) {
    if (values === null || values === undefined) return;
    const heap = host[heapName];
    const at = pointer >> 2;
    if (ArrayBuffer.isView(values)) {
        values.set(heap.subarray(at, at + values.length));
    } else {
        for (let i = 0; i < values.length; i++) values[i] = heap[at + i];
    }
}
function bytesIn(bytes, index) {
    if (bytes === null || bytes === undefined) return 0;
    const view = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : bytes;
    const pointer = slot(index, view.length);
    host["HEAPU8"].set(view, pointer);
    return pointer;
}
function bytesOut(pointer, size) {
    return pointer === 0 || size <= 0 ? new Uint8Array(0) : host["HEAPU8"].slice(pointer, pointer + size);
}
const handlers = [null, null, null, null];
const NAMES = ["init", "tick", "frame", "shutdown"];
let trampolines = null;
let faulted = false;
function dispatch(which) {
    if (faulted && which !== 3) return;
    const mark = host["stackSave"]();
    try {
        const handler = handlers[which];
        if (handler) handler();
    } catch (e) {
        faulted = true;
        console.error(`wgf: uncaught exception in ${NAMES[which]}:`, e);
        host["_wgf_app_quit"]();
    }
    host["stackRestore"](mark);
}
export function matchesHost() {
    const major = host["_wgf_version_get_major"](), minor = host["_wgf_version_get_minor"]();
    if (major === BUILT_VERSION["major"] && minor === BUILT_VERSION["minor"]) return true;
    console.error(`wgf: the JS binding is for libwgf ${BUILT_VERSION["major"]}.${BUILT_VERSION["minor"]}, `
        + `the host is ${major}.${minor}: load the host the binding was made with (or regenerate it, `
        + `tools/gen_binding.py)`);
    return false;
}
export function wgf_app_run(init, tick, frame, shutdown) {
    handlers[0] = init || null;
    handlers[1] = tick || null;
    handlers[2] = frame || null;
    handlers[3] = shutdown || null;
    if (!matchesHost()) return false;
    if (trampolines === null) {
        trampolines = [0, 1, 2, 3].map((which) => host["addFunction"](() => dispatch(which), "vi"));
    }
    faulted = false;
    return host["_wgf_app_run"](trampolines[0], trampolines[1], trampolines[2], trampolines[3], 0) !== 0;
}
export function wgf_version_get() {
    const value = host["_wgf_version_get"]();
    return str(value);
}
export function wgf_version_get_major() {
    const value = host["_wgf_version_get_major"]();
    return value;
}
export function wgf_version_get_minor() {
    const value = host["_wgf_version_get_minor"]();
    return value;
}
export function wgf_handle_is_alive(handle) {
    const value = host["_wgf_handle_is_alive"](handle);
    return value !== 0;
}
export function wgf_fs_exists(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_fs_exists"](cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_fs_task_get_status(task) {
    const value = host["_wgf_fs_task_get_status"](task);
    return value;
}
export function wgf_fs_task_destroy(task) {
    const value = host["_wgf_fs_task_destroy"](task);
    return value !== 0;
}
export function wgf_log_message(level, text) {
    const mark = host["stackSave"]();
    host["_wgf_log_message"](level, cstr(text));
    host["stackRestore"](mark);
}
export function wgf_probe_set_value(name, value_) {
    const mark = host["stackSave"]();
    const value = host["_wgf_probe_set_value"](cstr(name), value_);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_probe_has_value(name) {
    const mark = host["stackSave"]();
    const value = host["_wgf_probe_has_value"](cstr(name));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_resource_get_status(resource) {
    const value = host["_wgf_resource_get_status"](resource);
    return value;
}
export function wgf_resource_release(resource) {
    const value = host["_wgf_resource_release"](resource);
    return value !== 0;
}
export function wgf_keyboard_is_pressed(key) {
    const value = host["_wgf_keyboard_is_pressed"](key);
    return value !== 0;
}
export function wgf_action_bind_key(action, key) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_bind_key"](cstr(action), key);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_bind_keys(action, negative, positive) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_bind_keys"](cstr(action), negative, positive);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_bind_pad_button(action, button) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_bind_pad_button"](cstr(action), button);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_bind_pad_axis(action, axis, direction, threshold) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_bind_pad_axis"](cstr(action), axis, direction, threshold);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_get_axis(action) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_get_axis"](cstr(action));
    host["stackRestore"](mark);
    return value;
}
export function wgf_action_get_value(action) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_get_value"](cstr(action));
    host["stackRestore"](mark);
    return value;
}
export function wgf_action_is_down(action) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_is_down"](cstr(action));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_is_pressed(action) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_is_pressed"](cstr(action));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_action_get_binding_text(action, index) {
    const mark = host["stackSave"]();
    const value = host["_wgf_action_get_binding_text"](cstr(action), index);
    host["stackRestore"](mark);
    return str(value);
}
export function wgf_window_set_title(title) {
    const mark = host["stackSave"]();
    host["_wgf_window_set_title"](cstr(title));
    host["stackRestore"](mark);
}
export function wgf_asset_set_host(host_) {
    const mark = host["stackSave"]();
    const value = host["_wgf_asset_set_host"](cstr(host_));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_asset_set_manifest(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_asset_set_manifest"](cstr(path));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_asset_ensure(path, fetch_url, flags) {
    const mark = host["stackSave"]();
    const value = host["_wgf_asset_ensure"](cstr(path), cstr(fetch_url), flags);
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_asset_task_get_status(task) {
    const value = host["_wgf_asset_task_get_status"](task);
    return value;
}
export function wgf_asset_task_get_progress(task) {
    const value = host["_wgf_asset_task_get_progress"](task);
    return value;
}
export function wgf_asset_task_destroy(task) {
    const value = host["_wgf_asset_task_destroy"](task);
    return value !== 0;
}
export function wgf_asset_group_create() {
    const value = host["_wgf_asset_group_create"]();
    return value >>> 0;
}
export function wgf_asset_group_add(group, task) {
    const value = host["_wgf_asset_group_add"](group, task);
    return value !== 0;
}
export function wgf_actor_set_parent(actor, parent) {
    const value = host["_wgf_actor_set_parent"](actor, parent);
    return value !== 0;
}
export function wgf_actor_set_position(actor, x, y, z) {
    const value = host["_wgf_actor_set_position"](actor, x, y, z);
    return value !== 0;
}
export function wgf_actor_get_position(actor, into) {
    const ret = result(12);
    host["_wgf_actor_get_position"](ret, actor);
    return vector(ret, 3, into);
}
export function wgf_actor_set_transform(actor, position_x, position_y, position_z, rotation_x, rotation_y, rotation_z, scale_x, scale_y, scale_z) {
    const value = host["_wgf_actor_set_transform"](actor, position_x, position_y, position_z, rotation_x, rotation_y, rotation_z, scale_x, scale_y, scale_z);
    return value !== 0;
}
export function wgf_actor_snap(actor) {
    const value = host["_wgf_actor_snap"](actor);
    return value !== 0;
}
export function wgf_actor_get_world_position(actor, into) {
    const ret = result(12);
    host["_wgf_actor_get_world_position"](ret, actor);
    return vector(ret, 3, into);
}
export function wgf_actor_get_drawn_position(actor, into) {
    const ret = result(12);
    host["_wgf_actor_get_drawn_position"](ret, actor);
    return vector(ret, 3, into);
}
export function wgf_actor_get_world_direction(actor, x, y, z, into) {
    const ret = result(12);
    host["_wgf_actor_get_world_direction"](ret, actor, x, y, z);
    return vector(ret, 3, into);
}
export function wgf_actor_get_drawn_direction(actor, x, y, z, into) {
    const ret = result(12);
    host["_wgf_actor_get_drawn_direction"](ret, actor, x, y, z);
    return vector(ret, 3, into);
}
export function wgf_actor_look_at(actor, x, y, z, up_x, up_y, up_z) {
    const value = host["_wgf_actor_look_at"](actor, x, y, z, up_x, up_y, up_z);
    return value !== 0;
}
export function wgf_actor_find(actor, path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_actor_find"](actor, cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_camera3d_create() {
    const value = host["_wgf_camera3d_create"]();
    return value >>> 0;
}
export function wgf_camera3d_set_fov(camera, radians) {
    const value = host["_wgf_camera3d_set_fov"](camera, radians);
    return value !== 0;
}
export function wgf_camera3d_set_clip(camera, near_z, far_z) {
    const value = host["_wgf_camera3d_set_clip"](camera, near_z, far_z);
    return value !== 0;
}
const LOCAL_STOCK = [
    0x00000000, 0xFFFFFFFF, 0x000000FF, 0xC8C8C8FF, 0x828282FF, 0x505050FF, 0xFFFF00FF, 0xFFCB00FF, 0xFFA100FF,
    0xFF6DC2FF, 0xE62937FF, 0xBE212DFF, 0x00E430FF, 0x009E2FFF, 0x00752CFF, 0x66BFFFFF, 0x0079F1FF, 0x0052ACFF,
    0xC87AFFFF, 0x873CBEFF, 0x701F7EFF, 0xD3B083FF, 0x7F6A4FFF, 0x4C3F2FFF, 0xFF00FFFF, 0xF5F5F5FF,
];
function localComponent(v) {
    v |= 0;
    return v < 0 ? 0 : v > 255 ? 255 : v;
}
function localComponentFloat(v) {
    const scaled = Math.fround(Math.fround(Math.fround(v) * 255) + 0.5);
    return Math.trunc(scaled < 0 ? 0 : scaled > 255 ? 255 : scaled);
}
export function wgf_color_get(stock) {
    return stock >= 0 && stock < LOCAL_STOCK.length ? LOCAL_STOCK[stock | 0] : 0;
}
export function wgf_color_make(r, g, b, a) {
    return ((localComponent(r) << 24) | (localComponent(g) << 16) | (localComponent(b) << 8) | localComponent(a)) >>> 0;
}
export function wgf_texture_create(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_texture_create"](cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_material_create(shading) {
    const value = host["_wgf_material_create"](shading);
    return value >>> 0;
}
export function wgf_material_set_float(material, name, value_) {
    const mark = host["stackSave"]();
    const value = host["_wgf_material_set_float"](material, cstr(name), value_);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_material_set_color(material, name, color) {
    const mark = host["stackSave"]();
    const value = host["_wgf_material_set_color"](material, cstr(name), color);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_draw_rectangle(x, y, width, height, color) {
    host["_wgf_draw_rectangle"](x, y, width, height, color);
}
export function wgf_draw_text(font, text, x, y, size, color) {
    const mark = host["stackSave"]();
    host["_wgf_draw_text"](font, cstr(text), x, y, size, color);
    host["stackRestore"](mark);
}
export function wgf_draw_text_aligned(font, text, x, y, size, color, horizontal, vertical) {
    const mark = host["stackSave"]();
    host["_wgf_draw_text_aligned"](font, cstr(text), x, y, size, color, horizontal, vertical);
    host["stackRestore"](mark);
}
export function wgf_draw_begin_3d(camera) {
    const value = host["_wgf_draw_begin_3d"](camera);
    return value !== 0;
}
export function wgf_draw_end_3d() {
    host["_wgf_draw_end_3d"]();
}
export function wgf_emitter2d_create() {
    const value = host["_wgf_emitter2d_create"]();
    return value >>> 0;
}
export function wgf_emitter3d_skip_trail(emitter) {
    const value = host["_wgf_emitter3d_skip_trail"](emitter);
    return value !== 0;
}
export function wgf_emitter3d_set_rate(emitter, per_second) {
    const value = host["_wgf_emitter3d_set_rate"](emitter, per_second);
    return value !== 0;
}
export function wgf_emitter3d_add_size_key(emitter, t, size) {
    const value = host["_wgf_emitter3d_add_size_key"](emitter, t, size);
    return value !== 0;
}
export function wgf_emitter3d_clear_size_keys(emitter) {
    const value = host["_wgf_emitter3d_clear_size_keys"](emitter);
    return value !== 0;
}
export function wgf_emitter3d_add_color_key(emitter, t, color) {
    const value = host["_wgf_emitter3d_add_color_key"](emitter, t, color);
    return value !== 0;
}
export function wgf_emitter3d_clear_color_keys(emitter) {
    const value = host["_wgf_emitter3d_clear_color_keys"](emitter);
    return value !== 0;
}
export function wgf_emitter3d_get_count(emitter) {
    const value = host["_wgf_emitter3d_get_count"](emitter);
    return value;
}
export function wgf_environment_create(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_environment_create"](cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_light_create(type) {
    const value = host["_wgf_light_create"](type);
    return value >>> 0;
}
export function wgf_light_set_color(light, color) {
    const value = host["_wgf_light_set_color"](light, color);
    return value !== 0;
}
export function wgf_light_set_intensity(light, intensity) {
    const value = host["_wgf_light_set_intensity"](light, intensity);
    return value !== 0;
}
export function wgf_light_set_shadow_casting(light, casting) {
    const value = host["_wgf_light_set_shadow_casting"](light, (casting ? 1 : 0));
    return value !== 0;
}
export function wgf_light_set_shadow_distance(light, distance) {
    const value = host["_wgf_light_set_shadow_distance"](light, distance);
    return value !== 0;
}
export function wgf_light_set_shadow_map_size(light, size) {
    const value = host["_wgf_light_set_shadow_map_size"](light, size);
    return value !== 0;
}
export function wgf_light_set_shadow_strength(light, strength) {
    const value = host["_wgf_light_set_shadow_strength"](light, strength);
    return value !== 0;
}
export function wgf_mesh_create(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_mesh_create"](cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_model_set_tint(model, color) {
    const value = host["_wgf_model_set_tint"](model, color);
    return value !== 0;
}
export function wgf_model_set_material(model, slot_, material) {
    const value = host["_wgf_model_set_material"](model, slot_, material);
    return value !== 0;
}
export function wgf_presentation_set(mode, width, height) {
    const value = host["_wgf_presentation_set"](mode, width, height);
    return value !== 0;
}
export function wgf_presentation_get_visible(into) {
    const ret = result(16);
    host["_wgf_presentation_get_visible"](ret);
    return vector(ret, 4, into);
}
export function wgf_render_set_clear_color(color) {
    host["_wgf_render_set_clear_color"](color);
}
export function wgf_stage3d_create() {
    const value = host["_wgf_stage3d_create"]();
    return value >>> 0;
}
export function wgf_stage3d_draw(stage) {
    host["_wgf_stage3d_draw"](stage);
}
export function wgf_stage3d_set_camera(stage, camera) {
    const value = host["_wgf_stage3d_set_camera"](stage, camera);
    return value !== 0;
}
export function wgf_stage3d_set_tonemap(stage, tonemap, exposure) {
    const value = host["_wgf_stage3d_set_tonemap"](stage, tonemap, exposure);
    return value !== 0;
}
export function wgf_stage3d_set_environment(stage, environment, intensity, rotation) {
    const value = host["_wgf_stage3d_set_environment"](stage, environment, intensity, rotation);
    return value !== 0;
}
export function wgf_stage3d_set_background(stage, environment, blur) {
    const value = host["_wgf_stage3d_set_background"](stage, environment, blur);
    return value !== 0;
}
export function wgf_behavior_get_name(actor, behavior) {
    const value = host["_wgf_behavior_get_name"](actor, behavior);
    return str(value);
}
export function wgf_behavior_get_param_number(actor, behavior, key) {
    const mark = host["stackSave"]();
    const value = host["_wgf_behavior_get_param_number"](actor, behavior, cstr(key));
    host["stackRestore"](mark);
    return value;
}
export function wgf_actor_count_with_behavior(name) {
    const mark = host["stackSave"]();
    const value = host["_wgf_actor_count_with_behavior"](cstr(name));
    host["stackRestore"](mark);
    return value;
}
export function wgf_scene_create(path) {
    const mark = host["stackSave"]();
    const value = host["_wgf_scene_create"](cstr(path));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_scene_instantiate(scene, parent) {
    const value = host["_wgf_scene_instantiate"](scene, parent);
    return value;
}
export function wgf_scene_find_prefab(scene, name) {
    const mark = host["stackSave"]();
    const value = host["_wgf_scene_find_prefab"](scene, cstr(name));
    host["stackRestore"](mark);
    return value >>> 0;
}
export function wgf_prefab_spawn(prefab, parent) {
    const value = host["_wgf_prefab_spawn"](prefab, parent);
    return value >>> 0;
}
export function wgf_world_take_events(out) {
    const outPointer = arrayOut(out, 0);
    const value = host["_wgf_world_take_events"](outPointer, lengthOf(out));
    arrayBack(outPointer, out, "HEAP32");
    return value;
}
export function wgf_body_get_spin(actor, into) {
    const ret = result(12);
    host["_wgf_body_get_spin"](ret, actor);
    return vector(ret, 3, into);
}
export function wgf_physics_set_gravity(x, y, z) {
    const value = host["_wgf_physics_set_gravity"](x, y, z);
    return value !== 0;
}
export function wgf_physics_draw_bodies(color) {
    host["_wgf_physics_draw_bodies"](color);
}
export function wgf_vehicle_set_input(actor, throttle, brake, steer, hand_brake) {
    const value = host["_wgf_vehicle_set_input"](actor, throttle, brake, steer, (hand_brake ? 1 : 0));
    return value !== 0;
}
export function wgf_vehicle_get_speed(actor) {
    const value = host["_wgf_vehicle_get_speed"](actor);
    return value;
}
export function wgf_vehicle_get_rpm(actor) {
    const value = host["_wgf_vehicle_get_rpm"](actor);
    return value;
}
export function wgf_vehicle_get_gear(actor) {
    const value = host["_wgf_vehicle_get_gear"](actor);
    return value;
}
export function wgf_vehicle_get_wheel_slip(actor, index) {
    const value = host["_wgf_vehicle_get_wheel_slip"](actor, index);
    return value;
}
export function wgf_vehicle_reset(actor) {
    const value = host["_wgf_vehicle_reset"](actor);
    return value !== 0;
}
export function wgf_ui_begin() {
    const value = host["_wgf_ui_begin"]();
    return value !== 0;
}
export function wgf_ui_end() {
    const value = host["_wgf_ui_end"]();
    return value !== 0;
}
export function wgf_ui_begin_panel(id) {
    const mark = host["stackSave"]();
    const value = host["_wgf_ui_begin_panel"](cstr(id));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_ui_end_panel() {
    const value = host["_wgf_ui_end_panel"]();
    return value !== 0;
}
export function wgf_ui_set_align(x, y) {
    const value = host["_wgf_ui_set_align"](x, y);
    return value !== 0;
}
export function wgf_ui_label(text, size) {
    const mark = host["stackSave"]();
    const value = host["_wgf_ui_label"](cstr(text), size);
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_ui_spacer(size) {
    const value = host["_wgf_ui_spacer"](size);
    return value !== 0;
}
export function wgf_ui_button(id, text) {
    const mark = host["stackSave"]();
    const value = host["_wgf_ui_button"](cstr(id), cstr(text));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_ui_progress(value_) {
    const value = host["_wgf_ui_progress"](value_);
    return value !== 0;
}
export function wgf_ui_set_focus(id) {
    const mark = host["stackSave"]();
    const value = host["_wgf_ui_set_focus"](cstr(id));
    host["stackRestore"](mark);
    return value !== 0;
}
export function wgf_app_quit() {
    host["_wgf_app_quit"]();
}
export function wgf_app_can_quit() {
    const value = host["_wgf_app_can_quit"]();
    return value !== 0;
}
export function wgf_loop_get_frame_delta() {
    const value = host["_wgf_loop_get_frame_delta"]();
    return value;
}
export function wgf_loop_set_tick_rate(hz) {
    host["_wgf_loop_set_tick_rate"](hz);
}
export function wgf_loop_get_tick_delta() {
    const value = host["_wgf_loop_get_tick_delta"]();
    return value;
}
