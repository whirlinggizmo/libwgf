#!/usr/bin/env python3
"""Check the public API's shape against the rules in docs/CONVENTIONS.md.

    tools/check_api.py [--self-test]

Reads every public header (<layer>/include/*.h) through clang, all of them in one
parse (tools/headers.py), and checks each exported function -- the ones a binding calls:

  name        wgf_<section>_<action>, and is marked WGF_API (exported). A layer
              (math, core, platform, gfx, app...) isn't a section: only the runtime's
              calls in wgf_app.h keep their layer's name. No _priv_ name is public
  types       parameters and the return are void, bool, numbers, enums, handles,
              math values (returned by value, never taken), or const char *; a
              handle is its kind's type (a typedef of wgf_handle_t), but in the
              calls ANY_HANDLE lists; a const unsigned char * is a
              byte span: a parameter followed by `int size`, or returned by a
              _get_data with a _get_size beside it; a pointer to float, int, or a
              handle type is a caller-owned array: a parameter followed by `int count`
              (or `int <name>_count`, for a call taking two). No other pointer, struct, function
              pointer, or `...`, but for the callbacks CALLBACKS_ALLOWED lists
  getters     every set_X has a get_X, is_X, or has_X, unless GETTERS_EXEMPT says why;
              a setter of several values has the getters GETTERS_PAIRED lists;
              a resource created from a file (wgf_X_create taking `path`) has
              wgf_X_get_path, or wgf_resource_get_path serves it
  predicates  is_, has_, and can_ return bool
  prefixes    a call named for a kind of handle (wgf_<kind>_*), or for one of ACTOR_FACETS
              (a kind of actor, or a component or behavior an actor has), takes that
              kind first among its handles -- an actor, for a facet -- or, taking none,
              returns one; MADE_FROM lists the calls that take what they are made from
              (a sprite's texture). A name says what it acts on (CONVENTIONS.md,
              "Naming")
  lists       an entry in GETTERS_EXEMPT, GETTERS_PAIRED, CALLBACKS_ALLOWED, ANY_HANDLE, or MADE_FROM that
              names no public call fails, and so does an exempt setter with a getter,
              so the lists can't go stale
  macros      every macro a public header defines is WGF_ (or wgf_, for the log
              calls'): every program that includes the header gets the name

static inline functions are C conveniences no binding sees (math's operations are
among them), so they aren't checked. clang comes with Emscripten:
$EMSDK, or the emcc on PATH, or a clang on PATH. Exits 77 (ctest's skip) when there is
none. --self-test checks the checker against a header that breaks every rule.
Standard library only.
"""
import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import headers  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SKIP = 77

# Setters with no getter on purpose, and why. A decision, not a backlog.
UI_OPEN_BOX = 'immediate mode: it describes the open box for this frame alone, and nothing keeps it to read back'
GETTERS_EXEMPT = {
    'wgf_vehicle_set_input': 'the driver\'s intent, a tick\'s: what it did is read back (speed, rpm, gear, slip)',
    'wgf_ui_set_width': UI_OPEN_BOX,
    'wgf_ui_set_height': UI_OPEN_BOX,
    'wgf_ui_set_padding': UI_OPEN_BOX,
    'wgf_ui_set_gap': UI_OPEN_BOX,
    'wgf_ui_set_align': UI_OPEN_BOX,
    'wgf_ui_set_color': UI_OPEN_BOX,
    'wgf_material_set_color': 'a vec3 or vec4 parameter from an sRGB color, stored linear: read back as one '
                              '(wgf_material_get_vec3, get_vec4)',
}

# The only functions that may take callbacks, with the callback type and why: each
# may also take the `void *user` passed back to them. A decision, not a backlog.
CALLBACKS_ALLOWED = {
    'wgf_app_run': ('wgf_app_callback_t', 'sokol_app owns the frame loop: on the web the browser runs it'),
}

