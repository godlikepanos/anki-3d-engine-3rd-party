/* Copyright (c) <2003-2016> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/


#include "CustomJointLibraryStdAfx.h"
#include "CustomJoint.h"
#include "CustomGear.h"
#include "CustomUniversal.h"
#include "CustomVehicleControllerManager.h"

//#define D_PLOT_ENGINE_CURVE


#ifdef D_PLOT_ENGINE_CURVE 
static FILE* file_xxx;
#endif


#define D_VEHICLE_NEUTRAL_GEAR						0
#define D_VEHICLE_REVERSE_GEAR						1
#define D_VEHICLE_FIRST_GEAR						2
#define D_VEHICLE_MAX_DRIVETRAIN_DOF				32
#define D_VEHICLE_REGULARIZER						dFloat(1.0001f)


#define D_LIMITED_SLIP_DIFFERENTIAL_LOCK_RPS		dFloat(10.0f)

#define D_VEHICLE_ENGINE_IDLE_GAS_VALVE				dFloat(0.1f)
#define D_VEHICLE_ENGINE_IDLE_FRICTION_COEFFICIENT	dFloat(0.25f)

#define D_VEHICLE_MAX_SIDESLIP_ANGLE				dFloat(35.0f * 3.1416f / 180.0f)
#define D_VEHICLE_MAX_SIDESLIP_RATE					dFloat(15.0f * 3.1416f / 180.0f)



class CustomVehicleControllerManager::TireFilter: public CustomControllerConvexCastPreFilter
{
	public:
	TireFilter(const CustomVehicleController::BodyPartTire* const tire, const CustomVehicleController* const controller)
		:CustomControllerConvexCastPreFilter(tire->GetBody())
		,m_tire (tire)
		,m_controller(controller)
	{
	}

	unsigned Prefilter(const NewtonBody* const body, const NewtonCollision* const myCollision)
	{
		dAssert(body != m_me);
		for (int i = 0; i < m_tire->m_collidingCount; i ++) {
			if (m_tire->m_contactInfo[i].m_hitBody == body) {
				return 0;
			}
		}

		for (dList<CustomVehicleController::BodyPart*>::dListNode* node = m_controller->m_bodyPartsList.GetFirst(); node; node = node->GetNext()) {
			if (node->GetInfo()->GetBody() == body) {
				return 0;
			}
		}

		return (body != m_controller->GetBody()) ? 1 : 0;
	}

	const CustomVehicleController* m_controller;
	const CustomVehicleController::BodyPartTire* m_tire;
};


void CustomVehicleController::dInterpolationCurve::InitalizeCurve(int points, const dFloat* const steps, const dFloat* const values)
{
	m_count = points;
	dAssert(points <= int(sizeof(m_nodes) / sizeof (m_nodes[0])));
	memset(m_nodes, 0, sizeof (m_nodes));
	for (int i = 0; i < m_count; i++) {
		m_nodes[i].m_param = steps[i];
		m_nodes[i].m_value = values[i];
	}
}

dFloat CustomVehicleController::dInterpolationCurve::GetValue(dFloat param) const
{
	dFloat interplatedValue = 0.0f;
	if (m_count) {
		param = dClamp(param, dFloat(0.0f), m_nodes[m_count - 1].m_param);
		interplatedValue = m_nodes[m_count - 1].m_value;
		for (int i = 1; i < m_count; i++) {
			if (param < m_nodes[i].m_param) {
				dFloat df = m_nodes[i].m_value - m_nodes[i - 1].m_value;
				dFloat ds = m_nodes[i].m_param - m_nodes[i - 1].m_param;
				dFloat step = param - m_nodes[i - 1].m_param;

				interplatedValue = m_nodes[i - 1].m_value + df * step / ds;
				break;
			}
		}
	}
	return interplatedValue;
}

class CustomVehicleController::WheelJoint: public CustomJoint
{
	public:
	WheelJoint (const dMatrix& pinAndPivotFrame, NewtonBody* const tire, NewtonBody* const parentBody, BodyPartTire* const tireData)
		:CustomJoint (6, tire, parentBody)
		,m_lateralDir(0.0f, 0.0f, 0.0f, 0.0f)
		,m_longitudinalDir(0.0f, 0.0f, 0.0f, 0.0f)
		,m_tire (tireData)
		,m_tireLoad(0.0f)
		,m_steerRate (0.5f * 3.1416f)
		,m_steerAngle0(0.0f)
		,m_steerAngle1(0.0f)
		,m_brakeTorque(0.0f)
	{
		CalculateLocalMatrix (pinAndPivotFrame, m_localMatrix0, m_localMatrix1);
	}

	dFloat CalculateTireParametricPosition(const dMatrix& tireMatrix, const dMatrix& chassisMatrix) const 
	{
		const dVector& chassisP0 = chassisMatrix.m_posit;
		dVector chassisP1(chassisMatrix.m_posit + chassisMatrix.m_up.Scale(m_tire->m_data.m_suspesionlenght));
		dVector p1p0(chassisP1 - chassisP0);
		dVector q1p0(tireMatrix.m_posit - chassisP0);
		dFloat num = q1p0.DotProduct3(p1p0);
		dFloat den = p1p0.DotProduct3(p1p0);
		return num / den;
	}

	void ApplyBumperImpactLimit(const dVector& dir, dFloat param)
	{
		dComplentaritySolver::dJacobian tireJacobian;
		dComplentaritySolver::dJacobian chassisJacobian;

		dMatrix tireMatrix;
		dMatrix tireInvInertia;
		dMatrix chassisMatrix;
		dMatrix chassisInvInertia;
		dVector com(0.0f);
		dVector tireVeloc(0.0f);
		dVector tireOmega(0.0f);
		dVector chassisVeloc(0.0f);
		dVector chassisOmega(0.0f);

		dFloat tireInvMass;
		dFloat chassisInvMass;
		dFloat Ixx;
		dFloat Iyy;
		dFloat Izz;

		NewtonBody* const tire = m_body0;
		NewtonBody* const chassis = m_body1;
		NewtonBodyGetMatrix(tire, &tireMatrix[0][0]);
		NewtonBodyGetMatrix(chassis, &chassisMatrix[0][0]);
		NewtonBodyGetCentreOfMass(chassis, &com[0]);

		NewtonBodyGetOmega(tire, &tireOmega[0]);
		NewtonBodyGetVelocity(tire, &tireVeloc[0]);
		NewtonBodyGetOmega(chassis, &chassisOmega[0]);
		NewtonBodyGetVelocity(chassis, &chassisVeloc[0]);

		dVector r (tireMatrix.m_posit - chassisMatrix.TransformVector(com));
		tireJacobian.m_linear = dir;
		tireJacobian.m_angular = dVector (0.0f);
		chassisJacobian.m_linear = dir.Scale (-1.0f);
		chassisJacobian.m_angular = r.CrossProduct(chassisJacobian.m_linear);

		dFloat relativeVeloc = tireVeloc.DotProduct3(tireJacobian.m_linear) + tireOmega.DotProduct3(tireJacobian.m_angular) + chassisVeloc.DotProduct3(chassisJacobian.m_linear) + chassisOmega.DotProduct3(chassisJacobian.m_angular);
		if (relativeVeloc > 0.0f) {
			dComplentaritySolver::dJacobian tireInvMassJacobianTrans;
			dComplentaritySolver::dJacobian chassisInvMassJacobianTrans;

			NewtonBodyGetInvMass(tire, &tireInvMass, &Ixx, &Ixx, &Ixx);
			NewtonBodyGetInvMass(chassis, &chassisInvMass, &Ixx, &Iyy, &Izz);
			NewtonBodyGetInvInertiaMatrix(tire, &tireInvInertia[0][0]);
			NewtonBodyGetInvInertiaMatrix(chassis, &chassisInvInertia[0][0]);

			tireInvMassJacobianTrans.m_linear = tireJacobian.m_linear.Scale (tireInvMass);
			tireInvMassJacobianTrans.m_angular = tireInvInertia.RotateVector(tireJacobian.m_angular);

			chassisInvMassJacobianTrans.m_linear = chassisJacobian.m_linear.Scale(tireInvMass);
			chassisInvMassJacobianTrans.m_angular = chassisInvInertia.RotateVector (chassisJacobian.m_angular);

			dFloat den = tireJacobian.m_linear.DotProduct3(tireInvMassJacobianTrans.m_linear) + tireJacobian.m_angular.DotProduct3(tireInvMassJacobianTrans.m_angular) + 
						 chassisJacobian.m_linear.DotProduct3(chassisInvMassJacobianTrans.m_linear) + chassisJacobian.m_angular.DotProduct3(chassisInvMassJacobianTrans.m_angular);

			dFloat impulse = - relativeVeloc / den;
			tireVeloc += tireInvMassJacobianTrans.m_linear.Scale (impulse);
			tireOmega += tireInvMassJacobianTrans.m_angular.Scale (impulse);
			chassisVeloc += chassisInvMassJacobianTrans.m_linear.Scale (impulse);
			chassisOmega += chassisInvMassJacobianTrans.m_angular.Scale (impulse);

			NewtonBodySetOmega(tire, &tireOmega[0]);
			NewtonBodySetVelocity(tire, &tireVeloc[0]);
			NewtonBodySetOmega(chassis, &chassisOmega[0]);
			NewtonBodySetVelocity(chassis, &chassisVeloc[0]);
		}
	}

	void RemoveKinematicError(dFloat timestep)
	{
		dAssert (0);
/*
		dMatrix tireMatrix;
		dMatrix chassisMatrix;
		dVector tireVeloc(0.0f);
		dVector tireOmega(0.0f);
		dVector chassisVeloc(0.0f);
		dVector chassisOmega(0.0f);

		CalculateGlobalMatrix(tireMatrix, chassisMatrix);
		chassisMatrix = dYawMatrix(m_steerAngle0) * chassisMatrix;

		tireMatrix.m_front = chassisMatrix.m_front;
		tireMatrix.m_right = tireMatrix.m_front.CrossProduct(tireMatrix.m_up);
		tireMatrix.m_right = tireMatrix.m_right.Scale(1.0f / dSqrt(tireMatrix.m_right.DotProduct3(tireMatrix.m_right)));
		tireMatrix.m_up = tireMatrix.m_right.CrossProduct(tireMatrix.m_front);

		dFloat param = 0.0f;
		if (!m_tire->GetController()->m_isAirborned) {
			param = CalculateTireParametricPosition (tireMatrix, chassisMatrix);
			if (param > 1.0f) {
				param = 1.0f;
				ApplyBumperImpactLimit(chassisMatrix.m_up, param);
			} else if (param < 0.0f){
				param = 0.0f;
				ApplyBumperImpactLimit(chassisMatrix.m_up, param);
			}
		}
		
		tireMatrix.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale (param * m_tire->m_data.m_suspesionlenght);

		NewtonBody* const tire = m_body0;
		NewtonBody* const chassis = m_body1;

		tireMatrix = GetMatrix0().Inverse() * tireMatrix;
		NewtonBodyGetVelocity(tire, &tireVeloc[0]);
		NewtonBodyGetPointVelocity(chassis, &tireMatrix.m_posit[0], &chassisVeloc[0]);
		chassisVeloc -= chassisMatrix.m_up.Scale (chassisVeloc.DotProduct3(chassisMatrix.m_up));
		tireVeloc = chassisVeloc + chassisMatrix.m_up.Scale (tireVeloc.DotProduct3(chassisMatrix.m_up));
		
		NewtonBodyGetOmega(tire, &tireOmega[0]);
		NewtonBodyGetOmega(chassis, &chassisOmega[0]);
		tireOmega = chassisOmega + tireMatrix.m_front.Scale (tireOmega.DotProduct3(tireMatrix.m_front));

		NewtonBodySetMatrixNoSleep(tire, &tireMatrix[0][0]);
		NewtonBodySetVelocityNoSleep(tire, &tireVeloc[0]);
		NewtonBodySetOmegaNoSleep(tire, &tireOmega[0]);
*/
	}

	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		dMatrix tireMatrix;
		dMatrix chassisMatrix;

		NewtonBody* const tire = m_body0;
		NewtonBody* const chassis = m_body1;
		dAssert (m_body0 == m_tire->GetBody());
		dAssert (m_body1 == m_tire->GetParent()->GetBody());

		// calculate the position of the pivot point and the Jacobian direction vectors, in global space. 
		CalculateGlobalMatrix(tireMatrix, chassisMatrix);
		chassisMatrix = dYawMatrix(m_steerAngle0) * chassisMatrix;

		m_lateralDir = chassisMatrix.m_front;
		m_longitudinalDir = chassisMatrix.m_right;

		NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &m_lateralDir[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &m_longitudinalDir[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		dFloat angle = -CalculateAngle(tireMatrix.m_front, chassisMatrix.m_front, chassisMatrix.m_right);
		NewtonUserJointAddAngularRow(m_joint, -angle, &chassisMatrix.m_right[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		angle = -CalculateAngle(tireMatrix.m_front, chassisMatrix.m_front, chassisMatrix.m_up);
		NewtonUserJointAddAngularRow(m_joint, -angle, &chassisMatrix.m_up[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		dFloat param = CalculateTireParametricPosition(tireMatrix, chassisMatrix);
		if (param >= 1.0f) {
			dVector posit (chassisMatrix.m_posit + m_tire->m_data.m_suspesionlenght);
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &posit[0], &chassisMatrix.m_up[0]);
			NewtonUserJointSetRowMaximumFriction(m_joint, 0.0f);
		} else if (param <= 0.0f) {
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
			NewtonUserJointSetRowMinimumFriction(m_joint, 0.0f);
		} else if (m_tire->m_data.m_suspentionType == BodyPartTire::Info::m_roller) {
			dAssert (0);
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
		}

		if (m_brakeTorque > 1.0e-3f) {
			dVector tireOmega(0.0f);
			dVector chassisOmega(0.0f);
			NewtonBodyGetOmega(tire, &tireOmega[0]);
			NewtonBodyGetOmega(chassis, &chassisOmega[0]);
			dVector relOmega(tireOmega - chassisOmega);

			dFloat speed = relOmega.DotProduct3(m_lateralDir);
			NewtonUserJointAddAngularRow(m_joint, 0.0f, &m_lateralDir[0]);
			NewtonUserJointSetRowAcceleration(m_joint, -speed / timestep);
			NewtonUserJointSetRowMinimumFriction(m_joint, -m_brakeTorque);
			NewtonUserJointSetRowMaximumFriction(m_joint, m_brakeTorque);
		}
		m_brakeTorque = 0.0f;
	}

	dFloat GetTireLoad () const
	{
		return NewtonUserJointGetRowForce(m_joint, 4);
	}

	dVector GetLongitudinalForce() const
	{
		return m_longitudinalDir.Scale(NewtonUserJointGetRowForce(m_joint, 1));
	}

	dVector GetLateralForce() const
	{
		return m_lateralDir.Scale (NewtonUserJointGetRowForce (m_joint, 0));
	}

	dVector m_lateralDir;
	dVector m_longitudinalDir;
	BodyPartTire* m_tire;
	dFloat m_tireLoad;
	dFloat m_steerRate;
	dFloat m_steerAngle0;
	dFloat m_steerAngle1;
	dFloat m_brakeTorque;
};


class CustomVehicleController::EngineJoint: public CustomUniversal
{
	public:
	EngineJoint (const dMatrix& pinAndPivotFrame, NewtonBody* const engineBody, NewtonBody* const chassisBody)
		:CustomUniversal(pinAndPivotFrame, engineBody, chassisBody)
		,m_sleepDifrentialSpeed()
		,m_slipDifrentialOn(true)
//		,m_turRateAccel(3.0f)
//		,m_turnRate(0.0f)
//		,m_turnRateTarget(0.0f)
	{
		EnableLimit_0(false);
		EnableLimit_1(false);
	}
/*
	void ApplySteering(SteeringController* const steering, dFloat timestep)
	{
		dAssert(0);
		m_turnRateTarget = steering->m_param * 10.0f;

		if (m_turnRate < m_turnRateTarget) {
			m_turnRate += m_turRateAccel * timestep;
			if (m_turnRate > m_turnRateTarget) {
				m_turnRate = m_turnRateTarget;
			}
		} else if (m_turnRate > m_turnRateTarget) {
			m_turnRate -= m_turRateAccel * timestep;
			if (m_turnRate < m_turnRateTarget) {
				m_turnRate = m_turnRateTarget;
			}
		}
	}
*/
	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		CustomUniversal::SubmitConstraints(timestep, threadIndex);

		// y axis controls the slip differential feature.
		NewtonBody* const engineBody = GetBody0();
		NewtonBody* const chassisBody = GetBody1();

		dMatrix chassisMatrix;
		dMatrix engineMatrix;
		dVector chassisOmega;
		dVector engineOmega;

		NewtonBodyGetOmega(engineBody, &engineOmega[0]);
		NewtonBodyGetOmega(chassisBody, &chassisOmega[0]);

		// calculate the position of the pivot point and the Jacobian direction vectors, in global space. 
		CalculateGlobalMatrix(engineMatrix, chassisMatrix);

		dFloat wRel = engineMatrix.m_front.DotProduct3(engineOmega) - chassisMatrix.m_front.DotProduct3(chassisOmega);
		if (wRel > D_LIMITED_SLIP_DIFFERENTIAL_LOCK_RPS) {
			wRel -= D_LIMITED_SLIP_DIFFERENTIAL_LOCK_RPS;
			NewtonUserJointAddAngularRow(m_joint, -0.5f * wRel/timestep, &chassisMatrix.m_front[0]);
			NewtonUserJointSetRowMinimumFriction(m_joint, 0.0f);
		} else if (wRel < - D_LIMITED_SLIP_DIFFERENTIAL_LOCK_RPS) {
			wRel -= -D_LIMITED_SLIP_DIFFERENTIAL_LOCK_RPS;
			NewtonUserJointAddAngularRow(m_joint, -0.5f * wRel / timestep, &chassisMatrix.m_front[0]);
			NewtonUserJointSetRowMaximumFriction(m_joint, 0.0f);
		}
/*
		const dVector& pin = differentialMatrix.m_front;
		dVector relOmega(chassisOmega - differentialOmega);
		dFloat accel = 0.5f * (relOmega.DotProduct3(pin) + m_turnRate) / timestep;
		NewtonUserJointAddAngularRow(m_joint, 0.0f, &pin[0]);
		NewtonUserJointSetRowAcceleration(m_joint, accel);
*/	
	}

	dFloat m_sleepDifrentialSpeed;
	bool m_slipDifrentialOn;
//	dFloat m_turRateAccel;
//	dFloat m_turnRate;
//	dFloat m_turnRateTarget;
};

class CustomVehicleController::AxelJoint: public CustomGear
{
	public:
	AxelJoint(dFloat gearRatio, const dVector& childPin, const dVector& parentPin, const dVector& referencePin, NewtonBody* const child, NewtonBody* const parent, NewtonBody* const parentReference)
		:CustomGear(gearRatio, childPin, parentPin, child, parent)
		,m_parentReference(parentReference)
	{
		dMatrix dommyMatrix;
		// calculate the local matrix for body body0
 		dMatrix pinAndPivot0(dGrammSchmidt(childPin));

		CalculateLocalMatrix(pinAndPivot0, m_localMatrix0, dommyMatrix);
		m_localMatrix0.m_posit = dVector(0.0f, 0.0f, 0.0f, 1.0f);

		// calculate the local matrix for body body1  
		dMatrix pinAndPivot1(dGrammSchmidt(parentPin));
		CalculateLocalMatrix(pinAndPivot1, dommyMatrix, m_localMatrix1);
		m_localMatrix1.m_posit = dVector(0.0f, 0.0f, 0.0f, 1.0f);

		dMatrix referenceMatrix;
		NewtonBodyGetMatrix(m_parentReference, &referenceMatrix[0][0]);
		m_pintOnReference = referenceMatrix.UnrotateVector(referencePin);
	}

	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		dMatrix matrix0;
		dMatrix matrix1;
		dVector omega0(0.0f);
		dVector omega1(0.0f);
		dFloat jacobian0[6];
		dFloat jacobian1[6];

		// calculate the position of the pivot point and the Jacobian direction vectors, in global space. 
		CalculateGlobalMatrix(matrix0, matrix1);

		// calculate the angular velocity for both bodies
		dVector dir0(matrix0.m_front.Scale(m_gearRatio));
		dVector dir2(matrix1.m_front);

		dMatrix referenceMatrix;
		NewtonBodyGetMatrix(m_parentReference, &referenceMatrix[0][0]);
		dVector dir3(referenceMatrix.RotateVector(m_pintOnReference));
		dVector dir1(dir2 + dir3);

		jacobian0[0] = 0.0f;
		jacobian0[1] = 0.0f;
		jacobian0[2] = 0.0f;
		jacobian0[3] = dir0.m_x;
		jacobian0[4] = dir0.m_y;
		jacobian0[5] = dir0.m_z;

		jacobian1[0] = 0.0f;
		jacobian1[1] = 0.0f;
		jacobian1[2] = 0.0f;
		jacobian1[3] = dir1.m_x;
		jacobian1[4] = dir1.m_y;
		jacobian1[5] = dir1.m_z;

		NewtonBodyGetOmega(m_body0, &omega0[0]);
		NewtonBodyGetOmega(m_body1, &omega1[0]);

		dFloat w0 = omega0.DotProduct3(dir0);
		dFloat w1 = omega1.DotProduct3(dir1);

		dFloat relOmega = w0 + w1;
		dFloat invTimestep = (timestep > 0.0f) ? 1.0f / timestep : 1.0f;
		dFloat relAccel = -0.5f * relOmega * invTimestep;
		NewtonUserJointAddGeneralRow(m_joint, jacobian0, jacobian1);
		NewtonUserJointSetRowAcceleration(m_joint, relAccel);
	}

	dVector m_pintOnReference;
	NewtonBody* m_parentReference;
};



void CustomVehicleController::BodyPartChassis::ApplyDownForce ()
{
	// add aerodynamics forces
	dMatrix matrix;
	dVector veloc(0.0f);

	NewtonBody* const body = GetBody();
	NewtonBodyGetVelocity(body, &veloc[0]);
	NewtonBodyGetMatrix(body, &matrix[0][0]);

	veloc -= matrix.m_up.Scale (veloc.DotProduct3(matrix.m_up));
	dFloat downForceMag = m_aerodynamicsDownForceCoefficient * veloc.DotProduct3(veloc);
	if (downForceMag > m_aerodynamicsDownForce0) {
		dFloat speed = dSqrt (veloc.DotProduct3(veloc));
		dFloat topSpeed = GetController()->GetEngine() ? GetController()->GetEngine()->GetTopGear() : 30.0f;
		dFloat speedRatio = (speed - m_aerodynamicsDownSpeedCutOff) / (topSpeed - speed); 
		downForceMag = m_aerodynamicsDownForce0 + (m_aerodynamicsDownForce1 - m_aerodynamicsDownForce0) * speedRatio; 
	}

	dVector downforce(matrix.m_up.Scale (-downForceMag));
	NewtonBodyAddForce(body, &downforce[0]);
}


CustomVehicleController::BodyPartTire::BodyPartTire()
	:BodyPart()
	,m_lateralSlip(0.0f)
	,m_longitudinalSlip(0.0f)
	,m_aligningTorque(0.0f)
	,m_index(0)
	,m_collidingCount(0)
{
}

CustomVehicleController::BodyPartTire::~BodyPartTire()
{
}

// Using brush tire model explained on this paper
// https://ddl.stanford.edu/sites/default/files/2013_Thesis_Hindiyeh_Dynamics_and_Control_of_Drifting_in_Automobiles.pdf
void CustomVehicleController::BodyPartTire::FrictionModel::GetForces(const BodyPartTire* const tire, const NewtonBody* const otherBody, const NewtonMaterial* const material, dFloat tireLoad, dFloat& longitudinalForce, dFloat& lateralForce, dFloat& aligningTorque) const
{
	const CustomVehicleController* const controller = tire->GetController();
	const dFloat gravityMag = controller->m_gravityMag;
	dFloat phy_y = tire->m_lateralSlip * tire->m_data.m_lateralStiffness * gravityMag;
	dFloat phy_x = tire->m_longitudinalSlip * tire->m_data.m_longitudialStiffness * gravityMag;
	dFloat gamma = dMax(dSqrt(phy_x * phy_x + phy_y * phy_y), dFloat(0.1f));

	dFloat fritionCoeficicent = dClamp(GetFrictionCoefficient(material, tire->GetBody(), otherBody), dFloat(0.0f), dFloat(1.0f));
	tireLoad *= fritionCoeficicent;
	dFloat phyMax = 3.0f * tireLoad + 1.0f;

	dFloat F = (gamma <= phyMax) ? (gamma * (1.0f - gamma / phyMax + gamma * gamma / (3.0f * phyMax * phyMax))) : tireLoad;

	dFloat fraction = F / gamma;
	lateralForce = -phy_y * fraction;
	longitudinalForce = -phy_x * fraction;

	aligningTorque = 0.0f;
}


void CustomVehicleController::BodyPartTire::Init (BodyPart* const parentPart, const dMatrix& locationInGlobalSpase, const Info& info)
{
	m_data = info;
	m_parent = parentPart;
	m_userData = info.m_userData;
	m_controller = parentPart->m_controller;

	m_collidingCount = 0;
	m_lateralSlip = 0.0f;
	m_aligningTorque = 0.0f;
	m_longitudinalSlip = 0.0f;

	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)m_controller->GetManager();

	NewtonWorld* const world = ((CustomVehicleControllerManager*)m_controller->GetManager())->GetWorld();
	NewtonCollisionSetScale(manager->m_tireShapeTemplate, m_data.m_width, m_data.m_radio, m_data.m_radio);

	// create the rigid body that will make this bone
	dMatrix matrix (dYawMatrix(-0.5f * 3.1415927f) * locationInGlobalSpase);
	m_body = NewtonCreateDynamicBody(world, manager->m_tireShapeTemplate, &matrix[0][0]);
	NewtonCollision* const collision = NewtonBodyGetCollision(m_body);
	NewtonCollisionSetUserData1 (collision, this);
	
	NewtonBodySetMaterialGroupID(m_body, manager->GetTireMaterial());

	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);
	NewtonBodySetMaxRotationPerStep(m_body, 3.141692f);
	
	// set the standard force and torque call back
	NewtonBodySetForceAndTorqueCallback(m_body, m_controller->m_forceAndTorque);

	// tire are highly non linear, sung spherical inertia matrix make the calculation more accurate 
	dFloat inertia = 2.0f * m_data.m_mass * m_data.m_radio * m_data.m_radio / 5.0f;
	NewtonBodySetMassMatrix (m_body, m_data.m_mass, inertia, inertia, inertia);

	m_joint = new WheelJoint (matrix, m_body, parentPart->m_body, this);
}

