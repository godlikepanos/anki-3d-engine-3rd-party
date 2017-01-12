/* Copyright (c) <2009> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/

#include <toolbox_stdafx.h>
#include "DemoMesh.h"
#include "DemoEntity.h"
#include "PhysicsUtils.h"
#include "DemoEntityManager.h"
#include "toolBox/OpenGlUtil.h"
#include "toolBox/DebugDisplay.h"

static int showContacts;



//static char* MAGIC_NUMBER = "serialize data";
static void SerializeFile (void* const serializeHandle, const void* const buffer, int size)
{
	// check that each chunk is a multiple of 4 bytes, this is useful for easy little to big Indian conversion
	_ASSERTE ((size & 0x03) == 0);
	fwrite (buffer, size, 1, (FILE*) serializeHandle);
}

static void DeSerializeFile (void* const serializeHandle, void* const buffer, int size)
{
	// check that each chunk is a multiple of 4 bytes, this is useful for easy little to big Indian conversion
	_ASSERTE ((size & 0x03) == 0);
	fread (buffer, size, 1, (FILE*) serializeHandle);
}

static void BodySerialization (NewtonBody* const body, void* const serializeHandle, NewtonSerializeCallback serializecallback)
{
	// here the use can save information of this body, ex:
	// a string naming the body,  
	// serialize the visual mesh, or save a link to the visual mesh
	// save labels for looking up the body call backs

}

NewtonCollision* TestSerialization (const char* const name, NewtonCollision* const source, NewtonWorld* const world)
{
	#ifdef USE_TEST_SERIALIZATION
		// save the collision file
		char fullPathName[2048];
		GetWorkingFileName (name, fullPathName);
		FILE* file = fopen (fullPathName, "wb");

		//	SerializeFile (file, MAGIC_NUMBER, strlen (MAGIC_NUMBER) + 1);
		NewtonCollisionSerialize (world, source, SerializeFile, file);
		fclose (file);

		// destroy the collision and reload it again
		NewtonDestroyCollision (source);

		//load from a serialized height map collision
		file = fopen (fullPathName, "rb");
		//	char magicNumber[256]; 
		//	DeSerializeFile (file, magicNumber, int (strlen (MAGIC_NUMBER) + 1));
		NewtonCollision* const collision = NewtonCreateCollisionFromSerialization (world, DeSerializeFile, file);
		fclose (file);
		return collision;

	#else 		
		return source;
	#endif
}


dVector ForceBetweenBody (NewtonBody* const body0, NewtonBody* const body1)
{
	dVector reactionforce (0.0f, 0.0f, 0.0f, 0.0f);
	for (NewtonJoint* joint = NewtonBodyGetFirstContactJoint(body0); joint; joint = NewtonBodyGetNextContactJoint(body0, joint)) {
		if ((NewtonJointGetBody0(joint) == body0) || (NewtonJointGetBody0(joint) == body1)) {
			for (void* contact = NewtonContactJointGetFirstContact (joint); contact; contact = NewtonContactJointGetNextContact (joint, contact)) {
				dVector point;
				dVector normal;	
				dVector contactForce;
				NewtonMaterial* const material = NewtonContactGetMaterial (contact);
				NewtonMaterialGetContactPositionAndNormal (material, body0, &point.m_x, &normal.m_x);
				NewtonMaterialGetContactForce(material, body0, &contactForce[0]);
				//forceAcc += normal.Scale (forceMag);
				reactionforce += contactForce;
			}
			break;
		}
	}
	return reactionforce;
}



static dFloat RayCastPlacement (const NewtonBody* body, const dFloat* normal, int collisionID, void* userData, dFloat intersetParam)
{
	dFloat* const paramPtr = (dFloat*)userData;
	if (intersetParam < paramPtr[0]) {
		paramPtr[0] = intersetParam;
	}
	return paramPtr[0];
}


//dFloat FindFloor (const NewtonWorld* world, dFloat x, dFloat z)
dVector FindFloor (const NewtonWorld* world, const dVector& origin, dFloat dist)
{
	dFloat parameter;

	//parameter = 1.2f;
	//float q0[] = {8.07129, -1.66341 - 1.5, 0};
	//float q1[] = {8.04629, -3.1684  - 1.5, 0};
	//NewtonWorldRayCast(world, q0, q1, RayCastPlacement, &parameter, NULL);



	// shot a vertical ray from a high altitude and collect the intersection parameter.
	dVector p0 (origin); 
	dVector p1 (origin - dVector (0.0f, dAbs (dist), 0.0f, 0.0f)); 

	parameter = 1.2f;
	NewtonWorldRayCast (world, &p0[0], &p1[0], RayCastPlacement, &parameter, NULL);
	//_ASSERTE (parameter < 1.0f);

	// the intersection is the interpolated value
	//return 1000.0f - 2000.0f * parameter;

	if (parameter < 1.0f) {
		p0 -= dVector (0.0f, dAbs (dist) * parameter, 0.0f, 0.0f);
	}
	return p0;
}



#if 0
#include "SceneManager.h"

#include "toolBox/MousePick.h"
#include "toolBox/OpenGlUtil.h"

#include "toolBox/RenderPrimitive.h"
#include "NewtonCustomJoint.h"






dFloat rollAngle = 0.0f;
dFloat yawAngle = 0.0f;

dVector cameraDir (1.0f, 0.0f, 0.0f, 0.0f);
dVector cameraEyepoint (-40.0f, 10.0f, 0.0f, 0.0f);

static int showIslans;


static unsigned ConvexCastCallback (const NewtonBody* body, const NewtonCollision* collision, void* userData)
{
	NewtonBody* me = (NewtonBody*) userData;
	return (me == body) ? 0 : 1;
}


void ConvexCastPlacement (NewtonBody* body)
{
	dFloat param;
	dMatrix matrix;
	NewtonWorld* world;
	NewtonCollision* collision;
	NewtonWorldConvexCastReturnInfo info[16];


	NewtonBodyGetMatrix (body, &matrix[0][0]);

	matrix.m_posit.m_y += 40.0f;
	dVector p (matrix.m_posit);
	p.m_y -= 80.0f;

	world = NewtonBodyGetWorld(body);
	collision = NewtonBodyGetCollision(body);
	NewtonWorldConvexCast (world, &matrix[0][0], &p[0], collision, &param, body, ConvexCastCallback, info, 16, 0);
	_ASSERTE (param < 1.0f);

	matrix.m_posit.m_y += (p.m_y - matrix.m_posit.m_y) * param;

	NewtonBodySetMatrix(body, &matrix[0][0]);
}





void SetShowContacts (SceneManager& me, int mode)
{
	showContacts = mode;
}






void ShowJointInfo(const NewtonCustomJoint* joint)
{
	NewtonJointRecord info;

	if (showContacts) {
		joint->GetInfo (&info);

		dMatrix bodyMatrix0;
		NewtonBodyGetMatrix (info.m_attachBody_0, &bodyMatrix0[0][0]);
		dMatrix matrix0 (*((dMatrix*) &info.m_attachmenMatrix_0[0]));
		matrix0 = matrix0 * bodyMatrix0;
		DebugDrawLine (matrix0.m_posit, matrix0.m_posit + matrix0.m_front, dVector (1, 0, 0));
		DebugDrawLine (matrix0.m_posit, matrix0.m_posit + matrix0.m_up, dVector (0, 1, 0));
		DebugDrawLine (matrix0.m_posit, matrix0.m_posit + matrix0.m_right, dVector (0, 0, 1));


		dMatrix bodyMatrix1;
		NewtonBodyGetMatrix (info.m_attachBody_1, &bodyMatrix1[0][0]);
		dMatrix matrix1 (*((dMatrix*) &info.m_attachmenMatrix_1[0]));
		matrix1 = matrix1 * bodyMatrix1;
		DebugDrawLine (matrix1.m_posit, matrix1.m_posit + matrix1.m_front, dVector (1, 0, 0));
		DebugDrawLine (matrix1.m_posit, matrix1.m_posit + matrix1.m_up,	   dVector (0, 1, 0));
		DebugDrawLine (matrix1.m_posit, matrix1.m_posit + matrix1.m_right, dVector (0, 0, 1));

	}

}


static void ShowMeshCollidingFaces (
	const NewtonBody* bodyWithTreeCollision, 
	const NewtonBody* body, 
	int faceID, 
	int vertexCount, 
	const dFloat* vertex, 
	int vertexstrideInBytes)
{

	// we are coping data to and array of memory, another call back may be doing the same thing
	// here fore we need to avoid race conditions
	NewtonWorldCriticalSectionLock (NewtonBodyGetWorld (bodyWithTreeCollision));

	dVector face[64];
	int stride = vertexstrideInBytes / sizeof (dFloat);
	for (int j = 0; j < vertexCount; j ++) {
		face [j] = dVector (vertex[j * stride + 0], vertex[j * stride + 1] , vertex[j * stride + 2]);
	}
	DebugDrawPolygon (vertexCount, face);

	// unlock the critical section
	NewtonWorldCriticalSectionUnlock (NewtonBodyGetWorld (bodyWithTreeCollision));
}


void SetShowMeshCollision (SceneManager& me, int mode)
{
	NewtonTreeCollisionCallback showFaceCallback;

	showFaceCallback = NULL;
	if (mode) {
		showFaceCallback = ShowMeshCollidingFaces;
	}

	// iterate the world
	for (const NewtonBody* body = NewtonWorldGetFirstBody (me.m_world); body; body = NewtonWorldGetNextBody (me.m_world, body)) {
		NewtonCollision* collision;
		NewtonCollisionInfoRecord info;

		collision = NewtonBodyGetCollision (body);
		NewtonCollisionGetInfo (collision, &info);

		switch (info.m_collisionType) 
		{
			case SERIALIZE_ID_TREE:
			case SERIALIZE_ID_SCENE:
			case SERIALIZE_ID_USERMESH:
			case SERIALIZE_ID_HEIGHTFIELD:
			{
				NewtonStaticCollisionSetDebugCallback (collision, showFaceCallback);
				break;
			}

			default: 
				break;
		}
	}
}


void SetShowIslands (SceneManager& me, int mode)
{
	showIslans = mode;
}


//static void CalculateAABB (const NewtonBody* body, dVector& minP, dVector& maxP)

int PhysicsIslandUpdate (const NewtonWorld* world, const void* islandHandle, int bodyCount)
{
	if (showIslans) {
		dVector minAABB ( 1.0e10f,  1.0e10f,  1.0e10f, 0.0f);
		dVector maxAABB (-1.0e10f, -1.0e10f, -1.0e10f, 0.0f);
		for (int i = 0; i < bodyCount; i ++) {
			dVector p0;
			dVector p1;

#if 0
			// show the engine loose aabb
			NewtonIslandGetBodyAABB (islandHandle, i, &p0[0], &p1[0]);
#else
			// calculate the shape aabb

			dMatrix matrix;
			NewtonBody* body;
			NewtonCollision *collision;

			body = NewtonIslandGetBody (islandHandle, i);
			collision = NewtonBodyGetCollision (body);
			NewtonBodyGetMatrix (body, &matrix[0][0]);
			CalculateAABB (collision, matrix, p0, p1);
#endif
			for (int j = 0; j < 3; j ++ ) {
				minAABB[j] = p0[j] < minAABB[j] ? p0[j] : minAABB[j];
				maxAABB[j] = p1[j] > maxAABB[j] ? p1[j] : maxAABB[j];
			}
		}
		DebugDrawAABB (minAABB, maxAABB);
	}

	//	g_activeBodies += bodyCount;
	return 1;
}

// this callback is called for every contact between the two bodies
//int GenericContactProcess (const NewtonMaterial* material, const NewtonBody* const body0, const NewtonBody* const body1, dFloat timestep, int threadIndex)


/*
void GenericContactProcess (const NewtonJoint* contactJoint, dFloat timestep, int threadIndex)
{
	int isHightField;
	NewtonBody* body;
	NewtonCollision* collision;
	NewtonCollisionInfoRecord info;

	isHightField = 1;
	body = NewtonJointGetBody0 (contactJoint);
	collision = NewtonBodyGetCollision(body);
	NewtonCollisionGetInfo(collision, &info);
	if (info.m_collisionType != SERIALIZE_ID_HEIGHTFIELD) {
		body = NewtonJointGetBody1 (contactJoint);
		collision = NewtonBodyGetCollision(body);
		NewtonCollisionGetInfo(collision, &info);
		isHightField  = (info.m_collisionType == SERIALIZE_ID_HEIGHTFIELD); 
	}

#define HOLE_INTERRAIN 10
	if (isHightField) {
		void* nextContact;
		for (void* contact = NewtonContactJointGetFirstContact (contactJoint); contact; contact = nextContact) {
			int faceID;
			NewtonMaterial* material;

			nextContact = NewtonContactJointGetNextContact (contactJoint, contact);

			material = NewtonContactGetMaterial (contact);
			faceID = NewtonMaterialGetContactFaceAttribute (material);
			if (faceID == HOLE_INTERRAIN) {
				NewtonContactJointRemoveContact (contactJoint, contact); 
			}
		}
	}
}
*/



