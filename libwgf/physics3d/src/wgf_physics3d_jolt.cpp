// physics3d's way into Jolt (wgf_physics3d_jolt_priv.h): the one C++ file in libwgf, built
// without exceptions or RTTI, its C interface the calls physics3d makes. Only the shapes it
// makes are registered with Jolt's collision dispatch, not every type (Jolt's
// RegisterTypes), which keeps the rest of Jolt out of a program: measured on a car on a
// floor, 191 KB of wasm gzipped against 368 through joltc's API, whose init registers them
// all (docs/HISTORY.md, "Milestone 2, step 5").

#include "wgf_physics3d_jolt_priv.h"
#include "wgf_physics3d_jolt_world_priv.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/UnorderedMap.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterMask.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>

using namespace JPH;

namespace {

constexpr uint MAX_BODIES = 16384;
constexpr uint MAX_PAIRS = 16384;
constexpr uint MAX_CONTACTS = 8192;
constexpr uint32 STATIC_GROUP = 0x8000; // the group bit a static body has: its broadphase tree's
constexpr uint32 USER_BITS = 0x7FFF;

// Two broadphase trees, the static bodies' and the moving ones', as Jolt advises.
constexpr BroadPhaseLayer NON_MOVING(0);
constexpr BroadPhaseLayer MOVING(1);

// Sensors' overlaps, counted per pair of bodies: a shape of many parts (a mesh, a
// compound) touches a sensor with several, and enters and leaves it once.
class Overlaps final : public ContactListener {
public:
    void OnContactAdded(const Body &a, const Body &b, const ContactManifold &, ContactSettings &) override
    {
        const bool sensor = a.IsSensor() || b.IsSensor();
        // two solid bodies touching: told when one of them moves (a static pair never meets)
        if (!sensor && a.IsStatic() && b.IsStatic()) return;
        const uint64 key = Key(a.GetID(), b.GetID());
        uint32 &count = mCounts[key];
        if (count++ == 0) Note(true, a.IsSensor() ? a : b, a.IsSensor() ? b : a, !sensor);
    }

    void OnContactRemoved(const SubShapeIDPair &pair) override
    {
        const uint64 key = Key(pair.GetBody1ID(), pair.GetBody2ID());
        auto found = mCounts.find(key);
        if (found == mCounts.end()) return;
        if (--found->second == 0) {
            mCounts.erase(found);
            mGone.push_back(key);
        }
    }

    // After a step: the pairs that stopped touching, told now that the bodies can be read.
    void Settle(const BodyInterface &bodies)
    {
        for (uint64 key : mGone) {
            const BodyID a{uint32(key >> 32)}, b{uint32(key)};
            const bool a_sensor = bodies.IsSensor(a), contact = !a_sensor && !bodies.IsSensor(b);
            NoteIds(false, a_sensor ? a : b, a_sensor ? b : a, bodies, contact);
        }
        mGone.clear();
    }

    // A body gone: its pairs dropped, each telling the other it left.
    void Forget(BodyID id, const BodyInterface &bodies)
    {
        Array<uint64> gone;
        for (auto it = mCounts.begin(); it != mCounts.end(); ++it) {
            const BodyID a{uint32(it->first >> 32)}, b{uint32(it->first)};
            if (a == id || b == id) {
                const BodyID other = a == id ? b : a;
                if (bodies.IsAdded(other)) {
                    const bool id_sensor = bodies.IsSensor(id), contact = !id_sensor && !bodies.IsSensor(other);
                    const uint64 sensor_user = bodies.GetUserData(id_sensor ? id : other);
                    const uint64 other_user = bodies.GetUserData(id_sensor ? other : id);
                    mOut.push_back({false, contact, sensor_user, other_user});
                }
                gone.push_back(it->first);
            }
        }
        for (uint64 key : gone) mCounts.erase(key);
    }

    Array<wgf_physics3d_priv_overlap_t> mOut;

private:
    static uint64 Key(BodyID a, BodyID b)
    {
        const uint32 x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
        return x < y ? (uint64(x) << 32) | y : (uint64(y) << 32) | x;
    }

    void Note(bool entered, const Body &sensor, const Body &other, bool contact)
    {
        mOut.push_back({entered, contact, sensor.GetUserData(), other.GetUserData()});
    }