# Setters that take several values, and the one getter per value each pairs with.
GETTERS_PAIRED = {
    'wgf_window_set_size': ('wgf_window_get_width', 'wgf_window_get_height'),
    'wgf_window_set_position': ('wgf_window_get_x', 'wgf_window_get_y'),
    'wgf_actor_set_transform': ('wgf_actor_get_position', 'wgf_actor_get_rotation', 'wgf_actor_get_scale'),
    'wgf_texture_set_sampling': ('wgf_texture_get_wrap_u', 'wgf_texture_get_wrap_v', 'wgf_texture_get_filter'),
    'wgf_text_set_align': ('wgf_text_get_halign', 'wgf_text_get_valign'),
    'wgf_vehicle_set_wheels': ('wgf_vehicle_get_wheel_count', 'wgf_vehicle_get_wheel'),
    'wgf_vehicle_set_gears': ('wgf_vehicle_get_gear_count', 'wgf_vehicle_get_gear_ratio'),
    'wgf_shape2d_set_rectangle': ('wgf_shape2d_get_kind', 'wgf_shape2d_get_size'),
    'wgf_shape2d_set_circle': ('wgf_shape2d_get_kind', 'wgf_shape2d_get_radius'),
    'wgf_shape2d_set_line': ('wgf_shape2d_get_kind', 'wgf_shape2d_get_line_start', 'wgf_shape2d_get_line_end'),
    'wgf_shape2d_set_polygon': ('wgf_shape2d_get_kind', 'wgf_shape2d_get_point_count', 'wgf_shape2d_get_points'),
    'wgf_emitter2d_set_life': ('wgf_emitter2d_get_life_min', 'wgf_emitter2d_get_life_max'),
    'wgf_emitter2d_set_speed': ('wgf_emitter2d_get_speed_min', 'wgf_emitter2d_get_speed_max'),
    'wgf_emitter2d_set_size': ('wgf_emitter2d_get_size_start', 'wgf_emitter2d_get_size_end'),
    'wgf_emitter2d_set_color': ('wgf_emitter2d_get_color_start', 'wgf_emitter2d_get_color_end'),
    'wgf_camera3d_set_clip': ('wgf_camera3d_get_near', 'wgf_camera3d_get_far'),
    'wgf_shape3d_set_cube': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_size'),
    'wgf_shape3d_set_sphere': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_radius'),
    'wgf_shape3d_set_rectangle': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_size'),
    'wgf_shape3d_set_circle': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_radius'),
    'wgf_shape3d_set_line': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_line_start', 'wgf_shape3d_get_line_end'),
    'wgf_shape3d_set_line_strip': ('wgf_shape3d_get_kind', 'wgf_shape3d_get_point_count', 'wgf_shape3d_get_points'),
    'wgf_material_set_alpha_mode': ('wgf_material_get_alpha_mode', 'wgf_material_get_alpha_cutoff'),
    'wgf_stage3d_set_ambient': ('wgf_stage3d_get_ambient_color', 'wgf_stage3d_get_ambient_intensity'),
    'wgf_stage3d_set_tonemap': ('wgf_stage3d_get_tonemap', 'wgf_stage3d_get_exposure'),
    'wgf_light_set_spot_cone': ('wgf_light_get_spot_inner_angle', 'wgf_light_get_spot_outer_angle'),
    'wgf_material_set_texture_sampling': ('wgf_material_get_texture_wrap_u', 'wgf_material_get_texture_wrap_v',
                                          'wgf_material_get_texture_filter'),
}

# Calls that take or return a handle of any kind, as bare wgf_handle_t, and why; every
# other handle is its kind's type (wgf_texture_t). A decision, not a backlog.
ANY_HANDLE = {
    'wgf_handle_get_kind_name': 'names the kind of any handle',
    'wgf_handle_is_alive': 'tells whether any handle is alive',
    'wgf_resource_get_status': 'every resource kind shares it',
    'wgf_resource_get_path': 'every resource kind shares it',
    'wgf_resource_release': 'every resource kind shares it',
}

# Sections named for what an actor is, or has, whose calls take the actor: wgf_sprite_*
# on a sprite, wgf_motion_* on an actor with motion, wgf_stage2d_* on a 2D stage.
ACTOR_FACETS = ('sprite', 'text', 'shape2d', 'shape3d', 'emitter2d', 'model', 'light', 'camera2d', 'camera3d',
                'collider', 'motion', 'bounds', 'lifetime', 'behavior', 'stage2d', 'stage3d', 'body', 'vehicle')

# The calls that take what they are made from, before what they act on: their first handle
# isn't their prefix's kind. A new one is listed here with what it is made from.
MADE_FROM = {
    'wgf_sprite_create': 'a sprite is made from its texture',
    'wgf_text_create': 'a text is made from its font',
    'wgf_model_create': 'a model is made from its mesh',
    'wgf_voice_create': 'a voice is made from its sound',
}

# Math values a public call may return by value: their layout can never change. None is
# ever a parameter: a setter takes its components (docs/HISTORY.md, "Math values are
# returned, never taken").
MATH_VALUES = {'wgf_vec2_t', 'wgf_vec3_t', 'wgf_vec4_t', 'wgf_quat_t', 'wgf_mat4_t'}