void GetForceOnStaticBody (NewtonBody* body, NewtonBody* staticBody)
{
	for (NewtonJoint* joint = NewtonBodyGetFirstContactJoint (body); joint; joint = NewtonBodyGetNextContactJoint (body, joint)) {
		NewtonBody* body0;
		NewtonBody* body1;

		body0 = NewtonJointGetBody0(joint);
		body1 = NewtonJointGetBody1(joint);
		if ((body0 == staticBody) || (body1 == staticBody)) {

			for (void* contact = NewtonContactJointGetFirstContact (joint); contact; contact = NewtonContactJointGetNextContact (joint, contact)) {

				float forceMag;
				dVector point;
				dVector normal;	
				NewtonMaterial* material;

				material = NewtonContactGetMaterial (contact);
				
				NewtonMaterialGetContactForce (material, &forceMag);
				NewtonMaterialGetContactPositionAndNormal (material, &point.m_x, &normal.m_x);

				dVector force (normal.Scale (-forceMag));

				// do wherever you want withteh force
			}
		}
	}
}




static void ExtrudeFaces (void* userData, int vertexCount, const dFloat* faceVertec, int id)
{
	float OFFSET = 0.1f;
	float face[32][10];

	NewtonMesh* mesh = (NewtonMesh*) userData;

	// calculate the face normal
	dVector normal (0, 0, 0);
	dVector p0 (faceVertec[0 * 3 + 0], faceVertec[0 * 3 + 1], faceVertec[0 * 3 + 2]);
	dVector p1 (faceVertec[1 * 3 + 0], faceVertec[1 * 3 + 1], faceVertec[1 * 3 + 2]);

	dVector e0 (p1 - p0);
	for (int i = 2; i < vertexCount; i ++) {
		dVector p2 (faceVertec[i * 3 + 0], faceVertec[i * 3 + 1], faceVertec[i * 3 + 2]);
		dVector e1 (p2 - p0);

		normal += e0 * e1;
		e0 = e1;
	}
	normal = normal.Scale (1.0f / dSqrt (normal % normal));

	dVector displacemnet (normal.Scale (OFFSET));

	// add the face displace by some offset
	for (int i = 0; i < vertexCount; i ++) {
		dVector p1 (faceVertec[i * 3 + 0], faceVertec[i * 3 + 1], faceVertec[i * 3 + 2]);
		p1 += displacemnet;

		face[i][0] = p1.m_x; 
		face[i][1] = p1.m_y;  
		face[i][2] = p1.m_z;   

		face[i][3] = normal.m_x; 
		face[i][4] = normal.m_y;  
		face[i][5] = normal.m_z;  

		face[i][6] = 0.0f; 
		face[i][7] = 0.0f;  
		face[i][8] = 0.0f;  
		face[i][9] = 0.0f;  
	}
	
	// add the face
	NewtonMeshAddFace (mesh, vertexCount, &face[0][0], 10 * sizeof (float), id);


	// now add on face walk the perimeter and add a rivet face
	dVector q0 (faceVertec[(vertexCount - 1) * 3 + 0], faceVertec[(vertexCount - 1) * 3 + 1], faceVertec[(vertexCount - 1) * 3 + 2]);
	q0 += displacemnet;
	for (int i = 0; i < vertexCount; i ++) {
		dVector q1 (faceVertec[i * 3 + 0], faceVertec[i * 3 + 1], faceVertec[i * 3 + 2]);
		q1 += displacemnet;

		// calculate the river normal
		dVector edge (q1 - q0);
		dVector n (edge * normal);
		n = n.Scale (1.0f / sqrtf (n % n));

		// build a quad to serve a the face between the two parellel faces
		face[0][0] = q0.m_x; 
		face[0][1] = q0.m_y;  
		face[0][2] = q0.m_z;   
		face[0][3] = n.m_x; 
		face[0][4] = n.m_y;  
		face[0][5] = n.m_z;  
		face[0][6] = 0.0f; 
		face[0][7] = 0.0f;  
		face[0][8] = 0.0f;  
		face[0][9] = 0.0f;  

		face[1][0] = q1.m_x; 
		face[1][1] = q1.m_y;  
		face[1][2] = q1.m_z;   
		face[1][3] = n.m_x; 
		face[1][4] = n.m_y;  
		face[1][5] = n.m_z;  
		face[1][6] = 0.0f; 
		face[1][7] = 0.0f;  
		face[1][8] = 0.0f;  
		face[1][9] = 0.0f;  

		face[2][0] = q1.m_x - displacemnet.m_x; 
		face[2][1] = q1.m_y - displacemnet.m_y;  
		face[2][2] = q1.m_z - displacemnet.m_z;   
		face[2][3] = n.m_x; 
		face[2][4] = n.m_y;  
		face[2][5] = n.m_z;  
		face[2][6] = 0.0f; 
		face[2][7] = 0.0f;  
		face[2][8] = 0.0f;  
		face[2][9] = 0.0f;  

		face[3][0] = q0.m_x - displacemnet.m_x; 
		face[3][1] = q0.m_y - displacemnet.m_y;  
		face[3][2] = q0.m_z - displacemnet.m_z;   
		face[3][3] = n.m_x; 
		face[3][4] = n.m_y;  
		face[3][5] = n.m_z;  
		face[3][6] = 0.0f; 
		face[3][7] = 0.0f;  
		face[3][8] = 0.0f;  
		face[3][9] = 0.0f;  

		// save the first point for the next rivet
		q0 = q1;

		// add this face to the mesh
		NewtonMeshAddFace (mesh, 4, &face[0][0], 10 * sizeof (float), id);
	}
}