    void NoteIds(bool entered, BodyID sensor, BodyID other, const BodyInterface &bodies, bool contact)
    {
        mOut.push_back({entered, contact, bodies.GetUserData(sensor), bodies.GetUserData(other)});
    }

    UnorderedMap<uint64, uint32> mCounts;
    Array<uint64> mGone;
};

// Which bodies meet: two meet when either one's mask has the other's layer, as two colliders
// do (wgf_collider.h), where Jolt's own mask filter asks it of both. Two statics never meet.
class EitherMask final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override
    {
        const uint32 group_a = ObjectLayerPairFilterMask::sGetGroup(a);
        const uint32 group_b = ObjectLayerPairFilterMask::sGetGroup(b);
        const uint32 a_in_b = group_a & ObjectLayerPairFilterMask::sGetMask(b);
        const uint32 b_in_a = group_b & ObjectLayerPairFilterMask::sGetMask(a);
        if ((group_a & group_b & STATIC_GROUP) != 0) return false;
        return ((a_in_b | b_in_a) & USER_BITS) != 0;
    }
};

// Which broadphase tree a body looks in: both, since a body whose own mask has nothing may be
// in another's; but a static never looks among the statics.
class EitherTree final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer tree) const override
    {
        return (ObjectLayerPairFilterMask::sGetGroup(layer) & STATIC_GROUP) == 0 || tree != NON_MOVING;
    }
};

struct World {
    TempAllocatorImpl temp{8 * 1024 * 1024};
    JobSystemSingleThreaded jobs{cMaxPhysicsJobs};
    BroadPhaseLayerInterfaceMask broad{2};
    EitherTree object_vs_broad;
    EitherMask pairs;
    PhysicsSystem system;
    Overlaps overlaps;
};

World *world = nullptr;
// The parts' own (characters, vehicles), each set as the part first makes something: the
// world's end, and a step's start.
void (*on_stop[2])() = {};
void (*before_step)() = nullptr;

Vec3 V(const float v[3])
{
    return Vec3(v[0], v[1], v[2]);
}

Quat Q(const float q[4])
{
    return Quat(q[0], q[1], q[2], q[3]).Normalized();
}

void Put(Vec3 v, float out[3])
{
    out[0] = v.GetX();
    out[1] = v.GetY();
    out[2] = v.GetZ();
}

void Put(Quat q, float out[4])
{
    out[0] = q.GetX();
    out[1] = q.GetY();
    out[2] = q.GetZ();
    out[3] = q.GetW();
}

void Quiet(const char *format, ...)
{
    (void)format; // Jolt's traces are for its developers; a failure is reported by the call that met it
}

RefConst<Shape> MakeShape(const wgf_physics3d_priv_body_desc_t &d)
{
    switch (d.shape) {
        case WGF_PHYSICS3D_PRIV_BOX: {
            const Vec3 half(std::max(d.size[0], 0.01f), std::max(d.size[1], 0.01f), std::max(d.size[2], 0.01f));
            return new BoxShape(half, std::min(0.05f, half.ReduceMin() * 0.5f));
        }
        case WGF_PHYSICS3D_PRIV_SPHERE: return new SphereShape(std::max(d.size[0], 0.01f));
        case WGF_PHYSICS3D_PRIV_CAPSULE:
            return new CapsuleShape(std::max(d.size[1], 0.01f), std::max(d.size[0], 0.01f));
        case WGF_PHYSICS3D_PRIV_CONVEX: {
            if (d.points == nullptr || d.point_count < 4) return nullptr;
            Array<Vec3> points;
            points.reserve(d.point_count);
            for (int i = 0; i < d.point_count; i++)
                points.push_back(Vec3(d.points[3 * i], d.points[3 * i + 1], d.points[3 * i + 2]));
            ConvexHullShapeSettings settings(points);
            Shape::ShapeResult result = settings.Create();
            return result.IsValid() ? result.Get() : nullptr;
        }
        case WGF_PHYSICS3D_PRIV_MESH: {
            if (d.points == nullptr || d.indices == nullptr || d.index_count < 3) return nullptr;
            VertexList vertices;
            IndexedTriangleList triangles;
            vertices.reserve(d.point_count);
            for (int i = 0; i < d.point_count; i++)
                vertices.push_back(Float3(d.points[3 * i], d.points[3 * i + 1], d.points[3 * i + 2]));
            for (int i = 0; i + 2 < d.index_count; i += 3) {
                if (d.indices[i] >= uint32(d.point_count) || d.indices[i + 1] >= uint32(d.point_count) ||
                    d.indices[i + 2] >= uint32(d.point_count)) {
                    continue;
                }
                triangles.push_back(IndexedTriangle(d.indices[i], d.indices[i + 1], d.indices[i + 2]));
            }
            MeshShapeSettings settings(vertices, triangles);
            Shape::ShapeResult result = settings.Create();
            return result.IsValid() ? result.Get() : nullptr;
        }
    }
    return nullptr;
}

