// Wheeled vehicles on Jolt (wgf_physics3d_jolt_priv.h): Jolt's VehicleConstraint and its wheeled
// controller, a file of its own so a program that makes none links none of it (about 44 KB of
// code), reached only from physics3d's vehicles (wgf_physics3d_vehicle.c), which a program links
// on purpose (wgf_physics3d_enable_vehicles). Its own storage; the world it lives in is the Jolt
// layer's (wgf_physics3d_jolt_world_priv.h), which wakes its bodies as each step begins and lets
// it go as the world stops.

#include "wgf_physics3d_jolt_priv.h"
#include "wgf_physics3d_jolt_world_priv.h"

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <algorithm>
#include <cmath>

using namespace JPH;
using wgf_jolt::V;

namespace {

constexpr int MAX_VEHICLES = 64;
constexpr float PEAK_ALONG = 0.06f; // the tires' grip peaks: their curves' (below)
constexpr float PEAK_ACROSS = 3.0f * JPH_PI / 180.0f;

VehicleConstraint *vehicles[MAX_VEHICLES] = {};
bool ready = false; // this world's: its wheels' shape registered, its hooks set

WheeledVehicleController *ControllerOf(int vehicle)
{
    if (wgf_jolt::System() == nullptr || vehicle < 0 || vehicle >= MAX_VEHICLES || vehicles[vehicle] == nullptr)
        return nullptr;
    return static_cast<WheeledVehicleController *>(vehicles[vehicle]->GetController());
}

// Each step's start: a vehicle's body kept awake, so it answers its driver at once.
void KeepAwake()
{
    for (VehicleConstraint *v : vehicles) {
        if (v != nullptr) wgf_jolt::System()->GetBodyInterface().ActivateBody(v->GetVehicleBody()->GetID());
    }
}

void StopAll();

// The first vehicle of a world: its wheels' shape, cast as cylinders, in the collision dispatch,
// and the world's hooks set.
void Ready()
{
    if (ready) return;
    CylinderShape::sRegister();
    wgf_jolt::AddOnStop(StopAll);
    wgf_jolt::SetBeforeStep(KeepAwake);
    ready = true;
}

} // namespace

extern "C" {

int wgf_physics3d_priv_jolt_vehicle_create(uint32_t body, const wgf_physics3d_priv_vehicle_desc_t *d)
{
    if (wgf_jolt::System() == nullptr || d == nullptr || d->wheel_count < 2 || d->wheel_count > 8) return -1;
    int slot = 0;
    while (slot < MAX_VEHICLES && vehicles[slot] != nullptr) slot++;
    if (slot == MAX_VEHICLES) return -1;
    Ready();
    BodyLockWrite lock(wgf_jolt::System()->GetBodyLockInterface(), BodyID(body));
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
    for (VehicleDifferentialSettings &differential : controller->mDifferentials)
        differential.mEngineTorqueRatio = share;
    settings.mController = controller;

    VehicleConstraint *vehicle = new VehicleConstraint(lock.GetBody(), settings);
    vehicle->SetVehicleCollisionTester(new VehicleCollisionTesterCastCylinder(lock.GetBody().GetObjectLayer()));
    // the body's lock let go before the system's own are taken: a step takes them the other
    // way round (its listeners' lock, then every body's)
    lock.ReleaseLock();
    wgf_jolt::System()->AddConstraint(vehicle);
    wgf_jolt::System()->AddStepListener(vehicle);
    vehicles[slot] = vehicle;
    return slot;
}

void wgf_physics3d_priv_jolt_vehicle_destroy(int vehicle)
{
    if (wgf_jolt::System() == nullptr || vehicle < 0 || vehicle >= MAX_VEHICLES || vehicles[vehicle] == nullptr) return;
    wgf_jolt::System()->RemoveStepListener(vehicles[vehicle]);
    wgf_jolt::System()->RemoveConstraint(vehicles[vehicle]); // the system's reference was the last
    vehicles[vehicle] = nullptr;
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
    VehicleAntiRollBars &bars = vehicles[vehicle]->GetAntiRollBars();
    const int wheels = int(vehicles[vehicle]->GetWheels().size());
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
    if (ControllerOf(vehicle) == nullptr || wheel < 0 || wheel >= int(vehicles[vehicle]->GetWheels().size()))
        return 0.0f;
    const WheelWV *w = static_cast<const WheelWV *>(vehicles[vehicle]->GetWheel(wheel));
    if (!w->HasContact()) return 0.0f;
    // as Jolt's tire model measures it (the ground's speed at the contact against the
    // wheel's rim), but against at least 1 m/s, so a car pulling away from rest reads what
    // its tires do rather than a ratio of near nothing; each as a share of where its grip
    // peaks (the tire curves made with the vehicle: a slip ratio of 0.06 along, 3 degrees
    // across): a free-rolling wheel reads a little above 0, a spinning one well above 1
    const Body *body = vehicles[vehicle]->GetVehicleBody();
    Vec3 relative = body->GetPointVelocity(w->GetContactPosition()) - w->GetContactPointVelocity();
    relative -= w->GetContactNormal().Dot(relative) * w->GetContactNormal();
    const float ground = relative.Dot(w->GetContactLongitudinal()), side = relative.Dot(w->GetContactLateral());
    const float rim = w->GetAngularVelocity() * w->GetSettings()->mRadius;
    const float speed = std::max(std::abs(ground), 1.0f);
    const float along = std::abs(rim - ground) / speed / PEAK_ALONG;
    const float across = ATan2(std::abs(side), speed) / PEAK_ACROSS; // Jolt's: the same on every target
    return std::min(std::max(along, across), 10.0f);
}

void wgf_physics3d_priv_jolt_vehicle_get_wheel(int vehicle, int wheel, float position[3], float rotation[4])
{
    if (ControllerOf(vehicle) == nullptr || wheel < 0 || wheel >= int(vehicles[vehicle]->GetWheels().size())) return;
    // a wheel model's axle along its x, as a cylinder's is along y in Jolt's
    const Mat44 m = vehicles[vehicle]->GetWheelLocalTransform(uint(wheel), Vec3::sAxisX(), Vec3::sAxisY());
    wgf_jolt::Put(m.GetTranslation(), position);
    wgf_jolt::Put(m.GetQuaternion(), rotation);
}

void wgf_physics3d_priv_jolt_vehicle_reset(int vehicle)
{
    WheeledVehicleController *controller = ControllerOf(vehicle);
    if (controller == nullptr) return;
    controller->SetDriverInput(0.0f, 0.0f, 0.0f, 0.0f);
    controller->GetEngine().SetCurrentRPM(controller->GetEngine().mMinRPM);
    controller->GetTransmission().Set(0, 1.0f);
    for (Wheel *w : vehicles[vehicle]->GetWheels()) {
        w->SetAngularVelocity(0.0f);
        w->SetSteerAngle(0.0f);
    }
}

} // extern "C"

namespace {

// The world stopping: every vehicle let go of before it goes.
void StopAll()
{
    for (int i = 0; i < MAX_VEHICLES; i++) wgf_physics3d_priv_jolt_vehicle_destroy(i);
    ready = false; // a world made again registers its dispatch anew
}

} // namespace