NewtonMesh* CreateCollisionTreeDoubleFaces (NewtonWorld* world, NewtonCollision* optimizedDoubelFacesTree)
{
	NewtonMesh* mesh = NewtonMeshCreate(world);
	dMatrix matrix (GetIdentityMatrix());

	NewtonMeshBeginFace(mesh);
	NewtonCollisionForEachPolygonDo (optimizedDoubelFacesTree, &matrix[0][0], ExtrudeFaces, mesh);	
	NewtonMeshEndFace(mesh);

	return mesh;
}

/*
// return the collision joint, if the body collide
NewtonJoint* CheckIfBodiesCollide (NewtonBody* body0, NewtonBody* body1)
{
	for (NewtonJoint* joint = NewtonBodyGetFirstContactJoint (body0); joint; joint = NewtonBodyGetNextContactJoint (body0, joint)) {
		if ((NewtonJointGetBody0(joint) == body1) || (NewtonJointGetBody1(joint) == body1)) {
			return joint;
		}
	}
	return NULL;
}

//to get teh collision points
void HandlecollisionPoints (NewtonJoint* contactjoint)
{
	NewtonBody* body0 = NewtonJointGetBody0(contactjoint);
	NewtonBody* body1 = NewtonJointGetBody1(contactjoint);
	for (void* contact = NewtonContactJointGetFirstContact (contactjoint); contact; contact = NewtonContactJointGetNextContact (contactjoint, contact)) {

		float forceMag;
		dVector point;
		dVector normal;	
		NewtonMaterial* material = NewtonContactGetMaterial (contact);

		// do whatever you want here

		//NewtonMaterialGetContactForce (material, &forceMag);
//		NewtonMaterialGetContactPositionAndNormal (material, &point.m_x, &normal.m_x);
		// do whatever you want with the force
	}
}


void GetContactOnBody (NewtonBody* body)
{
	for (NewtonJoint* joint = NewtonBodyGetFirstContactJoint (body); joint; joint = NewtonBodyGetNextContactJoint (body, joint)) {
		for (void* contact = NewtonContactJointGetFirstContact (joint); contact; contact = NewtonContactJointGetNextContact (joint, contact)) {

			float forceMag;
			dVector point;
			dVector normal;	
			NewtonBody* body0 = NewtonJointGetBody0(joint);
			NewtonBody* body1 = NewtonJointGetBody1(joint);
			NewtonMaterial* material = NewtonContactGetMaterial (contact);

			//NewtonMaterialGetContactForce (material, &forceMag);
			NewtonMaterialGetContactPositionAndNormal (material, &point.m_x, &normal.m_x);
			// do whatever you want with the force
		}
	}
}
*/

