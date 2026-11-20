// physics3d's way into Jolt (wgf_physics3d_jolt_priv.h): the one C++ file in libwgf, built
// without exceptions or RTTI, its C interface the calls physics3d makes. Only the shapes it
// makes are registered with Jolt's collision dispatch, not every type (Jolt's
// RegisterTypes), which keeps the rest of Jolt out of a program: measured on a car on a
// floor, 191 KB of wasm gzipped against 368 through joltc's API, whose init registers them
// all (docs/HISTORY.md, "Milestone 2, step 5").

#include "wgf_physics3d_jolt_priv.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/UnorderedMap.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h>
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
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <algorithm>
#include <cmath>

using namespace JPH;

namespace {

constexpr uint MAX_BODIES = 16384;
constexpr uint MAX_PAIRS = 16384;
constexpr uint MAX_CONTACTS = 8192;
constexpr int MAX_VEHICLES = 64;
constexpr int MAX_CHARACTERS = 256;
constexpr uint32 STATIC_GROUP = 0x8000; // the group bit a static body has, so movers' masks reach statics
constexpr uint32 USER_BITS = 0x7FFF;
constexpr float PEAK_ALONG = 0.06f;                 // the tires' grip peaks: their curves' (below)
constexpr float PEAK_ACROSS = 3.0f * JPH_PI / 180.0f;

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

struct World {
    TempAllocatorImpl temp{8 * 1024 * 1024};
    JobSystemSingleThreaded jobs{cMaxPhysicsJobs};
    BroadPhaseLayerInterfaceMask broad{2};
    ObjectVsBroadPhaseLayerFilterMask object_vs_broad{broad};
    ObjectLayerPairFilterMask pairs;
    PhysicsSystem system;
    Overlaps overlaps;
    VehicleConstraint *vehicles[MAX_VEHICLES] = {};
    CharacterVirtual *characters[MAX_CHARACTERS] = {};
    ObjectLayer character_layers[MAX_CHARACTERS] = {};
};

World *world = nullptr;

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
            for (int i = 0; i < d.point_count; i++) points.push_back(Vec3(d.points[3 * i], d.points[3 * i + 1], d.points[3 * i + 2]));
            ConvexHullShapeSettings settings(points);
            Shape::ShapeResult result = settings.Create();
            return result.IsValid() ? result.Get() : nullptr;
        }
        case WGF_PHYSICS3D_PRIV_MESH: {
            if (d.points == nullptr || d.indices == nullptr || d.index_count < 3) return nullptr;
            VertexList vertices;
            IndexedTriangleList triangles;
            vertices.reserve(d.point_count);
            for (int i = 0; i < d.point_count; i++) vertices.push_back(Float3(d.points[3 * i], d.points[3 * i + 1], d.points[3 * i + 2]));
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
    uint32 group = d.layer & USER_BITS, mask = d.mask & USER_BITS;
    if (d.motion == WGF_PHYSICS3D_PRIV_STATIC && !d.sensor) {
        group |= STATIC_GROUP;
    } else if (!d.sensor) {
        mask |= STATIC_GROUP; // a moving body meets the static world whatever its mask
    }
    return ObjectLayerPairFilterMask::sGetObjectLayer(group, mask);
}

