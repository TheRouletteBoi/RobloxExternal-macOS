#pragma once

#include <mach/mach.h>

namespace roblox::offsets {
// DataModel / Game
inline int WHAT_GAME_POINTS_TO = 0x524a4d0;
inline int PLACEID = 0x5cb3c58;
inline int DATAMODEL_JOBID = 0x138;

// Instance base
inline int INSTANCE_SELF = 0x8;
inline int INSTANCE_CLASS_INFO = 0x18;
inline int INSTANCE_PARENT = 0x68;
inline int INSTANCE_CHILDREN = 0x70;
inline int INSTANCE_NAME = 0xb0;

// Camera
inline int CAMERA_CFRAME = 0xf0;
inline int CAMERA_CAMERASUBJECT = 0xe0;
inline int CAMERA_FIELDOFVIEW = 0x158;

// Player
inline int PLAYER_CHARACTER = 0x338;
inline int PLAYER_TEAM = 0x248;
inline int PLAYER_DISPLAYNAME = 0x118;
inline int PLAYER_LAST_INPUT_TIMESTAMP = 0xb70;

// Players service
inline int PLAYERS_MAXPLAYERS = 0x130;

// Humanoid
inline int HUMANOID_DISPLAYNAME = 0xd0;
inline int HUMANOID_HEALTH = 0x184;
inline int HUMANOID_HIPHEIGHT = 0x190;
inline int HUMANOID_WALKSPEED = 0x1cc;
inline int HUMANOID_SEATPART = 0x110;

// BasePart
inline int BASEPART_PROPERTIES = 0x138;
inline int BASEPART_COLOR = 0x184;

// BasePart properties (relative to BASEPART_PROPERTIES pointer)
inline int BASEPART_PROPS_CFRAME = 0xc0;
inline int BASEPART_PROPS_RECEIVEAGE = 0xbc;
inline int BASEPART_PROPS_VELOCITY = 0xf0;
inline int BASEPART_PROPS_ROTVELOCITY = 0xfc;
inline int BASEPART_PROPS_SIZE = 0x1b0;
inline int BASEPART_PROPS_CANCOLLIDE = 0x1ae;

// Model
inline int MODEL_PRIMARYPART = 0x238;

// Team
inline int TEAM_BRICKCOLOR = 0xd0;

// Value objects
inline int INTVALUE_VALUE = 0xd0;
inline int STRINGVALUE_VALUE = 0xd0;
inline int CFRAMEVALUE_VALUE = 0xd0;

} // namespace roblox::offsets