// The shape moved off the body's origin and its mass off the shape's own center, as asked.
RefConst<Shape> Placed(RefConst<Shape> shape, const wgf_physics3d_priv_body_desc_t &d)
{
    if (shape == nullptr) return nullptr;
    const Vec3 offset = V(d.offset), mass_offset = V(d.mass_offset);
    if (offset != Vec3::sZero()) shape = new RotatedTranslatedShape(offset, Quat::sIdentity(), shape);
    if (mass_offset != Vec3::sZero()) shape = new OffsetCenterOfMassShape(shape, mass_offset);
    return shape;
}

ObjectLayer LayerOf(const wgf_physics3d_priv_body_desc_t &d)
{
    uint32 group = d.layer & USER_BITS;
    const uint32 mask = d.mask & USER_BITS;
    if (d.motion == WGF_PHYSICS3D_PRIV_STATIC && !d.sensor) group |= STATIC_GROUP;
    return ObjectLayerPairFilterMask::sGetObjectLayer(group, mask);
}


// ------------------------------------------------------------------ queries ----


// A query meets a body whose layer is in its mask, and no sensor.
class QueryLayers final : public ObjectLayerFilter {
public:
    explicit QueryLayers(uint32 mask) : mMask(mask & USER_BITS) {}
    bool ShouldCollide(ObjectLayer layer) const override
    {
        return (ObjectLayerPairFilterMask::sGetGroup(layer) & mMask) != 0;
    }

private:
    uint32 mMask;
};

class NoSensors final : public BodyFilter {
public:
    bool ShouldCollideLocked(const Body &body) const override { return !body.IsSensor(); }
};

// The nearest hit of each body, nearest first, into `out`: how many.
int Nearest(Array<wgf_physics3d_priv_hit_t> &hits, wgf_physics3d_priv_hit_t *out, int count)
{
    std::sort(hits.begin(), hits.end(), [](const wgf_physics3d_priv_hit_t &a, const wgf_physics3d_priv_hit_t &b) {
        return a.distance < b.distance;
    });
    int n = 0;
    for (const wgf_physics3d_priv_hit_t &hit : hits) {
        bool seen = false;
        for (int i = 0; i < n && !seen; i++) seen = out[i].user == hit.user;
        if (seen) continue;
        if (n == count) break;
        out[n++] = hit;
    }
    return n;
}

RefConst<Shape> QueryShape(int shape, const float size[3])
{
    if (shape == WGF_PHYSICS3D_PRIV_SPHERE) return new SphereShape(std::max(size[0], 0.001f));
    return new BoxShape(Vec3(std::max(size[0], 0.001f), std::max(size[1], 0.001f), std::max(size[2], 0.001f)),
                        std::min({size[0], size[1], size[2], 0.05f}) * 0.5f);
}

} // namespace

namespace wgf_jolt {

JPH::PhysicsSystem *System()
{
    return world != nullptr ? &world->system : nullptr;
}

JPH::TempAllocator *Temp()
{
    return world != nullptr ? &world->temp : nullptr;
}

JPH::RefConst<JPH::Shape> PlacedShape(const wgf_physics3d_priv_body_desc_t &d)
{
    return Placed(MakeShape(d), d);
}

JPH::ObjectLayer LayerOf(const wgf_physics3d_priv_body_desc_t &d)
{
    return ::LayerOf(d);
}

JPH::Vec3 V(const float v[3])
{
    return ::V(v);
}

JPH::Quat Q(const float q[4])
{
    return ::Q(q);
}

void AddOnStop(void (*stop)())
{
    for (auto &slot : on_stop) {
        if (slot == stop) return;
        if (slot == nullptr) {
            slot = stop;
            return;
        }
    }
}

void SetBeforeStep(void (*step)())
{
    before_step = step;
}

void Put(JPH::Vec3 v, float out[3])
{
    ::Put(v, out);
}

void Put(JPH::Quat q, float out[4])
{
    ::Put(q, out);
}

} // namespace wgf_jolt