NUMBERS = {'_Bool', 'bool', 'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int',
           'unsigned int', 'long', 'unsigned long', 'long long', 'unsigned long long', 'float', 'double'}


def classify(written, canonical, handles):
    """'math', 'handle', 'any-handle', 'value', 'text', 'span', or None (not allowed).
    `handles` is every handle type: a typedef of wgf_handle_t."""
    plain_written = written.replace('const ', '').strip()
    if plain_written in MATH_VALUES:
        return 'math'
    if plain_written == 'wgf_handle_t':
        return 'any-handle'
    if plain_written in handles:
        return 'handle'
    plain = canonical.replace('const ', '').strip()
    if plain == 'void' or plain in NUMBERS or plain.startswith('enum '):
        return 'value'
    if canonical.replace(' ', '') == 'constchar*':
        return 'text'
    if canonical.replace(' ', '') == 'constunsignedchar*':
        return 'span'
    if written.count('*') == 1 and written.endswith('*'):
        element = written[:-1].replace('const ', '').strip()
        if element in ('float', 'int') or element in handles:
            return 'array'
    return None


def check_function(fn, errors, callbacks, handles, any_handle):
    name = fn.name
    where = f'{fn.header}: {name}'
    layer = fn.header.split('/', 1)[0]
    if not name.startswith('wgf_'):
        errors.append(f'{where}: a public function is named wgf_<section>_<action>')
    elif name.startswith(f'wgf_{layer}_') and not fn.header.endswith(f'/wgf_{layer}.h'):
        errors.append(f'{where}: a public function is named wgf_<section>_<action>; {layer} is a layer, '
                      'not a section')
    if '_priv_' in name:
        errors.append(f'{where}: a private name (_priv_) in a public header')
    if not fn.exported:
        errors.append(f'{where}: not marked WGF_API, so it is not exported')
    if fn.variadic:
        errors.append(f'{where}: takes "...": a binding can\'t pass it; take finished text')
    if fn.returns_canonical is None:
        errors.append(f'{where}: returns a function pointer')
    else:
        kind = classify(fn.returns, fn.returns_canonical, handles)
        if kind == 'any-handle' and name not in any_handle:
            errors.append(f'{where}: returns wgf_handle_t: a handle is its kind\'s type (wgf_<kind>_t), '
                          'unless ANY_HANDLE says why')
        if kind is None or kind == 'array':
            errors.append(f'{where}: returns {fn.returns}, which the hard rule doesn\'t allow')
        elif kind == 'span' and not name.endswith('_get_data'):
            errors.append(f'{where}: returns bytes, which only a _get_data with a _get_size may')
    for i, param in enumerate(fn.params):
        kind = classify(param.type, param.canonical, handles)
        pname = param.name or f'#{i + 1}'
        if name in callbacks and param.type in (callbacks[name][0], 'void *'):
            continue
        if kind == 'math':
            errors.append(f'{where}: parameter {pname} is {param.type}: a math value is only returned; '
                          'take its components')
        elif kind == 'any-handle' and name not in any_handle:
            errors.append(f'{where}: parameter {pname} is wgf_handle_t: a handle is its kind\'s type '
                          '(wgf_<kind>_t), unless ANY_HANDLE says why')
        if kind is None:
            errors.append(f'{where}: parameter {pname} is {param.type}, which the hard rule doesn\'t allow')
        elif kind == 'span':
            following = fn.params[i + 1] if i + 1 < len(fn.params) else None
            if following is None or following.canonical.replace('const ', '') != 'int' or following.name != 'size':
                errors.append(f'{where}: byte span {pname} must be followed by `int size`')
        elif kind == 'array':
            following = fn.params[i + 1] if i + 1 < len(fn.params) else None
            if following is None or following.canonical.replace('const ', '') != 'int' or \
                    not (following.name == 'count' or following.name.endswith('_count')):
                errors.append(f'{where}: array {pname} must be followed by `int count` (or `int <name>_count`)')


