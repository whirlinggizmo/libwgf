#include "wgf_scene.h"

#include <stdlib.h>
#include <string.h>

#include "wgf_actor.h"
#include "wgf_asset.h"
#include "wgf_behavior.h"
#include "wgf_component.h"
#include "wgf_ecs_priv.h"
#include "wgf_ecs_scene_priv.h"

/* A scene loaded again in place (wgf_scene.h): what changed between a block as its actors were
 * made and as the file has it now, changed on them, block by block, matched by their paths.
 * A file of its own, linked by wgf_scene_enable_reload alone, which hands the scene file its
 * side (wgf_ecs_priv_scene_set_reload): a program that never calls it links none of this, the
 * records of what scenes made among it. */

typedef wgf_ecs_priv_scene_setting_t setting_t;
typedef wgf_ecs_priv_scene_line_t line_t;
typedef wgf_ecs_priv_scene_thing_t thing_t;

/* ---- what scenes made ------------------------------------------------------------- */

/* What scenes made, kept to change in place when a scene's file loads again: each actor with
 * its block (an index into its scene's data, moved along as the file loads again), and each
 * parent an instantiate put a scene's top actors under. */
typedef struct made_t {
    wgf_scene_t scene; /* 0: let go of */
    int thing;
    wgf_actor_t actor;
    bool node; /* a glTF file's node the block's lines were put on: the file's, never removed */
} made_t;

typedef struct placed_t {
    wgf_scene_t scene;
    wgf_actor_t parent; /* 0: none */
} placed_t;

static struct {
    made_t *at;
    int count, capacity;
} made;

static struct {
    placed_t *at;
    int count, capacity;
} placed;

static bool changing; /* a scene's file loading again: its records walked by index, none compacted */

/* The records of actors gone, and of scenes let go of, dropped. */
static void compact(void)
{
    int i, kept = 0;
    for (i = 0; i < made.count; i++) {
        if (made.at[i].scene != 0 && wgf_handle_is_alive(made.at[i].actor)) made.at[kept++] = made.at[i];
    }
    made.count = kept;
    for (i = kept = 0; i < placed.count; i++) {
        if (placed.at[i].scene != 0 && (placed.at[i].parent == 0 || wgf_handle_is_alive(placed.at[i].parent))) {
            placed.at[kept++] = placed.at[i];
        }
    }
    placed.count = kept;
}

static void remember(wgf_scene_t scene, int thing, wgf_actor_t actor, bool node)
{
    if (made.count == made.capacity && !changing) compact();
    if (made.count == made.capacity) {
        const int capacity = made.capacity > 0 ? made.capacity * 2 : 64;
        made_t *grown = (made_t *)realloc(made.at, sizeof(made_t) * (size_t)capacity);
        if (grown == NULL) return; /* not changed when its file loads again: nothing else lost */
        made.at = grown;
        made.capacity = capacity;
    }
    made.at[made.count].scene = scene;
    made.at[made.count].thing = thing;
    made.at[made.count].actor = actor;
    made.at[made.count].node = node;
    made.count++;
}

/* Kept once. */
static void remember_parent(wgf_scene_t scene, wgf_actor_t parent)
{
    int i;
    for (i = 0; i < placed.count; i++) {
        if (placed.at[i].scene == scene && placed.at[i].parent == parent) return;
    }
    if (placed.count == placed.capacity) {
        const int capacity = placed.capacity > 0 ? placed.capacity * 2 : 8;
        placed_t *grown = (placed_t *)realloc(placed.at, sizeof(placed_t) * (size_t)capacity);
        if (grown == NULL) return;
        placed.at = grown;
        placed.capacity = capacity;
    }
    placed.at[placed.count].scene = scene;
    placed.at[placed.count].parent = parent;
    placed.count++;
}

static void forget(wgf_scene_t scene)
{
    int i;
    for (i = 0; i < made.count; i++) {
        if (made.at[i].scene == scene) made.at[i].scene = 0;
    }
    for (i = 0; i < placed.count; i++) {
        if (placed.at[i].scene == scene) placed.at[i].scene = 0;
    }
}

static void free_records(void)
{
    free(made.at);
    free(placed.at);
    memset(&made, 0, sizeof(made));
    memset(&placed, 0, sizeof(placed));
}

/* ---- what changed ----------------------------------------------------------------- */

/* A block's settings as an actor is made of them: its prefab's first (and that one's), then its
 * own, the last of each line kind, behavior, and key kept. A fact with no setting is a line's
 * own: a component or a behavior the block has. What a file loaded again is compared by. */
typedef struct fact_t {
    int kind;                 /* the line's, into kinds[] */
    const char *behavior;     /* a behavior line's name; "" for the rest */
    const setting_t *setting; /* NULL: the line itself */
    const line_t *line;
} fact_t;

typedef struct facts_t {
    fact_t *at;
    int count, capacity;
} facts_t;

