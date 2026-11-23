#include "wgf_physics3d.h"

#include "wgf_core_load_priv.h" /* WGF_CORE_PRIV_ON_LINK */
#include "wgf_physics3d_jolt_priv.h"
#include "wgf_physics3d_priv.h"

/* Characters linked on purpose (wgf_physics3d_enable_characters, or a game's "characters"
 * feature, which links this file): the hooks physics3d reaches Jolt's CharacterVirtual
 * through, set as this file is linked, so a program that names it has them from its start. */

static const wgf_physics3d_priv_characters_t characters = {
    wgf_physics3d_priv_jolt_character_create, wgf_physics3d_priv_jolt_character_destroy,
    wgf_physics3d_priv_jolt_character_update, wgf_physics3d_priv_jolt_character_set_pose,
    wgf_physics3d_priv_jolt_character_ground};

WGF_CORE_PRIV_ON_LINK(link_characters)
{
    wgf_physics3d_priv_set_characters(&characters);
}

bool wgf_physics3d_enable_characters(void)
{
    wgf_physics3d_priv_set_characters(&characters);
    return true;
}