dFloat CustomVehicleController::BodyPartTire::GetRPM() const
{
	dVector omega(0.0f); 
	WheelJoint* const joint = (WheelJoint*) m_joint;
	NewtonBodyGetOmega(m_body, &omega[0]);
	return joint->m_lateralDir.DotProduct3(omega) * 9.55f;
}


void CustomVehicleController::BodyPartTire::SetSteerAngle (dFloat angleParam, dFloat timestep)
{
	WheelJoint* const tire = (WheelJoint*)m_joint;
	tire->m_steerAngle1 = -angleParam * m_data.m_maxSteeringAngle;

	if (tire->m_steerAngle0 < tire->m_steerAngle1) {
		tire->m_steerAngle0 += tire->m_steerRate * timestep;
		if (tire->m_steerAngle0 > tire->m_steerAngle1) {
			tire->m_steerAngle0 = tire->m_steerAngle1;
		}
	} else if (tire->m_steerAngle0 > tire->m_steerAngle1) {
		tire->m_steerAngle0 -= tire->m_steerRate * timestep;
		if (tire->m_steerAngle0 < tire->m_steerAngle1) {
			tire->m_steerAngle0 = tire->m_steerAngle1;
		}
	}
}

void CustomVehicleController::BodyPartTire::SetBrakeTorque(dFloat torque)
{
	WheelJoint* const tire = (WheelJoint*)m_joint;
	tire->m_brakeTorque = dMax (torque, tire->m_brakeTorque);
}

