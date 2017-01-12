/* Copyright (c) <2003-2016> <Julio Jerez, Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 
* 3. This notice may not be removed or altered from any source distribution.
*/

#include "dgPhysicsStdafx.h"

#include "dgWorld.h"
#include "dgContact.h"
#include "dgMeshEffect.h"
#include "dgDynamicBody.h"
#include "dgCollisionDeformableMesh.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#define DG_SMALLEST_SPRING_LENGTH			dgFloat32 (1.0e-3f) 

dgVector dgCollisionDeformableMesh::m_smallestLenght2	(DG_SMALLEST_SPRING_LENGTH * DG_SMALLEST_SPRING_LENGTH);

dgCollisionDeformableMesh::dgCollisionDeformableMesh(dgWorld* const world, dgCollisionID collisionID)
	:dgCollisionLumpedMassParticles(world, collisionID)
	,m_linkList(world->GetAllocator())
	,m_linksCount(0)
{
	m_rtti |= dgCollisionDeformableMesh_RTTI;
}

dgCollisionDeformableMesh::dgCollisionDeformableMesh(const dgCollisionDeformableMesh& source)
	:dgCollisionLumpedMassParticles(source)
	,m_linkList(source.m_linkList, source.m_linksCount)
	,m_linksCount(source.m_linksCount)
{
	m_rtti = source.m_rtti;
}

dgCollisionDeformableMesh::dgCollisionDeformableMesh(dgWorld* const world, dgDeserialize deserialization, void* const userData, dgInt32 revisionNumber)
	:dgCollisionLumpedMassParticles(world, deserialization, userData, revisionNumber)
	,m_linkList(world->GetAllocator())
	,m_linksCount(0)
{
	dgAssert(0);
}

dgCollisionDeformableMesh::~dgCollisionDeformableMesh(void)
{
}


void dgCollisionDeformableMesh::FinalizeBuild()
{
	dgCollisionLumpedMassParticles::FinalizeBuild();
}

void dgCollisionDeformableMesh::Serialize(dgSerialize callback, void* const userData) const
{
	dgAssert(0);
}


dgInt32 dgCollisionDeformableMesh::GetLinksCount() const
{
	return m_linksCount;
}

void dgCollisionDeformableMesh::DisableInactiveLinks ()
{
	dgAssert (0);
/*
	for (dgInt32 i = 0; i < m_linksCount; i ++) {
		dgInt32 v0 = m_linkList[i].m_v0;
		dgInt32 v1 = m_linkList[i].m_v1;
		if ((m_unitMassScaler[v0] == dgFloat32 (0.0f)) && (m_unitMassScaler[v1] == dgFloat32 (0.0f))) {
			m_linksCount --;
			dgSwap(m_linkList[m_linksCount], m_linkList[i]);
			i --;
		}
	}
*/
}

const dgInt16* dgCollisionDeformableMesh::GetLinks() const
{
	return &m_linkList[0].m_m0;
}



void dgCollisionDeformableMesh::ConstraintParticle(dgInt32 particleIndex, const dgVector& posit, const dgBody* const body)
{
	dgAssert(0);
}


void dgCollisionDeformableMesh::DebugCollision(const dgMatrix& matrix, dgCollision::OnDebugCollisionMeshCallback callback, void* const userData) const
{
	const dgVector* const posit = &m_posit[0];
	const dgSpringDamperLink* const links = &m_linkList[0];
	for (dgInt32 i = 0; i < m_linksCount; i++) {
		const dgInt32 j0 = links[i].m_m0;
		const dgInt32 j1 = links[i].m_m1;
		dgVector p0(matrix.TransformVector(posit[j0]));
		dgVector p1(matrix.TransformVector(posit[j1]));
		dgTriplex points[2];
		points[0].m_x = p0.m_x;
		points[0].m_y = p0.m_y;
		points[0].m_z = p0.m_z;
		points[1].m_x = p1.m_x;
		points[1].m_y = p1.m_y;
		points[1].m_z = p1.m_z;
		callback(userData, 2, &points[0].m_x, 0);
	}
}