static int find_fact(const facts_t *facts, int kind, const char *behavior, const char *key)
{
    int i;
    for (i = 0; i < facts->count; i++) {
        const fact_t *f = &facts->at[i];
        if (f->kind == kind && strcmp(f->behavior, behavior) == 0 &&
            (f->setting == NULL ? key == NULL : key != NULL && strcmp(f->setting->key, key) == 0)) {
            return i;
        }
    }
    return -1;
}

/* `fact` in place of the last of its kind, behavior, and key, or added; false out of memory. */
static bool add_fact(facts_t *facts, fact_t fact)
{
    const int at = find_fact(facts, fact.kind, fact.behavior, fact.setting != NULL ? fact.setting->key : NULL);
    if (at >= 0) {
        facts->at[at] = fact;
        return true;
    }
    if (facts->count == facts->capacity) {
        const int capacity = facts->capacity > 0 ? facts->capacity * 2 : 32;
        fact_t *grown = (fact_t *)realloc(facts->at, sizeof(fact_t) * (size_t)capacity);
        if (grown == NULL) return false;
        facts->at = grown;
        facts->capacity = capacity;
    }
    facts->at[facts->count++] = fact;
    return true;
}

/* Block `thing`'s facts into `facts`, as apply_thing applies its lines; `behavior` the name of
 * the behavior its parameters go to, as it goes. False out of memory. */
static bool gather(const wgf_ecs_priv_scene_data_t *data, int thing, facts_t *facts, const char **behavior,
                   int depth)
{
    const thing_t *block = &data->things[thing];
    int i, j;
    if (block->from >= 0 && depth < WGF_ECS_PRIV_SCENE_DEPTH_MAX &&
        !gather(data, block->from, facts, behavior, depth + 1)) {
        return false;
    }
    for (i = 0; i < block->count; i++) {
        const line_t *line = &block->lines[i];
        const bool is_behavior = wgf_ecs_priv_scene_role(line->kind) == WGF_ECS_PRIV_SCENE_ROLE_BEHAVIOR;
        fact_t fact;
        for (j = 0; is_behavior && j < line->count; j++) {
            if (strcmp(line->settings[j].key, "name") == 0) *behavior = line->settings[j].value;
        }
        fact.kind = line->kind;
        fact.behavior = is_behavior ? *behavior : "";
        fact.setting = NULL;
        fact.line = line;
        if (!add_fact(facts, fact)) return false;
        for (j = 0; j < line->count; j++) {
            if (is_behavior && strcmp(line->settings[j].key, "name") == 0) continue;
            fact.setting = &line->settings[j];
            if (!add_fact(facts, fact)) return false;
        }
    }
    return true;
}

/* What changed between an actor's block as it was made (`was`) and as the file has it now: its
 * components and behaviors the file no longer has let go of, the ones it has new added, and
 * each setting new or of another value applied. A setting the file no longer has is left as
 * it is: what it was before the file set it isn't known. A voice's play isn't played again. */
static void change(wgf_actor_t actor, const facts_t *was, const facts_t *now)
{
    bool behaviors = false;
    int i;
    for (i = 0; i < was->count; i++) {
        const fact_t *f = &was->at[i];
        if (f->setting != NULL || find_fact(now, f->kind, f->behavior, NULL) >= 0) continue;
        if (wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_COMPONENT) {
            wgf_actor_remove_component(actor, (wgf_component_t)wgf_ecs_priv_scene_what(f->kind));
        } else if (wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_BEHAVIOR && f->behavior[0] != '\0') {
            const int behavior = wgf_actor_find_behavior(actor, f->behavior);
            if (behavior != 0) wgf_actor_remove_behavior(actor, behavior);
            behaviors = true;
        }
    }
    for (i = 0; i < now->count; i++) {
        const fact_t *f = &now->at[i];
        if (f->setting != NULL || find_fact(was, f->kind, f->behavior, NULL) >= 0) continue;
        if (wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_COMPONENT) {
            wgf_actor_add_component(actor, (wgf_component_t)wgf_ecs_priv_scene_what(f->kind));
        } else if (wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_BEHAVIOR && f->behavior[0] != '\0') {
            wgf_actor_add_behavior(actor, f->behavior);
            behaviors = true;
        }
    }
    for (i = 0; i < now->count; i++) {
        const fact_t *f = &now->at[i];
        int at, behavior = 0;
        if (f->setting == NULL) continue;
        at = find_fact(was, f->kind, f->behavior, f->setting->key);
        if (at >= 0 && strcmp(was->at[at].setting->value, f->setting->value) == 0) continue;
        if (wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_BEHAVIOR) {
            behavior = wgf_actor_find_behavior(actor, f->behavior);
            if (behavior == 0) continue;
            behaviors = true;
        }
        if (wgf_ecs_priv_scene_what(f->kind) == WGF_COMPONENT_VOICE &&
            wgf_ecs_priv_scene_role(f->kind) == WGF_ECS_PRIV_SCENE_ROLE_COMPONENT &&
            (strcmp(f->setting->key, "sound") == 0 || strcmp(f->setting->key, "streamed") == 0)) {
            wgf_ecs_priv_scene_apply_sound(actor, f->line);
            continue;
        }
        wgf_ecs_priv_scene_apply(actor, f->kind, f->setting, behavior);
    }
    if (behaviors) wgf_ecs_priv_behaviors_resolve(actor); /* its references found in the tree it's in now */
}