dFloat CustomVehicleController::BodyPartTire::GetLateralSlip () const
{
	return m_lateralSlip;
}

dFloat CustomVehicleController::BodyPartTire::GetLongitudinalSlip () const
{
	return m_longitudinalSlip;
}


CustomVehicleController::BodyPartEngine::BodyPartEngine(CustomVehicleController* const controller, dFloat mass, dFloat amatureRadius)
	:BodyPart()
{
	m_parent = &controller->m_chassis;
	m_controller = controller;

	NewtonBody* const chassisBody = m_controller->GetBody();
	NewtonWorld* const world = ((CustomVehicleControllerManager*)m_controller->GetManager())->GetWorld();

	//NewtonCollision* const collision = NewtonCreateNull(world);
	NewtonCollision* const collision = NewtonCreateSphere(world, 0.1f, 0, NULL);
	//NewtonCollision* const collision = NewtonCreateCylinder(world, 0.1f, 0.1f, 0.5f, 0, NULL);
	NewtonCollisionSetMode (collision, 0);
	
	dVector origin;
	NewtonBodyGetCentreOfMass(chassisBody, &origin[0]);
//origin.m_y -= 0.5f;

	dMatrix matrix;
	NewtonBodyGetMatrix(chassisBody, &matrix[0][0]);
	matrix.m_posit = matrix.TransformVector(origin);

	m_body = NewtonCreateDynamicBody(world, collision, &matrix[0][0]);
	NewtonDestroyCollision(collision);

	dFloat inertia = 2.0f * mass * amatureRadius * amatureRadius / 5.0f;
	NewtonBodySetMassMatrix(m_body, mass, inertia, inertia, inertia);

	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);
	NewtonBodySetMaxRotationPerStep(m_body, 3.141692f);
	NewtonBodySetForceAndTorqueCallback(m_body, m_controller->m_forceAndTorque);

	dMatrix pinMatrix (matrix);
	pinMatrix.m_front = matrix.m_front;
	pinMatrix.m_up = matrix.m_right;
	pinMatrix.m_right = pinMatrix.m_front.CrossProduct(pinMatrix.m_up);
	m_joint = new EngineJoint(pinMatrix, m_body, chassisBody);
}

CustomVehicleController::BodyPartEngine::~BodyPartEngine()
{
}


void CustomVehicleController::EngineController::Info::ConvertToMetricSystem()
{
	const dFloat horsePowerToWatts = 735.5f;
	const dFloat kmhToMetersPerSecunds = 0.278f;
	const dFloat rpmToRadiansPerSecunds = 0.105f;
	const dFloat poundFootToNewtonMeters = 1.356f;

	m_idleTorque *= poundFootToNewtonMeters;
	m_peakTorque *= poundFootToNewtonMeters;

	m_peakTorqueRpm *= rpmToRadiansPerSecunds;
	m_peakHorsePowerRpm *= rpmToRadiansPerSecunds;
	m_readLineRpm *= rpmToRadiansPerSecunds;
	m_idleTorqueRpm *= rpmToRadiansPerSecunds;

	m_peakHorsePower *= horsePowerToWatts;
	m_vehicleTopSpeed *= kmhToMetersPerSecunds;

	m_peakPowerTorque = m_peakHorsePower / m_peakHorsePowerRpm;

	dAssert(m_idleTorqueRpm > 0.0f);
	dAssert(m_idleTorqueRpm < m_peakHorsePowerRpm);
	dAssert(m_peakTorqueRpm < m_peakHorsePowerRpm);
	dAssert(m_peakHorsePowerRpm < m_readLineRpm);

	dAssert(m_idleTorque > 0.0f);
	dAssert(m_peakTorque > m_peakPowerTorque);
	//dAssert(m_peakPowerTorque > m_redLineTorque);
	//dAssert(m_redLineTorque > 0.0f);
	dAssert((m_peakTorque * m_peakTorqueRpm) < m_peakHorsePower);
}


CustomVehicleController::EngineController::EngineController (CustomVehicleController* const controller, const Info& info, const Differential& differential)
	:Controller(controller)
	,m_info(info)
	,m_infoCopy(info)
	,m_controller(controller)
	,m_clutchParam(1.0f)
	,m_gearTimer(0)
	,m_currentGear(D_VEHICLE_NEUTRAL_GEAR)
	,m_ignitionKey(false)
	,m_automaticTransmissionMode(true)
{
	controller->m_engineControl = this;

	dMatrix engineMatrix;
	dMatrix chassisMatrix;
	NewtonBody* const chassisBody = controller->m_chassis.GetBody();
	NewtonBodyGetMatrix (chassisBody, &chassisMatrix[0][0]);
//	chassisMatrix = controller->m_localFrame * chassisMatrix;
	
	NewtonBody* const engineBody = controller->m_engine->GetBody();
	dAssert (engineBody == controller->m_engine->GetJoint()->GetBody0());
	NewtonBodyGetMatrix (engineBody, &engineMatrix[0][0]);

	engineMatrix = controller->m_engine->GetJoint()->GetMatrix0() * engineMatrix;
	switch (differential.m_type)
	{
		case Differential::m_2wd:
		{
			dMatrix leftTireMatrix;
			NewtonBody* const leftTireBody = differential.m_axel.m_leftTire->GetBody();
			dAssert (leftTireBody == differential.m_axel.m_leftTire->GetJoint()->GetBody0());
			NewtonBodyGetMatrix (leftTireBody, &leftTireMatrix[0][0]);
			leftTireMatrix = differential.m_axel.m_leftTire->GetJoint()->GetMatrix0() * leftTireMatrix;
			AxelJoint* const leftGear = new AxelJoint(1.0f, leftTireMatrix[0], engineMatrix[0].Scale (-1.0f), chassisMatrix[2], leftTireBody, engineBody, chassisBody);
			NewtonSkeletonContainerAttachCyclingJoint (controller->m_skeleton, leftGear->GetJoint());

			dMatrix rightTireMatrix;
			NewtonBody* const rightTireBody = differential.m_axel.m_rightTire->GetBody();
			dAssert(rightTireBody == differential.m_axel.m_rightTire->GetJoint()->GetBody0());
			NewtonBodyGetMatrix(rightTireBody, &rightTireMatrix[0][0]);
			rightTireMatrix = differential.m_axel.m_rightTire->GetJoint()->GetMatrix0() * rightTireMatrix;
			AxelJoint* const rightGear = new AxelJoint(1.0f, rightTireMatrix[0], engineMatrix[0].Scale (1.0f), chassisMatrix[2], rightTireBody, engineBody, chassisBody);
			NewtonSkeletonContainerAttachCyclingJoint (controller->m_skeleton, rightGear->GetJoint());

			break;
		}
/*
		case Differential::m_4wd:
		{
			const Differential4wd& diff = (Differential4wd&) differential;
			m_engine = new DriveTrainEngine4W (invInertia, diff.m_axel, diff.m_secundAxel.m_axel);
			break;
		}

		case Differential::m_8wd:
		{
			const Differential8wd& diff = (Differential8wd&) differential;
			m_engine = new DriveTrainEngine8W (invInertia, diff.m_axel, diff.m_secundAxel.m_axel, diff.m_secund4Wd.m_axel, diff.m_secund4Wd.m_secundAxel.m_axel);
			break;
		}

		case Differential::m_track:
		{
			const DifferentialTracked& diff = (DifferentialTracked&) differential;
			m_engine = new DriveTrainEngineTracked(invInertia, diff, controller);
			break;
		}
		default:
			dAssert(0);
*/
	}

	SetInfo(info);
}

CustomVehicleController::EngineController::~EngineController()
{
}

CustomVehicleController::EngineController::Info CustomVehicleController::EngineController::GetInfo() const
{
	return m_infoCopy;
}

void CustomVehicleController::EngineController::SetInfo(const Info& info)
{
	m_info = info;
	m_infoCopy = info;

//	dFloat inertiaInv = 1.0f / (2.0f * m_info.m_mass * m_info.m_radio * m_info.m_radio / 5.0f);
//	dVector invInertia (inertiaInv, inertiaInv, inertiaInv, 0.0f);

	m_info.m_clutchFrictionTorque = dMax (dFloat(10.0f), dAbs (m_info.m_clutchFrictionTorque));
	m_infoCopy.m_clutchFrictionTorque = m_info.m_clutchFrictionTorque;

//	m_engine->RebuildEngine (dVector (inertiaInv, inertiaInv, inertiaInv, 0.0f));
/*
	InitEngineTorqueCurve();


	dAssert(info.m_gearsCount < (int(sizeof (m_info.m_gearRatios) / sizeof (m_info.m_gearRatios[0])) - D_VEHICLE_FIRST_GEAR));
	m_info.m_gearsCount = info.m_gearsCount + D_VEHICLE_FIRST_GEAR;

	m_info.m_gearRatios[D_VEHICLE_NEUTRAL_GEAR] = 0.0f;
	m_info.m_gearRatios[D_VEHICLE_REVERSE_GEAR] = -dAbs(info.m_reverseGearRatio);
	for (int i = 0; i < (m_info.m_gearsCount - D_VEHICLE_FIRST_GEAR); i++) {
		m_info.m_gearRatios[i + D_VEHICLE_FIRST_GEAR] = dAbs(info.m_gearRatios[i]);
	}

	for (dList<BodyPartTire>::dListNode* tireNode = m_controller->m_tireList.GetFirst(); tireNode; tireNode = tireNode->GetNext()) {
		BodyPartTire& tire = tireNode->GetInfo();
		dFloat angle = (1.0f / 30.0f) * (0.277778f) * info.m_vehicleTopSpeed / tire.m_data.m_radio;
		NewtonBodySetMaxRotationPerStep(tire.GetBody(), angle);
	}

	m_controller->SetAerodynamicsDownforceCoefficient (info.m_aerodynamicDownforceFactor, info.m_aerodynamicDownForceSurfaceCoeficident, info.m_aerodynamicDownforceFactorAtTopSpeed);
*/
}

