#!/usr/bin/env python3
"""Generate the capture-scoped VRage.Physics.Native forwarding wrapper."""

import json
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).parent.parent
CAPTURES = Path.home() / "Documents/se2-native-wrappers/Playback/TestData"
OUTPUT = ROOT / "src/Physics.cpp"
MANIFEST = ROOT / "src/Physics.exports"

TYPE_MAP = {
    "void": "void",
    "ptr": "void *",
    "fnptr": "void *",
    "str": "const char *",
    "i1": "int8_t",
    "u1": "uint8_t",
    "i4": "int32_t",
    "u4": "uint32_t",
    "u8": "uint64_t",
    "r4": "float",
    "bool1": "uint8_t",
    "bool4": "int32_t",
}
STRUCT_TYPES = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}

SET_EXPAND = "?SetExpandStub@HkBuffer@@SAXP6APEAXP6APEAXPEAU1@PEAXH@Z01H@Z@Z"
HK_INIT = "?HkInit@HkMainSystem@@SAXAEBUHkMainCinfo@1@@Z"
HK_QUIT = "?HkQuit@HkMainSystem@@SAXXZ"
SET_DELETION = "?SetColliderDeletionCallback@@YAXP6AXPEBVhknpShape@@@Z@Z"
CREATE_DEBUG_DRAW = "?CreateDebugDrawSystem@HkSession@@QEAAPEAVHkDebugProcessManager@@AEBUDebugDrawImplementationTable@@PEAX@Z"
CREATE_CHARACTER = "?Create@HkCharacterController@@SAPEAV1@AEBUHkCharacterControllerCinfo@@@Z"
SESSION_CTOR = "??0HkSession@@QEAA@AEBUHkSessionCinfo@0@@Z"
SESSION_DTOR = "??1HkSession@@QEAA@XZ"
ALLOCATE_WORLD = "?AllocateWorld@HkSession@@QEAAAEAVhknpWorld@@_NAEBVhkVector4d@@1H@Z"
RELEASE_WORLD = "?ReleaseWorld@HkSession@@QEAAXAEAVhknpWorld@@@Z"
SUBSCRIBE_MUTATION = "?SubscribeToMutation@@YAXPEBVhknpShape@@PEAX@Z"
UNSUBSCRIBE_MUTATION = "?UnsubscribeFromMutation@@YAXPEBVhknpShape@@PEAX@Z"
TOGGLE_GRAVITY = "?ToggleGravity@HkRagdoll@@QEBAX_N@Z"
REMOVE_RAGDOLL_FROM_WORLD = "?RemoveFromWorld@HkRagdoll@@QEAAXW4ActivationMode@hknpWorldWriter@@@Z"
SET_RAGDOLL_ENTITY_LAYER = "?SetEntityLayerIndex@HkRagdoll@@QEAAXH@Z"
GET_RAGDOLL_ROOT_TRANSFORM = "?GetRootBodyTransform@HkRagdoll@@QEBA?AUHkPackedWorldTransform@@XZ"
SET_CHARACTER_BODY_INFO = "?SetBodyInfo@HkCharacterController@@QEAAXAEBUBodyInfoPackf@@M_N@Z"
REMOVE_BODIES = "?removeBodies@hknpWorld@@UEAAXPEBUhknpBodyId@@HW4ActivationMode@hknpWorldWriter@@@Z"
DESTROY_BODIES = "?destroyBodies@hknpWorld@@UEAAXPEBUhknpBodyId@@HW4ActivationMode@hknpWorldWriter@@@Z"
GET_BODY_CONSTRAINT_IDS = "?GetBodyConstraintIds@HkConstraintHandler@@QEAAXAEAVhknpWorld@@UhknpBodyId@@AEAU?$HkBufferReference@UhknpConstraintId@@@@@Z"
DESTROY_BODY_CONSTRAINTS = "?DestroyBodyConstraints@HkConstraintHandler@@QEAAXAEAVhknpWorld@@UhknpBodyId@@@Z"
GET_CHILD = "?GetChild@@YA_NPEBVhknpShape@@IPEAPEBV1@@Z"
GET_SHAPE_CHUNK = "?GetShapeChunk@HkSimdTreeGridShape@@QEBA?AUVector3I@@I@Z"
CLONE_SHAPE_SHALLOW = "?cloneShapeShallow@HkBreakableCompoundShapeManipulator@@SAPEAXPEBX@Z"
DISJOINT_EDGE = "?disjointEdge@HkBreakableCompoundShapeManipulator@@SAXPEAXH@Z"
DISJOINT_NODE_EDGES = "?disjointNodeEdges@HkBreakableCompoundShapeManipulator@@SAXPEAXU?$hkHandle@I$0PPPPPPPP@UhknpShapeInstanceIdDiscriminant@@@@@Z"
DISJOINT_SETS = "?disjointSets@HkBreakableCompoundShapeManipulator@@SAPEAXPEAX@Z"
DISPOSE_SETS = "?disposeSets@HkBreakableCompoundShapeManipulator@@SAXPEAX@Z"
REBUILD_MASS_PROPERTIES_TREE = "?rebuildMassPropertiesTree@HkBreakableCompoundShapeManipulator@@SAXPEAVhknpCompoundShape@@@Z"
HACK_BREAKABLE_FUNCTIONS = "?hackBreakableCompoundShapeFunctions@HkBreakableCompoundShapeManipulator@@SAXXZ"
UNREGISTER_SHAPE = "?UnregisterShape@HkMaterialLibrary@@QEAAXPEAVhknpShape@@@Z"
SET_BODIES_MOTION_TYPE = "?SetBodiesMotionType@HkSession@@QEAA?AUhknpMotionId@@AEAVhknpWorld@@PEAUhknpBodyId@@HPEAUhknpMotionProperties@@W4Enum@hknpMotionType@@@Z"
GET_MOTION = "?getMotion@hknpWorld@@UEBAAEBVhknpMotion@@UhknpMotionId@@@Z"
SET_PACKED_MOTION_MASS_PROPERTIES = "?SetMotionMassProperties@HkWorld@@SAXAEAVhknpMotion@@AEBUhkDiagonalizedMassProperties@@AEBUHkPackedWorldTransform@@@Z"
UPDATE_MOTION_TRANSFORMS = "?UpdateMotionTransforms@HkWorld@@SAXAEAVhknpWorld@@AEBVhknpMotion@@@Z"
SET_BODY_TO_MOTION_TRANSFORM = "?SetBodyToMotionTransform@HkWorld@@SAXAEAVhknpWorld@@UhknpBodyId@@AEAUhkQTransform@@W4ActivationMode@hknpWorldWriter@@@Z"
CONTAINS_CHUNK = "?ContainsChunk@HkSimdTreeGridShape@@QEBA_NUVector3I@@@Z"
IS_SHAPE_INDEX_VALID = "?IsShapeIndexValid@HkSimdTreeGridShape@@QEBA_NI@Z"
GET_CHILD_SHAPE = "?GetChildShape@HkSimdTreeGridShape@@QEBAPEBVhknpShape@@I@Z"
IS_BODY_ADDED = "?isBodyAdded@hknpWorld@@UEBA_NUhknpBodyId@@@Z"
GET_BODY_MOTION_ID = "?GetBodyMotionId@HkWorld@@SA?AUhknpMotionId@@AEAVhknpWorld@@UhknpBodyId@@@Z"
CLEAR_MOTION_GRAVITY = "?ClearMotionGravity@HkGravityModifier@@QEAAXUhknpMotionId@@@Z"
REMOVE_INSTANCES = "?RemoveInstances@@YAXPEAVhknpCompoundShape@@AEBV?$hkArrayView@$$CBI@@@Z"
GET_FLOAT32 = "?getFloat32@hkHalf16@@QEBAMXZ"
DISABLE_BODY_FLAGS = "?disableBodyFlags@hknpWorld@@UEAAXUhknpBodyId@@UhknpCollisionFlags@@W4ActivationMode@hknpWorldWriter@@W4UpdateCachesMode@5@@Z"
NEAREST_POINTS_TO_POINT = "?NearestPointsToPoint@HkColliderQueryDispatcher@@SAXAEBUHkColliderNearestPointsArgs@1@AEAU?$HkBufferReference@UHkColliderNearestQueryHit@HkColliderQueryDispatcher@@@@@Z"
QUERY_NEAREST_POINTS_TO_POINT = "?NearestPointsToPoint@HkQueryDispatcher@@SAXAEBUHkNearestPointsToPointArgs@1@AEAU?$HkBufferReference@UHkNearestQueryHit@HkQueryDispatcher@@@@@Z"
QUERY_AABB = "?QueryAabb@HkQueryDispatcher@@SAXAEBUHkQueryAabbArgs@1@AEAU?$HkBufferReference@UhknpBodyId@@@@@Z"
COLLIDER_QUERY_AABB = "?QueryAabb@HkColliderQueryDispatcher@@SAXAEBUHkColliderQueryAABBArgs@1@AEAU?$HkBufferReference@UvrShapeKey@@@@@Z"
MUTATE_BOX = "?MutateBox@@YAXPEAVhknpBoxShape@@AEBUvrBoxParameters@@@Z"
FIND_LEAF = "?FindLeaf@@YA_NPEBVhknpShape@@IPEAPEBV1@PEAUHkTriangleOrQuad@@PEAG@Z"
ALLOCATE_CALLBACK_VELOCITY_CONSTRAINT_MOTOR = "?AllocateCallbackVelocityConstraintMotor@HkConstraintHandler@@SAPEAVvrCallbackVelocityConstraintMotor@@MMMMM@Z"
ALLOCATE_LIMITED_HINGE_CONSTRAINT_DATA = "?AllocateLimitedHingeConstraintData@HkConstraintHandler@@SAPEAVhknpLimitedHingeConstraintData@@AEBUHkSingleAxisConstraintArgs@1@@Z"
ALLOCATE_PRISMATIC_CONSTRAINT_DATA = "?AllocatePrismaticConstraintData@HkConstraintHandler@@SAPEAVhknpPrismaticConstraintData@@AEBUHkSingleAxisConstraintArgs@1@@Z"
SET_ANGLE = "?SetAngle@HkConstraintHandler@@SAXPEBVhknpConstraint@@M@Z"
SET_MOTOR_ENABLED_LIMITED_HINGE = "?SetMotorEnabled@HkConstraintHandler@@SAXAEAVhknpLimitedHingeConstraintData@@_N@Z"
SET_MOTOR_ENABLED_PRISMATIC = "?SetMotorEnabled@HkConstraintHandler@@SAXAEAVhknpPrismaticConstraintData@@_N@Z"
SET_MOTOR_ENABLED_WHEEL = "?SetMotorEnabled@HkConstraintHandler@@SAXAEAVhknpWheelConstraintData@@_N@Z"
GET_ANGLE = "?GetAngle@HkConstraintHandler@@SAMPEBVhknpConstraint@@@Z"
SET_MOTOR_TARGET_ANGLE_LIMITED_HINGE = "?setMotorTargetAngle@hknpLimitedHingeConstraintData@@QEAAXM@Z"
SET_MAX_ANGULAR_LIMIT_LIMITED_HINGE = "?setMaxAngularLimit@hknpLimitedHingeConstraintData@@QEAAXM@Z"
SET_MIN_LINEAR_LIMIT_PRISMATIC = "?setMinLinearLimit@hknpPrismaticConstraintData@@QEAAXM@Z"
SET_MAX_LINEAR_LIMIT_PRISMATIC = "?setMaxLinearLimit@hknpPrismaticConstraintData@@QEAAXM@Z"
SET_VELOCITY_TARGET_CALLBACK_MOTOR = "?setVelocityTarget@vrCallbackVelocityConstraintMotor@@QEAAXM@Z"
SET_MOTOR_TARGET_POSITION_PRISMATIC = "?setMotorTargetPosition@hknpPrismaticConstraintData@@QEAAXM@Z"
SET_BODY_NOT_TO_COLLIDE_WITH = "?setBodyNotToCollideWith@@YAXPEAVhknpCompoundShape@@IUhknpBodyId@@@Z"
SET_SHAPE_AT_LOD = "?setShapeAtLod@hknpLodShape@@QEAA?AUhkResult@@W4hknpLevelOfDetail@@PEBVhknpShape@@@Z"
ALLOCATE_POSITION_CONSTRAINT_MOTOR = "?AllocatePositionConstraintMotor@HkConstraintHandler@@SAPEAVhknpPositionConstraintMotor@@MMMMMM@Z"
ALLOCATE_VELOCITY_CONSTRAINT_MOTOR = "?AllocateVelocityConstraintMotor@HkConstraintHandler@@SAPEAVhknpVelocityConstraintMotor@@MMMMM@Z"
ALLOCATE_WHEEL_CONSTRAINT_DATA = "?AllocateWheelConstraintData@HkConstraintHandler@@SAPEAVhknpWheelConstraintData@@AEBUHkWheelConstraintArgs@1@@Z"
UPDATE_INSTANCES = "?UpdateInstances@@YAXPEAVhknpCompoundShape@@AEBV?$hkArrayView@$$CBI@@AEBV?$hkArrayView@$$CBUHkPackedCompoundShapeInstance@@@@@Z"
UPDATE_SIMD_TREE_SHAPES = "?UpdateShapes@HkSimdTreeGridShape@@QEAAXPEBIPEAPEBVhknpShape@@H@Z"
SET_MANIFOLD_INERTIA_MULTIPLIER = "?set@VrManifoldInertiaMultiplier@@SAXPEAVhknpWorld@@UhknpBodyId@@AEBU1@@Z"
SET_WHEEL_FRICTION_ENABLED = "?setFrictionEnabled@hknpWheelConstraintData@@QEAAX_N@Z"
SET_WHEEL_STEERING_ANGLE = "?setSteeringAngle@hknpWheelConstraintData@@QEAAXM@Z"
SET_BODY_QUALITY = "?setBodyQuality@hknpWorld@@UEAAXUhknpBodyId@@UhknpBodyQualityId@@W4ActivationMode@hknpWorldWriter@@W4UpdateCachesMode@5@@Z"
SET_WHEEL_SUSPENSION_DAMPING = "?setSuspensionDampingFactor@hknpWheelConstraintData@@QEAAXM@Z"
SET_WHEEL_SUSPENSION_STRENGTH = "?setSuspensionStrengthFactor@hknpWheelConstraintData@@QEAAXM@Z"
SET_WHEEL_MAX_FRICTION_TORQUE = "?setMaxFrictionTorque@hknpWheelConstraintData@@QEAAXM@Z"


