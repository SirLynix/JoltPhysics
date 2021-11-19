// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#pragma once

namespace JPH {

const Mat44 Body::GetWorldTransform() const
{
	JPH_ASSERT(BodyAccess::sCheckRights(BodyAccess::sPositionAccess, BodyAccess::EAccess::Read)); 

	return Mat44::sRotationTranslation(mRotation, GetPosition());
}

const Mat44 Body::GetCenterOfMassTransform() const
{
	JPH_ASSERT(BodyAccess::sCheckRights(BodyAccess::sPositionAccess, BodyAccess::EAccess::Read)); 

	return Mat44::sRotationTranslation(mRotation, mPosition);
}

const Mat44 Body::GetInverseCenterOfMassTransform() const
{
	JPH_ASSERT(BodyAccess::sCheckRights(BodyAccess::sPositionAccess, BodyAccess::EAccess::Read)); 

	return Mat44::sInverseRotationTranslation(mRotation, mPosition);
}

inline bool Body::sFindCollidingPairsCanCollide(const Body &inBody1, const Body &inBody2)
{
	// One of these conditions must be true
	// - One of the bodies must be dynamic to collide
	// - A kinematic object can collide with a sensor
	if ((!inBody1.IsDynamic() && !inBody2.IsDynamic()) 
		&& !(inBody1.IsKinematic() && inBody2.IsSensor()))
		return false;

	// Check that body 1 is active
	uint32 body1_index_in_active_bodies = inBody1.GetIndexInActiveBodiesInternal();
	JPH_ASSERT(!inBody1.IsStatic() && body1_index_in_active_bodies != Body::cInactiveIndex, "This function assumes that Body 1 is active");

	// If the pair A, B collides we need to ensure that the pair B, A does not collide or else we will handle the collision twice.
	// If A is the same body as B we don't want to collide (1)
	// If A is dynamic and B is static we should collide (2)
	// If A is dynamic / kinematic and B is dynamic / kinematic we should only collide if (kinematic vs kinematic is ruled out by the if above)
	//	- A is active and B is not yet active (3)
	//	- A is active and B will become active during this simulation step (4)
	//	- A is active and B is active, we require a condition that makes A, B collide and B, A not (5)
	//
	// In order to implement this we use the index in the active body list and make use of the fact that 
	// a body not in the active list has Body.Index = 0xffffffff which is the highest possible value for an uint32.
	//
	// Because we know that A is active we know that A.Index != 0xffffffff:
	// (1) Because A.Index != 0xffffffff, if A.Index = B.Index then A = B, so to collide A.Index != B.Index
	// (2) A.Index != 0xffffffff, B.Index = 0xffffffff (because it's static and cannot be in the active list), so to collide A.Index != B.Index
	// (3) A.Index != 0xffffffff, B.Index = 0xffffffff (because it's not yet active), so to collide A.Index != B.Index
	// (4) A.Index != 0xffffffff, B.Index = 0xffffffff currently. But it can activate during the Broad/NarrowPhase step at which point it 
	//     will be added to the end of the active list which will make B.Index > A.Index (this holds only true when we don't deactivate 
	//     bodies during the Broad/NarrowPhase step), so to collide A.Index < B.Index.
	// (5) As tie breaker we can use the same condition A.Index < B.Index to collide, this means that if A, B collides then B, A won't
	static_assert(Body::cInactiveIndex == 0xffffffff, "The algorithm below uses this value");
	if (body1_index_in_active_bodies >= inBody2.GetIndexInActiveBodiesInternal())
		return false;
	JPH_ASSERT(inBody1.GetID() != inBody2.GetID(), "Read the comment above, A and B are the same body which should not be possible!");

	// Bodies in the same group don't collide
	if (!inBody1.GetCollisionGroup().CanCollide(inBody2.GetCollisionGroup()))
		return false;

	return true;
}

void Body::AddRotationStep(Vec3Arg inAngularVelocityTimesDeltaTime)
{ 
	JPH_ASSERT(BodyAccess::sCheckRights(BodyAccess::sPositionAccess, BodyAccess::EAccess::ReadWrite)); 

	// This used to use the equation: d/dt R(t) = 1/2 * w(t) * R(t) so that R(t + dt) = R(t) + 1/2 * w(t) * R(t) * dt
	// See: Appendix B of An Introduction to Physically Based Modeling: Rigid Body Simulation II-Nonpenetration Constraints
	// URL: https://www.cs.cmu.edu/~baraff/sigcourse/notesd2.pdf
	// But this is a first order approximation and does not work well for kinematic ragdolls that are driven to a new
	// pose if the poses differ enough. So now we split w(t) * dt into an axis and angle part and create a quaternion with it.
	// Note that the resulting quaternion is normalized since otherwise numerical drift will eventually make the rotation non-normalized.
	float len = inAngularVelocityTimesDeltaTime.Length(); 
	if (len > 1.0e-6f) 
	{
		mRotation = (Quat::sRotation(inAngularVelocityTimesDeltaTime / len, len) * mRotation).Normalized(); 
		JPH_ASSERT(!mRotation.IsNaN()); 
	}
}

void Body::SubRotationStep(Vec3Arg inAngularVelocityTimesDeltaTime)
{ 
	JPH_ASSERT(BodyAccess::sCheckRights(BodyAccess::sPositionAccess, BodyAccess::EAccess::ReadWrite)); 

	// See comment at Body::AddRotationStep
	float len = inAngularVelocityTimesDeltaTime.Length(); 
	if (len > 1.0e-6f) 
	{
		mRotation = (Quat::sRotation(inAngularVelocityTimesDeltaTime / len, -len) * mRotation).Normalized(); 
		JPH_ASSERT(!mRotation.IsNaN()); 
	}
}

Vec3 Body::GetWorldSpaceSurfaceNormal(const SubShapeID &inSubShapeID, Vec3Arg inPosition) const
{
	Mat44 inv_com = GetInverseCenterOfMassTransform();
	return inv_com.Multiply3x3Transposed(mShape->GetSurfaceNormal(inSubShapeID, inv_com * inPosition)).Normalized();
}

Mat44 Body::GetInverseInertia() const
{
	JPH_ASSERT(IsDynamic());

	return GetMotionProperties()->GetInverseInertiaForRotation(Mat44::sRotation(mRotation));
}

void Body::AddImpulse(Vec3Arg inImpulse)
{
	JPH_ASSERT(IsDynamic());

	SetLinearVelocityClamped(mMotionProperties->GetLinearVelocity() + inImpulse * mMotionProperties->GetInverseMass());
}

void Body::AddImpulse(Vec3Arg inImpulse, Vec3Arg inPosition)
{
	JPH_ASSERT(IsDynamic());

	SetLinearVelocityClamped(mMotionProperties->GetLinearVelocity() + inImpulse * mMotionProperties->GetInverseMass());

	SetAngularVelocityClamped(mMotionProperties->GetAngularVelocity() + GetInverseInertia() * (inPosition - mPosition).Cross(inImpulse));
}

void Body::AddAngularImpulse(Vec3Arg inAngularImpulse)
{
	JPH_ASSERT(IsDynamic());

	SetAngularVelocityClamped(mMotionProperties->GetAngularVelocity() + GetInverseInertia() * inAngularImpulse);
}

} // JPH