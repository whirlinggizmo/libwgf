// Characters on Jolt (wgf_physics3d_jolt_priv.h): Jolt's CharacterVirtual, a file of its own so
// a program that makes none links none of it (about 42 KB of code), reached only from
// physics3d's characters (wgf_physics3d_character.c), which a program links on purpose
// (wgf_physics_enable_characters). Its own storage; the world it lives in is the Jolt layer's
// (wgf_physics3d_jolt_world_priv.h), which lets it go as the world stops.

#include "wgf_physics3d_jolt_priv.h"
#include "wgf_physics3d_jolt_world_priv.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <algorithm>

using namespace JPH;

namespace {

constexpr int MAX_CHARACTERS = 256;

CharacterVirtual *characters[MAX_CHARACTERS] = {};
ObjectLayer layers[MAX_CHARACTERS] = {};

CharacterVirtual *At(int character)
{
    return wgf_jolt::System() != nullptr && character >= 0 && character < MAX_CHARACTERS ? characters[character]
                                                                                         : nullptr;
}

// The world stopping: every character let go of before it goes.
void StopAll()
{
    for (int i = 0; i < MAX_CHARACTERS; i++) {
        if (characters[i] != nullptr) characters[i]->Release();
        characters[i] = nullptr;
    }
}

} // namespace

extern "C" {

int wgf_physics3d_priv_jolt_character_create(const wgf_physics3d_priv_body_desc_t *d, float max_slope)
{
    PhysicsSystem *system = wgf_jolt::System();
    if (system == nullptr || d == nullptr) return -1;
    int slot = 0;
    while (slot < MAX_CHARACTERS && characters[slot] != nullptr) slot++;
    if (slot == MAX_CHARACTERS) return -1;
    RefConst<Shape> shape = wgf_jolt::PlacedShape(*d);
    if (shape == nullptr) return -1;
    wgf_jolt::SetOnStop(StopAll);
    Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings();
    settings->mShape = shape;
    settings->mMaxSlopeAngle = max_slope;
    settings->mInnerBodyShape = shape; // a body in the world too, so rays, sensors, and queries find it
    settings->mInnerBodyLayer = wgf_jolt::LayerOf(*d);
    CharacterVirtual *character =
        new CharacterVirtual(settings, RVec3(wgf_jolt::V(d->position)), wgf_jolt::Q(d->rotation), d->user, system);
    character->AddRef();
    characters[slot] = character;
    layers[slot] = wgf_jolt::LayerOf(*d);
    return slot;
}

void wgf_physics3d_priv_jolt_character_destroy(int character)
{
    CharacterVirtual *c = At(character);
    if (c == nullptr) return;
    c->Release();
    characters[character] = nullptr;
}

// One step of the character: moved by `velocity` (gravity added while it isn't supported),
// sliding, climbing steps up to `step`, kept on the floor going down them.
void wgf_physics3d_priv_jolt_character_update(int character, const float velocity[3], float step, float dt,
                                              float position[3], float out_velocity[3])
{
    CharacterVirtual *c = At(character);
    if (c == nullptr) return;
    PhysicsSystem &system = *wgf_jolt::System();
    Vec3 v = wgf_jolt::V(velocity);
    if (!c->IsSupported()) v += system.GetGravity() * dt;
    else if (v.GetY() < 0.0f) v.SetY(0.0f);
    c->SetLinearVelocity(v);
    CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = Vec3(0.0f, std::max(step, 0.0f), 0.0f);
    settings.mStickToFloorStepDown = Vec3(0.0f, -std::max(step, 0.05f), 0.0f);
    const ObjectLayer layer = layers[character];
    c->ExtendedUpdate(dt, system.GetGravity(), settings, system.GetDefaultBroadPhaseLayerFilter(layer),
                      system.GetDefaultLayerFilter(layer), {}, {}, *wgf_jolt::Temp());
    const RVec3 p = c->GetPosition();
    position[0] = float(p.GetX()), position[1] = float(p.GetY()), position[2] = float(p.GetZ());
    const Vec3 moved = c->GetLinearVelocity();
    out_velocity[0] = moved.GetX(), out_velocity[1] = moved.GetY(), out_velocity[2] = moved.GetZ();
}

void wgf_physics3d_priv_jolt_character_set_pose(int character, const float position[3], const float rotation[4])
{
    CharacterVirtual *c = At(character);
    if (c == nullptr) return;
    c->SetPosition(RVec3(wgf_jolt::V(position)));
    c->SetRotation(wgf_jolt::Q(rotation));
}

bool wgf_physics3d_priv_jolt_character_ground(int character, float normal[3])
{
    const CharacterVirtual *c = At(character);
    if (c == nullptr) return false;
    const Vec3 n = c->GetGroundNormal();
    normal[0] = n.GetX(), normal[1] = n.GetY(), normal[2] = n.GetZ();
    return c->GetGroundState() == CharacterBase::EGroundState::OnGround;
}

} // extern "C"