#endif

dFloat RandomVariable(dFloat amp)
{
	unsigned val;
	val = dRand() + dRand();
	return amp * (dFloat (val) / dFloat(dRAND_MAX) - 1.0f) * 0.5f;
}



// rigid body destructor
void  PhysicsBodyDestructor (const NewtonBody* body)
{
//	RenderPrimitive* primitive;

	// get the graphic object form the rigid body
//	primitive = (RenderPrimitive*) NewtonBodyGetUserData (body);

	// destroy the graphic object
	//	delete primitive;
}


// add force and torque to rigid body
void  PhysicsApplyGravityForce (const NewtonBody* body, dFloat timestep, int threadIndex)
{
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dFloat mass;


	NewtonBodyGetMassMatrix (body, &mass, &Ixx, &Iyy, &Izz);
//mass*= 0.0f;

	dVector force (0.0f, mass * DEMO_GRAVITY, 0.0f);
	NewtonBodySetForce (body, &force.m_x);
}


void GenericContactProcess (const NewtonJoint* contactJoint, dFloat timestep, int threadIndex)
{
#if 0 
	dFloat speed0;
	dFloat speed1;
	SpecialEffectStruct* currectEffect;

	// get the pointer to the special effect structure
	currectEffect = (SpecialEffectStruct *)NewtonMaterialGetMaterialPairUserData (material);

	// save the contact information
	NewtonMaterialGetContactPositionAndNormal (material, &currectEffect->m_position.m_x, &currectEffect->m_normal.m_x);
	NewtonMaterialGetContactTangentDirections (material, &currectEffect->m_tangentDir0.m_x, &currectEffect->m_tangentDir1.m_x);


	// Get the maximum normal speed of this impact. this can be used for positioning collision sound
	speed0 = NewtonMaterialGetContactNormalSpeed (material);
	if (speed0 > currectEffect->m_contactMaxNormalSpeed) {
		// save the position of the contact (for 3d sound of particles effects)
		currectEffect->m_contactMaxNormalSpeed = speed0;
	}

	// get the maximum of the two sliding contact speed
	speed0 = NewtonMaterialGetContactTangentSpeed (material, 0);
	speed1 = NewtonMaterialGetContactTangentSpeed (material, 1);
	if (speed1 > speed0) {
		speed0 = speed1;
	}

	// Get the maximum tangent speed of this contact. this can be used for particles(sparks) of playing scratch sounds 
	if (speed0 > currectEffect->m_contactMaxTangentSpeed) {
		// save the position of the contact (for 3d sound of particles effects)
		currectEffect->m_contactMaxTangentSpeed = speed0;
	}


#endif
	
	// read the table direction
//	dVector dir (tableDir);
//	dVector updir (TableDir);
//	NewtonBody* const body = NewtonJointGetBody0(contactJoint);
//	for (void* contact = NewtonContactJointGetFirstContact (contactJoint); contact; contact = NewtonContactJointGetNextContact (contactJoint, contact)) {
//		dFloat speed;
//		dVector point;
//		dVector normal;	
//		dVector dir0;	
//		dVector dir1;	
//		dVector force;
//		NewtonMaterial* material;
//
//		material = NewtonContactGetMaterial (contact);
//		NewtonMaterialGetContactPositionAndNormal (material, body, &point.m_x, &normal.m_x);
//
//		// if the normal is vertical is large the say 40 degrees
//		if (fabsf (normal % upDir) > 0.7f) {
//			// rotate the normal to be aligned with the table direction
//			NewtonMaterialContactRotateTangentDirections (material, dir);
//		}
//	}


	NewtonBody* const body = NewtonJointGetBody0(contactJoint);
	for (void* contact = NewtonContactJointGetFirstContact (contactJoint); contact; contact = NewtonContactJointGetNextContact (contactJoint, contact)) {
		dFloat speed;
		dVector point;
		dVector normal;	
		dVector dir0;	
		dVector dir1;	
		dVector force;
		NewtonMaterial* material;

		material = NewtonContactGetMaterial (contact);

		NewtonMaterialGetContactForce (material, body, &force.m_x);
		NewtonMaterialGetContactPositionAndNormal (material, body, &point.m_x, &normal.m_x);
		NewtonMaterialGetContactTangentDirections (material, body, &dir0.m_x, &dir1.m_x);
		speed = NewtonMaterialGetContactNormalSpeed(material);


		//speed = NewtonMaterialGetContactNormalSpeed(material);
		// play sound base of the contact speed.
		//
	}
}