extern "C" {

bool wgf_physics3d_priv_jolt_start(float gx, float gy, float gz)
{
    if (world != nullptr) return true;
    RegisterDefaultAllocator();
    JPH::Trace = Quiet;
    // the shapes physics3d makes, each a collision dispatch's entries; Jolt's RegisterTypes
    // would register every type and link every one
    CollisionDispatch::sInit();
    ConvexShape::sRegister();
    SphereShape::sRegister();
    BoxShape::sRegister();
    CapsuleShape::sRegister();
    ConvexHullShape::sRegister();
    MeshShape::sRegister();
    RotatedTranslatedShape::sRegister();
    OffsetCenterOfMassShape::sRegister();
    world = new World();
    world->broad.ConfigureLayer(NON_MOVING, STATIC_GROUP, 0);
    world->broad.ConfigureLayer(MOVING, USER_BITS, STATIC_GROUP);
    world->system.Init(MAX_BODIES, 0, MAX_PAIRS, MAX_CONTACTS, world->broad, world->object_vs_broad, world->pairs);
    world->system.SetGravity(Vec3(gx, gy, gz));
    world->system.SetContactListener(&world->overlaps);
    return true;
}

void wgf_physics3d_priv_jolt_stop(void)
{
    if (world == nullptr) return;
    BodyInterface &bodies = world->system.GetBodyInterface();
    for (auto stop : on_stop) {
        if (stop != nullptr) stop(); // a part's own (characters, vehicles), before the world goes
    }
    BodyIDVector ids;
    world->system.GetBodies(ids);
    for (BodyID id : ids) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
    }
    delete world;
    world = nullptr;
}

void wgf_physics3d_priv_jolt_set_gravity(float gx, float gy, float gz)
{
    if (world != nullptr) world->system.SetGravity(Vec3(gx, gy, gz));
}