def load_signatures():
    signatures = {}
    paths = [CAPTURES / stamp / "VRage.Physics.Native.jsonl"
             for stamp in ("20260718-135130", "20260718-135240", "20260718-135348")]
    if len(paths) != 3:
        raise SystemExit(f"expected 3 Physics captures, found {len(paths)}")
    for path in paths:
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                if '"ph":"sig"' not in line:
                    continue
                record = json.loads(line)
                signature = {"name": record["fn"], "ret": record["ret"], "params": record["params"]}
                previous = signatures.setdefault(record["ep"], signature)
                if previous != signature:
                    raise SystemExit(f"conflicting signature for {record['ep']}")
    if len(signatures) != 153:
        raise SystemExit(f"expected 153 Physics exports, found {len(signatures)}")
    signatures[REMOVE_BODIES] = {
        "name": "removeBodies",
        "ret": {"kind": "void"},
        "params": [
            {"kind": "ptr", "n": "instance"},
            {"kind": "ptr", "n": "bodyIds"},
            {"kind": "i4", "n": "numBodyIds"},
            {"kind": "i4", "n": "activationMode"},
        ],
    }
    signatures[DESTROY_BODIES] = {
        "name": "destroyBodies",
        "ret": {"kind": "void"},
        "params": [
            {"kind": "ptr", "n": "instance"},
            {"kind": "ptr", "n": "bodyIds"},
            {"kind": "i4", "n": "numBodyIds"},
            {"kind": "i4", "n": "activationMode"},
        ],
    }
    signatures[GET_BODY_CONSTRAINT_IDS] = {
        "name": "GetBodyConstraintIds",
        "ret": {"kind": "void"},
        "params": [
            {"kind": "ptr", "n": "instance"},
            {"kind": "ptr", "n": "world"},
            {"kind": "u8", "n": "body"},
            {"kind": "ptr", "n": "constraintIdsOut"},
        ],
    }
    signatures[DESTROY_BODY_CONSTRAINTS] = {
        "name": "DestroyBodyConstraints",
        "ret": {"kind": "void"},
        "params": [
            {"kind": "ptr", "n": "instance"},
            {"kind": "ptr", "n": "world"},
            {"kind": "u8", "n": "body"},
        ],
    }
    signatures[GET_CHILD] = {
        "name": "GetChild",
        "ret": {"kind": "bool1"},
        "params": [
            {"kind": "ptr", "n": "compositeCollider"},
            {"kind": "u4", "n": "index"},
            {"kind": "ptr", "n": "child"},
        ],
    }
    signatures[GET_SHAPE_CHUNK] = {
        "name": "GetShapeChunk",
        "ret": {"kind": "void"},
        "params": [
            {"kind": "ptr", "n": "instance"},
            {"kind": "ptr", "n": "returnValue"},
            {"kind": "u4", "n": "shapeId"},
        ],
    }
    signatures[CLONE_SHAPE_SHALLOW] = {
        "name": "CloneShapeShallow",
        "ret": {"kind": "ptr"},
        "params": [{"kind": "ptr", "n": "shape"}],
    }
    signatures[DISJOINT_EDGE] = {"name": "DisjointEdge", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "i4", "n": "edgeId"}]}
    signatures[DISJOINT_NODE_EDGES] = {"name": "DisjointNodeEdges", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "u4", "n": "nodeId"}]}
    signatures[DISJOINT_SETS] = {"name": "DisjointSets", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "shape"}]}
    signatures[DISPOSE_SETS] = {"name": "DisposeSets", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "sets"}]}
    signatures[REBUILD_MASS_PROPERTIES_TREE] = {"name": "RebuildMassPropertiesTree", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "compoundShape"}]}
    signatures[HACK_BREAKABLE_FUNCTIONS] = {"name": "HackBreakableCompoundShapeFunctions", "ret": {"kind": "void"}, "params": []}
    signatures[UNREGISTER_SHAPE] = {"name": "UnregisterShape", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "shape"}]}
    signatures[SET_BODIES_MOTION_TYPE] = {"name": "SetBodiesMotionType", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "returnValue"}, {"kind": "ptr", "n": "world"}, {"kind": "ptr", "n": "bodyIds"}, {"kind": "i4", "n": "count"}, {"kind": "ptr", "n": "properties"}, {"kind": "i4", "n": "motionType"}]}
    signatures[GET_MOTION] = {"name": "GetMotion", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u4", "n": "motionId"}]}
    signatures[SET_PACKED_MOTION_MASS_PROPERTIES] = {"name": "SetPackedMotionMassProperties", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "motion"}, {"kind": "ptr", "n": "massProperties"}, {"kind": "ptr", "n": "worldTransform"}]}
    signatures[UPDATE_MOTION_TRANSFORMS] = {"name": "UpdateMotionTransforms", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "world"}, {"kind": "ptr", "n": "motion"}]}
    signatures[SET_BODY_TO_MOTION_TRANSFORM] = {"name": "SetBodyToMotionTransform", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "world"}, {"kind": "u8", "n": "bodyId"}, {"kind": "ptr", "n": "transform"}, {"kind": "i4", "n": "activationMode"}]}
    signatures[CONTAINS_CHUNK] = {"name": "ContainsChunk", "ret": {"kind": "bool1"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "chunkPos"}]}
    signatures[IS_SHAPE_INDEX_VALID] = {"name": "IsShapeIndexValid", "ret": {"kind": "bool1"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u4", "n": "index"}]}
    signatures[GET_CHILD_SHAPE] = {"name": "GetChildShape", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u4", "n": "leafId"}]}
    signatures[REMOVE_RAGDOLL_FROM_WORLD] = {"name": "RemoveFromWorld", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "i4", "n": "activationMode"}]}
    signatures[SET_RAGDOLL_ENTITY_LAYER] = {"name": "SetRagdollEntityLayer", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "i4", "n": "entityLayerIndex"}]}
    signatures[GET_RAGDOLL_ROOT_TRANSFORM] = {"name": "GetRagdollRootTransform", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "returnValue"}]}
    signatures[SESSION_DTOR] = {"name": "HkSession_dtor", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}]}
    signatures[IS_BODY_ADDED] = {"name": "isBodyAdded", "ret": {"kind": "bool1"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u8", "n": "bodyId"}]}
    signatures[GET_BODY_MOTION_ID] = {"name": "GetBodyMotionId", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "returnValue"}, {"kind": "ptr", "n": "world"}, {"kind": "u8", "n": "bodyId"}]}
    signatures[CLEAR_MOTION_GRAVITY] = {"name": "ClearMotionGravity", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u4", "n": "motionId"}]}
    signatures[REMOVE_INSTANCES] = {"name": "RemoveInstances", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "ptr", "n": "instancesToRemove"}]}
    signatures[GET_FLOAT32] = {"name": "GetFloat32", "ret": {"kind": "r4"}, "params": [{"kind": "ptr", "n": "instance"}]}
    signatures[DISABLE_BODY_FLAGS] = {"name": "disableBodyFlags", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u8", "n": "bodyId"}, {"kind": "u8", "n": "flagsToDisable"}, {"kind": "i4", "n": "activationMode"}, {"kind": "i4", "n": "cacheBehavior"}]}
    signatures[NEAREST_POINTS_TO_POINT] = {"name": "NearestPointsToPoint", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "args"}, {"kind": "ptr", "n": "outHits"}]}
    signatures[QUERY_NEAREST_POINTS_TO_POINT] = {"name": "HkQueryDispatcher_NearestPointsToPoint", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "args"}, {"kind": "ptr", "n": "outHits"}]}
    signatures[QUERY_AABB] = {"name": "QueryAabb", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "args"}, {"kind": "ptr", "n": "outHits"}]}
    signatures[COLLIDER_QUERY_AABB] = {"name": "QueryAabb", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "args"}, {"kind": "ptr", "n": "outHits"}]}
    signatures[MUTATE_BOX] = {"name": "MutateBox", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "ptr", "n": "parameters"}]}
    signatures[FIND_LEAF] = {"name": "FindLeaf", "ret": {"kind": "bool1"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "u4", "n": "shapeKey"}, {"kind": "ptr", "n": "leafShape"}, {"kind": "ptr", "n": "triangleOrQuad"}, {"kind": "ptr", "n": "shapeTag"}]}
    signatures[ALLOCATE_CALLBACK_VELOCITY_CONSTRAINT_MOTOR] = {"name": "AllocateCallbackVelocityConstraintMotor", "ret": {"kind": "ptr"}, "params": [{"kind": "r4", "n": "tau"}, {"kind": "r4", "n": "damping"}, {"kind": "r4", "n": "velocityTarget"}, {"kind": "r4", "n": "minForce"}, {"kind": "r4", "n": "maxForce"}]}
    signatures[ALLOCATE_LIMITED_HINGE_CONSTRAINT_DATA] = {"name": "AllocateLimitedHingeConstraintData", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "args"}]}
    signatures[ALLOCATE_PRISMATIC_CONSTRAINT_DATA] = {"name": "AllocatePrismaticConstraintData", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "args"}]}
    signatures[SET_ANGLE] = {"name": "SetAngle", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "constraint"}, {"kind": "r4", "n": "angle"}]}
    signatures[SET_MOTOR_ENABLED_LIMITED_HINGE] = {"name": "SetMotorEnabled", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "data"}, {"kind": "bool1", "n": "enabled"}]}
    signatures[SET_MOTOR_ENABLED_PRISMATIC] = {"name": "SetMotorEnabledPrismatic", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "data"}, {"kind": "bool1", "n": "enabled"}]}
    signatures[SET_MOTOR_ENABLED_WHEEL] = {"name": "SetMotorEnabledWheel", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "data"}, {"kind": "bool1", "n": "enabled"}]}
    signatures[GET_ANGLE] = {"name": "GetAngle", "ret": {"kind": "r4"}, "params": [{"kind": "ptr", "n": "constraint"}]}
    signatures[SET_MOTOR_TARGET_ANGLE_LIMITED_HINGE] = {"name": "SetMotorTargetAngle", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "angle"}]}
    signatures[SET_MAX_ANGULAR_LIMIT_LIMITED_HINGE] = {"name": "SetMaxAngularLimit", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "angle"}]}
    signatures[SET_MIN_LINEAR_LIMIT_PRISMATIC] = {"name": "SetMinLinearLimit", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "limit"}]}
    signatures[SET_MAX_LINEAR_LIMIT_PRISMATIC] = {"name": "SetMaxLinearLimit", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "limit"}]}
    signatures[SET_VELOCITY_TARGET_CALLBACK_MOTOR] = {"name": "SetCallbackMotorVelocityTarget", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "velocity"}]}
    signatures[SET_MOTOR_TARGET_POSITION_PRISMATIC] = {"name": "SetMotorTargetPosition", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "position"}]}
    signatures[SET_BODY_NOT_TO_COLLIDE_WITH] = {"name": "SetBodyNotToCollideWith", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "u4", "n": "instanceId"}, {"kind": "u8", "n": "bodyNotToCollideWith"}]}
    signatures[SET_SHAPE_AT_LOD] = {"name": "SetShapeAtLod", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "returnValue"}, {"kind": "u1", "n": "level"}, {"kind": "ptr", "n": "shape"}]}
    signatures[ALLOCATE_POSITION_CONSTRAINT_MOTOR] = {"name": "AllocatePositionConstraintMotor", "ret": {"kind": "ptr"}, "params": [{"kind": "r4", "n": "tau"}, {"kind": "r4", "n": "damping"}, {"kind": "r4", "n": "minForce"}, {"kind": "r4", "n": "maxForce"}, {"kind": "r4", "n": "proportionalRecoveryVelocity"}, {"kind": "r4", "n": "constantRecoveryVelocity"}]}
    signatures[ALLOCATE_VELOCITY_CONSTRAINT_MOTOR] = {"name": "AllocateVelocityConstraintMotor", "ret": {"kind": "ptr"}, "params": [{"kind": "r4", "n": "tau"}, {"kind": "r4", "n": "damping"}, {"kind": "r4", "n": "velocityTarget"}, {"kind": "r4", "n": "minForce"}, {"kind": "r4", "n": "maxForce"}]}
    signatures[ALLOCATE_WHEEL_CONSTRAINT_DATA] = {"name": "AllocateWheelConstraintData", "ret": {"kind": "ptr"}, "params": [{"kind": "ptr", "n": "args"}]}
    signatures[UPDATE_INSTANCES] = {"name": "UpdateInstances", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "shape"}, {"kind": "ptr", "n": "instancesToUpdate"}, {"kind": "ptr", "n": "packedInstances"}]}
    signatures[UPDATE_SIMD_TREE_SHAPES] = {"name": "UpdateSimdTreeShapes", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "ptr", "n": "shapeIds"}, {"kind": "ptr", "n": "shapes"}, {"kind": "i4", "n": "count"}]}
    signatures[SET_MANIFOLD_INERTIA_MULTIPLIER] = {"name": "SetManifoldInertiaMultiplier", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "world"}, {"kind": "u8", "n": "body"}, {"kind": "ptr", "n": "multiplier"}]}
    signatures[SET_WHEEL_FRICTION_ENABLED] = {"name": "SetWheelFrictionEnabled", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "bool1", "n": "enabled"}]}
    signatures[SET_WHEEL_STEERING_ANGLE] = {"name": "SetWheelSteeringAngle", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "angle"}]}
    signatures[SET_BODY_QUALITY] = {"name": "SetBodyQuality", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "u8", "n": "bodyId"}, {"kind": "u1", "n": "qualityId"}, {"kind": "i4", "n": "activationMode"}, {"kind": "i4", "n": "cacheBehavior"}]}
    signatures[SET_WHEEL_SUSPENSION_DAMPING] = {"name": "SetWheelSuspensionDamping", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "damping"}]}
    signatures[SET_WHEEL_SUSPENSION_STRENGTH] = {"name": "SetWheelSuspensionStrength", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "strength"}]}
    signatures[SET_WHEEL_MAX_FRICTION_TORQUE] = {"name": "SetWheelMaxFrictionTorque", "ret": {"kind": "void"}, "params": [{"kind": "ptr", "n": "instance"}, {"kind": "r4", "n": "torque"}]}
    signatures[HK_QUIT] = {"name": "HkQuit", "ret": {"kind": "void"}, "params": []}
    signatures = sorted(signatures.items())
    counts = Counter(signature["name"] for _, signature in signatures)
    generated = []
    for entry_point, signature in signatures:
        name = identifier(signature["name"])
        if counts[signature["name"]] > 1:
            name = f"{identifier(owner(entry_point))}_{name}"
        generated.append(name)
    generated_counts = Counter(generated)
    used = set()
    for (entry_point, signature), name in zip(signatures, generated):
        if generated_counts[name] > 1:
            params = [param for param in parameter_names(signature) if param != "instance"]
            name = f"{name}_{'_'.join(params) or 'void'}"
        if name in used:
            raise SystemExit(f"duplicate generated name {name} for {entry_point}")
        signature["cpp_name"] = name
        used.add(name)
    return signatures