bool CustomVehicleController::EngineController::GetDifferentialLock() const
{
	return m_info.m_differentialLock ? true : false;
}

void CustomVehicleController::EngineController::SetDifferentialLock(bool lock)
{
	m_info.m_differentialLock = lock ? 1 : 0;
}

dFloat CustomVehicleController::EngineController::GetTopGear() const
{
	return m_info.m_gearRatios[m_info.m_gearsCount - 1];
}

void CustomVehicleController::EngineController::CalculateCrownGear()
{
	dAssert(m_info.m_vehicleTopSpeed >= 0.0f);
	dAssert(m_info.m_vehicleTopSpeed < 100.0f);

/*
	DriveTrain* nodeArray[256];
	int nodesCount = m_engine->GetNodeArray(nodeArray);

	BodyPartTire* tire = NULL;
	for (int i = 0; i < nodesCount; i ++) {
		DriveTrainTire* const tireNode = nodeArray[i]->CastAsTire();
		if (tireNode) {
			tire = tireNode->m_tire;
			break;
		}
	}
	dAssert (tire);


	// drive train geometrical relations
	// G0 = m_differentialGearRatio
	// G1 = m_transmissionGearRatio
	// s = topSpeedMPS
	// r = tireRadio
	// wt = rpsAtTire
	// we = rpsAtPickPower
	// we = G1 * G0 * wt;
	// wt = e / r
	// we = G0 * G1 * s / r
	// G0 = r * we / (G1 * s)
	// using the top gear and the optimal engine torque for the calculations
	
	dFloat topGearRatio = GetTopGear();
	dFloat tireRadio = tire->GetInfo().m_radio;
	m_info.m_crownGearRatio = tireRadio * m_info.m_peakHorsePowerRpm / (m_info.m_vehicleTopSpeed * topGearRatio);
*/
}


void CustomVehicleController::EngineController::InitEngineTorqueCurve()
{
	m_info.ConvertToMetricSystem();

	CalculateCrownGear();

	dFloat rpsTable[5];
	dFloat torqueTable[5];

	rpsTable[0] = 0.0f;
	rpsTable[1] = m_info.m_idleTorqueRpm;
	rpsTable[2] = m_info.m_peakTorqueRpm;
	rpsTable[3] = m_info.m_peakHorsePowerRpm;
	rpsTable[4] = m_info.m_readLineRpm;

	m_info.m_readLineTorque = m_info.m_peakPowerTorque * 0.75f;

	torqueTable[0] = m_info.m_idleTorque;
	torqueTable[1] = m_info.m_idleTorque;
	torqueTable[2] = m_info.m_peakTorque;
	torqueTable[3] = m_info.m_peakPowerTorque;
	torqueTable[4] = m_info.m_readLineTorque;


	m_info.m_idleFriction = dMin (m_info.m_idleTorque, m_info.m_readLineTorque) * D_VEHICLE_ENGINE_IDLE_FRICTION_COEFFICIENT;

	dFloat speed = m_info.m_idleTorqueRpm;
	dFloat torque = m_info.m_idleTorque - m_info.m_idleFriction;
	m_info.m_idleViscousDrag2 = torque / (speed * speed);

	speed = m_info.m_readLineRpm * 1.1f;
	torque = m_info.m_readLineTorque - m_info.m_idleFriction;
	m_info.m_driveViscousDrag2 = torque / (speed * speed);

	m_torqueRPMCurve.InitalizeCurve(sizeof (rpsTable) / sizeof (rpsTable[0]), rpsTable, torqueTable);
}

void CustomVehicleController::EngineController::PlotEngineCurve() const
{
#ifdef D_PLOT_ENGINE_CURVE 
	dFloat rpm0 = m_torqueRPMCurve.m_nodes[0].m_param;
	dFloat rpm1 = m_torqueRPMCurve.m_nodes[m_torqueRPMCurve.m_count - 1].m_param;
	int steps = 40;
	dFloat omegaStep = (rpm1 - rpm0) / steps;
	dTrace(("rpm\ttorque\tpower\n"));
	for (int i = 0; i < steps; i++) {
		dFloat r = rpm0 + omegaStep * i;
		dFloat torque = m_torqueRPMCurve.GetValue(r);
		dFloat power = r * torque;
		const dFloat horsePowerToWatts = 735.5f;
		const dFloat rpmToRadiansPerSecunds = 0.105f;
		const dFloat poundFootToNewtonMeters = 1.356f;
		dTrace(("%6.2f\t%6.2f\t%6.2f\n", r / 0.105f, torque / 1.356f, power / 735.5f));
	}
#endif
}


dFloat CustomVehicleController::EngineController::GetGearRatio () const
{
	return m_info.m_crownGearRatio * m_info.m_gearRatios[m_currentGear];
}


void CustomVehicleController::EngineController::UpdateAutomaticGearBox(dFloat timestep)
{
//m_info.m_gearsCount = 4;
//m_currentGear = D_VEHICLE_NEUTRAL_GEAR;
dAssert (0);
/*
	m_gearTimer--;
	if (m_gearTimer < 0) {
		dFloat omega = m_engine->m_omega.m_x;
		switch (m_currentGear) 
		{
			case D_VEHICLE_NEUTRAL_GEAR:
			{
			   SetGear(D_VEHICLE_NEUTRAL_GEAR);
			   break;
			}

			case D_VEHICLE_REVERSE_GEAR:
			{
				SetGear(D_VEHICLE_REVERSE_GEAR);
				break;
			}

			default:
			{
				//if (omega > m_info.m_rpmAtPeakHorsePower) {
			   if (omega > m_torqueRPMCurve.GetValue(3).m_param) {
					if (m_currentGear < (m_info.m_gearsCount - 1)) {
						SetGear(m_currentGear + 1);
					}
				//} else if (omega < m_info.m_rpmAtPeakTorque) {
				} else if (omega < m_torqueRPMCurve.GetValue(2).m_param) {
					if (m_currentGear > D_VEHICLE_FIRST_GEAR) {
						SetGear(m_currentGear - 1);
					}
				}
			}
		}
	}
*/
}

void CustomVehicleController::EngineController::ApplyTorque(dFloat torque, dFloat timestep)
{
	dAssert (0);
//	m_engine->m_omega.m_x = dClamp(m_engine->m_omega.m_x, dFloat (0.0f), m_info.m_readLineRpm);
//	m_engine->Update(this, torque, timestep);
}

void CustomVehicleController::EngineController::Update(dFloat timestep)
{

static int xxxx;
xxxx ++;
if (xxxx > 500){
dMatrix xxx1;
dVector xxx2;
NewtonBodyGetOmega (m_controller->m_chassis.GetBody(), &xxx2[0]);
NewtonBodyGetMatrix(m_controller->m_chassis.GetBody(), &xxx1[0][0]);
dVector xxxx (xxx1.m_right.Scale (50.0f));
NewtonBodySetOmega (m_controller->m_engine->GetBody(), &xxxx[0]);
}

/*
	if (m_automaticTransmissionMode) {
		UpdateAutomaticGearBox (timestep);
	}

	dFloat torque = 0.0f;
	if (m_ignitionKey) {
		dFloat omega = m_engine->m_omega.m_x;
		if (m_param < D_VEHICLE_ENGINE_IDLE_GAS_VALVE) {
			m_engine->SetFriction(m_info.m_idleFriction);
			dFloat viscuosFriction0 = dClamp (m_info.m_idleViscousDrag2 * omega * omega, dFloat (0.0), m_info.m_idleFriction);
			dFloat viscuosFriction1 = m_info.m_driveViscousDrag2 * omega * omega;
			torque = m_info.m_idleFriction + (m_torqueRPMCurve.GetValue(omega) - m_info.m_idleFriction) * D_VEHICLE_ENGINE_IDLE_GAS_VALVE - viscuosFriction0 - viscuosFriction1;
		} else {
			m_engine->SetFriction(m_info.m_idleFriction);
			torque = (m_torqueRPMCurve.GetValue(omega) - m_info.m_idleFriction) * m_param - m_info.m_driveViscousDrag2 * omega * omega;
		}
	} else {
		m_engine->SetFriction(m_info.m_idleFriction);
	}

	ApplyTorque(torque, timestep);
*/
}

bool CustomVehicleController::EngineController::GetTransmissionMode() const
{
	return m_automaticTransmissionMode;
}

void CustomVehicleController::EngineController::SetIgnition(bool key)
{
	m_ignitionKey = key;
}

bool CustomVehicleController::EngineController::GetIgnition() const
{
	return m_ignitionKey;
}


void CustomVehicleController::EngineController::SetTransmissionMode(bool mode)
{
	m_automaticTransmissionMode = mode;
}

void CustomVehicleController::EngineController::SetClutchParam (dFloat cluthParam)
{
	m_clutchParam = dClamp (cluthParam, dFloat(0.0f), dFloat(1.0f));
}


int CustomVehicleController::EngineController::GetGear() const
{
	return m_currentGear;
}

void CustomVehicleController::EngineController::SetGear(int gear)
{
	m_currentGear = dClamp(gear, 0, m_info.m_gearsCount);
	m_gearTimer = 30;
}

int CustomVehicleController::EngineController::GetNeutralGear() const
{
	return D_VEHICLE_NEUTRAL_GEAR;
}

int CustomVehicleController::EngineController::GetReverseGear() const
{
	return D_VEHICLE_REVERSE_GEAR;
}

int CustomVehicleController::EngineController::GetFirstGear() const
{
	return D_VEHICLE_FIRST_GEAR;
}

int CustomVehicleController::EngineController::GetLastGear() const
{
	return m_info.m_gearsCount - 1;
}


dFloat CustomVehicleController::EngineController::GetRPM() const
{
dAssert (0);
//	return m_engine->m_omega.m_x * 9.55f;
return 0;
}

dFloat CustomVehicleController::EngineController::GetIdleRPM() const
{
	return m_info.m_idleTorqueRpm * 9.55f;
}

dFloat CustomVehicleController::EngineController::GetRedLineRPM() const
{
	return m_info.m_readLineRpm * 9.55f;
}

dFloat CustomVehicleController::EngineController::GetSpeed() const
{
	dMatrix matrix;
	dVector veloc(0.0f);
	NewtonBody* const chassis = m_controller->GetBody();

	NewtonBodyGetMatrix(chassis, &matrix[0][0]);
	NewtonBodyGetVelocity(chassis, &veloc[0]);

dAssert (0);
//	dVector pin (matrix.RotateVector (m_controller->m_localFrame.m_front));
	dVector pin (matrix.m_front);
	return pin.DotProduct3(veloc);
}

dFloat CustomVehicleController::EngineController::GetTopSpeed() const
{
	return m_info.m_vehicleTopSpeed;
}

CustomVehicleController::SteeringController::SteeringController (CustomVehicleController* const controller)
	:Controller(controller)
	,m_isSleeping(false)
{
}


void CustomVehicleController::SteeringController::Update(dFloat timestep)
{
	m_isSleeping = true;
	for (dList<BodyPartTire*>::dListNode* node = m_tires.GetFirst(); node; node = node->GetNext()) {
		BodyPartTire& tire = *node->GetInfo();
		WheelJoint* const joint = (WheelJoint*)tire.m_joint;
		tire.SetSteerAngle(m_param, timestep);
		m_isSleeping &= dAbs (joint->m_steerAngle1 - joint->m_steerAngle0) < 1.0e-4f;
	}

//	if (m_controller->m_engineControl) {
//		m_controller->m_engineControl->m_engine->ApplySteering(this, timestep);
//	}
}

void CustomVehicleController::SteeringController::AddTire (CustomVehicleController::BodyPartTire* const tire)
{
	m_tires.Append(tire);
}

CustomVehicleController::BrakeController::BrakeController(CustomVehicleController* const controller, dFloat maxBrakeTorque)
	:Controller(controller)
	,m_maxTorque(maxBrakeTorque)
{
}

void CustomVehicleController::BrakeController::AddTire(BodyPartTire* const tire)
{
	m_tires.Append(tire);
}

void CustomVehicleController::BrakeController::Update(dFloat timestep)
{
	dFloat torque = m_maxTorque * m_param;
   	for (dList<BodyPartTire*>::dListNode* node = m_tires.GetFirst(); node; node = node->GetNext()) {
		BodyPartTire& tire = *node->GetInfo();
		tire.SetBrakeTorque (torque);
	}
}