/* An actor made from block `thing` of `was` changed to block `now_thing` of `now`; false out of
 * memory (it is left as it was). */
static bool change_block(wgf_actor_t actor, const wgf_ecs_priv_scene_data_t *was, int thing,
                         const wgf_ecs_priv_scene_data_t *now, int now_thing)
{
    facts_t before = {NULL, 0, 0}, after = {NULL, 0, 0};
    const char *behavior = "";
    bool ok = gather(was, thing, &before, &behavior, 0);
    behavior = "";
    ok = ok && gather(now, now_thing, &after, &behavior, 0);
    if (ok) change(actor, &before, &after);
    free(before.at);
    free(after.at);
    return ok;
}

/* `scene`'s file loaded again, its data `was` and now `now` (the scene's already): what it
 * made changed in place, block by block, matched by their paths. */
static void again(wgf_scene_t scene, const wgf_ecs_priv_scene_data_t *was, const wgf_ecs_priv_scene_data_t *now)
{
    wgf_actor_t *fresh = NULL;
    int i, j, count, place_count, fresh_count = 0;
    wgf_scene_t before;
    wgf_ecs_priv_scene_follow(scene, was, now);
    before = wgf_ecs_priv_scene_set_making(scene);
    /* each actor it made: changed to its block in the new file, removed with it, or made again
       for a kind that changed (its handle a new one); its record moved to the new block */
    compact();
    changing = true;
    count = made.count;
    for (i = 0; i < count; i++) {
        const made_t m = made.at[i]; /* by index: making an actor may move the records */
        int now_thing;
        if (m.scene != scene || !wgf_handle_is_alive(m.actor)) continue;
        now_thing = wgf_ecs_priv_scene_find_path(now, was->things[m.thing].path, was->things[m.thing].prefab);
        if (now_thing < 0) {
            made.at[i].scene = 0;
            if (!m.node) wgf_actor_destroy(m.actor, WGF_ACTOR_DESTROY_CHILDREN);
            continue;
        }
        if (!m.node && wgf_ecs_priv_scene_kind_of(now, now_thing) != wgf_ecs_priv_scene_kind_of(was, m.thing)) {
            const wgf_actor_t parent = wgf_actor_get_parent(m.actor);
            wgf_actor_t *grown;
            made.at[i].scene = 0;
            wgf_actor_destroy(m.actor, WGF_ACTOR_DESTROY_CHILDREN);
            grown = (wgf_actor_t *)realloc(fresh, sizeof(wgf_actor_t) * (size_t)(fresh_count + 1));
            if (grown == NULL) continue;
            fresh = grown;
            fresh[fresh_count] = wgf_ecs_priv_scene_make(now, now_thing, parent, parent != 0 ? 1 : 0);
            if (fresh[fresh_count] != 0) fresh_count++;
            continue;
        }
        change_block(m.actor, was, m.thing, now, now_thing);
        made.at[i].thing = now_thing;
    }
    /* each block new to the file, made under each actor of its parent's block (one new too is
       made with its parent), or, at the top, under each parent an instantiate put the scene */
    count = made.count;
    place_count = placed.count;
    for (j = 0; j < now->count; j++) {
        const thing_t *block = &now->things[j];
        if (wgf_ecs_priv_scene_find_path(was, block->path, block->prefab) >= 0) continue;
        if (block->parent >= 0 &&
            wgf_ecs_priv_scene_find_path(was, now->things[block->parent].path, block->prefab) < 0) {
            continue;
        }
        if (block->parent < 0 && block->prefab) continue; /* a prefab: made as it is spawned */
        for (i = 0; i < (block->parent >= 0 ? count : place_count); i++) {
            wgf_actor_t parent, actor;
            wgf_actor_t *grown;
            if (block->parent >= 0) {
                const made_t *m = &made.at[i];
                if (m->scene != scene || m->thing != block->parent || !wgf_handle_is_alive(m->actor)) continue;
                parent = m->actor;
            } else {
                if (placed.at[i].scene != scene) continue;
                parent = placed.at[i].parent;
            }
            actor = wgf_ecs_priv_scene_make(now, j, parent, block->parent >= 0 ? 1 : 0);
            grown = actor != 0 ? (wgf_actor_t *)realloc(fresh, sizeof(wgf_actor_t) * (size_t)(fresh_count + 1)) : NULL;
            if (grown == NULL) continue;
            fresh = grown;
            fresh[fresh_count++] = actor;
        }
    }
    wgf_ecs_priv_scene_set_making(before);
    changing = false;
    for (i = 0; i < fresh_count; i++) wgf_ecs_priv_scene_resolve(fresh[i]); /* all made: each can refer to any */
    free(fresh);
}

bool wgf_scene_enable_reload(void)
{
    wgf_asset_enable_reload(); /* what loads anything again */
    static const wgf_ecs_priv_scene_reload_t side = {again, remember, remember_parent, forget, free_records};
    wgf_ecs_priv_scene_set_reload(&side);
    return true;
}