WheeledVehicleController *ControllerOf(int vehicle)
{
    if (world == nullptr || vehicle < 0 || vehicle >= MAX_VEHICLES || world->vehicles[vehicle] == nullptr) return nullptr;
    return static_cast<WheeledVehicleController *>(world->vehicles[vehicle]->GetController());
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
    CylinderShape::sRegister(); // the vehicle's wheels, cast as cylinders
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
    for (int i = 0; i < MAX_CHARACTERS; i++) wgf_physics3d_priv_jolt_character_destroy(i);
    for (int i = 0; i < MAX_VEHICLES; i++) wgf_physics3d_priv_jolt_vehicle_destroy(i);
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
    if (motion == EMotionType::Static && d->sensor) settings.mMotionType = EMotionType::Kinematic; // a sensor moves with its actor
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
    for (VehicleConstraint *v : world->vehicles) {
        if (v != nullptr) world->system.GetBodyInterface().ActivateBody(v->GetVehicleBody()->GetID());
    }
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

int wgf_physics3d_priv_jolt_vehicle_create(uint32_t body, const wgf_physics3d_priv_vehicle_desc_t *d)
{
    if (world == nullptr || d == nullptr || d->wheel_count < 2 || d->wheel_count > 8) return -1;
    int slot = 0;
    while (slot < MAX_VEHICLES && world->vehicles[slot] != nullptr) slot++;
    if (slot == MAX_VEHICLES) return -1;
    BodyLockWrite lock(world->system.GetBodyLockInterface(), BodyID(body));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return -1;

    VehicleConstraintSettings settings;
    settings.mUp = Vec3::sAxisY();
    settings.mForward = Vec3::sAxisZ();
    LinearCurve longitudinal, lateral; // Jolt's tire curves, scaled by the grip
    longitudinal.Reserve(3);
    longitudinal.AddPoint(0.0f, 0.0f);
    longitudinal.AddPoint(PEAK_ALONG, 1.2f * d->grip);
    longitudinal.AddPoint(0.2f, 1.0f * d->grip);
    lateral.Reserve(3);
    lateral.AddPoint(0.0f, 0.0f);
    lateral.AddPoint(3.0f, 1.2f * d->grip); // degrees: PEAK_ACROSS
    lateral.AddPoint(20.0f, 1.0f * d->grip);
    for (int i = 0; i < d->wheel_count; i++) {
        WheelSettingsWV *w = new WheelSettingsWV;
        w->mPosition = V(d->wheels[i]);
        w->mRadius = d->radius;
        w->mWidth = d->width;
        w->mSuspensionMinLength = 0.0f;
        w->mSuspensionMaxLength = d->suspension;
        w->mSuspensionSpring.mFrequency = d->frequency;
        w->mSuspensionSpring.mDamping = d->damping;
        w->mMaxSteerAngle = i < 2 ? d->max_steer : 0.0f;
        w->mMaxHandBrakeTorque = i < 2 ? 0.0f : 4000.0f;
        w->mLongitudinalFriction = longitudinal;
        w->mLateralFriction = lateral;
        settings.mWheels.push_back(w);
    }
    WheeledVehicleControllerSettings *controller = new WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = d->engine_torque;
    controller->mEngine.mMaxRPM = d->max_rpm;
    controller->mEngine.mMinRPM = std::min(1000.0f, d->max_rpm * 0.25f);
    controller->mTransmission.mShiftUpRPM = d->max_rpm * 0.75f;
    controller->mTransmission.mShiftDownRPM = d->max_rpm * 0.35f;
    if (d->gear_count > 0) {
        controller->mTransmission.mGearRatios.clear();
        for (int i = 0; i < d->gear_count && i < 8; i++) controller->mTransmission.mGearRatios.push_back(d->gears[i]);
    }
    for (int axle = 0; axle * 2 + 1 < d->wheel_count; axle++) {
        const bool front = axle == 0;
        if (d->drive == 0 && !front) continue;
        if (d->drive == 1 && front && d->wheel_count > 2) continue;
        VehicleDifferentialSettings differential;
        differential.mLeftWheel = axle * 2;
        differential.mRightWheel = axle * 2 + 1;
        controller->mDifferentials.push_back(differential);
    }
    for (int axle = 0; d->anti_roll > 0.0f && axle * 2 + 1 < d->wheel_count; axle++) {
        VehicleAntiRollBar bar;
        bar.mLeftWheel = axle * 2;
        bar.mRightWheel = axle * 2 + 1;
        bar.mStiffness = d->anti_roll;
        settings.mAntiRollBars.push_back(bar);
    }
    const float share = controller->mDifferentials.empty() ? 1.0f : 1.0f / float(controller->mDifferentials.size());
    for (VehicleDifferentialSettings &differential : controller->mDifferentials) differential.mEngineTorqueRatio = share;
    settings.mController = controller;

    VehicleConstraint *vehicle = new VehicleConstraint(lock.GetBody(), settings);
    vehicle->SetVehicleCollisionTester(new VehicleCollisionTesterCastCylinder(lock.GetBody().GetObjectLayer()));
    world->system.AddConstraint(vehicle);
    world->system.AddStepListener(vehicle);
    world->vehicles[slot] = vehicle;
    return slot;
}

void wgf_physics3d_priv_jolt_vehicle_destroy(int vehicle)
{
    if (world == nullptr || vehicle < 0 || vehicle >= MAX_VEHICLES || world->vehicles[vehicle] == nullptr) return;
    world->system.RemoveStepListener(world->vehicles[vehicle]);
    world->system.RemoveConstraint(world->vehicles[vehicle]); // the system's reference was the last
    world->vehicles[vehicle] = nullptr;
}

void wgf_physics3d_priv_jolt_vehicle_set_input(int vehicle, float forward, float right, float brake, float hand_brake)
{
    WheeledVehicleController *controller = ControllerOf(vehicle);
    if (controller != nullptr) controller->SetDriverInput(forward, right, brake, hand_brake);
}

float wgf_physics3d_priv_jolt_vehicle_get_rpm(int vehicle)
{
    const WheeledVehicleController *controller = ControllerOf(vehicle);
    return controller != nullptr ? controller->GetEngine().GetCurrentRPM() : 0.0f;
}

int wgf_physics3d_priv_jolt_vehicle_get_gear(int vehicle)
{
    const WheeledVehicleController *controller = ControllerOf(vehicle);
    return controller != nullptr ? controller->GetTransmission().GetCurrentGear() : 0;
}

void wgf_physics3d_priv_jolt_vehicle_set_anti_roll(int vehicle, float stiffness)
{
    if (ControllerOf(vehicle) == nullptr) return;
    VehicleAntiRollBars &bars = world->vehicles[vehicle]->GetAntiRollBars();
    const int wheels = int(world->vehicles[vehicle]->GetWheels().size());
    bars.clear();
    for (int axle = 0; stiffness > 0.0f && axle * 2 + 1 < wheels; axle++) {
        VehicleAntiRollBar bar;
        bar.mLeftWheel = axle * 2;
        bar.mRightWheel = axle * 2 + 1;
        bar.mStiffness = stiffness;
        bars.push_back(bar);
    }
}

float wgf_physics3d_priv_jolt_vehicle_get_slip(int vehicle, int wheel)
{
    if (ControllerOf(vehicle) == nullptr || wheel < 0 || wheel >= int(world->vehicles[vehicle]->GetWheels().size())) return 0.0f;
    const WheelWV *w = static_cast<const WheelWV *>(world->vehicles[vehicle]->GetWheel(wheel));
    if (!w->HasContact()) return 0.0f;
    // as Jolt's tire model measures it (the ground's speed at the contact against the
    // wheel's rim), but against at least 1 m/s, so a car pulling away from rest reads what
    // its tires do rather than a ratio of near nothing; each as a share of where its grip
    // peaks (the tire curves made with the vehicle: a slip ratio of 0.06 along, 3 degrees
    // across): a free-rolling wheel reads a little above 0, a spinning one well above 1
    const Body *body = world->vehicles[vehicle]->GetVehicleBody();
    Vec3 relative = body->GetPointVelocity(w->GetContactPosition()) - w->GetContactPointVelocity();
    relative -= w->GetContactNormal().Dot(relative) * w->GetContactNormal();
    const float ground = relative.Dot(w->GetContactLongitudinal()), side = relative.Dot(w->GetContactLateral());
    const float rim = w->GetAngularVelocity() * w->GetSettings()->mRadius;
    const float speed = std::max(std::abs(ground), 1.0f);
    const float along = std::abs(rim - ground) / speed / PEAK_ALONG;
    const float across = std::atan2(std::abs(side), speed) / PEAK_ACROSS;
    return std::min(std::max(along, across), 10.0f);
}

void wgf_physics3d_priv_jolt_vehicle_get_wheel(int vehicle, int wheel, float position[3], float rotation[4])
{
    if (ControllerOf(vehicle) == nullptr || wheel < 0 || wheel >= int(world->vehicles[vehicle]->GetWheels().size())) return;
    // a wheel model's axle along its x, as a cylinder's is along y in Jolt's
    const Mat44 m = world->vehicles[vehicle]->GetWheelLocalTransform(uint(wheel), Vec3::sAxisX(), Vec3::sAxisY());
    Put(m.GetTranslation(), position);
    Put(m.GetQuaternion(), rotation);
}

void wgf_physics3d_priv_jolt_vehicle_reset(int vehicle)
{
    WheeledVehicleController *controller = ControllerOf(vehicle);
    if (controller == nullptr) return;
    controller->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
    controller->GetEngine().SetCurrentRPM(controller->GetEngine().mMinRPM);
    controller->GetTransmission().Set(0, 1.0f);
    for (Wheel *w : world->vehicles[vehicle]->GetWheels()) {
        w->SetAngularVelocity(0.0f);
        w->SetSteerAngle(0.0f);
    }
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
        RShapeCast::sFromWorldTransform(cast, Vec3::sOne(), RMat44::sTranslation(V(origin)), dir.Normalized() * distance);
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
                        {float(r.mContactPointOn2.GetX()), float(r.mContactPointOn2.GetY()), float(r.mContactPointOn2.GetZ())},
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

// ------------------------------------------------------------- characters ----

int wgf_physics3d_priv_jolt_character_create(const wgf_physics3d_priv_body_desc_t *d, float max_slope)
{
    if (world == nullptr || d == nullptr) return -1;
    int slot = 0;
    while (slot < MAX_CHARACTERS && world->characters[slot] != nullptr) slot++;
    if (slot == MAX_CHARACTERS) return -1;
    RefConst<Shape> made = MakeShape(*d);
    if (made == nullptr) return -1;
    RefConst<Shape> shape = Placed(made, *d);
    Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings();
    settings->mShape = shape;
    settings->mMaxSlopeAngle = max_slope;
    settings->mInnerBodyShape = shape; // a body in the world too, so rays, sensors, and queries find it
    settings->mInnerBodyLayer = LayerOf(*d);
    CharacterVirtual *character = new CharacterVirtual(settings, RVec3(V(d->position)), Q(d->rotation), d->user, &world->system);
    character->AddRef();
    world->characters[slot] = character;
    world->character_layers[slot] = LayerOf(*d);
    return slot;
}

void wgf_physics3d_priv_jolt_character_destroy(int character)
{
    if (world == nullptr || character < 0 || character >= MAX_CHARACTERS || world->characters[character] == nullptr) return;
    world->characters[character]->Release();
    world->characters[character] = nullptr;
}

// One step of the character: moved by `velocity` (gravity added while it isn't supported),
// sliding, climbing steps up to `step`, kept on the floor going down them.
void wgf_physics3d_priv_jolt_character_update(int character, const float velocity[3], float step, float dt,
                                              float position[3], float out_velocity[3])
{
    if (world == nullptr || character < 0 || character >= MAX_CHARACTERS || world->characters[character] == nullptr) return;
    CharacterVirtual *c = world->characters[character];
    Vec3 v = V(velocity);
    if (!c->IsSupported()) v += world->system.GetGravity() * dt;
    else if (v.GetY() < 0.0f) v.SetY(0.0f);
    c->SetLinearVelocity(v);
    CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = Vec3(0.0f, std::max(step, 0.0f), 0.0f);
    settings.mStickToFloorStepDown = Vec3(0.0f, -std::max(step, 0.05f), 0.0f);
    const ObjectLayer layer = world->character_layers[character];
    c->ExtendedUpdate(dt, world->system.GetGravity(), settings, world->system.GetDefaultBroadPhaseLayerFilter(layer),
                      world->system.GetDefaultLayerFilter(layer), {}, {}, world->temp);
    const RVec3 p = c->GetPosition();
    position[0] = float(p.GetX()), position[1] = float(p.GetY()), position[2] = float(p.GetZ());
    const Vec3 moved = c->GetLinearVelocity();
    out_velocity[0] = moved.GetX(), out_velocity[1] = moved.GetY(), out_velocity[2] = moved.GetZ();
}

void wgf_physics3d_priv_jolt_character_set_pose(int character, const float position[3], const float rotation[4])
{
    if (world == nullptr || character < 0 || character >= MAX_CHARACTERS || world->characters[character] == nullptr) return;
    world->characters[character]->SetPosition(RVec3(V(position)));
    world->characters[character]->SetRotation(Q(rotation));
}

bool wgf_physics3d_priv_jolt_character_ground(int character, float normal[3])
{
    if (world == nullptr || character < 0 || character >= MAX_CHARACTERS || world->characters[character] == nullptr) return false;
    const CharacterVirtual *c = world->characters[character];
    const Vec3 n = c->GetGroundNormal();
    normal[0] = n.GetX(), normal[1] = n.GetY(), normal[2] = n.GetZ();
    return c->GetGroundState() == CharacterBase::EGroundState::OnGround;
}

} // extern "C"