def identifier(name):
    name = re.sub(r"[^A-Za-z0-9_]", "_", name).strip("_")
    return name if name and not name[0].isdigit() else f"function_{name}"


def owner(entry_point):
    match = re.match(r"\?\?[01]([^@]+)@@", entry_point)
    if match is None:
        match = re.match(r"\?[^@]+@([^@]+)@@", entry_point)
    return match.group(1) if match else "Global"


def parameter_names(signature):
    reserved = {"alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
                "char", "class", "const", "constexpr", "continue", "default", "delete", "do",
                "double", "else", "enum", "explicit", "extern", "false", "float", "for", "friend",
                "goto", "if", "inline", "int", "long", "namespace", "new", "noexcept", "not",
                "nullptr", "operator", "or", "private", "protected", "public", "register", "return",
                "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw",
                "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
                "volatile", "while"}
    names = []
    for index, param in enumerate(signature["params"]):
        name = identifier(param.get("n", f"arg{index}").lstrip("_"))
        if name in reserved:
            name += "_value"
        names.append(name)
    return names


def cpp_type(value):
    if value["kind"] == "struct":
        if value.get("cls") != ["int"] or value.get("size") not in STRUCT_TYPES:
            raise SystemExit(f"unsupported struct ABI: {value}")
        return STRUCT_TYPES[value["size"]]
    try:
        return TYPE_MAP[value["kind"]]
    except KeyError:
        raise SystemExit(f"unsupported ABI kind: {value['kind']}") from None