uint32_t wgf_physics3d_priv_jolt_body_create(const wgf_physics3d_priv_body_desc_t *d)
{
    if (world == nullptr || d == nullptr) return WGF_PHYSICS3D_PRIV_NO_BODY;
    RefConst<Shape> shape = Placed(MakeShape(*d), *d);
    if (shape == nullptr) return WGF_PHYSICS3D_PRIV_NO_BODY;
    const EMotionType motion = d->motion == WGF_PHYSICS3D_PRIV_DYNAMIC     ? EMotionType::Dynamic
                               : d->motion == WGF_PHYSICS3D_PRIV_KINEMATIC ? EMotionType::Kinematic
                                                                           : EMotionType::Static;
    BodyCreationSettings settings(shape, RVec3(d->position[0], d->position[1], d->position[2]), Q(d->rotation),
                                  motion, LayerOf(*d));
    settings.mIsSensor = d->sensor;
    settings.mCollideKinematicVsNonDynamic = d->sensor; // a kinematic sensor still sees kinematic bodies
    settings.mFriction = d->friction;
    settings.mRestitution = d->restitution;
    settings.mLinearDamping = d->linear_damping;
    settings.mAngularDamping = d->angular_damping;
    settings.mUserData = d->user;
    if (motion == EMotionType::Dynamic && d->mass > 0.0f) {
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = d->mass;
    }
    // a sensor moves with its actor
    if (motion == EMotionType::Static && d->sensor) settings.mMotionType = EMotionType::Kinematic;
    BodyInterface &bodies = world->system.GetBodyInterface();
    Body *body = bodies.CreateBody(settings);
    if (body == nullptr) return WGF_PHYSICS3D_PRIV_NO_BODY; // out of bodies
    bodies.AddBody(body->GetID(), motion == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

void wgf_physics3d_priv_jolt_body_destroy(uint32_t body)
{
    if (world == nullptr || body == WGF_PHYSICS3D_PRIV_NO_BODY) return;
    BodyInterface &bodies = world->system.GetBodyInterface();
    const BodyID id(body);
    if (!bodies.IsAdded(id)) return;
    world->overlaps.Forget(id, bodies);
    /* what rests on it wakes, or a sleeping body would stay where it no longer has support
       (a floor made again from its file's new place, or gone) */
    bodies.ActivateBodiesInAABox(bodies.GetTransformedShape(id).GetWorldSpaceBounds(), {}, {});
    bodies.RemoveBody(id);
    bodies.DestroyBody(id);
}

void wgf_physics3d_priv_jolt_body_get_pose(uint32_t body, float position[3], float rotation[4])
{
    if (world == nullptr) return;
    RVec3 p;
    Quat q;
    world->system.GetBodyInterface().GetPositionAndRotation(BodyID(body), p, q);
    Put(Vec3(p), position);
    Put(q, rotation);
}

void wgf_physics3d_priv_jolt_body_set_pose(uint32_t body, const float position[3], const float rotation[4])
{
    if (world == nullptr) return;
    BodyInterface &bodies = world->system.GetBodyInterface();
    const BodyID id(body);
    bodies.SetPositionAndRotation(id, RVec3(V(position)), Q(rotation),
                                  bodies.GetMotionType(id) == EMotionType::Static ? EActivation::DontActivate
                                                                                   : EActivation::Activate);
}

void wgf_physics3d_priv_jolt_body_move(uint32_t body, const float position[3], const float rotation[4], float dt)
{
    if (world == nullptr || dt <= 0.0f) return;
    world->system.GetBodyInterface().MoveKinematic(BodyID(body), RVec3(V(position)), Q(rotation), dt);
}

void wgf_physics3d_priv_jolt_body_get_velocity(uint32_t body, float linear[3], float angular[3])
{
    if (world == nullptr) return;
    Vec3 l, a;
    world->system.GetBodyInterface().GetLinearAndAngularVelocity(BodyID(body), l, a);
    Put(l, linear);
    Put(a, angular);
}

void wgf_physics3d_priv_jolt_body_set_velocity(uint32_t body, const float linear[3], const float angular[3])
{
    if (world == nullptr) return;
    world->system.GetBodyInterface().SetLinearAndAngularVelocity(BodyID(body), V(linear), V(angular));
}

void wgf_physics3d_priv_jolt_body_add_impulse(uint32_t body, const float impulse[3])
{
    if (world == nullptr) return;
    world->system.GetBodyInterface().AddImpulse(BodyID(body), V(impulse));
}

void wgf_physics3d_priv_jolt_body_set_material(uint32_t body, float friction, float restitution, float linear_damping,
                                              float angular_damping)
{
    if (world == nullptr) return;
    BodyLockWrite lock(world->system.GetBodyLockInterface(), BodyID(body));
    if (!lock.Succeeded()) return;
    Body &b = lock.GetBody();
    b.SetFriction(friction);
    b.SetRestitution(restitution);
    if (MotionProperties *motion = b.GetMotionPropertiesUnchecked(); motion != nullptr && !b.IsStatic()) {
        motion->SetLinearDamping(linear_damping);
        motion->SetAngularDamping(angular_damping);
    }
}

bool wgf_physics3d_priv_jolt_body_is_active(uint32_t body)
{
    return world != nullptr && world->system.GetBodyInterface().IsActive(BodyID(body));
}

void wgf_physics3d_priv_jolt_step(float dt)
{
    if (world == nullptr || dt <= 0.0f) return;
    if (before_step != nullptr) before_step(); // a part's (vehicles: their bodies kept awake)
    world->system.Update(dt, 1, &world->temp, &world->jobs);
    world->overlaps.Settle(world->system.GetBodyInterface());
}

int wgf_physics3d_priv_jolt_take_overlaps(wgf_physics3d_priv_overlap_t *out, int count)
{
    if (world == nullptr || out == nullptr) return 0;
    Array<wgf_physics3d_priv_overlap_t> &all = world->overlaps.mOut;
    const int n = std::min(count, int(all.size()));
    for (int i = 0; i < n; i++) out[i] = all[i];
    all.erase(all.begin(), all.begin() + n);
    return n;
}


int wgf_physics3d_priv_jolt_cast_ray(const float origin[3], const float direction[3], float distance, uint32_t mask,
                                     wgf_physics3d_priv_hit_t *out, int count)
{
    if (world == nullptr || out == nullptr || count <= 0 || !(distance > 0.0f)) return 0;
    const Vec3 dir = V(direction);
    if (dir.LengthSq() < 1e-12f) return 0;
    const RRayCast ray(V(origin), dir.Normalized() * distance);
    AllHitCollisionCollector<CastRayCollector> collector;
    RayCastSettings settings;
    settings.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces;
    QueryLayers layers(mask);
    NoSensors bodies;
    world->system.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layers, bodies);
    Array<wgf_physics3d_priv_hit_t> hits;
    const BodyLockInterface &locks = world->system.GetBodyLockInterface();
    for (const RayCastResult &r : collector.mHits) {
        BodyLockRead lock(locks, r.mBodyID);
        if (!lock.Succeeded()) continue;
        const RVec3 point = ray.GetPointOnRay(r.mFraction);
        const Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(r.mSubShapeID2, point);
        hits.push_back({lock.GetBody().GetUserData(),
                        {float(point.GetX()), float(point.GetY()), float(point.GetZ())},
                        {normal.GetX(), normal.GetY(), normal.GetZ()},
                        r.mFraction * distance});
    }
    return Nearest(hits, out, count);
}

int wgf_physics3d_priv_jolt_cast_shape(int shape, const float size[3], const float origin[3], const float direction[3],
                                       float distance, uint32_t mask, wgf_physics3d_priv_hit_t *out, int count)
{
    if (world == nullptr || out == nullptr || count <= 0 || !(distance > 0.0f)) return 0;
    const Vec3 dir = V(direction);
    if (dir.LengthSq() < 1e-12f) return 0;
    RefConst<Shape> cast = QueryShape(shape, size);
    const RShapeCast sweep =
        RShapeCast::sFromWorldTransform(cast, Vec3::sOne(), RMat44::sTranslation(V(origin)),
                                        dir.Normalized() * distance);
    ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = EBackFaceMode::IgnoreBackFaces;
    AllHitCollisionCollector<CastShapeCollector> collector;
    QueryLayers layers(mask);
    NoSensors bodies;
    world->system.GetNarrowPhaseQuery().CastShape(sweep, settings, RVec3::sZero(), collector, {}, layers, bodies);
    Array<wgf_physics3d_priv_hit_t> hits;
    const BodyLockInterface &locks = world->system.GetBodyLockInterface();
    for (const ShapeCastResult &r : collector.mHits) {
        BodyLockRead lock(locks, r.mBodyID2);
        if (!lock.Succeeded()) continue;
        const Vec3 axis = r.mPenetrationAxis.LengthSq() > 1e-12f ? -r.mPenetrationAxis.Normalized() : -dir.Normalized();
        hits.push_back({lock.GetBody().GetUserData(),
                        {float(r.mContactPointOn2.GetX()), float(r.mContactPointOn2.GetY()),
                         float(r.mContactPointOn2.GetZ())},
                        {axis.GetX(), axis.GetY(), axis.GetZ()},
                        r.mFraction * distance});
    }
    return Nearest(hits, out, count);
}

int wgf_physics3d_priv_jolt_overlap(int shape, const float size[3], const float center[3], uint32_t mask, uint64_t *out,
                                    int count)
{
    if (world == nullptr || out == nullptr || count <= 0) return 0;
    RefConst<Shape> probe = QueryShape(shape, size);
    CollideShapeSettings settings;
    AllHitCollisionCollector<CollideShapeCollector> collector;
    QueryLayers layers(mask);
    NoSensors bodies;
    world->system.GetNarrowPhaseQuery().CollideShape(probe, Vec3::sOne(), RMat44::sTranslation(V(center)), settings,
                                                    RVec3::sZero(), collector, {}, layers, bodies);
    int n = 0;
    const BodyInterface &all = world->system.GetBodyInterface();
    for (const CollideShapeResult &r : collector.mHits) {
        const uint64 user = all.GetUserData(r.mBodyID2);
        bool seen = false;
        for (int i = 0; i < n && !seen; i++) seen = out[i] == user;
        if (seen) continue;
        if (n == count) break;
        out[n++] = user;
    }
    return n;
}

} // extern "C"
