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
#include "dgBody.h"
#include "dgWorld.h"
#include "dgConstraint.h"
#include "dgDynamicBody.h"
#include "dgSkeletonContainer.h"
#include "dgWorldDynamicUpdate.h"
#include "dgBilateralConstraint.h"

class dgSkeletonContainer::dgNodePair
{
	public:
	dgInt32 m_m0;
	dgInt32 m_m1;
};

DG_MSC_VECTOR_ALIGMENT
class dgSkeletonContainer::dgForcePair
{
	public:
	dgSpatialVector m_joint;
	dgSpatialVector m_body;
} DG_GCC_VECTOR_ALIGMENT;

DG_MSC_VECTOR_ALIGMENT 
class dgSkeletonContainer::dgMatriData
{
	public:
	dgSpatialMatrix m_jt;
	dgSpatialMatrix m_mass;
	dgSpatialMatrix m_invMass;
} DG_GCC_VECTOR_ALIGMENT;

DG_MSC_VECTOR_ALIGMENT 
class dgSkeletonContainer::dgBodyJointMatrixDataPair
{
	public:
	dgMatriData m_body;
	dgMatriData m_joint;
} DG_GCC_VECTOR_ALIGMENT;

dgInt32 dgSkeletonContainer::m_lruMarker = 1;
dgInt32 dgSkeletonContainer::m_uniqueID = DG_SKELETON_BASEW_UNIQUE_ID;

class dgSkeletonContainer::dgGraph
{
	public:

	DG_CLASS_ALLOCATOR(allocator)
	dgGraph (dgDynamicBody* const body, dgBilateralConstraint* const Joint, dgGraph* const parent)
		:m_body (body)
		,m_joint (Joint)
		,m_parent(parent)
		,m_child(NULL)
		,m_sibling(NULL)
		,m_index(0)
		,m_dof(0)
		,m_primaryStart(0)
		,m_auxiliaryStart(0)
	{
		if (m_parent) {
			if (m_parent->m_child) {
				m_sibling = m_parent->m_child;
			}
			m_parent->m_child = this;
		}
	}

	DG_INLINE ~dgGraph()
	{
		dgGraph* next;
		m_body->SetSkeleton(NULL);

		for (dgGraph* ptr = m_child; ptr; ptr = next) {
			next = ptr->m_sibling;
			delete ptr;
		}
	}

	DG_INLINE void Factorize(dgBodyJointMatrixDataPair* const data)
	{
		const dgSpatialMatrix& bodyMass = data[m_index].m_body.m_mass;
		dgSpatialMatrix& bodyInvMass = data[m_index].m_body.m_invMass;

		if (m_body->GetInvMass().m_w != dgFloat32(0.0f)) {
			for (dgGraph* child = m_child; child; child = child->m_sibling) {
				CalculateBodyDiagonal(data, child);
			}
			bodyMass.Inverse(bodyInvMass, 6);
		} else {
			bodyInvMass.SetZero();
		}

		if (m_joint) {
			dgSpatialMatrix& bodyJt = data[m_index].m_body.m_jt;
			dgAssert(m_parent);
			for (dgInt32 i = 0; i < m_dof; i++) {
				bodyInvMass.MultiplyNxNMatrixTimeVector(bodyJt[i], bodyJt[i]);
			}
			CalculateJointDiagonal(data);
			CalculateJacobianBlock(data);
		}
	}

	DG_INLINE void CalculateInertiaMatrix(dgBodyJointMatrixDataPair* const data)
	{
		dgSpatialMatrix& bodyMass = data[m_index].m_body.m_mass;

        dgFloat32 mass = m_body->GetMass().m_w;
		dgAssert(mass < dgFloat32(1.0e10f));
		dgMatrix inertia(m_body->CalculateInertiaMatrix());
		for (dgInt32 i = 0; i < 3; i++) {
			bodyMass[i][i] = mass;
			for (dgInt32 j = 0; j < 3; j++) {
				bodyMass[i + 3][j + 3] = inertia[i][j];
			}
		}
	}

	DG_INLINE void GetJacobians(const dgJointInfo* const jointInfo, const dgJacobianMatrixElement* const matrixRow, dgBodyJointMatrixDataPair* const data)
	{
		dgAssert(m_parent);
		dgAssert(jointInfo->m_joint == m_joint);
		dgAssert(jointInfo->m_joint->GetBody0() == m_body);
		dgAssert(jointInfo->m_joint->GetBody1() == m_parent->m_body);

		dgSpatialMatrix& bodyJt = data[m_index].m_body.m_jt;
		dgSpatialMatrix& jointJ = data[m_index].m_joint.m_jt;
		dgSpatialMatrix& jointMass = data[m_index].m_joint.m_mass;

		const dgInt32 start = jointInfo->m_pairStart;
		for (dgInt32 i = 0; i < m_dof; i++) {
			const dgInt32 k = m_sourceJacobianIndex[i];
			const dgJacobianMatrixElement* const row = &matrixRow[start + k];
			jointMass[i].SetZero();
			jointMass[i][i] = -row->m_diagDamp;
			bodyJt[i] = dgSpatialVector (row->m_Jt.m_jacobianM0.m_linear.CompProduct4(dgVector::m_negOne), row->m_Jt.m_jacobianM0.m_angular.CompProduct4(dgVector::m_negOne));
			jointJ[i] = dgSpatialVector (row->m_Jt.m_jacobianM1.m_linear.CompProduct4(dgVector::m_negOne), row->m_Jt.m_jacobianM1.m_angular.CompProduct4(dgVector::m_negOne));
		}
	}

	DG_INLINE dgInt32 Factorize(const dgJointInfo* const jointInfoArray, dgJacobianMatrixElement* const matrixRow, dgBodyJointMatrixDataPair* const data)
	{
		dgAssert((dgUnsigned64(data) & 0x0f) == 0);

		dgSpatialMatrix& bodyMass = data[m_index].m_body.m_mass;

		bodyMass.SetZero();
        if (m_body->GetInvMass().m_w != dgFloat32 (0.0f)) {
			CalculateInertiaMatrix(data);
		}

		m_ordinals = m_ordinalInit;
		dgInt32 boundedDof = 0;
		if (m_joint) {
			dgAssert (m_parent);
			const dgJointInfo* const jointInfo = &jointInfoArray[m_joint->m_index];
			dgAssert(jointInfo->m_joint == m_joint);
			dgAssert(jointInfo->m_joint->GetBody0() == m_body);
			dgAssert(jointInfo->m_joint->GetBody1() == m_parent->m_body);

			m_dof = 0;
			dgInt32 count = jointInfo->m_pairCount;
			const dgInt32 first = jointInfo->m_pairStart;
			for (dgInt32 i = 0; i < count; i++) {
				dgInt32 k = m_sourceJacobianIndex[i];
				const dgJacobianMatrixElement* const row = &matrixRow[k + first];
				if ((row->m_lowerBoundFrictionCoefficent <= dgFloat32 (-DG_LCP_MAX_VALUE)) && (row->m_upperBoundFrictionCoefficent >= dgFloat32 (DG_LCP_MAX_VALUE))) {
					m_dof ++;
				} else {
					dgSwap(m_sourceJacobianIndex[i], m_sourceJacobianIndex[count - 1]);
					i--;
					count--;
				}
			}
			boundedDof += jointInfo->m_pairCount - count;
			GetJacobians(jointInfo, matrixRow, data);
		}
		Factorize(data);
		return boundedDof;
	}