NewtonCollision* CreateConvexCollision (NewtonWorld* world, const dMatrix& srcMatrix, const dVector& originalSize, PrimitiveType type, int materialID__)
{
	dMatrix matrix (srcMatrix);
	matrix.m_front = matrix.m_front.Scale (1.0f / dSqrt (matrix.m_front % matrix.m_front));
	matrix.m_right = matrix.m_front * matrix.m_up;
	matrix.m_right = matrix.m_right.Scale (1.0f / dSqrt (matrix.m_right % matrix.m_right));
	matrix.m_up = matrix.m_right * matrix.m_front;

	dVector size (originalSize);

	NewtonCollision* collision = NULL;

	

	switch (type) 
	{
		case _SPHERE_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateSphere (world, size.m_x * 0.5f, 0, NULL); 
			break;
		}

		case _BOX_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateBox (world, size.m_x, size.m_y, size.m_z, 0, NULL); 
			break;
		}


		case _CONE_PRIMITIVE:
		{
			dFloat r = size.m_x * 0.5f;
			dFloat h = size.m_y;

			// create the collision 
			collision = NewtonCreateCone (world, r, h, 0, NULL); 
			break;
		}

		case _CYLINDER_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateCylinder (world, size.m_x * 0.5f, size.m_y, 0, NULL); 
			break;
		}


		case _CAPSULE_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateCapsule (world, size.m_x * 0.5f, size.m_y, 0, NULL); 
			break;
		}

		case _TAPERED_CAPSULE_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateTaperedCapsule (world, size.m_x * 0.5f, size.m_z * 0.5f, size.m_y, 0, NULL); 
			break;
		}


		case _CHAMFER_CYLINDER_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateChamferCylinder (world, size.m_x * 0.5f, size.m_y, 0, NULL); 
			break;
		}

		case _TAPERED_CYLINDER_PRIMITIVE:
		{
			// create the collision 
			collision = NewtonCreateTaperedCylinder (world, size.m_x * 0.5f, size.m_z * 0.5f, size.m_y, 0, NULL); 
			break;
		}


		case _RANDOM_CONVEX_HULL_PRIMITIVE:
		{
			// Create a clouds of random point around the origin
			#define SAMPLE_COUNT 5000
			dVector cloud [SAMPLE_COUNT];

			// make sure that at least the top and bottom are present
			cloud [0] = dVector ( size.m_x * 0.5f, -0.3f,  0.0f);
			cloud [1] = dVector ( size.m_x * 0.5f,  0.3f,  0.3f);
			cloud [2] = dVector ( size.m_x * 0.5f,  0.3f, -0.3f);
			cloud [3] = dVector (-size.m_x * 0.5f, -0.3f,  0.0f);
			cloud [4] = dVector (-size.m_x * 0.5f,  0.3f,  0.3f);
			cloud [5] = dVector (-size.m_x * 0.5f,  0.3f, -0.3f);
			int count = 5;

			// populate the cloud with pseudo Gaussian random points
			for (int i = 6; i < SAMPLE_COUNT; i ++) {
				cloud [i].m_x = RandomVariable(size.m_x);
				cloud [i].m_y = RandomVariable(size.m_y);
				cloud [i].m_z = RandomVariable(size.m_z);
				count ++;
			}
			collision = NewtonCreateConvexHull (world, count, &cloud[0].m_x, sizeof (dVector), 0.01f, 0, NULL); 
			break;
		}

		case _REGULAR_CONVEX_HULL_PRIMITIVE:
		{
			// Create a clouds of random point around the origin
			#define STEPS_HULL 6
			//#define STEPS_HULL 3

			dVector cloud [STEPS_HULL * 4 + 256];
			int count = 0;
			dFloat radius = size.m_y;
			dFloat height = size.m_x * 0.999f;
			dFloat x = - height * 0.5f;
			dMatrix rotation (dPitchMatrix(2.0f * 3.141592f / STEPS_HULL));
			for (int i = 0; i < 4; i ++) {
				dFloat pad;
				pad = ((i == 1) || (i == 2)) * 0.25f * radius;
				dVector p (x, 0.0f, radius + pad);
				x += 0.3333f * height;
				dMatrix acc (GetIdentityMatrix());
				for (int j = 0; j < STEPS_HULL; j ++) {
					cloud[count] = acc.RotateVector(p);
					acc = acc * rotation;
					count ++;
				}
			}

			collision = NewtonCreateConvexHull (world, count, &cloud[0].m_x, sizeof (dVector), 0.02f, 0, NULL); 
			break;
		}

		default: _ASSERTE (0);
	}

	// test the serialization
	//collision = TestSerialization ("convexShape.bin", collision, world);

	return collision;
}