def emit(signatures):
    lines = [
        "// Generated by tools/generate_physics_wrapper.py. Do not edit.",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <cstdlib>",
        "#include <cstring>",
        "#include <mutex>",
        "#include <stdexcept>",
        "#include <string>",
        "#include <unordered_map>",
        "",
        '#include "dll_loader.h"',
        "",
        "namespace {",
        "pe_image physics_image;",
        "std::mutex physics_mutex;",
        "std::string physics_path;",
        "",
        "void *sysv_expand_callback;",
        "void *sysv_log_callback;",
        "void *sysv_debug_callback;",
        "void *sysv_deletion_callback;",
        "void *sysv_mutation_callback;",
        "",
        "void *WINAPI expand_bridge(void *resize, void *buffer, void *current_data, int requested_size)",
        "{",
        "    using Function = void *(*)(void *, void *, void *, int);",
        "    return reinterpret_cast<Function>(sysv_expand_callback)(resize, buffer, current_data, requested_size);",
        "}",
        "",
        "void WINAPI log_bridge(uint32_t message_id, uint8_t level, void *message, void *context)",
        "{",
        "    using Function = void (*)(uint32_t, uint8_t, void *, void *);",
        "    reinterpret_cast<Function>(sysv_log_callback)(message_id, level, message, context);",
        "}",
        "",
        "void WINAPI debug_bridge(void *message, void *context)",
        "{",
        "    using Function = void (*)(void *, void *);",
        "    reinterpret_cast<Function>(sysv_debug_callback)(message, context);",
        "}",
        "",
        "void WINAPI deletion_bridge(void *shape)",
        "{",
        "    using Function = void (*)(void *);",
        "    reinterpret_cast<Function>(sysv_deletion_callback)(shape);",
        "}",
        "",
        "void WINAPI debug_draw_bridge() {}",
        "",
        "struct ContactVector4 { float x, y, z, w; };",
        "static_assert(sizeof(ContactVector4) == 16);",
        "using ContactImpulseCallback = void (*)(uint64_t, uint64_t, void *, float *, void *, uint16_t, uint16_t, ContactVector4, uint32_t, uint32_t);",
        "struct ContactRoute { void *session; ContactImpulseCallback callback; };",
        "std::mutex contact_mutex;",
        "std::unordered_map<void *, ContactImpulseCallback> contact_callbacks;",
        "std::unordered_map<void *, ContactRoute> contact_routes;",
        "",
        "void WINAPI contact_impulse_bridge(uint64_t body_a, uint64_t body_b, void *manifold, float *impulses, void *world, uint16_t material_a, uint16_t material_b, ContactVector4 projected_velocities, uint32_t shape_key_a, uint32_t shape_key_b)",
        "{",
        "    ContactImpulseCallback callback = nullptr;",
        "    {",
        "        std::lock_guard<std::mutex> lock(contact_mutex);",
        "        auto route = contact_routes.find(world);",
        "        if (route != contact_routes.end())",
        "            callback = route->second.callback;",
        "    }",
        "    if (callback)",
        "        callback(body_a, body_b, manifold, impulses, world, material_a, material_b, projected_velocities, shape_key_a, shape_key_b);",
        "}",
        "",
        "void *character_original_vtable[20];",
        "void *character_vtable[20];",
        "std::once_flag character_vtable_once;",
        "",
        "template<std::size_t Index, typename Return, typename... Args>",
        "Return character_call(void *instance, Args... args)",
        "{",
        "    using Function = Return(WINAPI *)(void *, Args...);",
        "    return reinterpret_cast<Function>(character_original_vtable[Index])(instance, args...);",
        "}",
        "",
        "void character_dtor(void *i) { character_call<0, void>(i, 0); }",
        "void character_set_support_distance(void *i, float v) { character_call<1, void>(i, v); }",
        "float character_get_support_distance(void *i) { return character_call<2, float>(i); }",
        "void character_set_sticky_factor(void *i, float v) { character_call<3, void>(i, v); }",
        "float character_get_sticky_factor(void *i) { return character_call<4, float>(i); }",
        "void character_set_transform(void *i, void *v) { character_call<5, void>(i, v); }",
        "void *character_get_transform(void *i) { return character_call<6, void *>(i); }",
        "void character_set_velocities(void *i, void *v) { character_call<7, void>(i, v); }",
        "void character_get_velocities(void *i, void *v) { character_call<8, void>(i, v); }",
        "void character_add_to_world(void *i, int32_t a, int32_t b) { character_call<9, void>(i, a, b); }",
        "void character_remove_from_world(void *i, int32_t v) { character_call<10, void>(i, v); }",
        "void character_set_floating(void *i, uint8_t v) { character_call<11, void>(i, v); }",
        "void character_set_shape(void *i, void *v) { character_call<12, void>(i, v); }",
        "void character_get_support_contacts(void *i, void *v) { character_call<13, void>(i, v); }",
        "void character_get_body(void *i, void *v) { character_call<14, void>(i, v); }",
        "void character_detach_from_body(void *i) { character_call<15, void>(i); }",
        "void character_migrate(void *i, void *w, uint64_t id) { character_call<16, void>(i, w, id); }",
        "void character_pre_migrate(void *i) { character_call<17, void>(i); }",
        "void character_post_migrate(void *i, void *v) { character_call<18, void>(i, v); }",
        "",
        "void bridge_character_vtable(void *instance)",
        "{",
        "    std::call_once(character_vtable_once, [instance] {",
        "        void **original = *reinterpret_cast<void ***>(instance);",
        "        std::memcpy(character_original_vtable, original, sizeof(character_original_vtable));",
        "        std::memcpy(character_vtable, original, sizeof(character_vtable));",
        "        void *bridges[] = { reinterpret_cast<void *>(&character_dtor), reinterpret_cast<void *>(&character_set_support_distance), reinterpret_cast<void *>(&character_get_support_distance), reinterpret_cast<void *>(&character_set_sticky_factor), reinterpret_cast<void *>(&character_get_sticky_factor), reinterpret_cast<void *>(&character_set_transform), reinterpret_cast<void *>(&character_get_transform), reinterpret_cast<void *>(&character_set_velocities), reinterpret_cast<void *>(&character_get_velocities), reinterpret_cast<void *>(&character_add_to_world), reinterpret_cast<void *>(&character_remove_from_world), reinterpret_cast<void *>(&character_set_floating), reinterpret_cast<void *>(&character_set_shape), reinterpret_cast<void *>(&character_get_support_contacts), reinterpret_cast<void *>(&character_get_body), reinterpret_cast<void *>(&character_detach_from_body), reinterpret_cast<void *>(&character_migrate), reinterpret_cast<void *>(&character_pre_migrate), reinterpret_cast<void *>(&character_post_migrate) };",
        "        std::memcpy(character_vtable, bridges, sizeof(bridges));",
        "    });",
        "    *reinterpret_cast<void ***>(instance) = character_vtable;",
        "}",
        "",
        "void WINAPI mutation_bridge(void *shape, uint8_t flags)",
        "{",
        "    using Function = void (*)(void *, uint8_t);",
        "    reinterpret_cast<Function>(sysv_mutation_callback)(shape, flags);",
        "}",
        "",
        "void initialize(const char *dll_path, const char *sidecar_path);",
        "",
        "void ensure_thread_info()",
        "{",
        "    initialize(nullptr, nullptr);",
        "    if (!setup_nt_threadinfo(nullptr))",
        "        std::abort();",
        "    pe_ensure_tls_for_loaded_images();",
        "}",
        "",
    ]

    for _, signature in signatures:
        ret = cpp_type(signature["ret"])
        params = ", ".join(cpp_type(param) for param in signature["params"]) or "void"
        name = signature["cpp_name"]
        lines.append(f"using {name}_t = {ret}(WINAPI *)({params});")
        lines.append(f"{name}_t p{name};")

    lines += [
        "",
        "void initialize(const char *dll_path, const char *sidecar_path)",
        "{",
        "    std::lock_guard<std::mutex> lock(physics_mutex);",
        "    if (physics_image.image)",
        "        return;",
        "",
        "    if (dll_path) {",
        "        physics_path = dll_path;",
        "    } else {",
        "        // Playback loads exports directly; deployed wrappers receive the path through Init.",
        '        const char *home = std::getenv("HOME");',
        "        if (!home)",
        '            throw std::runtime_error("HOME is not set; call Init with the Physics DLL path");',
        '        physics_path = std::string(home) + "/Documents/SpaceEngineers2/Game2/VRage.Physics.Native.dll";',
        "    }",
        "",
        "    if (!load_dll(&physics_image, physics_path.c_str(), sidecar_path))",
        '        throw std::runtime_error("Failed to load VRage.Physics.Native.dll");',
    ]
    for entry_point, signature in signatures:
        name = signature["cpp_name"]
        lines += [
            f'    p{name} = reinterpret_cast<{name}_t>(get_export("{entry_point}"));',
            f"    if (!p{name})",
            f'        throw std::runtime_error("Missing Physics export: {entry_point}");',
        ]
    lines += ["}", "}", "", 'extern "C" {', "", "void Init(const char *dll_path, const char *sidecar_path)", "{", "    initialize(dll_path, sidecar_path);", "}", ""]

    manifest = []
    for entry_point, signature in signatures:
        ret = cpp_type(signature["ret"])
        name = signature["cpp_name"]
        names = parameter_names(signature)
        declarations = ", ".join(f"{cpp_type(param)} {param_name}" for param, param_name in zip(signature["params"], names)) or "void"
        arguments = ", ".join(names)
        placeholder = entry_point.replace("@", "$")
        if placeholder != entry_point:
            manifest.append(f"{placeholder}\t{entry_point}")
        lines += [
            f'{ret} {name}({declarations}) __asm__("\\\"{placeholder}\\\"");',
            f"{ret} {name}({declarations})",
            "{",
            "    ensure_thread_info();",
        ]
        first = names[0] if names else None
        if entry_point == SESSION_CTOR:
            lines += [
                "    alignas(void *) uint8_t cinfo[96];",
                f"    std::memcpy(cinfo, {names[1]}, sizeof(cinfo));",
                "    void *callback = *reinterpret_cast<void **>(cinfo);",
                "    if (callback)",
                "        *reinterpret_cast<void **>(cinfo) = reinterpret_cast<void *>(&contact_impulse_bridge);",
                f"    {names[1]} = cinfo;",
            ]
        elif entry_point == CREATE_DEBUG_DRAW:
            lines += [
                "    void *callbacks[7];",
                "    std::memcpy(callbacks, table, sizeof(callbacks));",
                "    for (void *&callback : callbacks)",
                "        callback = reinterpret_cast<void *>(&debug_draw_bridge);",
                "    table = callbacks;",
            ]
        elif entry_point in {SUBSCRIBE_MUTATION, UNSUBSCRIBE_MUTATION}:
            lines += [
                f"    sysv_mutation_callback = {names[1]};",
                f"    {names[1]} = {names[1]} ? reinterpret_cast<void *>(&mutation_bridge) : nullptr;",
            ]
        elif entry_point == SET_CHARACTER_BODY_INFO:
            lines.append(f"    *reinterpret_cast<void ***>({first}) = character_original_vtable;")
        elif entry_point == SET_EXPAND:
            lines += [f"    sysv_expand_callback = {first};", f"    {first} = reinterpret_cast<void *>(&expand_bridge);"]
        elif entry_point == SET_DELETION:
            lines += [f"    sysv_deletion_callback = {first};", f"    {first} = reinterpret_cast<void *>(&deletion_bridge);"]
        elif entry_point == HK_INIT:
            lines += [
                "    uint8_t cinfo[88];",
                f"    std::memcpy(cinfo, {first}, sizeof(cinfo));",
                "    sysv_log_callback = *reinterpret_cast<void **>(cinfo);",
                "    sysv_debug_callback = *reinterpret_cast<void **>(cinfo + 8);",
                "    *reinterpret_cast<void **>(cinfo) = reinterpret_cast<void *>(&log_bridge);",
                "    *reinterpret_cast<void **>(cinfo + 8) = reinterpret_cast<void *>(&debug_bridge);",
                f"    {first} = cinfo;",
            ]
        call = f"p{name}({arguments})"
        if entry_point == SESSION_CTOR:
            lines += [
                f"    void *result = {call};",
                "    if (result && callback) {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                "        contact_callbacks[result] = reinterpret_cast<ContactImpulseCallback>(callback);",
                "    }",
                "    return result;",
            ]
        elif entry_point == ALLOCATE_WORLD:
            lines += [
                f"    void *result = {call};",
                "    if (result) {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                f"        auto callback = contact_callbacks.find({first});",
                "        if (callback != contact_callbacks.end())",
                f"            contact_routes[result] = {{ {first}, callback->second }};",
                "    }",
                "    return result;",
            ]
        elif entry_point == RELEASE_WORLD:
            lines += [
                "    {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                f"        contact_routes.erase({names[1]});",
                "    }",
                f"    {call};",
            ]
        elif entry_point == SESSION_DTOR:
            lines += [
                "    {",
                "        std::lock_guard<std::mutex> lock(contact_mutex);",
                "        for (auto route = contact_routes.begin(); route != contact_routes.end();) {",
                f"            if (route->second.session == {first})",
                "                route = contact_routes.erase(route);",
                "            else",
                "                ++route;",
                "        }",
                f"        contact_callbacks.erase({first});",
                "    }",
                f"    {call};",
            ]
        elif entry_point == CREATE_CHARACTER:
            lines += [
                f"    void *result = {call};",
                "    if (result)",
                "        bridge_character_vtable(result);",
                "    return result;",
            ]
        else:
            lines.append(f"    {call};" if ret == "void" else f"    return {call};")
        lines += ["}", ""]
    lines += ["}", ""]
    return "\n".join(lines), "\n".join(manifest) + "\n"


source, manifest = emit(load_signatures())
OUTPUT.write_text(source, encoding="utf-8")
MANIFEST.write_text(manifest, encoding="utf-8")