	DG_INLINE void CalculateBodyDiagonal(dgBodyJointMatrixDataPair* const data, dgGraph* const child)
	{
		dgAssert(child->m_joint);
		
		dgSpatialMatrix copy;
		copy.SetZero();
		const dgInt32 dof = child->m_dof;
		const dgSpatialMatrix& jacobianMatrix = data[child->m_index].m_joint.m_jt;
		const dgSpatialMatrix& childDiagonal = data[child->m_index].m_joint.m_mass;
		for (dgInt32 i = 0; i < dof ; i++) {
			const dgSpatialVector& jacobian = jacobianMatrix[i];
			for (dgInt32 j = 0; j < dof ; j++) {
				dgAssert(dgAreEqual (dgFloat64(childDiagonal[i][j]), dgFloat64(childDiagonal[j][i]), dgFloat64(1.0e-5f)));
				dgFloat32 val = childDiagonal[i][j];
				jacobian.ScaleAdd(val, copy[j], copy[j]);
			}
		}

		dgSpatialMatrix& bodyMass = data[m_index].m_body.m_mass;
		for (dgInt32 i = 0; i < dof; i++) {
			const dgSpatialVector& Jacobian = copy[i];
			const dgSpatialVector& JacobianTranspose = jacobianMatrix[i];
			for (dgInt32 j = 0; j < 6; j++) {
				dgFloat32 val = -Jacobian[j];
				JacobianTranspose.ScaleAdd(val, bodyMass[j], bodyMass[j]);
			}
		}
	}

	DG_INLINE void CalculateJointDiagonal (dgBodyJointMatrixDataPair* const data)
	{
		const dgSpatialMatrix& bodyMass = data[m_index].m_body.m_mass;
		const dgSpatialMatrix& bodyJt = data[m_index].m_body.m_jt;

		dgSpatialMatrix tmp;
		for (dgInt32 i = 0; i < m_dof; i++) {
			bodyMass.MultiplyNxNMatrixTimeVector(bodyJt[i], tmp[i]);
		}

		dgSpatialMatrix& jointMass = data[m_index].m_joint.m_mass;
		for (dgInt32 i = 0; i < m_dof; i++) {
			dgFloat32 a = bodyJt[i].DotProduct(tmp[i]);
			jointMass[i][i] -= a;
			for (dgInt32 j = i + 1; j < m_dof; j++) {
				a = - bodyJt[i].DotProduct(tmp[j]);
				jointMass[i][j] = a;
				jointMass[j][i] = a;
			}
		}
		dgSpatialMatrix& jointInvMass = data[m_index].m_joint.m_invMass;
		jointMass.Inverse(jointInvMass, m_dof);
	}

	DG_INLINE void CalculateJacobianBlock(dgBodyJointMatrixDataPair* const data)
	{
		dgSpatialMatrix& jointJ = data[m_index].m_joint.m_jt;

		dgSpatialMatrix copy;
		for (dgInt32 i = 0; i < m_dof; i++) {
			copy[i] = jointJ[i];
			jointJ[i].SetZero();
		}

		const dgSpatialMatrix& jointInvMass = data[m_index].m_joint.m_invMass;
		for (dgInt32 i = 0; i < m_dof; i++) {
			const dgSpatialVector& jacobian = copy[i];
			const dgSpatialVector& invDiagonalRow = jointInvMass[i];
			for (dgInt32 j = 0; j < m_dof; j++) {
				dgFloat32 val = invDiagonalRow[j];
				jacobian.ScaleAdd(val, jointJ[j], jointJ[j]);
			}
		}
	}

	DG_INLINE void JointJacobianTimeMassForward (const dgBodyJointMatrixDataPair* const data, dgForcePair& force)
	{
		const dgSpatialMatrix& bodyJt = data[m_index].m_body.m_jt;
		for (dgInt32 i = 0; i < m_dof; i++) {
			force.m_joint[i] -= bodyJt[i].DotProduct(force.m_body);
		}
	}

	DG_INLINE void BodyJacobianTimeMassForward(const dgBodyJointMatrixDataPair* const data, const dgForcePair& force, dgForcePair& parentForce) const 
	{
		const dgSpatialMatrix& jointJ = data[m_index].m_joint.m_jt;
		for (dgInt32 i = 0; i < m_dof; i++) {
			jointJ[i].ScaleAdd(-force.m_joint[i], parentForce.m_body, parentForce.m_body);
		}
	}

	DG_INLINE void JointJacobianTimeSolutionBackward(const dgBodyJointMatrixDataPair* const data, dgForcePair& force, const dgForcePair& parentForce)
	{
		const dgSpatialMatrix& jointJ = data[m_index].m_joint.m_jt;
		const dgSpatialVector& f = parentForce.m_body;
		for (dgInt32 i = 0; i < m_dof; i++) {
			force.m_joint[i] -= f.DotProduct(jointJ[i]);
		}
	}

	DG_INLINE void BodyJacobianTimeSolutionBackward(const dgBodyJointMatrixDataPair* const data, dgForcePair& force)
	{
		const dgSpatialMatrix& bodyJt = data[m_index].m_body.m_jt;
		for (dgInt32 i = 0; i < m_dof; i++) {
			bodyJt[i].ScaleAdd(-force.m_joint[i], force.m_body, force.m_body);
		}
	}

	DG_INLINE void BodyDiagInvTimeSolution(const dgBodyJointMatrixDataPair* const data, dgForcePair& force)
	{
		const dgSpatialMatrix& bodyInvMass = data[m_index].m_body.m_invMass;
		bodyInvMass.MultiplyNxNMatrixTimeVector(force.m_body, force.m_body);
	}

	DG_INLINE void JointDiagInvTimeSolution(const dgBodyJointMatrixDataPair* const data, dgForcePair& force)
	{
		const dgSpatialMatrix& jointInvMass = data[m_index].m_joint.m_invMass;
		jointInvMass.MultiplyNxNMatrixTimeVector (force.m_joint, force.m_joint, m_dof);
	}

