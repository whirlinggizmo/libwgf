#ifndef WGF_PHYSICS3D_JOLT_WORLD_PRIV_H
#define WGF_PHYSICS3D_JOLT_WORLD_PRIV_H

/* What the Jolt layer's other C++ files (wgf_physics3d_jolt_character.cpp) take from its
 * world (wgf_physics3d_jolt.cpp), C++ alone: the system and its scratch, a body's shape and
 * layer made as a body's are, and a hook for the world's end, so a part a program may never
 * use is a file of its own that nothing else names (characters: Jolt's CharacterVirtual;
 * vehicles: its VehicleConstraint). */

#include "wgf_physics3d_jolt_priv.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace wgf_jolt {

/* The running world's; NULL (and nullptr) before start and after stop. */
JPH::PhysicsSystem *System();
JPH::TempAllocator *Temp();
/* A body's shape for `d`, placed and its mass offset as a body's (nullptr when it can't be
 * made), and its object layer. */
JPH::RefConst<JPH::Shape> PlacedShape(const wgf_physics3d_priv_body_desc_t &d);
JPH::ObjectLayer LayerOf(const wgf_physics3d_priv_body_desc_t &d);
JPH::Vec3 V(const float v[3]);
JPH::Quat Q(const float q[4]);
/* A part's own (characters, vehicles), each set as the part first makes something: called as
 * the world stops, before it goes (two at most), and as each step begins. */
void AddOnStop(void (*stop)());
void SetBeforeStep(void (*step)());
/* A vector or a turn into floats. */
void Put(JPH::Vec3 v, float out[3]);
void Put(JPH::Quat q, float out[4]);

} // namespace wgf_jolt

#endif