NewtonBody* CreateSimpleSolid (DemoEntityManager* const scene, DemoMesh* const mesh, dFloat mass, const dMatrix& matrix, NewtonCollision* const collision, int materialId)
{
	_ASSERTE (collision);
	_ASSERTE (mesh);

	// add an new entity to the world
	DemoEntity* const entity = new DemoEntity(matrix, NULL);
	scene->Append (entity);
	entity->SetMesh(mesh);

	// calculate the moment of inertia and the relative center of mass of the solid
	dVector origin;
	dVector inertia;
	NewtonConvexCollisionCalculateInertialMatrix (collision, &inertia[0], &origin[0]);	

	dFloat Ixx = mass * inertia[0];
	dFloat Iyy = mass * inertia[1];
	dFloat Izz = mass * inertia[2];

	//create the rigid body
	NewtonWorld* const world = scene->GetNewton();
	NewtonBody* const rigidBody = NewtonCreateBody (world, collision, &matrix[0][0]);

	// set the correct center of gravity for this body
	NewtonBodySetCentreOfMass (rigidBody, &origin[0]);

	// set the mass matrix
	NewtonBodySetMassMatrix (rigidBody, mass, Ixx, Iyy, Izz);

	// activate 
	//	NewtonBodyCoriolisForcesMode (blockBoxBody, 1);

	// save the pointer to the graphic object with the body.
	NewtonBodySetUserData (rigidBody, entity);

	// assign the wood id
	NewtonBodySetMaterialGroupID (rigidBody, materialId);

	//  set continue collision mode
	//	NewtonBodySetContinuousCollisionMode (rigidBody, continueCollisionMode);

	// set a destructor for this rigid body
	NewtonBodySetDestructorCallback (rigidBody, PhysicsBodyDestructor);

	// set the transform call back function
	NewtonBodySetTransformCallback (rigidBody, DemoEntity::TransformCallback);

	// set the force and torque call back function
	NewtonBodySetForceAndTorqueCallback (rigidBody, PhysicsApplyGravityForce);

	// set the matrix for both the rigid body and the graphic body
	//NewtonBodySetMatrix (rigidBody, &matrix[0][0]);
	//PhysicsSetTransform (rigidBody, &matrix[0][0], 0);

	//dVector xxx (0, -9.8f * mass, 0.0f, 0.0f);
	//NewtonBodySetForce (rigidBody, &xxx[0]);

	// force the body to be active of inactive
	//	NewtonBodySetAutoSleep (rigidBody, sleepMode);
	return rigidBody;
}