	DG_INLINE dgInt32 GetAuxiliaryRows(const dgJointInfo* const jointInfoArray, const dgJacobianMatrixElement* const matrixRow) const
	{
		dgInt32 rowCount = 0;
		if (m_joint) {
			dgAssert(m_parent);
			const dgJointInfo* const jointInfo = &jointInfoArray[m_joint->m_index];
			dgAssert(jointInfo->m_joint == m_joint);
			dgAssert(jointInfo->m_joint->GetBody0() == m_body);
			dgAssert(jointInfo->m_joint->GetBody1() == m_parent->m_body);

			dgInt32 count = jointInfo->m_pairCount;
			const dgInt32 first = jointInfo->m_pairStart;
			for (dgInt32 i = 0; i < count; i++) {
				const dgJacobianMatrixElement* const row = &matrixRow[i + first];
				if (!((row->m_lowerBoundFrictionCoefficent <= dgFloat32 (-DG_LCP_MAX_VALUE)) && (row->m_upperBoundFrictionCoefficent >= dgFloat32 (DG_LCP_MAX_VALUE)))) {
					rowCount++;
				}
			}
		}
		return rowCount;
	}
	
	dgDynamicBody* m_body;
	dgBilateralConstraint* m_joint;
	dgGraph* m_parent;
	dgGraph* m_child;
	dgGraph* m_sibling;
	dgInt16 m_index;
	dgInt16 m_dof;
	dgInt16 m_primaryStart;
	dgInt16 m_auxiliaryStart;
	union {
		dgInt8 m_sourceJacobianIndex[8];
		dgInt64 m_ordinals;
	};

	static dgInt64 m_ordinalInit;
};

dgInt64 dgSkeletonContainer::dgGraph::m_ordinalInit = 0x050403020100ll;

dgSkeletonContainer::dgSkeletonContainer(dgWorld* const world, dgDynamicBody* const rootBody)
	:m_world(world)
	,m_skeleton(new (rootBody->GetWorld()->GetAllocator()) dgGraph(rootBody, NULL, NULL))
	,m_nodesOrder(NULL)
	,m_pairs(NULL)
	,m_deltaForce(NULL)
	,m_massMatrix11(NULL)
	,m_massMatrix10(NULL)
	,m_rowArray(NULL)
	,m_localMatrices(NULL)
	,m_destructor(NULL)
	,m_cyclingJoints(rootBody->GetWorld()->GetAllocator())
	,m_id(m_uniqueID)
	,m_lru(0)
	,m_nodeCount(1)
	,m_rowCount(0)
	,m_auxiliaryRowCount(0)
{
	rootBody->SetSkeleton(this);
	m_uniqueID++;
}