void CustomVehicleControllerManager::DrawSchematic (const CustomVehicleController* const controller, dFloat scale) const
{
	controller->DrawSchematic(scale);
}

void CustomVehicleControllerManager::DrawSchematicCallback (const CustomVehicleController* const controller, const char* const partName, dFloat value, int pointCount, const dVector* const lines) const
{
}


void CustomVehicleController::DrawSchematic(dFloat scale) const
{
	dMatrix projectionMatrix(dGetIdentityMatrix());
	projectionMatrix[0][0] = scale;
	projectionMatrix[1][1] = 0.0f;
	projectionMatrix[2][1] = -scale;
	projectionMatrix[2][2] = 0.0f;
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();

	dMatrix matrix0;
	dVector com(0.0f);
	dFloat arrayPtr[32][4];
	dVector* const array = (dVector*)arrayPtr;
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dFloat mass;
	NewtonBody* const chassisBody = m_chassis.GetBody();

	dFloat velocityScale = 0.125f;

	NewtonBodyGetCentreOfMass(chassisBody, &com[0]);
	NewtonBodyGetMatrix(chassisBody, &matrix0[0][0]);
	matrix0.m_posit = matrix0.TransformVector(com);

	NewtonBodyGetMass(chassisBody, &mass, &Ixx, &Iyy, &Izz);
//	dMatrix chassisMatrix(GetLocalFrame() * matrix0);
	dMatrix chassisMatrix(matrix0);
	dMatrix worldToComMatrix(chassisMatrix.Inverse() * projectionMatrix);
	{
		// draw vehicle chassis
		dVector p0(D_CUSTOM_LARGE_VALUE, D_CUSTOM_LARGE_VALUE, D_CUSTOM_LARGE_VALUE, 0.0f);
		dVector p1(-D_CUSTOM_LARGE_VALUE, -D_CUSTOM_LARGE_VALUE, -D_CUSTOM_LARGE_VALUE, 0.0f);

		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			dMatrix matrix;
			BodyPartTire* const tire = &node->GetInfo();
			NewtonBody* const tireBody = tire->GetBody();
			
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			//dMatrix matrix (tire->CalculateSteeringMatrix() * m_chassisState.GetMatrix());
			dVector p(worldToComMatrix.TransformVector(matrix.m_posit));
			p0 = dVector(dMin(p.m_x, p0.m_x), dMin(p.m_y, p0.m_y), dMin(p.m_z, p0.m_z), 1.0f);
			p1 = dVector(dMax(p.m_x, p1.m_x), dMax(p.m_y, p1.m_y), dMax(p.m_z, p1.m_z), 1.0f);
		}

		array[0] = dVector(p0.m_x, p0.m_y, p0.m_z, 1.0f);
		array[1] = dVector(p1.m_x, p0.m_y, p0.m_z, 1.0f);
		array[2] = dVector(p1.m_x, p1.m_y, p0.m_z, 1.0f);
		array[3] = dVector(p0.m_x, p1.m_y, p0.m_z, 1.0f);
		manager->DrawSchematicCallback(this, "chassis", 0, 4, array);
	}

	{
		// draw vehicle tires
		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			dMatrix matrix;
			BodyPartTire* const tire = &node->GetInfo();
			dFloat width = tire->m_data.m_width * 0.5f;
			dFloat radio = tire->m_data.m_radio;
			NewtonBody* const tireBody = tire->GetBody();
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			matrix.m_up = chassisMatrix.m_up;
			matrix.m_right = matrix.m_front.CrossProduct(matrix.m_up);

			array[0] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(width, 0.0f, radio, 0.0f)));
			array[1] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(width, 0.0f, -radio, 0.0f)));
			array[2] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(-width, 0.0f, -radio, 0.0f)));
			array[3] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(-width, 0.0f, radio, 0.0f)));
			manager->DrawSchematicCallback(this, "tire", 0, 4, array);
		}
	}

	{
		// draw vehicle velocity
		dVector veloc(0.0f);
		dVector omega(0.0f);

		NewtonBodyGetOmega(chassisBody, &omega[0]);
		NewtonBodyGetVelocity(chassisBody, &veloc[0]);

		dVector localVelocity(chassisMatrix.UnrotateVector(veloc));
		localVelocity.m_y = 0.0f;

		localVelocity = projectionMatrix.RotateVector(localVelocity);
		array[0] = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		array[1] = localVelocity.Scale(velocityScale);
		manager->DrawSchematicCallback(this, "velocity", 0, 2, array);

		dVector localOmega(chassisMatrix.UnrotateVector(omega));
		array[0] = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		array[1] = dVector(0.0f, localOmega.m_y * 10.0f, 0.0f, 0.0f);
		manager->DrawSchematicCallback(this, "omega", 0, 2, array);
	}

	{
		dFloat scale1(2.0f / (mass * 10.0f));
		// draw vehicle forces
		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire* const tire = &node->GetInfo();
			dMatrix matrix;
			NewtonBody* const tireBody = tire->GetBody();
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			matrix.m_up = chassisMatrix.m_up;
			matrix.m_right = matrix.m_front.CrossProduct(matrix.m_up);

			dVector origin(worldToComMatrix.TransformVector(matrix.m_posit));

			dVector lateralForce(chassisMatrix.UnrotateVector(GetTireLateralForce(tire)));
			lateralForce = lateralForce.Scale(-scale1);
			lateralForce = projectionMatrix.RotateVector(lateralForce);

			array[0] = origin;
			array[1] = origin + lateralForce;
			manager->DrawSchematicCallback(this, "lateralForce", 0, 2, array);

			dVector longitudinalForce(chassisMatrix.UnrotateVector(GetTireLongitudinalForce(tire)));
			longitudinalForce = longitudinalForce.Scale(-scale1);
			longitudinalForce = projectionMatrix.RotateVector(longitudinalForce);

			array[0] = origin;
			array[1] = origin + longitudinalForce;
			manager->DrawSchematicCallback(this, "longitudinalForce", 0, 2, array);

			dVector veloc(0.0f);
			NewtonBodyGetVelocity(tireBody, &veloc[0]);
			veloc = chassisMatrix.UnrotateVector(veloc);
			veloc.m_y = 0.0f;
			veloc = projectionMatrix.RotateVector(veloc);
			array[0] = origin;
			array[1] = origin + veloc.Scale(velocityScale);
			manager->DrawSchematicCallback(this, "tireVelocity", 0, 2, array);
		}
	}
}

CustomVehicleControllerManager::CustomVehicleControllerManager(NewtonWorld* const world, int materialCount, int* const materialsList)
	:CustomControllerManager<CustomVehicleController> (world, VEHICLE_PLUGIN_NAME)
	,m_tireMaterial(NewtonMaterialCreateGroupID(world))
{
	// create the normalized size tire shape
	m_tireShapeTemplate = NewtonCreateChamferCylinder(world, 0.5f, 1.0f, 0, NULL);
	m_tireShapeTemplateData = NewtonCollisionDataPointer(m_tireShapeTemplate);

	// create a tire material and associate with the material the vehicle new to collide 
	for (int i = 0; i < materialCount; i ++) {
		NewtonMaterialSetCallbackUserData (world, m_tireMaterial, materialsList[i], this);
		if (m_tireMaterial != materialsList[i]) {
			NewtonMaterialSetContactGenerationCallback (world, m_tireMaterial, materialsList[i], OnContactGeneration);
		}
		NewtonMaterialSetCollisionCallback(world, m_tireMaterial, materialsList[i], OnTireAABBOverlap, OnTireContactsProcess);
	}
}

CustomVehicleControllerManager::~CustomVehicleControllerManager()
{
	NewtonDestroyCollision(m_tireShapeTemplate);
}

void CustomVehicleControllerManager::DestroyController(CustomVehicleController* const controller)
{
	controller->Cleanup();
	CustomControllerManager<CustomVehicleController>::DestroyController(controller);
}

int CustomVehicleControllerManager::GetTireMaterial() const
{
	return m_tireMaterial;
}

CustomVehicleController* CustomVehicleControllerManager::CreateVehicle(NewtonBody* const body, const dMatrix& vehicleFrame, NewtonApplyForceAndTorque forceAndTorque, void* const userData, dFloat gravityMag)
{
	dAssert (0);
//	CustomVehicleController* const controller = CreateController();
//	controller->Init(body, vehicleFrame, gravityVector);
//	return controller;
return NULL;	
}

CustomVehicleController* CustomVehicleControllerManager::CreateVehicle(NewtonCollision* const chassisShape, const dMatrix& vehicleFrame, dFloat mass, NewtonApplyForceAndTorque forceAndTorque, void* const userData, dFloat gravityMag)
{
	CustomVehicleController* const controller = CreateController();
	controller->Init(chassisShape, vehicleFrame, mass, forceAndTorque, userData, gravityMag);
	return controller;
}

void CustomVehicleController::Init(NewtonCollision* const chassisShape, const dMatrix& vehicleFrame, dFloat mass, NewtonApplyForceAndTorque forceAndTorque, void* const userData, dFloat gravityMag)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();
	NewtonWorld* const world = manager->GetWorld();

	// create a body and an call the low level init function
	dMatrix locationMatrix(dGetIdentityMatrix());
	NewtonBody* const body = NewtonCreateDynamicBody(world, chassisShape, &locationMatrix[0][0]);

	// set vehicle mass, inertia and center of mass
//mass = 0;
	NewtonBodySetMassProperties(body, mass, chassisShape);

	// initialize 
	Init(body, vehicleFrame, forceAndTorque, userData, gravityMag);
}

void CustomVehicleController::Init(NewtonBody* const body, const dMatrix& vehicleFrame, NewtonApplyForceAndTorque forceAndTorque, void* const userData, dFloat gravityMag)
{
	m_body = body;
	m_finalized = false;
	m_speed = 0.0f;
	m_speedRate = 0.0f;
	m_sideSlipRate = 0.0f;
	m_sideSlipAngle = 0.0f;
	m_weightDistribution = 0.5f;
	m_gravityMag = gravityMag;	
	m_forceAndTorque = forceAndTorque;

//	m_localFrame = vehicleFrame;
//	m_localFrame.m_posit = dVector (0.0f, 0.0f, 0.0f, 1.0f);
	
	m_localOmega = dVector(0.0f);
	m_localVeloc = dVector(0.0f);
	m_vehicleGlobalMatrix = dGetIdentityMatrix();

	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();
	NewtonWorld* const world = manager->GetWorld();

	// set linear and angular drag to zero
	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);

	// set the standard force and torque call back
	NewtonBodySetForceAndTorqueCallback(body, m_forceAndTorque);

	m_contactFilter = new BodyPartTire::FrictionModel(this);

	m_brakesControl = NULL;
	m_engineControl = NULL;
	m_handBrakesControl = NULL;
	m_steeringControl = NULL;
	
	m_collisionAggregate = NewtonCollisionAggregateCreate(world);
	NewtonCollisionAggregateSetSelfCollision (m_collisionAggregate, 0);
	NewtonCollisionAggregateAddBody (m_collisionAggregate, m_body);

	m_skeleton = NewtonSkeletonContainerCreate(world, m_body, NULL);

	m_chassis.Init(this, userData);
	m_bodyPartsList.Append(&m_chassis);

	SetAerodynamicsDownforceCoefficient(0.5f, 0.4f, 1.0f);

#ifdef D_PLOT_ENGINE_CURVE 
	file_xxx = fopen("vehiceLog.csv", "wb");
	fprintf (file_xxx, "eng_rpm, eng_torque, eng_nominalTorque,\n");
#endif
}

void CustomVehicleController::Cleanup()
{
	if (m_engine) {
		delete m_engine;
	}
	SetBrakes(NULL);
	SetEngine(NULL);
	SetSteering(NULL);
	SetHandBrakes(NULL);
	SetContactFilter(NULL);
}

const CustomVehicleController::BodyPart* CustomVehicleController::GetChassis() const
{
	return &m_chassis;
}

/*
const dMatrix& CustomVehicleController::GetLocalFrame() const
{
	dAssert (0);
//	return m_localFrame;
	return dGetIdentityMatrix();
}
*/

dMatrix CustomVehicleController::GetTransform() const
{
	dMatrix matrix;
	NewtonBodyGetMatrix (m_chassis.GetBody(), &matrix[0][0]);
	return matrix;
}

void CustomVehicleController::SetTransform(const dMatrix& matrix)
{
	NewtonBodySetMatrixRecursive (m_chassis.GetBody(), &matrix[0][0]);
}


CustomVehicleController::EngineController* CustomVehicleController::GetEngine() const
{
	return m_engineControl;
}


