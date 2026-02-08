#pragma once

#include <cstdint>

namespace offsets {

    namespace DataModel {
        inline uintptr_t DATAMODEL_JOBID = 0x138;
        inline uintptr_t DATAMODEL_PLACEID = 0x188;
    }

    namespace Instance {
        inline uintptr_t INSTANCE_SELF = 0x8;
        inline uintptr_t INSTANCE_CLASS_INFO = 0x18;
        inline uintptr_t INSTANCE_PARENT = 0x68;
        inline uintptr_t INSTANCE_CHILDREN = 0x70;
        inline uintptr_t INSTANCE_NAME = 0xb0;
    }

    namespace Camera {
        inline uintptr_t CAMERA_CFRAME = 0xf0;
        inline uintptr_t CAMERA_CAMERASUBJECT = 0xe0;
        inline uintptr_t CAMERA_FIELDOFVIEW = 0x158;
    }

    namespace Player {
        inline uintptr_t PLAYER_CHARACTER = 0x338;
        inline uintptr_t PLAYER_TEAM = 0x248;
        inline uintptr_t PLAYER_DISPLAYNAME = 0x118;
        inline uintptr_t PLAYER_LAST_INPUT_TIMESTAMP = 0xb70;
        inline uintptr_t PLAYER_USERID = 0x270;
        inline uintptr_t PLAYER_ACCOUNTAGE = 0x2c4;
    }

    namespace Players {
        inline uintptr_t PLAYERS_MAXPLAYERS = 0x124;
        inline uintptr_t PLAYERS_LOCALPLAYER = 0x120;
    }

    namespace Humanoid {
        inline uintptr_t HUMANOID_DISPLAYNAME = 0xd0;
        inline uintptr_t HUMANOID_HEALTH = 0x184;
        inline uintptr_t HUMANOID_MAXHEALTH = 0x1ac;
        inline uintptr_t HUMANOID_HIPHEIGHT = 0x190;
        inline uintptr_t HUMANOID_WALKSPEED = 0x1cc;
        inline uintptr_t HUMANOID_SEATPART = 0x110;
        inline uintptr_t HUMANOID_JUMPPOWER = 0x1a0;
        inline uintptr_t HUMANOID_JUMPHEIGHT = 0x19c;
    }

    namespace BasePart {
        inline uintptr_t BASEPART_PROPERTIES = 0x138;
        inline uintptr_t BASEPART_COLOR = 0x184;
        inline uintptr_t BASEPART_TRANSPARENCY = 0xf0;
    }

    namespace Primitive {
        inline uintptr_t BASEPART_PROPS_CFRAME = 0xc0;
        inline uintptr_t BASEPART_PROPS_POSITION = 0xe4;
        inline uintptr_t BASEPART_PROPS_RECEIVEAGE = 0xbc;
        inline uintptr_t BASEPART_PROPS_VELOCITY = 0xf0;
        inline uintptr_t BASEPART_PROPS_ROTVELOCITY = 0xfc;
        inline uintptr_t BASEPART_PROPS_SIZE = 0x1b0;
        inline uintptr_t BASEPART_PROPS_CANCOLLIDE = 0x1ae;
    }

    namespace ModelPrimative {
        inline uintptr_t MODEL_PRIMARYPART = 0x238;
    }

    namespace Team {
        inline uintptr_t TEAM_BRICKCOLOR = 0xd0;
    }

    namespace ValueObjects {
        inline uintptr_t INTVALUE_VALUE = 0xd0;
        inline uintptr_t STRINGVALUE_VALUE = 0xd0;
        inline uintptr_t CFRAMEVALUE_VALUE = 0xd0;
    }

    namespace Workspace {
        inline uintptr_t WORKSPACE_CURRENTCAMERA = 0x418;
    }

} // namespace offsets