dgSkeletonContainer::~dgSkeletonContainer()
{
	m_cyclingJoints.RemoveAll();

	if (m_destructor) {
		m_destructor (this);
	}
	
	dgMemoryAllocator* const allocator = m_world->GetAllocator();
	if (m_nodesOrder) {
		allocator->Free(m_nodesOrder);
	}
	delete m_skeleton;
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::GetRoot () const
{
	return m_skeleton;
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::GetParent (dgGraph* const node) const
{
	return node->m_parent;
}

dgBody* dgSkeletonContainer::GetBody(dgSkeletonContainer::dgGraph* const node) const
{
	return node->m_body;
}

dgBilateralConstraint* dgSkeletonContainer::GetParentJoint(dgSkeletonContainer::dgGraph* const node) const
{
	return node->m_joint;
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::GetFirstChild(dgSkeletonContainer::dgGraph* const parent) const
{
	return parent->m_child;
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::GetNextSiblingChild(dgSkeletonContainer::dgGraph* const sibling) const
{
	return sibling->m_sibling;
}

dgWorld* dgSkeletonContainer::GetWorld() const
{
	return m_world;
}

void dgSkeletonContainer::SetDestructorCallback (dgOnSkeletonContainerDestroyCallback destructor)
{
	m_destructor = destructor;
}

void dgSkeletonContainer::ResetUniqueId(dgInt32 id)
{
	m_uniqueID = id;
}

void dgSkeletonContainer::SortGraph(dgGraph* const root, dgGraph* const parent, dgInt32& index)
{
	for (dgGraph* node = root->m_child; node; node = node->m_sibling) {
		SortGraph(node, root, index);
	}

	dgAssert((m_nodeCount - index - 1) >= 0);
	m_nodesOrder[index] = root;
	root->m_index = dgInt16(index);
	index++;
	if (root->m_joint) {
		root->m_joint->m_hasSkeleton = true;
	}
	dgAssert(index <= m_nodeCount);
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::FindNode(dgDynamicBody* const body) const
{
	dgInt32 stack = 1;
	dgGraph* stackPool[1024];

	stackPool[0] = m_skeleton;
	while (stack) {
		stack--;
		dgGraph* const node = stackPool[stack];
		if (node->m_body == body) {
			return node;
		}

		for (dgGraph* ptr = node->m_child; ptr; ptr = ptr->m_sibling) {
			stackPool[stack] = ptr;
			stack++;
			dgAssert(stack < dgInt32(sizeof (stackPool) / sizeof (stackPool[0])));
		}
	}
	return NULL;
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::AddChild(dgBody* const child, dgBody* const parent)
{
	dgAssert(child);
	dgBody* const parentBody = parent ? parent : m_skeleton->m_body;
	dgAssert(parentBody);
	dgAssert(child->GetType() == dgBody::m_dynamicBody);
	dgAssert(parentBody->GetType() == dgBody::m_dynamicBody);
	return AddChild((dgDynamicBody*)child, (dgDynamicBody*)parentBody);
}

dgSkeletonContainer::dgGraph* dgSkeletonContainer::AddChild(dgDynamicBody* const child, dgDynamicBody* const parent)
{
	dgAssert (m_skeleton->m_body);
	dgBilateralConstraint* const joint = m_world->FindBilateralJoint(child, parent);
	dgAssert(joint);

	dgGraph* node = NULL;
	dgMemoryAllocator* const allocator = m_world->GetAllocator();
	if ((joint->GetBody0() == child) && (joint->GetBody1() == parent)) {
		dgGraph* const parentNode = FindNode(parent);
		dgAssert(parentNode);
		node = new (allocator)dgGraph(child, joint, parentNode);
	} else {
		dgAssert(joint->GetBody1() == child);
		dgAssert(joint->GetBody0() == parent);
		dgAssert (m_skeleton->m_body == parent);
		dgAssert (m_skeleton->m_joint == NULL);
		dgAssert (m_skeleton->m_sibling == NULL);
		m_skeleton->m_joint = joint;
		node = new (allocator) dgGraph (child, NULL, NULL);
		node->m_child = m_skeleton;
		m_skeleton->m_parent = node;
		dgSwap (m_skeleton, node);
	}

	dgAssert(node->m_joint->GetBody0() == node->m_body);
	dgAssert(node->m_joint->GetBody1() == node->m_parent->m_body);
	m_nodeCount ++;

	dgAssert (child->GetWorld()->GetSentinelBody() != child);
	child->SetSkeleton(this);
	
	return node;
}

bool dgSkeletonContainer::AttachCyclingJoint(dgBilateralConstraint* const joint)
{
	dgDynamicBody* const body0 = (dgDynamicBody*) joint->GetBody0();
	dgDynamicBody* const body1 = (dgDynamicBody*) joint->GetBody1();

	dgAssert (body0->IsRTTIType(dgBody::m_dynamicBodyRTTI));
	dgAssert (body1->IsRTTIType(dgBody::m_dynamicBodyRTTI));
	dgGraph* const node0 = FindNode (body0);
	dgGraph* const node1 = FindNode (body1);

	bool ret = node0 || node1; 
	ret = ret && (node0 || (body0->GetSkeleton() ==  NULL));
	ret = ret && (node1 || (body1->GetSkeleton() ==  NULL));
	m_cyclingJoints.Append(joint);

	return ret;
}

void dgSkeletonContainer::RemoveCyclingJoint(dgBilateralConstraint* const joint)
{
	for (dgList<dgConstraint*>::dgListNode* ptr = m_cyclingJoints.GetFirst(); ptr; ptr = ptr->GetNext()) {
		if (ptr->GetInfo() == joint) {
			m_cyclingJoints.Remove(ptr);
			break;
		}
	}
}

void dgSkeletonContainer::AddJointList (dgInt32 count, dgBilateralConstraint** const array)
{
	dgTree<dgBody*, dgBody*> filter(m_world->GetAllocator());
	dgTree<dgConstraint*, dgConstraint*> jointMap(m_world->GetAllocator());

	dgInt32 stack = 0;
	dgBody* pool[1024][2];
	filter.Insert(m_skeleton->m_body, m_skeleton->m_body);
	for (dgInt32 i = 0; i < count; i++) {
		dgBilateralConstraint* const joint = array[i];
		jointMap.Insert(joint, joint);

		dgBody* const body0 = joint->GetBody0();
		dgBody* const body1 = joint->GetBody1();
		if (body1 == m_skeleton->m_body) {
			pool[stack][0] = joint->GetBody0();
			pool[stack][1] = joint->GetBody1();
			filter.Insert(pool[stack][0], pool[stack][0]);
			stack++;
		} else if (body0 == m_skeleton->m_body) {
			pool[stack][0] = joint->GetBody1();
			pool[stack][1] = joint->GetBody0();
			filter.Insert(pool[stack][0], pool[stack][0]);
			stack++;
		}
	}

	while (stack) {
		stack--;
		dgBody* const child = pool[stack][0];
		dgBody* const parent = pool[stack][1];
		AddChild(child, parent);

		for (dgConstraint* joint = child->GetFirstJoint(); joint; joint = child->GetNextJoint(joint)) {
			dgAssert(joint->IsBilateral());
			if (jointMap.Find(joint)) {
				dgBody* const body = (joint->GetBody0() != child) ? joint->GetBody0() : joint->GetBody1();
				if (!filter.Find(body)) {
					pool[stack][0] = body;
					pool[stack][1] = child;
					stack++;
					filter.Insert(body, body);
					dgAssert(stack < sizeof (pool) / (2 * sizeof (pool[0][0])));
				}
			}
		}
	}
}


void dgSkeletonContainer::Finalize()
{
	dgAssert(m_nodeCount >= 1);

	dgMemoryAllocator* const allocator = m_skeleton->m_body->GetWorld()->GetAllocator();
	m_nodesOrder = (dgGraph**)allocator->Malloc(m_nodeCount * sizeof (dgGraph*));

	dgInt32 index = 0;
	SortGraph(m_skeleton, NULL, index);
	dgAssert(index == m_nodeCount);
}


void dgSkeletonContainer::InitMassMatrix(const dgJointInfo* const jointInfoArray, dgJacobianMatrixElement* const matrixRow, dgInt8* const memoryBuffer)
{
	dTimeTrackerEvent(__FUNCTION__);

	dgInt32 rowCount = 0;
	dgInt32 primaryStart = 0;
	dgInt32 auxiliaryStart = 0;

	m_localMatrices = (dgBodyJointMatrixDataPair*) memoryBuffer;
	dgAssert ((dgInt64 (m_localMatrices) & 0x0f) == 0);

	if (m_nodesOrder) {
		for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
			dgGraph* const node = m_nodesOrder[i];
			rowCount += jointInfoArray[node->m_joint->m_index].m_pairCount;
			node->m_auxiliaryStart = dgInt16 (auxiliaryStart);
			node->m_primaryStart = dgInt16 (primaryStart);
			auxiliaryStart += node->Factorize(jointInfoArray, matrixRow, m_localMatrices);
			primaryStart += node->m_dof;
		}
		m_nodesOrder[m_nodeCount - 1]->Factorize(jointInfoArray, matrixRow, m_localMatrices);
	}
	m_rowCount = dgInt16 (rowCount);
	m_auxiliaryRowCount = dgInt16 (auxiliaryStart);

	dgInt32 extraAuxiliaryRows = 0;
	for (dgList<dgConstraint*>::dgListNode* ptr = m_cyclingJoints.GetFirst(); ptr; ptr = ptr->GetNext()) {
		const dgConstraint* const joint = ptr->GetInfo();
		extraAuxiliaryRows += jointInfoArray[joint->m_index].m_pairCount;
	}
	m_rowCount += dgInt16 (extraAuxiliaryRows);
	m_auxiliaryRowCount += dgInt16 (extraAuxiliaryRows);

	if (m_auxiliaryRowCount) {
		const dgInt32 primaryCount = m_rowCount - m_auxiliaryRowCount;
		dgInt32 primaryIndex = 0;
		dgInt32 auxiliaryIndex = 0;

		m_rowArray = (dgJacobianMatrixElement**) &m_localMatrices[m_nodeCount];
		m_pairs = (dgNodePair*) &m_rowArray[m_rowCount];
		m_massMatrix11 = (dgFloat32*)&m_pairs[m_rowCount];
		m_massMatrix10 = &m_massMatrix11[m_auxiliaryRowCount * m_auxiliaryRowCount];
		m_deltaForce = &m_massMatrix10[m_auxiliaryRowCount * primaryCount];

		for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
			const dgGraph* const node = m_nodesOrder[i];
			const dgJointInfo* const jointInfo = &jointInfoArray[node->m_joint->m_index];
			
			const dgInt32 m0 = jointInfo->m_m0;
			const dgInt32 m1 = jointInfo->m_m1;
			const dgInt32 primaryDof = node->m_dof;
			const dgInt32 first = jointInfo->m_pairStart;

			for (dgInt32 j = 0; j < primaryDof; j++) {
				const dgInt32 index = node->m_sourceJacobianIndex[j];
				m_rowArray[primaryIndex] = &matrixRow[first + index];
				m_pairs[primaryIndex].m_m0 = m0;
				m_pairs[primaryIndex].m_m1 = m1;
				primaryIndex++;
			}

			const dgInt32 auxiliaryDof = jointInfo->m_pairCount - primaryDof;
			for (dgInt32 j = 0; j < auxiliaryDof; j++) {
				const dgInt32 index = node->m_sourceJacobianIndex[primaryDof + j];
				dgJacobianMatrixElement* const row = &matrixRow[first + index];
				m_rowArray[auxiliaryIndex + primaryCount] = row;
				m_pairs[auxiliaryIndex + primaryCount].m_m0 = m0;
				m_pairs[auxiliaryIndex + primaryCount].m_m1 = m1;
				auxiliaryIndex++;
			}
		}

		for (dgList<dgConstraint*>::dgListNode* ptr = m_cyclingJoints.GetFirst(); ptr; ptr = ptr->GetNext()) {
			const dgConstraint* const joint = ptr->GetInfo();
			const dgJointInfo* const jointInfo = &jointInfoArray[joint->m_index];
			
			const dgInt32 m0 = jointInfo->m_m0;
			const dgInt32 m1 = jointInfo->m_m1;
			const dgInt32 first = jointInfo->m_pairStart;
			const dgInt32 auxiliaryDof = jointInfo->m_pairCount;

			for (dgInt32 i = 0; i < auxiliaryDof; i++) {
				dgJacobianMatrixElement* const row = &matrixRow[first + i];
				m_rowArray[auxiliaryIndex + primaryCount] = row;
				m_pairs[auxiliaryIndex + primaryCount].m_m0 = m0;
				m_pairs[auxiliaryIndex + primaryCount].m_m1 = m1;
				auxiliaryIndex++;
			}
		}

		const dgInt32 auxiliaryStart = m_rowCount - m_auxiliaryRowCount;
		for (dgInt32 i = 0; i < m_auxiliaryRowCount; i++) {
			const dgJacobianMatrixElement* const row_i = m_rowArray[primaryCount + i];
			dgFloat32* const matrixRow11 = &m_massMatrix11[m_auxiliaryRowCount * i];

			dgJacobian JMinvM0(row_i->m_JMinv.m_jacobianM0);
			dgJacobian JMinvM1(row_i->m_JMinv.m_jacobianM1);

			dgVector element(JMinvM0.m_linear.CompProduct4(row_i->m_Jt.m_jacobianM0.m_linear) + JMinvM0.m_angular.CompProduct4(row_i->m_Jt.m_jacobianM0.m_angular) +
							 JMinvM1.m_linear.CompProduct4(row_i->m_Jt.m_jacobianM1.m_linear) + JMinvM1.m_angular.CompProduct4(row_i->m_Jt.m_jacobianM1.m_angular));
			element = element.AddHorizontal();
			dgFloat32 val = element.GetScalar() + row_i->m_diagDamp;
			matrixRow11[i] = val;

			const dgInt32 m0 = m_pairs[auxiliaryStart + i].m_m0;
			const dgInt32 m1 = m_pairs[auxiliaryStart + i].m_m1;
			for (dgInt32 j = i + 1; j < m_auxiliaryRowCount; j++) {
				const dgJacobianMatrixElement* const row_j = m_rowArray[auxiliaryStart + j];

				const dgInt32 k = auxiliaryStart + j;
				dgVector acc(dgVector::m_zero);
				if (m0 == m_pairs[k].m_m0) {
					acc += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
				} else if (m0 == m_pairs[k].m_m1) {
					acc += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
				}

				if (m1 == m_pairs[k].m_m1) {
					acc += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
				} else if (m1 == m_pairs[k].m_m0) {
					acc += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
				}
				acc = acc.AddHorizontal();
				dgFloat32 val = acc.GetScalar();
				matrixRow11[j] = val;
				m_massMatrix11[j * m_auxiliaryRowCount + i] = val;
			}

			dgFloat32* const matrixRow10 = &m_massMatrix10[primaryCount * i];
			for (dgInt32 j = 0; j < primaryCount; j++) {
				const dgJacobianMatrixElement* const row_j = m_rowArray[j];

				dgVector acc(dgVector::m_zero);
				if (m0 == m_pairs[j].m_m0) {
					acc += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
				} else if (m0 == m_pairs[j].m_m1) {
					acc += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
				}

				if (m1 == m_pairs[j].m_m1) {
					acc += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
				} else if (m1 == m_pairs[j].m_m0) {
					acc += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
				}
				acc = acc.AddHorizontal();
				dgFloat32 val = acc.GetScalar();
				matrixRow10[j] = val;
			}
		}

		dgForcePair* const forcePair = dgAlloca(dgForcePair, m_nodeCount);
		dgForcePair* const accelPair = dgAlloca(dgForcePair, m_nodeCount);
		accelPair[m_nodeCount - 1].m_body.SetZero();
		accelPair[m_nodeCount - 1].m_joint.SetZero();

		for (dgInt32 i = 0; i < m_auxiliaryRowCount; i++) {
			dgFloat32* const matrixRow10 = &m_massMatrix10[i * primaryCount];

			dgInt32 entry = 0;
			for (dgInt32 j = 0; j < m_nodeCount - 1; j++) {
				const dgGraph* const node = m_nodesOrder[j];
				const dgInt32 index = node->m_index;
				accelPair[index].m_body.SetZero();
				dgSpatialVector& a = accelPair[index].m_joint;

				const int count = node->m_dof;
				for (dgInt32 k = 0; k < count; k++) {
					a[k] = matrixRow10[entry];
					entry++;
				}
			}

			SolveFoward(forcePair, accelPair);
			SolveBackward(forcePair);

			entry = 0;
			dgFloat32* const deltaForcePtr = &m_deltaForce[i * primaryCount];
			for (dgInt32 j = 0; j < m_nodeCount - 1; j++) {
				const dgGraph* const node = m_nodesOrder[j];
				const dgInt32 index = node->m_index;
				const dgSpatialVector& f = forcePair[index].m_joint;
				const int count = node->m_dof;
				for (dgInt32 k = 0; k < count; k++) {
					deltaForcePtr[entry] = f[k];
					entry++;
				}
			}

			dgFloat32* const matrixRow11 = &m_massMatrix11[i * m_auxiliaryRowCount];
			dgFloat32 acc = dgFloat32(0.0f);

			for (dgInt32 k = 0; k < primaryCount; k++) {
				acc += deltaForcePtr[k] * matrixRow10[k];
			}
			matrixRow11[i] += acc;
			for (dgInt32 j = i + 1; j < m_auxiliaryRowCount; j++) {
				dgFloat32 acc = dgFloat32(0.0f);
				const dgFloat32* const matrixRow10 = &m_massMatrix10[j * primaryCount];
				for (dgInt32 k = 0; k < primaryCount; k++) {
					acc += deltaForcePtr[k] * matrixRow10[k];
				}
				matrixRow11[j] += acc;
				m_massMatrix11[j * m_auxiliaryRowCount + i] += acc;
			}
		}
	}
}

DG_INLINE void dgSkeletonContainer::SolveFoward (dgForcePair* const force, const dgForcePair* const accel) const
{
	const dgBodyJointMatrixDataPair* const data = m_localMatrices;
	for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
		dgGraph* const node = m_nodesOrder[i];
		dgAssert(node->m_joint);
		dgAssert(node->m_index == i);
		dgForcePair& f = force[i];
		const dgForcePair& a = accel[i];
		f.m_body = a.m_body;
		for (dgInt32 j = 0; j < node->m_dof; j ++) {
			f.m_joint[j] = a.m_joint[j]; 
		}
		for (dgGraph* child = node->m_child; child; child = child->m_sibling) {
			dgAssert(child->m_joint);
			dgAssert(child->m_parent->m_index == i);
			child->BodyJacobianTimeMassForward(data, force[child->m_index], f);
		}
		node->JointJacobianTimeMassForward(data, f);
	}

	force[m_nodeCount - 1] = accel[m_nodeCount - 1];
	for (dgGraph* child = m_nodesOrder[m_nodeCount - 1]->m_child; child; child = child->m_sibling) {
		child->BodyJacobianTimeMassForward(data, force[child->m_index], force[child->m_parent->m_index]);
	}
}

DG_INLINE void dgSkeletonContainer::SolveBackward (dgForcePair* const force) const
{
	const dgBodyJointMatrixDataPair* const data = m_localMatrices;
	m_nodesOrder[m_nodeCount - 1]->BodyDiagInvTimeSolution(data, force[m_nodeCount - 1]);
	for (dgInt32 i = m_nodeCount - 2; i >= 0; i--) {
		dgGraph* const node = m_nodesOrder[i];
		dgAssert (node->m_index == i);
		dgForcePair& f = force[i];
		node->JointDiagInvTimeSolution(data, f);
		node->JointJacobianTimeSolutionBackward(data, f, force[node->m_parent->m_index]);
		node->BodyDiagInvTimeSolution(data, f);
		node->BodyJacobianTimeSolutionBackward(data, f);
	}
}


DG_INLINE void dgSkeletonContainer::UpdateForces (dgJointInfo* const jointInfoArray, dgJacobian* const internalForces, dgJacobianMatrixElement* const matrixRow, const dgForcePair* const force) const
{
	dgVector zero (dgVector::m_zero);
	for (dgInt32 i = 0; i < (m_nodeCount - 1)  ; i ++) {
		dgGraph* const node = m_nodesOrder[i];
		const dgJointInfo* const jointInfo = &jointInfoArray[node->m_joint->m_index];

		dgJacobian y0;
		dgJacobian y1;
		y0.m_linear = zero;
		y0.m_angular = zero;
		y1.m_linear = zero;
		y1.m_angular = zero;
		dgAssert(i == node->m_index);

		const dgSpatialVector& f = force[i].m_joint;
		dgAssert(jointInfo->m_joint == node->m_joint);
		const dgInt32 first = jointInfo->m_pairStart;
		const dgInt32 count = node->m_dof;
		for (dgInt32 j = 0; j < count; j ++) {
			const dgInt32 k = node->m_sourceJacobianIndex[j];
			dgJacobianMatrixElement* const row = &matrixRow[first + k];

			row->m_force += f[j];
			dgVector jointForce = dgFloat32 (f[j]);
			y0.m_linear += row->m_Jt.m_jacobianM0.m_linear.CompProduct4(jointForce);
			y0.m_angular += row->m_Jt.m_jacobianM0.m_angular.CompProduct4(jointForce);
			y1.m_linear += row->m_Jt.m_jacobianM1.m_linear.CompProduct4(jointForce);
			y1.m_angular += row->m_Jt.m_jacobianM1.m_angular.CompProduct4(jointForce);
		}

		const dgInt32 m0 = jointInfo->m_m0;
		const dgInt32 m1 = jointInfo->m_m1;
		internalForces[m0].m_linear += y0.m_linear;
		internalForces[m0].m_angular += y0.m_angular;
		internalForces[m1].m_linear += y1.m_linear;
		internalForces[m1].m_angular += y1.m_angular;
	}
}

DG_INLINE void dgSkeletonContainer::CalculateJointAccel(dgJointInfo* const jointInfoArray, const dgJacobian* const internalForces, dgJacobianMatrixElement* const matrixRow, dgForcePair* const accel) const
{
	for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
		dgGraph* const node = m_nodesOrder[i];
		dgAssert(i == node->m_index);

		dgForcePair& a = accel[i];
		dgAssert(node->m_body);
		a.m_body.SetZero();
		a.m_joint.SetZero();

		dgAssert(node->m_joint);
		const dgJointInfo* const jointInfo = &jointInfoArray[node->m_joint->m_index];
		dgAssert(jointInfo->m_joint == node->m_joint);

		const dgInt32 first = jointInfo->m_pairStart;
		const dgInt32 dof = jointInfo->m_pairCount;
		const dgInt32 m0 = jointInfo->m_m0;
		const dgInt32 m1 = jointInfo->m_m1;
		const dgJacobian& y0 = internalForces[m0];
		const dgJacobian& y1 = internalForces[m1];

		for (dgInt32 j = 0; j < dof; j++) {
			const dgInt32 k = node->m_sourceJacobianIndex[j];
			const dgJacobianMatrixElement* const row = &matrixRow[first + k];
			dgVector acc(row->m_JMinv.m_jacobianM0.m_linear.CompProduct4(y0.m_linear) + row->m_JMinv.m_jacobianM0.m_angular.CompProduct4(y0.m_angular) +
						 row->m_JMinv.m_jacobianM1.m_linear.CompProduct4(y1.m_linear) + row->m_JMinv.m_jacobianM1.m_angular.CompProduct4(y1.m_angular));
			//acc = dgVector(row->m_coordenateAccel) - acc.AddHorizontal();
			//a.m_joint[j] = -acc.GetScalar();
			a.m_joint[j] = (acc.AddHorizontal()).GetScalar() - row->m_coordenateAccel;
		}
	}
	dgAssert((m_nodeCount - 1) == m_nodesOrder[m_nodeCount - 1]->m_index);
	accel[m_nodeCount - 1].m_body.SetZero();
	accel[m_nodeCount - 1].m_joint.SetZero();
}

void dgSkeletonContainer::BruteForceSolve(const dgJointInfo* const jointInfoArray, dgJacobian* const internalForces, dgJacobianMatrixElement* const matrixRow, const dgForcePair* const accel, dgForcePair* const force) const
{
	dTimeTrackerEvent(__FUNCTION__);
	dgNodePair* const pairs = dgAlloca(dgNodePair, 6 * (m_nodeCount - 1));
	dgJacobianMatrixElement** const rowArray = dgAlloca(dgJacobianMatrixElement*, 6 * (m_nodeCount - 1));

	dgInt32 count = 0;
	for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
		const dgGraph* const node = m_nodesOrder[i];
		const dgJointInfo* const jointInfo = &jointInfoArray[node->m_joint->m_index];
		const dgInt32 first = jointInfo->m_pairStart;
		const dgInt32 rowCount = jointInfo->m_pairCount;

		const dgInt32 m0 = jointInfo->m_m0;
		const dgInt32 m1 = jointInfo->m_m1;

		for (dgInt32 j = 0; j < rowCount; j++) {
			rowArray[count] = &matrixRow[first + j];
			pairs[count].m_m0 = m0;
			pairs[count].m_m1 = m1;
			count++;
		}
	}

	dgFloat32* const f = dgAlloca(dgFloat32, count);
	dgFloat32* const b = dgAlloca(dgFloat32, count);
	dgFloat32* const low = dgAlloca(dgFloat32, count);
	dgFloat32* const high = dgAlloca(dgFloat32, count);
	dgFloat32* const massMatrix = dgAlloca(dgFloat32, count * count);
	for (dgInt32 i = 0; i < count; i++) {

		const dgJacobianMatrixElement* const row_i = rowArray[i];
		const dgInt32 m0 = pairs[i].m_m0;
		const dgInt32 m1 = pairs[i].m_m1;
		dgFloat32* const matrixRow = &massMatrix[count * i];

		dgJacobian JMinvM0(row_i->m_JMinv.m_jacobianM0);
		dgJacobian JMinvM1(row_i->m_JMinv.m_jacobianM1);
		dgVector element(JMinvM0.m_linear.CompProduct4(row_i->m_Jt.m_jacobianM0.m_linear) + JMinvM0.m_angular.CompProduct4(row_i->m_Jt.m_jacobianM0.m_angular) +
						 JMinvM1.m_linear.CompProduct4(row_i->m_Jt.m_jacobianM1.m_linear) + JMinvM1.m_angular.CompProduct4(row_i->m_Jt.m_jacobianM1.m_angular));
		element = element.AddHorizontal();
		dgFloat32 val = element.GetScalar() + row_i->m_diagDamp;
		matrixRow[i] = val;

		const dgJacobian& y0 = internalForces[m0];
		const dgJacobian& y1 = internalForces[m1];
		dgVector acc(JMinvM0.m_linear.CompProduct4(y0.m_linear) + JMinvM0.m_angular.CompProduct4(y0.m_angular) +
					 JMinvM1.m_linear.CompProduct4(y1.m_linear) + JMinvM1.m_angular.CompProduct4(y1.m_angular));
		
		f[i] = dgFloat32 (0.0f);
		b[i] = row_i->m_coordenateAccel - acc.AddHorizontal().GetScalar();
		low[i] = row_i->m_lowerBoundFrictionCoefficent - row_i->m_force;
		high[i] = row_i->m_upperBoundFrictionCoefficent - row_i->m_force;

		for (dgInt32 j = i + 1; j < count; j++) {
			const dgJacobianMatrixElement* const row_j = rowArray[j];

			dgVector element(dgVector::m_zero);
			if (m0 == pairs[j].m_m0) {
				element += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
			} else if (m0 == pairs[j].m_m1) {
				element += JMinvM0.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM0.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
			}

			if (m1 == pairs[j].m_m1) {
				element += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM1.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM1.m_angular);
			} else if (m1 == pairs[j].m_m0) {
				element += JMinvM1.m_linear.CompProduct4(row_j->m_Jt.m_jacobianM0.m_linear) + JMinvM1.m_angular.CompProduct4(row_j->m_Jt.m_jacobianM0.m_angular);
			}
			element = element.AddHorizontal();
			dgFloat32 val = element.GetScalar();
			matrixRow[j] = val;
			massMatrix[j * count + i] = val;
		}
	}

	dgSolvePartitionDantzigLCP (count, massMatrix, f, b, low, high);

	for (dgInt32 i = 0; i < count ; i ++) {
		dgJacobianMatrixElement* const row = rowArray[i];
		const dgInt32 m0 = pairs[i].m_m0;
		const dgInt32 m1 = pairs[i].m_m1;

		row->m_force += f[i];
		dgVector jointForce (f[i]);
		internalForces[m0].m_linear += row->m_Jt.m_jacobianM0.m_linear.CompProduct4(jointForce);
		internalForces[m0].m_angular += row->m_Jt.m_jacobianM0.m_angular.CompProduct4(jointForce);
		internalForces[m1].m_linear += row->m_Jt.m_jacobianM1.m_linear.CompProduct4(jointForce);
		internalForces[m1].m_angular += row->m_Jt.m_jacobianM1.m_angular.CompProduct4(jointForce);
	}
}