CustomVehicleController::SteeringController* CustomVehicleController::GetSteering() const
{
	return m_steeringControl;
}

CustomVehicleController::BrakeController* CustomVehicleController::GetBrakes() const
{
	return m_brakesControl;
}

CustomVehicleController::BrakeController* CustomVehicleController::GetHandBrakes() const
{
	return m_handBrakesControl;
}

void CustomVehicleController::SetEngine(EngineController* const engineControl)
{
	if (m_engineControl) {
		delete m_engineControl;
	}
	m_engineControl = engineControl;
}


void CustomVehicleController::SetHandBrakes(BrakeController* const handBrakes)
{
	if (m_handBrakesControl) {
		delete m_handBrakesControl;
	}
	m_handBrakesControl = handBrakes;
}

void CustomVehicleController::SetBrakes(BrakeController* const brakes)
{
	if (m_brakesControl) {
		delete m_brakesControl;
	}
	m_brakesControl = brakes;
}


void CustomVehicleController::SetSteering(SteeringController* const steering)
{
	if (m_steeringControl) {
		delete m_steeringControl;
	}
	m_steeringControl = steering;
}

void CustomVehicleController::SetContactFilter(BodyPartTire::FrictionModel* const filter)
{
	if (m_contactFilter) {
		delete m_contactFilter;
	}
	m_contactFilter = filter;
}

dList<CustomVehicleController::BodyPartTire>::dListNode* CustomVehicleController::GetFirstTire() const
{
	return m_tireList.GetFirst();
}

dList<CustomVehicleController::BodyPartTire>::dListNode* CustomVehicleController::GetNextTire(dList<CustomVehicleController::BodyPartTire>::dListNode* const tireNode) const
{
	return tireNode->GetNext();
}

dList<CustomVehicleController::BodyPart*>::dListNode* CustomVehicleController::GetFirstBodyPart() const
{
	return m_bodyPartsList.GetFirst();
}

dList<CustomVehicleController::BodyPart*>::dListNode* CustomVehicleController::GetNextBodyPart(dList<BodyPart*>::dListNode* const part) const
{
	return part->GetNext();
}

void CustomVehicleController::SetCenterOfGravity(const dVector& comRelativeToGeomtriCenter)
{
	NewtonBodySetCentreOfMass(m_body, &comRelativeToGeomtriCenter[0]);
}

dFloat CustomVehicleController::GetWeightDistribution() const
{
	return m_weightDistribution;
}

void CustomVehicleController::SetWeightDistribution(dFloat weightDistribution)
{
	m_weightDistribution = dClamp (weightDistribution, dFloat(0.0f), dFloat(1.0f));
	if (m_finalized) {
		dFloat factor = m_weightDistribution - 0.5f;

		dMatrix matrix;
		dMatrix tireMatrix;
		dVector origin(0.0f);
		dVector totalMassOrigin (0.0f);
		dFloat xMin = 1.0e10f;
		dFloat xMax = -1.0e10f;
		dFloat totalMass;
		dFloat Ixx;
		dFloat Iyy;
		dFloat Izz;

		NewtonBodyGetMatrix(m_body, &matrix[0][0]);
		NewtonBodyGetCentreOfMass(m_body, &origin[0]);
		matrix.m_posit = matrix.TransformVector(origin);
		NewtonBodyGetMass (m_body, &totalMass, &Ixx, &Iyy, &Izz);
		totalMassOrigin = origin.Scale (totalMass);

		matrix = matrix.Inverse();
		if (m_engine) {
			dMatrix engineMatrixMatrix;
			dFloat mass;
			NewtonBodyGetMass (m_engine->GetBody(), &mass, &Ixx, &Iyy, &Izz);
			NewtonBodyGetMatrix(m_engine->GetBody(), &engineMatrixMatrix[0][0]);
			totalMass += mass;
			engineMatrixMatrix = engineMatrixMatrix * matrix;
			totalMassOrigin += engineMatrixMatrix.m_posit.Scale (mass);
		}

		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			dFloat mass;
			BodyPartTire* const tire = &node->GetInfo();
			NewtonBodyGetMatrix(tire->GetBody(), &tireMatrix[0][0]);
			NewtonBodyGetMass (tire->GetBody(), &mass, &Ixx, &Iyy, &Izz);

			totalMass += mass;
			tireMatrix = tireMatrix * matrix;
			totalMassOrigin += tireMatrix.m_posit.Scale (mass);
			xMin = dMin (xMin, tireMatrix.m_posit.m_x); 
			xMax = dMax (xMax, tireMatrix.m_posit.m_x); 
		}
		origin = totalMassOrigin.Scale (1.0f / totalMass);

		dVector vehCom (0.0f);
		NewtonBodyGetCentreOfMass(m_body, &vehCom[0]);
		vehCom.m_x = origin.m_x + (xMax - xMin) * factor;
		vehCom.m_z = origin.m_z;
		NewtonBodySetCentreOfMass(m_body, &vehCom[0]);
	}
}


void CustomVehicleController::Finalize()
{
//NewtonBodySetMassMatrix(GetBody(), 0.0f, 0.0f, 0.0f, 0.0f);
	m_finalized = true;
	NewtonSkeletonContainerFinalize(m_skeleton);
	SetWeightDistribution (m_weightDistribution);
}

bool CustomVehicleController::ControlStateChanged() const
{
	bool inputChanged = (m_steeringControl && m_steeringControl->ParamChanged());
	inputChanged = inputChanged || (m_steeringControl && !m_steeringControl->m_isSleeping);
	inputChanged = inputChanged || (m_engineControl && m_engineControl ->ParamChanged());
	inputChanged = inputChanged || (m_brakesControl && m_brakesControl->ParamChanged());
	inputChanged = inputChanged || (m_handBrakesControl && m_handBrakesControl->ParamChanged());
//	inputChanged = inputChanged || m_hasNewContact;
	return inputChanged;
}

CustomVehicleController::BodyPartTire* CustomVehicleController::AddTire(const BodyPartTire::Info& tireInfo)
{
	dList<BodyPartTire>::dListNode* const tireNode = m_tireList.Append();
	BodyPartTire& tire = tireNode->GetInfo();

	// calculate the tire matrix location,
	dMatrix matrix;
	NewtonBodyGetMatrix(m_body, &matrix[0][0]);
//	matrix = m_localFrame * matrix;
	matrix.m_posit = matrix.TransformVector (tireInfo.m_location);
	matrix.m_posit.m_w = 1.0f;

	tire.Init(&m_chassis, matrix, tireInfo);
	tire.m_index = m_tireList.GetCount() - 1;

	m_bodyPartsList.Append(&tire);
	NewtonCollisionAggregateAddBody (m_collisionAggregate, tire.GetBody());
	NewtonSkeletonContainerAttachBone (m_skeleton, tire.GetBody(), m_chassis.GetBody());
	return &tireNode->GetInfo();
}

CustomVehicleController::BodyPartEngine* CustomVehicleController::AddEngine (dFloat mass, dFloat amatureRadius)
{
	m_engine = new BodyPartEngine (this, mass, amatureRadius);

	m_bodyPartsList.Append(m_engine);
	NewtonCollisionAggregateAddBody(m_collisionAggregate, m_engine->GetBody());
	NewtonSkeletonContainerAttachBone(m_skeleton, m_engine->GetBody(), m_chassis.GetBody());
	return m_engine;
}

dVector CustomVehicleController::GetTireNormalForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*) tire->GetJoint();
	dFloat force = joint->GetTireLoad();
	return dVector (0.0f, force, 0.0f, 0.0f);
}

dVector CustomVehicleController::GetTireLateralForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*)tire->GetJoint();
	return joint->GetLateralForce();
}

dVector CustomVehicleController::GetTireLongitudinalForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*)tire->GetJoint();
	return joint->GetLongitudinalForce();
}

dFloat CustomVehicleController::GetAerodynamicsDowforceCoeficient() const
{
	return m_chassis.m_aerodynamicsDownForceCoefficient;
}

void CustomVehicleController::SetAerodynamicsDownforceCoefficient(dFloat downWeightRatioAtSpeedFactor, dFloat speedFactor, dFloat maxWeightAtTopSpeed)
{
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dFloat mass;

	dAssert (speedFactor >= 0.0f);
	dAssert (speedFactor <= 1.0f);
	dAssert (downWeightRatioAtSpeedFactor >= 0.0f);
	dAssert (downWeightRatioAtSpeedFactor < maxWeightAtTopSpeed);
	NewtonBody* const body = GetBody();
	NewtonBodyGetMass(body, &mass, &Ixx, &Iyy, &Izz);
	dFloat topSpeed = m_engineControl ? m_engineControl->GetTopSpeed() : 25.0f;
	m_chassis.m_aerodynamicsDownSpeedCutOff = topSpeed * speedFactor;
	m_chassis.m_aerodynamicsDownForce0 = mass * downWeightRatioAtSpeedFactor * dAbs (m_gravityMag);
	m_chassis.m_aerodynamicsDownForce1 = mass * maxWeightAtTopSpeed * dAbs (m_gravityMag);
	m_chassis.m_aerodynamicsDownForceCoefficient = m_chassis.m_aerodynamicsDownForce0 / (m_chassis.m_aerodynamicsDownSpeedCutOff * m_chassis.m_aerodynamicsDownSpeedCutOff);
}

int CustomVehicleControllerManager::OnTireAABBOverlap(const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonBody* const body1, int threadIndex)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)NewtonMaterialGetMaterialPairUserData(material);

	const NewtonCollision* const collision0 = NewtonBodyGetCollision(body0);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision0);
		dAssert(tire->GetParent()->GetBody() != otherBody);
		return manager->OnTireAABBOverlap(material, tire, otherBody);
	} 
	const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
	dAssert (NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData) ;
	const NewtonBody* const otherBody = body0;
	const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision1);
	dAssert(tire->GetParent()->GetBody() != otherBody);
	return manager->OnTireAABBOverlap(material, tire, otherBody);
}

int CustomVehicleControllerManager::OnTireAABBOverlap(const NewtonMaterial* const material, const CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody) const
{
	for (int i = 0; i < tire->m_collidingCount; i ++) {
		if (otherBody == tire->m_contactInfo[i].m_hitBody) {
			return 1;
		}
	}
//	tire->GetController()->m_hasNewContact |= tire->m_data.m_hasFender ? false : true;
	return tire->m_data.m_hasFender ? 0 : 1;
}

int CustomVehicleControllerManager::OnContactGeneration (const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonCollision* const collision0, const NewtonBody* const body1, const NewtonCollision* const collision1, NewtonUserContactPoint* const contactBuffer, int maxCount, int threadIndex)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*) NewtonMaterialGetMaterialPairUserData(material);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
//	const void* const data1 = NewtonCollisionDataPointer(collision1);
	dAssert ((data0 == manager->m_tireShapeTemplateData) || (NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData));
	dAssert (!((data0 == manager->m_tireShapeTemplateData) && (NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData)));

	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		const NewtonCollision* const tireCollision = collision0;
		const NewtonCollision* const otherCollision = collision1;
		const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(tireCollision);
		dAssert (tire->GetBody() == body0);
		return manager->OnContactGeneration (tire, otherBody, otherCollision, contactBuffer, maxCount, threadIndex);
	} 
	dAssert (NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData);
	const NewtonBody* const otherBody = body0;
	const NewtonCollision* const tireCollision = collision1;
	const NewtonCollision* const otherCollision = collision0;
	const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(tireCollision);
	dAssert (tire->GetBody() == body1);
	int count = manager->OnContactGeneration(tire, otherBody, otherCollision, contactBuffer, maxCount, threadIndex);

	for (int i = 0; i < count; i ++) {	
		contactBuffer[i].m_normal[0] *= -1.0f;
		contactBuffer[i].m_normal[1] *= -1.0f;
		contactBuffer[i].m_normal[2] *= -1.0f;
		dSwap (contactBuffer[i].m_shapeId0, contactBuffer[i].m_shapeId1);
	}
	return count;
}