def check_prefix(fn, errors, handles, made_from, facets=ACTOR_FACETS):
    """A call named for a kind, or an actor's facet, acts on that kind: its first handle
    parameter, or with none the handle it returns, is of that kind (an actor, for a facet),
    unless MADE_FROM lists it."""
    kinds = {h[len('wgf_'):-len('_t')] for h in handles} - {'handle'}
    prefix = next((k for k in sorted(kinds | set(facets), key=len, reverse=True) if fn.name.startswith(f'wgf_{k}_')),
                  None)
    if prefix is None or fn.name in made_from:
        return
    def kind_of(ctype):
        base = ctype.replace('const ', '').replace('*', '').strip()
        return base[len('wgf_'):-len('_t')] if base in handles else None
    found = next((k for k in (kind_of(p.type) for p in fn.params) if k is not None), None)
    what = 'takes'
    if found is None:
        found, what = kind_of(fn.returns), 'returns'
    if found is None or found == 'handle' or found == prefix or (prefix in facets and found == 'actor'):
        return
    expected = 'an actor (wgf_actor_t)' if prefix in facets else f'wgf_{prefix}_t'
    errors.append(f'{fn.header}: {fn.name}: named for {prefix}, it {what} wgf_{found}_t first: a call takes what '
                  f'its name says, {expected}; rename it for what it acts on, or list it in MADE_FROM with why')


def check_functions(functions, errors, exempt, paired):
    for name, fn in sorted(functions.items()):
        for verb in ('is', 'has', 'can'):
            if f'_{verb}_' in name and fn.returns_canonical not in ('_Bool', 'bool'):
                errors.append(f'{name}: a predicate ({verb}_) returns bool')
        if name in paired:
            for getter in paired[name]:
                if getter not in functions:
                    errors.append(f'{name}: GETTERS_PAIRED lists {getter}, which no public header declares')
        elif '_set_' in name:
            head, value = name.rsplit('_set_', 1)
            getters = [f'{head}_get_{value}', f'{head}_is_{value}', f'{head}_has_{value}']
            if not any(g in functions for g in getters) and name not in exempt:
                errors.append(f'{name}: no {getters[0]} (or is_/has_); add one, or list it in '
                              'GETTERS_EXEMPT with the reason')
            elif name in exempt and any(g in functions for g in getters):
                errors.append(f'{name}: listed in GETTERS_EXEMPT, but it has a getter now; take it off')
        # a resource's path is wgf_resource_get_path's (wgf_resource.h), for every kind
        if name.endswith('_create') and any(p.name == 'path' for p in fn.params) and \
                name[:-len('_create')] + '_get_path' not in functions and 'wgf_resource_get_path' not in functions:
            errors.append(f'{name}: made from a file, with no {name[:-len("_create")]}_get_path '
                          '(or wgf_resource_get_path) beside it')
        if name.endswith('_get_data') and name[:-len('_get_data')] + '_get_size' not in functions:
            errors.append(f'{name}: returns bytes with no {name[:-len("_get_data")]}_get_size beside it')


def check_lists(functions, errors, exempt, paired, callbacks, any_handle, made_from):
    """A list entry that names no public call fails, so the lists can't go stale."""
    for list_name, entries in (('GETTERS_EXEMPT', exempt), ('GETTERS_PAIRED', paired),
                               ('CALLBACKS_ALLOWED', callbacks), ('ANY_HANDLE', any_handle),
                               ('MADE_FROM', made_from)):
        for name in sorted(set(entries) - set(functions)):
            errors.append(f'{name}: listed in {list_name}, but no public header declares it; take it off')


def check_macros(api, errors):
    """Every macro a public header defines is a libwgf name: every program that includes
    the header gets it."""
    for name, (_, header) in sorted(api.defines.items()):
        if not name.startswith('WGF_'):
            errors.append(f'{header}: {name}: a public macro is WGF_<SECTION>_*')
    for name, header in sorted(api.macros.items()):
        if not name.lower().startswith('wgf_'):
            errors.append(f'{header}: {name}: a public macro is wgf_ or WGF_')


def handle_types(api):
    """Every handle type: a typedef of wgf_handle_t."""
    return {name for name, (spelling, _) in api.typedefs.items() if spelling == 'wgf_handle_t'}


def check_tree(root, exempt=None, paired=None, callbacks=None, any_handle=None, made_from=None):
    """Every rule broken in the public headers under `root`, and the public functions,
    or None when there is no clang."""
    exempt = GETTERS_EXEMPT if exempt is None else exempt
    paired = GETTERS_PAIRED if paired is None else paired
    callbacks = CALLBACKS_ALLOWED if callbacks is None else callbacks
    any_handle = ANY_HANDLE if any_handle is None else any_handle
    made_from = MADE_FROM if made_from is None else made_from
    api = headers.read(root)
    if api is None:
        return None
    errors = []
    # static inline functions are C sugar no binding sees
    functions = {name: fn for name, fn in api.functions.items() if not fn.static}
    handles = handle_types(api)
    for fn in functions.values():
        check_function(fn, errors, callbacks, handles, any_handle)
        check_prefix(fn, errors, handles, made_from)
    check_functions(functions, errors, exempt, paired)
    check_lists(functions, errors, exempt, paired, callbacks, any_handle, made_from)
    check_macros(api, errors)
    return errors, functions