void dgSkeletonContainer::SolveAuxiliary(const dgJointInfo* const jointInfoArray, dgJacobian* const internalForces, dgJacobianMatrixElement* const matrixRow, const dgForcePair* const accel, dgForcePair* const force) const
{
	dTimeTrackerEvent(__FUNCTION__);
	
	dgFloat32* const f = dgAlloca(dgFloat32, m_rowCount);
	dgFloat32* const u = dgAlloca(dgFloat32, m_auxiliaryRowCount);
	dgFloat32* const b = dgAlloca(dgFloat32, m_auxiliaryRowCount);
	dgFloat32* const low = dgAlloca(dgFloat32, m_auxiliaryRowCount);
	dgFloat32* const high = dgAlloca(dgFloat32, m_auxiliaryRowCount);
	dgFloat32* const massMatrix11 = dgAlloca(dgFloat32, m_auxiliaryRowCount * m_auxiliaryRowCount);

	dgInt32 primaryIndex = 0;
	dgInt32 auxiliaryIndex = 0;
	const dgInt32 primaryCount = m_rowCount - m_auxiliaryRowCount;
	
	for (dgInt32 i = 0; i < m_nodeCount - 1; i++) { 
		const dgGraph* const node = m_nodesOrder[i];
		const dgJointInfo* const jointInfo = &jointInfoArray[node->m_joint->m_index];
		const dgInt32 first = jointInfo->m_pairStart;

		const dgInt32 primaryDof = node->m_dof;
		const dgSpatialVector& accelSpatial = accel[i].m_joint;
		const dgSpatialVector& forceSpatial = force[i].m_joint;

		for (dgInt32 j = 0; j < primaryDof; j++) {
			f[primaryIndex] = forceSpatial[j];
			primaryIndex++;
		}

		const dgInt32 auxiliaryDof = jointInfo->m_pairCount - primaryDof;
		for (dgInt32 j = 0; j < auxiliaryDof; j++) {
			const dgInt32 index = node->m_sourceJacobianIndex[primaryDof + j];
			dgJacobianMatrixElement* const row = &matrixRow[first + index];
			//f[auxiliaryIndex + primaryCount] = forceSpatial[primaryDof + j];
			f[auxiliaryIndex + primaryCount] = dgFloat32 (0.0f);
			b[auxiliaryIndex] = -accelSpatial[primaryDof + j];
			low[auxiliaryIndex] = row->m_lowerBoundFrictionCoefficent - row->m_force;
			high[auxiliaryIndex] = row->m_upperBoundFrictionCoefficent - row->m_force;
			auxiliaryIndex++;
		}
	}

	for (dgList<dgConstraint*>::dgListNode* ptr = m_cyclingJoints.GetFirst(); ptr; ptr = ptr->GetNext()) {
		const dgConstraint* const joint = ptr->GetInfo();
		const dgJointInfo* const jointInfo = &jointInfoArray[joint->m_index];

		const dgInt32 m0 = jointInfo->m_m0;
		const dgInt32 m1 = jointInfo->m_m1;
		const dgInt32 first = jointInfo->m_pairStart;
		const dgInt32 auxiliaryDof = jointInfo->m_pairCount;
		const dgJacobian& y0 = internalForces[m0];
		const dgJacobian& y1 = internalForces[m1];

		for (dgInt32 i = 0; i < auxiliaryDof; i++) {
			dgJacobianMatrixElement* const row = &matrixRow[first + i];
			f[auxiliaryIndex + primaryCount] = dgFloat32 (0.0f);
			dgVector acc(row->m_JMinv.m_jacobianM0.m_linear.CompProduct4(y0.m_linear) + row->m_JMinv.m_jacobianM0.m_angular.CompProduct4(y0.m_angular) +
						 row->m_JMinv.m_jacobianM1.m_linear.CompProduct4(y1.m_linear) + row->m_JMinv.m_jacobianM1.m_angular.CompProduct4(y1.m_angular));
			b[auxiliaryIndex] = row->m_coordenateAccel - (acc.AddHorizontal()).GetScalar();
			low[auxiliaryIndex] = row->m_lowerBoundFrictionCoefficent - row->m_force;
			high[auxiliaryIndex] = row->m_upperBoundFrictionCoefficent - row->m_force;
			auxiliaryIndex++;
		}
	}

	memcpy (massMatrix11, m_massMatrix11, sizeof (dgFloat32) * m_auxiliaryRowCount * m_auxiliaryRowCount);
	for (dgInt32 i = 0; i < m_auxiliaryRowCount; i ++) {
		dgFloat32* const matrixRow10 = &m_massMatrix10[i * primaryCount];
		u[i] = dgFloat32(0.0f);
		dgFloat32 r = dgFloat32(0.0f);
		for (dgInt32 j = 0; j < primaryCount; j++) {
			r += matrixRow10[j] * f[j];
		}
		b[i] -= r;
	}
	
	dgSolveDantzigLCP(m_auxiliaryRowCount, massMatrix11, u, b, low, high);

	for (dgInt32 i = 0; i < m_auxiliaryRowCount; i++) {
		const dgFloat32 s = u[i];
		f[primaryCount + i] = s;
		const dgFloat32* const deltaForce = &m_deltaForce[i * primaryCount];
		for (dgInt32 j = 0; j < primaryCount; j++) {
			f[j] += deltaForce[j] * s;
		}
	}

	for (dgInt32 i = 0; i < m_rowCount; i++) {
		dgJacobianMatrixElement* const row = m_rowArray[i];
		const dgInt32 m0 = m_pairs[i].m_m0;
		const dgInt32 m1 = m_pairs[i].m_m1;

		row->m_force += f[i];
		dgVector jointForce(f[i]);
		internalForces[m0].m_linear += row->m_Jt.m_jacobianM0.m_linear.CompProduct4(jointForce);
		internalForces[m0].m_angular += row->m_Jt.m_jacobianM0.m_angular.CompProduct4(jointForce);
		internalForces[m1].m_linear += row->m_Jt.m_jacobianM1.m_linear.CompProduct4(jointForce);
		internalForces[m1].m_angular += row->m_Jt.m_jacobianM1.m_angular.CompProduct4(jointForce);
	}
}