int CustomVehicleControllerManager::Collide(CustomVehicleController::BodyPartTire* const tire, int threadIndex) const
{
	dMatrix tireMatrix;
	dMatrix chassisMatrix;

	const NewtonWorld* const world = GetWorld();
	const NewtonBody* const tireBody = tire->GetBody();
	const NewtonBody* const vehicleBody = tire->GetParent()->GetBody();
	CustomVehicleController* const controller = tire->GetController();

	NewtonBodyGetMatrix(tireBody, &tireMatrix[0][0]);
	NewtonBodyGetMatrix(vehicleBody, &chassisMatrix[0][0]);

//	chassisMatrix = controller->m_localFrame * chassisMatrix;
	chassisMatrix.m_posit = chassisMatrix.TransformVector(tire->m_data.m_location);
	chassisMatrix.m_posit.m_w = 1.0f;

	dVector suspensionSpan (chassisMatrix.m_up.Scale(tire->m_data.m_suspesionlenght));

	dMatrix tireSweeptMatrix;
	tireSweeptMatrix.m_up = chassisMatrix.m_up;
	tireSweeptMatrix.m_right = tireMatrix.m_front.CrossProduct(chassisMatrix.m_up);
	tireSweeptMatrix.m_right = tireSweeptMatrix.m_right.Scale(1.0f / dSqrt(tireSweeptMatrix.m_right.DotProduct3(tireSweeptMatrix.m_right)));
	tireSweeptMatrix.m_front = tireSweeptMatrix.m_up.CrossProduct(tireSweeptMatrix.m_right);
	tireSweeptMatrix.m_posit = chassisMatrix.m_posit + suspensionSpan;

	NewtonCollision* const tireCollision = NewtonBodyGetCollision(tireBody);
	TireFilter filter(tire, controller);

	dFloat timeOfImpact;
	tire->m_collidingCount = 0;
	const int maxContactCount = 2;
	dAssert (sizeof (tire->m_contactInfo) / sizeof (tire->m_contactInfo[0]) > 2);
	int count = NewtonWorldConvexCast (world, &tireSweeptMatrix[0][0], &chassisMatrix.m_posit[0], tireCollision, &timeOfImpact, &filter, CustomControllerConvexCastPreFilter::Prefilter, tire->m_contactInfo, maxContactCount, threadIndex);
	if (count) {
		timeOfImpact = 1.0f - timeOfImpact;
		dFloat num = (tireMatrix.m_posit - chassisMatrix.m_up.Scale (0.25f * tire->m_data.m_suspesionlenght) - chassisMatrix.m_posit).DotProduct3(suspensionSpan);
		dFloat tireParam = dMax (num / (tire->m_data.m_suspesionlenght * tire->m_data.m_suspesionlenght), dFloat(0.0f));

		if (tireParam <= timeOfImpact) {
			tireSweeptMatrix.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale(timeOfImpact * tire->m_data.m_suspesionlenght);
			for (int i = count - 1; i >= 0; i --) {
				dVector p (tireSweeptMatrix.UntransformVector (dVector (tire->m_contactInfo[i].m_point[0], tire->m_contactInfo[i].m_point[1], tire->m_contactInfo[i].m_point[2], 1.0f)));
				if ((p.m_y >= -(tire->m_data.m_radio * 0.5f)) || (dAbs (p.m_x / p.m_y) > 0.4f)) {
					tire->m_contactInfo[i] = tire->m_contactInfo[count - 1];
					count --;
				}
			}
			if (count) {
				dFloat x1 = timeOfImpact * tire->m_data.m_suspesionlenght;
				dFloat x0 = (tireMatrix.m_posit - chassisMatrix.m_posit).DotProduct3(chassisMatrix.m_up);
				dFloat x10 = x1 - x0;
				if (x10 > (1.0f / 32.0f)) {
					dFloat param = 1.0e10f;
					x1 = x0 + (1.0f / 32.0f);
					dMatrix origin (chassisMatrix);
					origin.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale(x1);
					NewtonWorldConvexCast (world, &chassisMatrix[0][0], &tireSweeptMatrix.m_posit[0], tireCollision, &param, &filter, CustomControllerConvexCastPreFilter::Prefilter, NULL, 0, threadIndex);
					count = (param < 1.0f) ? 0 : count;
				}
				if (count) {
					tireMatrix.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale(x1);
					NewtonBodySetMatrixNoSleep(tireBody, &tireMatrix[0][0]);
				}
			}
		} else {
			count = 0;
		}
	}

	tire->m_collidingCount = count;
	if (!tire->m_data.m_hasFender) {
		count = NewtonWorldCollide (world, &tireMatrix[0][0], tireCollision, &filter, CustomControllerConvexCastPreFilter::Prefilter, &tire->m_contactInfo[count], maxContactCount, threadIndex);
		for (int i = 0; i < count; i++) {
			if (tire->m_contactInfo[tire->m_collidingCount + i].m_penetration == 0.0f) {
				tire->m_contactInfo[tire->m_collidingCount + i].m_penetration = 1.0e-5f;
			}
		}
		tire->m_collidingCount += count;
	}

	return tire->m_collidingCount ? 1 : 0;
}


void CustomVehicleControllerManager::OnTireContactsProcess (const NewtonJoint* const contactJoint, dFloat timestep, int threadIndex)
{
	void* const contact = NewtonContactJointGetFirstContact(contactJoint);
	dAssert (contact);
	NewtonMaterial* const material = NewtonContactGetMaterial(contact);
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*) NewtonMaterialGetMaterialPairUserData(material);

	const NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
	const NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);
	const NewtonCollision* const collision0 = NewtonBodyGetCollision(body0);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision0);
		dAssert(tire->GetParent()->GetBody() != otherBody);
		manager->OnTireContactsProcess(contactJoint, tire, otherBody, timestep);
	} else {
		const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
		const void* const data1 = NewtonCollisionDataPointer(collision1);
		if (data1 == manager->m_tireShapeTemplateData) {
			const NewtonCollision* const collision2 = NewtonBodyGetCollision(body1);
			dAssert(NewtonCollisionDataPointer(collision2) == manager->m_tireShapeTemplateData);
			const NewtonBody* const otherBody = body0;
			CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision2);
			dAssert(tire->GetParent()->GetBody() != otherBody);
			manager->OnTireContactsProcess(contactJoint, tire, otherBody, timestep);
		}
	}
}


int CustomVehicleControllerManager::OnContactGeneration(const CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody, const NewtonCollision* const othercollision, NewtonUserContactPoint* const contactBuffer, int maxCount, int threadIndex) const
{
	int count = 0;
	NewtonCollision* const collisionA = NewtonBodyGetCollision(tire->GetBody());
	dLong tireID = NewtonCollisionGetUserID(collisionA);
	for (int i = 0; i < tire->m_collidingCount; i++) {
		if (otherBody == tire->m_contactInfo[i].m_hitBody) {
			contactBuffer[count].m_point[0] = tire->m_contactInfo[i].m_point[0];
			contactBuffer[count].m_point[1] = tire->m_contactInfo[i].m_point[1];
			contactBuffer[count].m_point[2] = tire->m_contactInfo[i].m_point[2];
			contactBuffer[count].m_point[3] = 1.0f;
			contactBuffer[count].m_normal[0] = tire->m_contactInfo[i].m_normal[0];
			contactBuffer[count].m_normal[1] = tire->m_contactInfo[i].m_normal[1];
			contactBuffer[count].m_normal[2] = tire->m_contactInfo[i].m_normal[2];
			contactBuffer[count].m_normal[3] = 0.0f;
			contactBuffer[count].m_penetration = tire->m_contactInfo[i].m_penetration;
			contactBuffer[count].m_shapeId0 = tireID;
			contactBuffer[count].m_shapeId1 = tire->m_contactInfo[i].m_contactID;
			count++;
		}
	}
	return count;
}

void CustomVehicleControllerManager::OnTireContactsProcess(const NewtonJoint* const contactJoint, CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody, dFloat timestep)
{
	dAssert((tire->GetBody() == NewtonJointGetBody0(contactJoint)) || (tire->GetBody() == NewtonJointGetBody1(contactJoint)));

	dMatrix tireMatrix;
	dVector tireOmega(0.0f);
	dVector tireVeloc(0.0f);

	NewtonBody* const tireBody = tire->GetBody();
	const CustomVehicleController* const controller = tire->GetController();
	CustomVehicleController::WheelJoint* const tireJoint = (CustomVehicleController::WheelJoint*) tire->GetJoint();

	dAssert(tireJoint->GetBody0() == tireBody);


	NewtonBodyGetMatrix(tireBody, &tireMatrix[0][0]);
	tireMatrix = tireJoint->GetMatrix0() * tireMatrix;

	NewtonBodyGetOmega(tireBody, &tireOmega[0]);
	NewtonBodyGetVelocity(tireBody, &tireVeloc[0]);

	tire->m_lateralSlip = 0.0f;
	tire->m_aligningTorque = 0.0f;
	tire->m_longitudinalSlip = 0.0f;

	for (void* contact = NewtonContactJointGetFirstContact(contactJoint); contact; contact = NewtonContactJointGetNextContact(contactJoint, contact)) {
		const dVector& lateralPin = tireMatrix.m_front;
		NewtonMaterial* const material = NewtonContactGetMaterial(contact);
		NewtonMaterialContactRotateTangentDirections(material, &lateralPin[0]);

//		if (NewtonMaterialGetContactPenetration (material) > 0.0f) {
//			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 0);
//			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 1);
//		} else {
		dVector contactPoint(0.0f);
		dVector contactNormal(0.0f);
		NewtonMaterialGetContactPositionAndNormal(material, tireBody, &contactPoint[0], &contactNormal[0]);
			
		dVector tireAnglePin(contactNormal.CrossProduct(lateralPin));
		dFloat pinMag2 = tireAnglePin.DotProduct3(tireAnglePin);
		if (pinMag2 > 0.25f) {
			// brush rubber tire friction model
			// project the contact point to the surface of the collision shape
			dVector contactPatch(contactPoint - lateralPin.Scale((contactPoint - tireMatrix.m_posit).DotProduct3(lateralPin)));
			dVector dp(contactPatch - tireMatrix.m_posit);
			dVector radius(dp.Scale(tire->m_data.m_radio / dSqrt(dp.DotProduct3(dp))));

			dVector lateralContactDir(0.0f);
			dVector longitudinalContactDir(0.0f);
			NewtonMaterialGetContactTangentDirections(material, tireBody, &lateralContactDir[0], &longitudinalContactDir[0]);

			dFloat tireOriginLongitudinalSpeed = tireVeloc.DotProduct3(longitudinalContactDir);
			dFloat tireContactLongitudinalSpeed = -longitudinalContactDir.DotProduct3 (tireOmega.CrossProduct(radius));

			if ((dAbs(tireOriginLongitudinalSpeed) < (1.0f)) || (dAbs(tireContactLongitudinalSpeed) < 0.1f)) {
				// vehicle  moving at speed for which tire physics is undefined, simple do a kinematic motion
				NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 0);
				NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 1);
			} else {
				// calculating Brush tire model with longitudinal and lateral coupling 
				// for friction coupling according to Motor Vehicle dynamics by: Giancarlo Genta 
				// reduces to this, which may have a divide by zero locked, so I am clamping to some small value
				// dFloat k = (vw - vx) / vx;
				// dFloat phy_x0 = k / (1.0f + k);
				// dFloat alphaTangent = vy / dAbs(vx);
				//dFloat phy_y0 = alphaTangent / (1.0f + k);
				if (dAbs(tireContactLongitudinalSpeed) < 0.01f) {
					tireContactLongitudinalSpeed = 0.01f * dSign(tireContactLongitudinalSpeed);
				}


				dFloat lateralAccel = 0.0f;
				dFloat longitudinalAccel = 0.0f;

				tire->m_longitudinalSlip = (tireContactLongitudinalSpeed - tireOriginLongitudinalSpeed) / tireOriginLongitudinalSpeed;

				if (tire->m_longitudinalSlip > 1.0f) {
					longitudinalAccel = 0.5f * (tireContactLongitudinalSpeed - 2.0f * tireOriginLongitudinalSpeed) / timestep;
				} else if (tire->m_longitudinalSlip < -1.0f) {
					longitudinalAccel = - 0.5f * (tireContactLongitudinalSpeed - 2.0f * tireOriginLongitudinalSpeed) / timestep;
				}
				
				dVector tireLocalPosit (controller->m_vehicleGlobalMatrix.UntransformVector(tireMatrix.m_posit));
				dVector tireSizeVeloc (controller->m_localOmega.CrossProduct(tireLocalPosit));
				tire->m_lateralSlip = dAtan2(controller->m_localVeloc.m_z + tireSizeVeloc.m_z, controller->m_localVeloc.m_x + tireSizeVeloc.m_x) - tireJoint->m_steerAngle0;

dTrace(("slip (%d %f) ", tire->m_index, tire->m_lateralSlip * 180.0f / 3.1416));

				if (dAbs(controller->m_sideSlipAngle) > D_VEHICLE_MAX_SIDESLIP_ANGLE) {
/*
					dFloat tanBeta = dTan (D_VEHICLE_MAX_SIDESLIP_ANGLE * dSign (controller->m_sideSlipAngle) + tireJoint->m_steerAngle0);
					dFloat den = tanBeta * tireLocalPosit.m_z + tireLocalPosit.m_x;
					dFloat w = (controller->m_localVeloc.m_x - tanBeta * controller->m_localVeloc.m_z) / den;

					dFloat dvx = w * tireLocalPosit.m_z - tireSizeVeloc.m_x;
					dFloat dvz = -w * tireLocalPosit.m_x - tireSizeVeloc.m_z;
					lateralAccel = -dvz / timestep;
					longitudinalAccel = -dvx / timestep;
*/
				}
//dTrace (("beta (%d %f) ", tire->m_index, tire->m_lateralSlip * 180.0f/ 3.141416f));


				dFloat lateralForce;
				dFloat aligningMoment;
				dFloat longitudinalForce;

				dVector tireLoadForce(0.0f);
				NewtonMaterialGetContactForce(material, tireBody, &tireLoadForce.m_x);
				dFloat tireLoad = tireLoadForce.DotProduct3(contactNormal);

				controller->m_contactFilter->GetForces(tire, otherBody, material, tireLoad, longitudinalForce, lateralForce, aligningMoment);
				
				NewtonMaterialSetContactTangentAcceleration (material, lateralAccel, 0);
				NewtonMaterialSetContactTangentFriction(material, dAbs (lateralForce), 0);

				NewtonMaterialSetContactTangentAcceleration (material, longitudinalAccel, 1);
				NewtonMaterialSetContactTangentFriction(material, dAbs (longitudinalForce), 1);
			}

		} else {
			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 0);
			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 1);
		}