void dgCollisionDeformableMesh::IntegrateForces(dgFloat32 timestep)
{
	dgAssert(m_body->m_invMass.m_w > dgFloat32(0.0f));
/*
	// calculate particles accelerations
	CalculateAcceleration (timestep);

	const dgMatrix& matrix = m_body->GetCollision()->GetGlobalMatrix();
	dgAssert (matrix[0][0] == dgFloat32 (1.0f));
	dgAssert (matrix[1][1] == dgFloat32 (1.0f));
	dgAssert (matrix[2][2] == dgFloat32 (1.0f));
	
	dgVector damp(dgFloat32(1.0f));
	if (m_body->m_linearDampOn) {
		const dgFloat32 tau = dgFloat32(1.0f) / (dgFloat32(60.0f) * timestep);
		damp = dgVector(dgPow(dgFloat32(1.0f) - m_body->m_dampCoef.m_w, tau));
	}

	// rigid body dynamic state
	dgVector timeV (timestep);
	dgVector den (dgFloat32(1.0f / m_particlesCount));
	
	dgVector xxSum(dgFloat32(0.0f));
	dgVector xySum(dgFloat32(0.0f));
	dgVector xxSum2(dgFloat32(0.0f));
	dgVector comVeloc(dgFloat32(0.0f));

	dgVector angularMomentum(0.0f);
	dgVector unitMass (m_unitMass);
	dgVector origin(m_body->m_localCentreOfMass + matrix.m_posit);

	dgVector* const posit = &m_posit[0];
	dgVector* const veloc = &m_veloc[0];
	for (dgInt32 i = 0; i < m_particlesCount; i++) {
		comVeloc += veloc[i];
		xxSum += posit[i];
		xxSum2 += posit[i].CompProduct4(posit[i]);
		xySum += posit[i].CompProduct4(posit[i].ShiftTripleRight());
		angularMomentum += posit[i].CrossProduct3(veloc[i]);
	}

	dgVector yySum(xxSum.ShiftTripleRight());
	dgVector com(xxSum.CompProduct4(den) + origin);
	dgVector pxx0(origin - com);
	dgVector pxy0(pxx0.ShiftTripleRight());
	dgVector Ixx(unitMass.CompProduct4(xxSum2 + xxSum.CompProduct4(pxx0.CompProduct4(dgVector::m_two))) + pxx0.CompProduct4(pxx0.Scale4(m_body->m_mass.m_w)));
	dgVector Ixy(unitMass.CompProduct4(xySum + xxSum.CompProduct4(pxy0) + yySum.CompProduct4(pxx0)) + pxx0.CompProduct4(pxy0.Scale4(m_body->m_mass.m_w)));
	comVeloc = comVeloc.CompProduct4(den);

	dgFloat32 II = m_unitInertia * m_particlesCount;
	dgMatrix inertia(dgGetIdentityMatrix());
	inertia[0][0] = II + Ixx[1] + Ixx[2];
	inertia[1][1] = II + Ixx[0] + Ixx[2];
	inertia[2][2] = II + Ixx[0] + Ixx[1];

	inertia[0][1] = -Ixy[0];
	inertia[1][0] = -Ixy[0];
	inertia[0][2] = -Ixy[1];
	inertia[2][0] = -Ixy[1];
	inertia[1][2] = -Ixy[2];
	inertia[2][1] = -Ixy[2];

	dgVector invTimestep (dgFloat32 (1.0f) / timestep);
	m_body->m_accel = invTimestep.CompProduct4(comVeloc - m_body->m_veloc);
	m_body->m_veloc = comVeloc;
	m_body->m_invWorldInertiaMatrix = inertia.Symetric3by3Inverse();

	com = xxSum.CompProduct4(den);
	for (dgInt32 i = 0; i < m_particlesCount; i++) {
		posit[i] -= com;
	}
*/
}