dgInt32 dgSkeletonContainer::CalculateMemoryBufferSizeInBytes (const dgJointInfo* const jointInfoArray, const dgJacobianMatrixElement* const matrixRow) const
{
	dgInt32 rowCount = 0;
	dgInt32 auxiliaryRowCount = 0;
	if (m_nodesOrder) {
		for (dgInt32 i = 0; i < m_nodeCount - 1; i++) {
			dgGraph* const node = m_nodesOrder[i];
			rowCount += jointInfoArray[node->m_joint->m_index].m_pairCount;
			auxiliaryRowCount += node->GetAuxiliaryRows(jointInfoArray, matrixRow);
		}
	}

	dgInt32 extraAuxiliaryRows = 0;
	for (dgList<dgConstraint*>::dgListNode* ptr = m_cyclingJoints.GetFirst(); ptr; ptr = ptr->GetNext()) {
		const dgConstraint* const joint = ptr->GetInfo();
		extraAuxiliaryRows += jointInfoArray[joint->m_index].m_pairCount;
	}
	rowCount += extraAuxiliaryRows;
	auxiliaryRowCount+= extraAuxiliaryRows;

	dgInt32 size = m_nodeCount * sizeof (dgBodyJointMatrixDataPair);
	size += sizeof (dgJacobianMatrixElement*) * rowCount;
	size += sizeof (dgNodePair) * rowCount;
	size += sizeof (dgFloat32) * auxiliaryRowCount * auxiliaryRowCount;
	size += sizeof (dgFloat32) * auxiliaryRowCount * (rowCount - auxiliaryRowCount);
	size += sizeof (dgFloat32) * auxiliaryRowCount * (rowCount - auxiliaryRowCount);
	return (size + 32) & -0x10;
}

void dgSkeletonContainer::CalculateJointForce(dgJointInfo* const jointInfoArray, const dgBodyInfo* const bodyArray, dgJacobian* const internalForces, dgJacobianMatrixElement* const matrixRow)
{
	dTimeTrackerEvent(__FUNCTION__);

	dgForcePair* const force = dgAlloca(dgForcePair, m_nodeCount);
	dgForcePair* const accel = dgAlloca(dgForcePair, m_nodeCount);

#if 0
	BruteForceSolve (jointInfoArray, internalForces, matrixRow, accel, force);
#else 
	CalculateJointAccel(jointInfoArray, internalForces, matrixRow, accel);
	SolveFoward(force, accel);
	SolveBackward(force);
	if (m_auxiliaryRowCount) {
		SolveAuxiliary (jointInfoArray, internalForces, matrixRow, accel, force);
	} else {
		UpdateForces(jointInfoArray, internalForces, matrixRow, force);
	}
#endif
}