void AddPrimitiveArray (DemoEntityManager* const scene, dFloat mass, const dVector& origin, const dVector& size, int xCount, int zCount, dFloat spacing, PrimitiveType type, int materialID)
{
	dMatrix matrix (GetIdentityMatrix());

	// create the shape and visual mesh as a common data to be re used
	NewtonWorld* const world = scene->GetNewton();
	NewtonCollision* const collision = CreateConvexCollision (world, GetIdentityMatrix(), size, type, materialID);

//	DemoMesh* const geometry = new DemoMesh("cylinder_1", collision, "wood_0.tga", "wood_0.tga", "wood_1.tga");
	DemoMesh* const geometry = new DemoMesh("cylinder_1", collision, "smilli.tga", "smilli.tga", "smilli.tga");

	dFloat startElevation = 1000.0f;

matrix = dRollMatrix(3.141592f/2.0f);

	for (int i = 0; i < xCount; i ++) {
		dFloat x = origin.m_x + (i - xCount / 2) * spacing;
		for (int j = 0; j < zCount; j ++) {
			dFloat z = origin.m_z + (j - zCount / 2) * spacing;

			matrix.m_posit.m_x = x;
			matrix.m_posit.m_z = z;
//			dVector floor (FindFloor (world, dVector (matrix.m_posit.m_x, startElevation, matrix.m_posit.m_z, 0.0f), startElevation + 100.0f));
			dVector floor (FindFloor (world, dVector (matrix.m_posit.m_x, startElevation, matrix.m_posit.m_z, 0.0f), 2.0f * startElevation));
			matrix.m_posit.m_y = floor.m_y + 2.f;
//if (i == 0 && j == 0)
			CreateSimpleSolid (scene, geometry, mass, matrix, collision, materialID);
		}
	}
	// do not forget to release the assets	
	geometry->Release(); 
	NewtonDestroyCollision (collision);
}


void CalculateAABB (const NewtonCollision* const collision, const dMatrix& matrix, dVector& minP, dVector& maxP)
{
	//	NewtonCollision *collision;

	// show an exact AABB
	//	collision = NewtonBodyGetCollision (body) ;

	//	dMatrix matrix;
	//	NewtonBodyGetMatrix (body, &matrix[0][0]);

	for (int i = 0; i < 3; i ++) {
		dVector support;
		dVector dir (0.0f, 0.0f, 0.0f, 0.0f);
		dir[i] = 1.0f;

		dVector localDir (matrix.UnrotateVector (dir));
		NewtonCollisionSupportVertex (collision, &localDir[0], &support[0]);
		support = matrix.TransformVector (support);
		maxP[i] = support[i];  

		localDir = localDir.Scale (-1.0f);
		NewtonCollisionSupportVertex (collision, &localDir[0], &support[0]);
		support = matrix.TransformVector (support);
		minP[i] = support[i];  
	}
}