BAD_HEADER = r'''
#ifndef WGF_BAD_H
#define WGF_BAD_H
#include <stdbool.h>
#include "wgf.h"
#include "wgf_handle.h"
#include "wgf_quat.h"
#include "wgf_vec2.h"
typedef struct wgf_bad_record_t { int a; } wgf_bad_record_t;
typedef void (*wgf_bad_fn)(void *user);
typedef wgf_handle_t wgf_good_t;
typedef wgf_handle_t wgf_tool_t;
WGF_API void wgf_good_mend(wgf_good_t good, wgf_tool_t tool);
typedef wgf_handle_t wgf_widget_t;
WGF_API void wgf_widget_takes_tool(wgf_tool_t tool);
WGF_API wgf_tool_t wgf_widget_makes_tool(void);
WGF_API wgf_good_t wgf_good_make(wgf_tool_t tool);
WGF_API void bad_unprefixed(void);
void wgf_bad_not_exported(void);
WGF_API void wgf_bad_printf(const char *format, ...);
WGF_API void wgf_bad_struct_pointer(wgf_bad_record_t *record);
WGF_API wgf_bad_record_t wgf_bad_record(void);
WGF_API void wgf_bad_callback(wgf_bad_fn fn, void *user);
WGF_API void wgf_bad_span(const unsigned char *data, unsigned size);
WGF_API const unsigned char *wgf_bad_bytes(void);
WGF_API const unsigned char *wgf_bad_lonely_get_data(void);
WGF_API void wgf_bad_set_speed(float speed);
WGF_API int wgf_bad_is_ready(void);
WGF_API void wgf_bad_vec_pointer(wgf_vec3_t *v);
WGF_API wgf_good_t wgf_bad_create(const char *path);
WGF_API wgf_good_t wgf_good_create(const char *path);
WGF_API const char *wgf_good_get_path(wgf_good_t good);
WGF_API void wgf_good_span(const unsigned char *data, int size);
WGF_API bool wgf_good_set_position(float x, float y, float z);
WGF_API wgf_vec3_t wgf_good_get_position(void);
WGF_API wgf_quat_t wgf_bad_rotation(wgf_quat_t a, const wgf_vec2_t b);
WGF_API wgf_handle_t wgf_bad_any_create(void);
WGF_API void wgf_bad_any_use(wgf_handle_t thing);
WGF_API void wgf_good_any_use(wgf_handle_t thing);
WGF_API int wgf_good_fill(const wgf_good_t *goods, int count, float *out_xy, int out_count);
WGF_API int wgf_good_read(const float *values, int count);
WGF_API void wgf_bad_array(const float *values, int n);
WGF_API void wgf_bad_array_alone(int *values);
WGF_API float *wgf_bad_array_return(void);
WGF_API void wgf_bad_double_array(const double *values, int count);
WGF_API void wgf_good_set_volume(float volume);
WGF_API void wgf_core_bad_layer_name(void);
WGF_API float wgf_good_get_volume(void);
WGF_API bool wgf_good_is_open(void);
WGF_API void wgf_bad_priv_leak(void);
WGF_API void wgf_bad_set_loud(float loud);
WGF_API float wgf_bad_get_loud(void);
#define BAD_UNPREFIXED 1
#define bad_unprefixed_macro(x) (x)
#define WGF_GOOD_CONSTANT 2
static inline void wgf_good_sugar(const char *format, ...) { (void)format; }
#endif
'''