//		}

		NewtonMaterialSetContactElasticity(material, 0.0f);
	}
}


dVector CustomVehicleController::GetLastLateralForce(BodyPartTire* const tire) const
{
	return (GetTireLateralForce(tire) + GetTireLongitudinalForce(tire)).Scale (-1.0f);
}



void CustomVehicleController::ApplySuspensionForces(dFloat timestep) const
{
	NewtonBody* const chassisBody = m_chassis.GetBody();
	dComplentaritySolver::dJacobianPair m_jt[64];
	dComplentaritySolver::dJacobianPair m_jInvMass[64];
	dFloat massMatrix[64][64];
	dFloat accel[64];
	BodyPartTire* tires[64];

	dVector chassisOrigin;
	dMatrix chassisMatrix;
	dMatrix chassisInvInertia;
	dFloat chassisMass;
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;

	NewtonBodyGetCentreOfMass(chassisBody, &chassisOrigin[0]);
	NewtonBodyGetMatrix(chassisBody, &chassisMatrix[0][0]);
	NewtonBodyGetInvMass(chassisBody, &chassisMass, &Ixx, &Iyy, &Izz);
	NewtonBodyGetInvInertiaMatrix(chassisBody, &chassisInvInertia[0][0]);

	chassisOrigin = chassisMatrix.TransformVector(chassisOrigin);
	int tireCount = 0;
	for (dList<BodyPartTire>::dListNode* tireNode = m_tireList.GetFirst(); tireNode; tireNode = tireNode->GetNext()) {
		BodyPartTire& tire = tireNode->GetInfo();

		if (tire.m_data.m_suspentionType != BodyPartTire::Info::m_roller) {
			tires[tireCount] = &tire;

			const WheelJoint* const joint = (WheelJoint*)tire.GetJoint();
			NewtonBody* const tireBody = tire.GetBody();
			dAssert(tireBody == joint->GetBody0());
			dAssert(chassisBody == joint->GetBody1());

			dMatrix tireMatrix;
			//dMatrix chassisMatrix;
			dVector tireVeloc(0.0f);
			dVector chassisPivotVeloc(0.0f);

			joint->CalculateGlobalMatrix(tireMatrix, chassisMatrix);
			NewtonBodyGetVelocity(tireBody, &tireVeloc[0]);
			NewtonBodyGetPointVelocity(chassisBody, &tireMatrix.m_posit[0], &chassisPivotVeloc[0]);

			dFloat param = joint->CalculateTireParametricPosition(tireMatrix, chassisMatrix);
			param = dClamp(param, dFloat(0.0f), dFloat(1.0f));

			dFloat x = tire.m_data.m_suspesionlenght * param;
			dFloat v = ((tireVeloc - chassisPivotVeloc).DotProduct3(chassisMatrix.m_up));

			dFloat weight = 1.0f;
			switch (tire.m_data.m_suspentionType)
			{
				case BodyPartTire::Info::m_offroad:
					weight = 0.9f;
					break;
				case BodyPartTire::Info::m_confort:
					weight = 1.0f;
					break;
				case BodyPartTire::Info::m_race:
					weight = 1.1f;
					break;
			}
			accel[tireCount] = -NewtonCalculateSpringDamperAcceleration(timestep, tire.m_data.m_springStrength * weight, x, tire.m_data.m_dampingRatio, v);

			dMatrix tireInvInertia;
			dFloat tireMass;

			NewtonBodyGetInvMass(tireBody, &tireMass, &Ixx, &Iyy, &Izz);
			NewtonBodyGetInvInertiaMatrix(tireBody, &tireInvInertia[0][0]);

			m_jt[tireCount].m_jacobian_IM0.m_linear = chassisMatrix.m_up.Scale(-1.0f);
			m_jt[tireCount].m_jacobian_IM0.m_angular = dVector(0.0f);
			m_jt[tireCount].m_jacobian_IM1.m_linear = chassisMatrix.m_up;
			m_jt[tireCount].m_jacobian_IM1.m_angular = (tireMatrix.m_posit - chassisOrigin).CrossProduct(chassisMatrix.m_up);

			m_jInvMass[tireCount].m_jacobian_IM0.m_linear = m_jt[tireCount].m_jacobian_IM0.m_linear.Scale(tireMass);
			m_jInvMass[tireCount].m_jacobian_IM0.m_angular = tireInvInertia.RotateVector(m_jt[tireCount].m_jacobian_IM0.m_angular);
			m_jInvMass[tireCount].m_jacobian_IM1.m_linear = m_jt[tireCount].m_jacobian_IM1.m_linear.Scale(chassisMass);
			m_jInvMass[tireCount].m_jacobian_IM1.m_angular = chassisInvInertia.RotateVector(m_jt[tireCount].m_jacobian_IM1.m_angular);

			tireCount++;
		}
	}

	dFloat*const massMatrixMem = &massMatrix[0][0];
	for (int i = 0; i < tireCount; i++) {
		dFloat* const row = &massMatrixMem[i * tireCount];
		dFloat aii = m_jInvMass[i].m_jacobian_IM0.m_linear.DotProduct3(m_jt[i].m_jacobian_IM0.m_linear) + m_jInvMass[i].m_jacobian_IM0.m_angular.DotProduct3(m_jt[i].m_jacobian_IM0.m_angular) +
					 m_jInvMass[i].m_jacobian_IM1.m_linear.DotProduct3(m_jt[i].m_jacobian_IM1.m_linear) + m_jInvMass[i].m_jacobian_IM1.m_angular.DotProduct3(m_jt[i].m_jacobian_IM1.m_angular);

		row[i] = aii;
		for (int j = i + 1; j < tireCount; j++) {
			dFloat aij = m_jInvMass[i].m_jacobian_IM1.m_linear.DotProduct3(m_jt[j].m_jacobian_IM1.m_linear) + m_jInvMass[i].m_jacobian_IM1.m_angular.DotProduct3(m_jt[j].m_jacobian_IM1.m_angular);
			row[j] = aij;
			massMatrixMem[j * tireCount + i] = aij;
		}
	}

	dCholeskyFactorization(tireCount, massMatrixMem);
	dCholeskySolve(tireCount, massMatrixMem, accel, tireCount);

	dVector chassisForce(0.0f);
	dVector chassisTorque(0.0f);
	for (int i = 0; i < tireCount; i++) {
		dVector tireForce(m_jt[i].m_jacobian_IM0.m_linear.Scale(accel[i]));
		dVector tireTorque(m_jt[i].m_jacobian_IM0.m_angular.Scale(accel[i]));
		NewtonBodyAddForce(tires[i]->GetBody(), &tireForce[0]);
		NewtonBodyAddTorque(tires[i]->GetBody(), &tireTorque[0]);
		chassisForce += m_jt[i].m_jacobian_IM1.m_linear.Scale(accel[i]);
		chassisTorque += m_jt[i].m_jacobian_IM1.m_angular.Scale(accel[i]);
	}
	NewtonBodyAddForce(chassisBody, &chassisForce[0]);
	NewtonBodyAddTorque(chassisBody, &chassisTorque[0]);
}


void CustomVehicleController::CalculateLateralDynamicState(dFloat timestep)
{
	dVector com(0.0f);

	NewtonBody* const chassisBody = m_chassis.GetBody();
	NewtonBodyGetCentreOfMass(chassisBody, &com[0]);
	NewtonBodyGetMatrix(chassisBody, &m_vehicleGlobalMatrix[0][0]);
//	m_vehicleGlobalMatrix = GetLocalFrame() * m_vehicleGlobalMatrix;
	m_vehicleGlobalMatrix.m_posit = m_vehicleGlobalMatrix.TransformVector(com);

	NewtonBodyGetOmega(chassisBody, &m_localOmega[0]);
	NewtonBodyGetVelocity(chassisBody, &m_localVeloc[0]);

static int xxx;
xxx++;
if (xxx > 3000)
	xxx *= 1;

	m_localVeloc = m_vehicleGlobalMatrix.UnrotateVector(m_localVeloc);
	m_localOmega = m_vehicleGlobalMatrix.UnrotateVector(m_localOmega);
	m_localOmega.m_x = 0.0f;
	m_localOmega.m_z = 0.0f;
	m_localVeloc.m_y = 0.0f;

	dFloat velocMag2 = m_localVeloc.DotProduct3(m_localVeloc);
	if (velocMag2 < 0.25f) {
		m_speed = 0.0f;
		m_speedRate = 0.0f;
		m_sideSlipAngle = 0.0f;
		m_sideSlipRate = 0.0f;
	} else {
		dFloat speed = dSqrt (velocMag2);
		m_speedRate = (speed - m_speed) / timestep;
		m_speed = speed;
		dFloat sideSlipAngle = dAtan2(m_localVeloc.m_z, m_localVeloc.m_x);
		m_sideSlipRate = (sideSlipAngle - m_sideSlipAngle) / timestep;
		m_sideSlipAngle = sideSlipAngle;
	}
	dTrace(("\n%d omega(%f) sideSlipRate (%f) sideSlip(%f) tires: ", xxx, m_localOmega.m_y * 180.0f / 3.141416f, m_sideSlipRate * 180.0f / 3.141416f, m_sideSlipAngle * 180.0f / 3.141416f));
}


void CustomVehicleController::PostUpdate(dFloat timestep, int threadIndex)
{
//dTrace (("\n"));
	dTimeTrackerEvent(__FUNCTION__);
	if (m_finalized) {
		if (!NewtonBodyGetSleepState(m_body)) {
			CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();
			for (dList<BodyPartTire>::dListNode* tireNode = m_tireList.GetFirst(); tireNode; tireNode = tireNode->GetNext()) {
				BodyPartTire& tire = tireNode->GetInfo();
				//WheelJoint* const tireJoint = (WheelJoint*)tire.GetJoint();
				//tireJoint->RemoveKinematicError(timestep);
				//int collided = manager->Collide(&tire, threadIndex);
				manager->Collide(&tire, threadIndex);
			}
		}

//dTrace (("\n"));

#ifdef D_PLOT_ENGINE_CURVE 
		dFloat engineOmega = m_engine->GetRPM();
		dFloat tireTorque = m_engine->GetLeftSpiderGear()->m_tireTorque + m_engine->GetRightSpiderGear()->m_tireTorque;
		dFloat engineTorque = m_engine->GetLeftSpiderGear()->m_engineTorque + m_engine->GetRightSpiderGear()->m_engineTorque;
		fprintf(file_xxx, "%f, %f, %f,\n", engineOmega, engineTorque, m_engine->GetNominalTorque());
#endif
	}
}


void CustomVehicleController::PreUpdate(dFloat timestep, int threadIndex)
{
	dTimeTrackerEvent(__FUNCTION__);
	if (m_finalized) {
		m_chassis.ApplyDownForce ();
		CalculateLateralDynamicState(timestep);
		ApplySuspensionForces (timestep);

		if (m_brakesControl) {
			m_brakesControl->Update(timestep);
		}

		if (m_handBrakesControl) {
			m_handBrakesControl->Update(timestep);
		}

		if (m_steeringControl) {
			m_steeringControl->Update(timestep);
		}

		if (m_engineControl) {
			m_engineControl->Update(timestep);
		}

		if (ControlStateChanged()) {
			NewtonBodySetSleepState(m_body, 0);
		}
	}
}