void SetAutoSleepMode (NewtonWorld* const world, int mode)
{
	mode = mode ? 0 : 1;
	for (const NewtonBody* body = NewtonWorldGetFirstBody (world); body; body = NewtonWorldGetNextBody (world, body)) {
		NewtonBodySetAutoSleep (body, mode);
	}
}



static NewtonBody* CreateLevelMeshBody (NewtonWorld* const world, DemoEntity* const ent, bool optimization)
{
	// measure the time to build a collision tree
	unsigned64 timer0 = dGetTimeInMicrosenconds();

	// create the collision tree geometry
	NewtonCollision* collision = NewtonCreateTreeCollision(world, 0);


	// set the application level callback
#ifdef USE_STATIC_MESHES_DEBUG_COLLISION
	//	NewtonStaticCollisionSetDebugCallback (collision, DebugCallback);
#endif

	// prepare to create collision geometry
	NewtonTreeCollisionBeginBuild(collision);


	int faceCount = 0;
	// iterate the entire geometry an build the collision
	for (DemoEntity* model = ent->GetFirst(); model; model = model->GetNext()) {

		dMatrix matrix (model->CalculateGlobalMatrix(ent));
		DemoMesh* const mesh = model->GetMesh();
		dFloat* const vertex = mesh->m_vertex;
		for (DemoMesh::dListNode* nodes = mesh->GetFirst(); nodes; nodes = nodes->GetNext()) {
			DemoSubMesh& segment = nodes->GetInfo();
			for (int i = 0; i < segment.m_indexCount; i += 3) {
				int index;	
				dVector face[3];

				index = segment.m_indexes[i + 0] * 3;
				face[0] = dVector (vertex[index + 0], vertex[index + 1], vertex[index + 2]);

				index = segment.m_indexes[i + 1] * 3;
				face[1] = dVector (vertex[index + 0], vertex[index + 1], vertex[index + 2]);

				index = segment.m_indexes[i + 2] * 3;
				face[2] = dVector (vertex[index + 0], vertex[index + 1], vertex[index + 2]);

				matrix.TransformTriplex (&face[0].m_x, sizeof (dVector), &face[0].m_x, sizeof (dVector), 3);

				// use material ids as physics materails 
				int matID = segment.m_textureHandle;
				faceCount ++;
				NewtonTreeCollisionAddFace(collision, 3, &face[0].m_x, sizeof (dVector), matID);
			}
		}
	}
	NewtonTreeCollisionEndBuild(collision, optimization ? 1 : 0);


	// test the serialization
	collision = TestSerialization ("treeCollision.bin", collision, world);


	// measure the time to build a collision tree
	timer0 = (dGetTimeInMicrosenconds() - timer0) / 1000;


	// Get the root Matrix
	dMatrix matrix (ent->CalculateGlobalMatrix(NULL));

	// create the level rigid body
	NewtonBody* const level = NewtonCreateBody(world, collision, &matrix[0][0]);

	// set the global position of this body
	//NewtonBodySetMatrix (level, &matrix[0][0]); 

	// save the pointer to the graphic object with the body.
	NewtonBodySetUserData (level, ent);

	// set a destructor for this rigid body
	//NewtonBodySetDestructorCallback (m_level, Destructor);




	// release the collision tree (this way the application does not have to do book keeping of Newton objects
	NewtonDestroyCollision (collision);

	return level;
}


NewtonBody* CreateLevelMesh (DemoEntityManager* const scene, const char* const name, bool optimized)
{
	// load the scene from and alchemedia file format
	char fileName[2048];
	GetWorkingFileName (name, fileName);
	scene->LoadScene (fileName);

	NewtonBody* levelBody = NULL;
	NewtonWorld* const world = scene->GetNewton();
	//for (DemoEntityManager::dListNode* node = scene->GetFirst(); node; node = node->GetNext()) {
	for (DemoEntityManager::dListNode* node = scene->GetLast(); node; node = node->GetPrev()) {
		DemoEntity* const ent = node->GetInfo();
		DemoMesh* const mesh = ent->GetMesh();
		if (mesh) {
			char name[2048];
			mesh->GetName(name);
			if (!strcmp (name, "levelGeometry_mesh")) {
				levelBody = CreateLevelMeshBody (world, ent, optimized);
				break;
			}
		}
	}
	return levelBody;
}


class MakeViualMesh: public dScene::dSceneExportCallback
{
	public: 
	MakeViualMesh (NewtonWorld* const world)
		:m_world (world)
	{
	}

	NewtonMesh* CreateVisualMesh (NewtonBody* const body, char* const name, int maxNameSize) const
	{
		// here the use should take the user data from the body create newtonMesh form it and return that back
		NewtonCollision* collision = NewtonBodyGetCollision(body);
		NewtonMesh* const mesh = NewtonMeshCreateFromCollision(collision);

		sprintf (name, "visual Mesh");
		return mesh;
	}

	NewtonWorld* m_world;
};

void ExportScene (NewtonWorld* const world, const char* const fileName)
{
	MakeViualMesh context (world);
	dScene testScene (world);
	testScene.NewtonWorldToScene (world, &context);
	testScene.Serialize (fileName);
}