EXPECTED = ['bad_unprefixed: a public function is named', 'wgf_bad_not_exported: not marked WGF_API',
            'wgf_bad_printf: takes "..."', 'wgf_bad_struct_pointer: parameter record',
            'wgf_bad_record: returns', 'wgf_bad_callback: parameter fn',
            'wgf_bad_callback: parameter user', 'wgf_bad_span: byte span data',
            'wgf_bad_bytes: returns bytes', 'wgf_bad_lonely_get_data: returns bytes with no',
            'wgf_bad_set_speed: no wgf_bad_get_speed', 'wgf_bad_is_ready: a predicate',
            'wgf_bad_vec_pointer: parameter v', 'wgf_bad_create: made from a file', 'wgf_core_bad_layer_name: a public function is named',
            'wgf_bad_priv_leak: a private name', 'BAD_UNPREFIXED: a public macro', 'bad_unprefixed_macro: a public macro',
            'wgf_bad_set_loud: listed in GETTERS_EXEMPT, but it has a getter',
            'wgf_bad_gone_set_x: listed in GETTERS_EXEMPT, but no public header',
            'wgf_bad_gone_set_y: listed in GETTERS_PAIRED, but no public header',
            'wgf_bad_gone_run: listed in CALLBACKS_ALLOWED, but no public header',
            'wgf_bad_gone_any: listed in ANY_HANDLE, but no public header',
            'wgf_bad_rotation: parameter a is wgf_quat_t', 'wgf_bad_rotation: parameter b is const wgf_vec2_t',
            'wgf_bad_any_create: returns wgf_handle_t', 'wgf_bad_any_use: parameter thing is wgf_handle_t',
            'wgf_bad_array: array values must be followed by `int count`',
            'wgf_bad_array_alone: array values must be followed', 'wgf_bad_array_return: returns float *',
            'wgf_bad_double_array: parameter values is const double *',
            'wgf_widget_takes_tool: named for widget, it takes wgf_tool_t first',
            'wgf_widget_makes_tool: named for widget, it returns wgf_tool_t first',
            'wgf_bad_gone_made: listed in MADE_FROM, but no public header']

# The self-test's own lists: one entry each that names nothing, and one exempt setter with a getter
SELF_TEST_EXEMPT = {'wgf_bad_set_loud': 'it has a getter', 'wgf_bad_gone_set_x': 'there is no such call'}
SELF_TEST_PAIRED = {'wgf_bad_gone_set_y': ('wgf_bad_gone_get_y',)}
SELF_TEST_CALLBACKS = {'wgf_bad_gone_run': ('wgf_bad_fn', 'there is no such call')}
SELF_TEST_ANY = {'wgf_good_any_use': 'takes any kind', 'wgf_bad_gone_any': 'there is no such call'}
SELF_TEST_MADE = {'wgf_good_make': 'a good is made from a tool', 'wgf_bad_gone_made': 'there is no such call'}


def self_test():
    """The checker against a header that breaks every rule: each break is caught, and
    nothing that keeps the rules is."""
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        (tree / 'core' / 'include').mkdir(parents=True)
        shutil.copytree(ROOT / 'include', tree / 'include')
        shutil.copy(ROOT / 'core' / 'include' / 'wgf.h', tree / 'core' / 'include')
        shutil.copy(ROOT / 'core' / 'include' / 'wgf_handle.h', tree / 'core' / 'include')
        shutil.copytree(ROOT / 'math' / 'include', tree / 'math' / 'include')
        (tree / 'core' / 'include' / 'wgf_bad.h').write_text(BAD_HEADER)
        errors, _ = check_tree(tree, SELF_TEST_EXEMPT, SELF_TEST_PAIRED, SELF_TEST_CALLBACKS, SELF_TEST_ANY,
                               SELF_TEST_MADE)
    errors = [e for e in errors if 'wgf_bad.h' in e or e.startswith('wgf_bad_') or e.startswith('wgf_core_bad_')]
    missed = [e for e in EXPECTED if not any(e in found for found in errors)]
    false = [e for e in errors if 'good' in e.split(':', 2)[1 if 'wgf_bad.h' in e else 0]]
    for e in missed:
        print(f'check_api self-test: missed: {e}')
    for e in false:
        print(f'check_api self-test: flagged a good one: {e}')
    if missed or false:
        return 1
    print(f'check_api self-test: all {len(EXPECTED)} kinds of break caught, none of the good ones flagged')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--self-test', action='store_true', help='check the checker against a header that breaks every rule')
    opts = parser.parse_args()
    if headers.find_clang()[0] is None:
        print('check_api: SKIPPING the public API check (no clang: none from Emscripten, $EMSDK, or PATH)')
        return SKIP
    if opts.self_test:
        return self_test()
    errors, functions = check_tree(ROOT)
    for e in errors:
        print(f'check_api: {e}')
    if errors:
        print(f'check_api: {len(errors)} broken rule(s) in {len(functions)} public functions')
        return 1
    print(f'check_api: {len(functions)} public functions keep the rules')
    return 0


if __name__ == '__main__':
    sys.exit(main())
