#pragma once

#include <cstdint>

namespace Compiler
{
    namespace Ai
    {
        const std::int32_t opcodeAiTravel = 0x20000;
        const std::int32_t opcodeAiTravelExplicit = 0x20001;
        const std::int32_t opcodeAiEscort = 0x20002;
        const std::int32_t opcodeAiEscortExplicit = 0x20003;
        const std::int32_t opcodeGetAiPackageDone = 0x200007c;
        const std::int32_t opcodeGetAiPackageDoneExplicit = 0x200007d;
        const std::int32_t opcodeGetCurrentAiPackage = 0x20001ef;
        const std::int32_t opcodeGetCurrentAiPackageExplicit = 0x20001f0;
        const std::int32_t opcodeGetDetected = 0x20001f1;
        const std::int32_t opcodeGetDetectedExplicit = 0x20001f2;
        const std::int32_t opcodeAiWander = 0x20010;
        const std::int32_t opcodeAiWanderExplicit = 0x20011;
        const std::int32_t opcodeAIActivate = 0x2001e;
        const std::int32_t opcodeAIActivateExplicit = 0x2001f;
        const std::int32_t opcodeAiEscortCell = 0x20020;
        const std::int32_t opcodeAiEscortCellExplicit = 0x20021;
        const std::int32_t opcodeAiFollow = 0x20022;
        const std::int32_t opcodeAiFollowExplicit = 0x20023;
        const std::int32_t opcodeAiFollowCell = 0x20024;
        const std::int32_t opcodeAiFollowCellExplicit = 0x20025;
        const std::int32_t opcodeSetHello = 0x200015c;
        const std::int32_t opcodeSetHelloExplicit = 0x200015d;
        const std::int32_t opcodeSetFight = 0x200015e;
        const std::int32_t opcodeSetFightExplicit = 0x200015f;
        const std::int32_t opcodeSetFlee = 0x2000160;
        const std::int32_t opcodeSetFleeExplicit = 0x2000161;
        const std::int32_t opcodeSetAlarm = 0x2000162;
        const std::int32_t opcodeSetAlarmExplicit = 0x2000163;
        const std::int32_t opcodeModHello = 0x20001b7;
        const std::int32_t opcodeModHelloExplicit = 0x20001b8;
        const std::int32_t opcodeModFight = 0x20001b9;
        const std::int32_t opcodeModFightExplicit = 0x20001ba;
        const std::int32_t opcodeModFlee = 0x20001bb;
        const std::int32_t opcodeModFleeExplicit = 0x20001bc;
        const std::int32_t opcodeModAlarm = 0x20001bd;
        const std::int32_t opcodeModAlarmExplicit = 0x20001be;
        const std::int32_t opcodeGetHello = 0x20001bf;
        const std::int32_t opcodeGetHelloExplicit = 0x20001c0;
        const std::int32_t opcodeGetFight = 0x20001c1;
        const std::int32_t opcodeGetFightExplicit = 0x20001c2;
        const std::int32_t opcodeGetFlee = 0x20001c3;
        const std::int32_t opcodeGetFleeExplicit = 0x20001c4;
        const std::int32_t opcodeGetAlarm = 0x20001c5;
        const std::int32_t opcodeGetAlarmExplicit = 0x20001c6;
        const std::int32_t opcodeGetLineOfSight = 0x2000222;
        const std::int32_t opcodeGetLineOfSightExplicit = 0x2000223;
        const std::int32_t opcodeToggleAI = 0x2000224;
        const std::int32_t opcodeGetTarget = 0x2000238;
        const std::int32_t opcodeGetTargetExplicit = 0x2000239;
        const std::int32_t opcodeStartCombat = 0x200023a;
        const std::int32_t opcodeStartCombatExplicit = 0x200023b;
        const std::int32_t opcodeStopCombat = 0x200023c;
        const std::int32_t opcodeStopCombatExplicit = 0x200023d;
        const std::int32_t opcodeFace = 0x200024c;
        const std::int32_t opcodeFaceExplicit = 0x200024d;
    }

    namespace Animation
    {
        const std::int32_t opcodeSkipAnim = 0x2000138;
        const std::int32_t opcodeSkipAnimExplicit = 0x2000139;
        const std::int32_t opcodePlayAnim = 0x20006;
        const std::int32_t opcodePlayAnimExplicit = 0x20007;
        const std::int32_t opcodeLoopAnim = 0x20008;
        const std::int32_t opcodeLoopAnimExplicit = 0x20009;
    }

    namespace Cell
    {
        const std::int32_t opcodeCellChanged = 0x2000000;
        const std::int32_t opcodeTestCells = 0x200030e;
        const std::int32_t opcodeTestInteriorCells = 0x200030f;
        const std::int32_t opcodeCOC = 0x2000026;
        const std::int32_t opcodeCOE = 0x2000226;
        const std::int32_t opcodeGetInterior = 0x2000131;
        const std::int32_t opcodeGetPCCell = 0x2000136;
        const std::int32_t opcodeGetWaterLevel = 0x2000141;
        const std::int32_t opcodeSetWaterLevel = 0x2000142;
        const std::int32_t opcodeModWaterLevel = 0x2000143;
    }

    namespace Console
    {

    }

    namespace Container
    {
        const std::int32_t opcodeAddItem = 0x2000076;
        const std::int32_t opcodeAddItemExplicit = 0x2000077;
        const std::int32_t opcodeGetItemCount = 0x2000078;
        const std::int32_t opcodeGetItemCountExplicit = 0x2000079;
        const std::int32_t opcodeRemoveItem = 0x200007a;
        const std::int32_t opcodeRemoveItemExplicit = 0x200007b;
        const std::int32_t opcodeEquip = 0x20001b3;
        const std::int32_t opcodeEquipExplicit = 0x20001b4;
        const std::int32_t opcodeGetArmorType = 0x20001d1;
        const std::int32_t opcodeGetArmorTypeExplicit = 0x20001d2;
        const std::int32_t opcodeHasItemEquipped = 0x20001d5;
        const std::int32_t opcodeHasItemEquippedExplicit = 0x20001d6;
        const std::int32_t opcodeHasSoulGem = 0x20001de;
        const std::int32_t opcodeHasSoulGemExplicit = 0x20001df;
        const std::int32_t opcodeGetWeaponType = 0x20001e0;
        const std::int32_t opcodeGetWeaponTypeExplicit = 0x20001e1;
    }

    namespace Control
    {
        const std::int32_t numberOfControls = 7;

        inline constexpr const char* controls[numberOfControls] = {
            "playercontrols",
            "playerfighting",
            "playerjumping",
            "playerlooking",
            "playermagic",
            "playerviewswitch",
            "vanitymode",
        };

        const std::int32_t opcodeEnable = 0x200007e;
        const std::int32_t opcodeDisable = 0x2000085;
        const std::int32_t opcodeToggleCollision = 0x2000130;
        const std::int32_t opcodeClearForceRun = 0x2000154;
        const std::int32_t opcodeClearForceRunExplicit = 0x2000155;
        const std::int32_t opcodeForceRun = 0x2000156;
        const std::int32_t opcodeForceRunExplicit = 0x2000157;
        const std::int32_t opcodeClearForceJump = 0x2000258;
        const std::int32_t opcodeClearForceJumpExplicit = 0x2000259;
        const std::int32_t opcodeForceJump = 0x200025a;
        const std::int32_t opcodeForceJumpExplicit = 0x200025b;
        const std::int32_t opcodeClearForceMoveJump = 0x200025c;
        const std::int32_t opcodeClearForceMoveJumpExplicit = 0x200025d;
        const std::int32_t opcodeForceMoveJump = 0x200025e;
        const std::int32_t opcodeForceMoveJumpExplicit = 0x200025f;
        const std::int32_t opcodeClearForceSneak = 0x2000158;
        const std::int32_t opcodeClearForceSneakExplicit = 0x2000159;
        const std::int32_t opcodeForceSneak = 0x200015a;
        const std::int32_t opcodeForceSneakExplicit = 0x200015b;
        const std::int32_t opcodeGetDisabled = 0x2000175;
        const std::int32_t opcodeGetPcRunning = 0x20001c9;
        const std::int32_t opcodeGetPcSneaking = 0x20001ca;
        const std::int32_t opcodeGetForceRun = 0x20001cb;
        const std::int32_t opcodeGetForceSneak = 0x20001cc;
        const std::int32_t opcodeGetForceRunExplicit = 0x20001cd;
        const std::int32_t opcodeGetForceSneakExplicit = 0x20001ce;
        const std::int32_t opcodeGetForceJump = 0x2000260;
        const std::int32_t opcodeGetForceMoveJump = 0x2000262;
        const std::int32_t opcodeGetForceJumpExplicit = 0x2000261;
        const std::int32_t opcodeGetForceMoveJumpExplicit = 0x2000263;
    }

    namespace Dialogue
    {
        const std::int32_t opcodeJournal = 0x2000133;
        const std::int32_t opcodeJournalExplicit = 0x200030b;
        const std::int32_t opcodeSetJournalIndex = 0x2000134;
        const std::int32_t opcodeGetJournalIndex = 0x2000135;
        const std::int32_t opcodeFillJournal = 0x2000326;
        const std::int32_t opcodeAddTopic = 0x200013a;
        const std::int32_t opcodeChoice = 0x2000a;
        const std::int32_t opcodeForceGreeting = 0x200014f;
        const std::int32_t opcodeForceGreetingExplicit = 0x2000150;
        const std::int32_t opcodeGoodbye = 0x2000152;
        const std::int32_t opcodeSetReputation = 0x20001ad;
        const std::int32_t opcodeModReputation = 0x20001ae;
        const std::int32_t opcodeSetReputationExplicit = 0x20001af;
        const std::int32_t opcodeModReputationExplicit = 0x20001b0;
        const std::int32_t opcodeGetReputation = 0x20001b1;
        const std::int32_t opcodeGetReputationExplicit = 0x20001b2;
        const std::int32_t opcodeSameFaction = 0x20001b5;
        const std::int32_t opcodeSameFactionExplicit = 0x20001b6;
        const std::int32_t opcodeModFactionReaction = 0x2000242;
        const std::int32_t opcodeSetFactionReaction = 0x20002ff;
        const std::int32_t opcodeGetFactionReaction = 0x2000243;
        const std::int32_t opcodeClearInfoActor = 0x2000245;
        const std::int32_t opcodeClearInfoActorExplicit = 0x2000246;
    }

    namespace Gui
    {
        const std::int32_t opcodeEnableBirthMenu = 0x200000e;
        const std::int32_t opcodeEnableClassMenu = 0x200000f;
        const std::int32_t opcodeEnableNameMenu = 0x2000010;
        const std::int32_t opcodeEnableRaceMenu = 0x2000011;
        const std::int32_t opcodeEnableStatsReviewMenu = 0x2000012;
        const std::int32_t opcodeEnableInventoryMenu = 0x2000013;
        const std::int32_t opcodeEnableMagicMenu = 0x2000014;
        const std::int32_t opcodeEnableMapMenu = 0x2000015;
        const std::int32_t opcodeEnableStatsMenu = 0x2000016;
        const std::int32_t opcodeEnableRest = 0x2000017;
        const std::int32_t opcodeEnableLevelupMenu = 0x2000300;
        const std::int32_t opcodeShowRestMenu = 0x2000018;
        const std::int32_t opcodeShowRestMenuExplicit = 0x2000234;
        const std::int32_t opcodeGetButtonPressed = 0x2000137;
        const std::int32_t opcodeToggleFogOfWar = 0x2000145;
        const std::int32_t opcodeToggleFullHelp = 0x2000151;
        const std::int32_t opcodeShowMap = 0x20001a0;
        const std::int32_t opcodeFillMap = 0x20001a1;
        const std::int32_t opcodeMenuTest = 0x2002c;
        const std::int32_t opcodeToggleMenus = 0x200024b;
    }

    namespace Misc
    {
        const std::int32_t opcodeXBox = 0x200000c;
        const std::int32_t opcodeOnActivate = 0x200000d;
        const std::int32_t opcodeOnActivateExplicit = 0x2000306;
        const std::int32_t opcodeActivate = 0x2000075;
        const std::int32_t opcodeActivateExplicit = 0x2000244;
        const std::int32_t opcodeLock = 0x20004;
        const std::int32_t opcodeLockExplicit = 0x20005;
        const std::int32_t opcodeUnlock = 0x200008c;
        const std::int32_t opcodeUnlockExplicit = 0x200008d;
        const std::int32_t opcodeToggleCollisionDebug = 0x2000132;
        const std::int32_t opcodeToggleCollisionBoxes = 0x20001ac;
        const std::int32_t opcodeToggleWireframe = 0x200013b;
        const std::int32_t opcodeFadeIn = 0x200013c;
        const std::int32_t opcodeFadeOut = 0x200013d;
        const std::int32_t opcodeFadeTo = 0x200013e;
        const std::int32_t opcodeToggleWater = 0x2000144;
        const std::int32_t opcodeToggleWorld = 0x20002f5;
        const std::int32_t opcodeTogglePathgrid = 0x2000146;
        const std::int32_t opcodeDontSaveObject = 0x2000153;
        const std::int32_t opcodePcForce1stPerson = 0x20002f6;
        const std::int32_t opcodePcForce3rdPerson = 0x20002f7;
        const std::int32_t opcodePcGet3rdPerson = 0x20002f8;
        const std::int32_t opcodeToggleVanityMode = 0x2000174;
        const std::int32_t opcodeGetPcSleep = 0x200019f;
        const std::int32_t opcodeGetPcJumping = 0x2000233;
        const std::int32_t opcodeWakeUpPc = 0x20001a2;
        const std::int32_t opcodeGetLocked = 0x20001c7;
        const std::int32_t opcodeGetLockedExplicit = 0x20001c8;
        const std::int32_t opcodeGetEffect = 0x20001cf;
        const std::int32_t opcodeGetEffectExplicit = 0x20001d0;
        const std::int32_t opcodeBetaComment = 0x2002d;
        const std::int32_t opcodeBetaCommentExplicit = 0x2002e;
        const std::int32_t opcodeAddSoulGem = 0x20001f3;
        const std::int32_t opcodeAddSoulGemExplicit = 0x20001f4;
        const std::int32_t opcodeRemoveSoulGem = 0x20027;
        const std::int32_t opcodeRemoveSoulGemExplicit = 0x20028;
        const std::int32_t opcodeDrop = 0x20001f8;
        const std::int32_t opcodeDropExplicit = 0x20001f9;
        const std::int32_t opcodeDropSoulGem = 0x20001fa;
        const std::int32_t opcodeDropSoulGemExplicit = 0x20001fb;
        const std::int32_t opcodeGetAttacked = 0x20001d3;
        const std::int32_t opcodeGetAttackedExplicit = 0x20001d4;
        const std::int32_t opcodeGetWeaponDrawn = 0x20001d7;
        const std::int32_t opcodeGetWeaponDrawnExplicit = 0x20001d8;
        const std::int32_t opcodeGetSpellReadied = 0x2000231;
        const std::int32_t opcodeGetSpellReadiedExplicit = 0x2000232;
        const std::int32_t opcodeGetSpellEffects = 0x20001db;
        const std::int32_t opcodeGetSpellEffectsExplicit = 0x20001dc;
        const std::int32_t opcodeGetCurrentTime = 0x20001dd;
        const std::int32_t opcodeSetDelete = 0x20001e5;
        const std::int32_t opcodeSetDeleteExplicit = 0x20001e6;
        const std::int32_t opcodeGetSquareRoot = 0x20001e7;
        const std::int32_t opcodeFall = 0x200020a;
        const std::int32_t opcodeFallExplicit = 0x200020b;
        const std::int32_t opcodeGetStandingPc = 0x200020c;
        const std::int32_t opcodeGetStandingPcExplicit = 0x200020d;
        const std::int32_t opcodeGetStandingActor = 0x200020e;
        const std::int32_t opcodeGetStandingActorExplicit = 0x200020f;
        const std::int32_t opcodeGetCollidingPc = 0x2000250;
        const std::int32_t opcodeGetCollidingPcExplicit = 0x2000251;
        const std::int32_t opcodeGetCollidingActor = 0x2000252;
        const std::int32_t opcodeGetCollidingActorExplicit = 0x2000253;
        const std::int32_t opcodeHurtStandingActor = 0x2000254;
        const std::int32_t opcodeHurtStandingActorExplicit = 0x2000255;
        const std::int32_t opcodeHurtCollidingActor = 0x2000256;
        const std::int32_t opcodeHurtCollidingActorExplicit = 0x2000257;
        const std::int32_t opcodeGetWindSpeed = 0x2000212;
        const std::int32_t opcodePlayBink = 0x20001f7;
        const std::int32_t opcodeGoToJail = 0x2000235;
        const std::int32_t opcodePayFine = 0x2000236;
        const std::int32_t opcodePayFineThief = 0x2000237;
        const std::int32_t opcodeHitOnMe = 0x2000213;
        const std::int32_t opcodeHitOnMeExplicit = 0x2000214;
        const std::int32_t opcodeHitAttemptOnMe = 0x20002f9;
        const std::int32_t opcodeHitAttemptOnMeExplicit = 0x20002fa;
        const std::int32_t opcodeDisableTeleporting = 0x2000215;
        const std::int32_t opcodeEnableTeleporting = 0x2000216;
        const std::int32_t opcodeShowVars = 0x200021d;
        const std::int32_t opcodeShowVarsExplicit = 0x200021e;
        const std::int32_t opcodeShow = 0x2000304;
        const std::int32_t opcodeShowExplicit = 0x2000305;
        const std::int32_t opcodeToggleGodMode = 0x200021f;
        const std::int32_t opcodeToggleScripts = 0x2000301;
        const std::int32_t opcodeDisableLevitation = 0x2000220;
        const std::int32_t opcodeEnableLevitation = 0x2000221;
        const std::int32_t opcodeCast = 0x2000227;
        const std::int32_t opcodeCastExplicit = 0x2000228;
        const std::int32_t opcodeExplodeSpell = 0x2000229;
        const std::int32_t opcodeExplodeSpellExplicit = 0x200022a;
        const std::int32_t opcodeGetPcInJail = 0x200023e;
        const std::int32_t opcodeGetPcTraveling = 0x200023f;
        const std::int32_t opcodeAddToLevCreature = 0x20002fb;
        const std::int32_t opcodeRemoveFromLevCreature = 0x20002fc;
        const std::int32_t opcodeAddToLevItem = 0x20002fd;
        const std::int32_t opcodeRemoveFromLevItem = 0x20002fe;
        const std::int32_t opcodeShowSceneGraph = 0x2002f;
        const std::int32_t opcodeShowSceneGraphExplicit = 0x20030;
        const std::int32_t opcodeToggleBorders = 0x2000307;
        const std::int32_t opcodeToggleNavMesh = 0x2000308;
        const std::int32_t opcodeToggleActorsPaths = 0x2000309;
        const std::int32_t opcodeSetNavMeshNumberToRender = 0x200030a;
        const std::int32_t opcodeRepairedOnMe = 0x200030c;
        const std::int32_t opcodeRepairedOnMeExplicit = 0x200030d;
        const std::int32_t opcodeToggleRecastMesh = 0x2000310;
        const std::int32_t opcodeMenuMode = 0x2000311;
        const std::int32_t opcodeRandom = 0x2000312;
        const std::int32_t opcodeScriptRunning = 0x2000313;
        const std::int32_t opcodeStartScript = 0x2000314;
        const std::int32_t opcodeStopScript = 0x2000315;
        const std::int32_t opcodeGetSecondsPassed = 0x2000316;
        const std::int32_t opcodeEnable = 0x2000317;
        const std::int32_t opcodeDisable = 0x2000318;
        const std::int32_t opcodeGetDisabled = 0x2000319;
        const std::int32_t opcodeEnableExplicit = 0x200031a;
        const std::int32_t opcodeDisableExplicit = 0x200031b;
        const std::int32_t opcodeGetDisabledExplicit = 0x200031c;
        const std::int32_t opcodeStartScriptExplicit = 0x200031d;
        const std::int32_t opcodeHelp = 0x2000320;
        const std::int32_t opcodeReloadLua = 0x2000321;
        const std::int32_t opcodeTestModels = 0x2000325;
    }

    namespace Sky
    {
        const std::int32_t opcodeToggleSky = 0x2000021;
        const std::int32_t opcodeTurnMoonWhite = 0x2000022;
        const std::int32_t opcodeTurnMoonRed = 0x2000023;
        const std::int32_t opcodeGetMasserPhase = 0x2000024;
        const std::int32_t opcodeGetSecundaPhase = 0x2000025;
        const std::int32_t opcodeGetCurrentWeather = 0x200013f;
        const std::int32_t opcodeChangeWeather = 0x2000140;
        const std::int32_t opcodeModRegion = 0x20026;
    }

    namespace Sound
    {
        const std::int32_t opcodeSay = 0x2000001;
        const std::int32_t opcodeSayDone = 0x2000002;
        const std::int32_t opcodeStreamMusic = 0x2000003;
        const std::int32_t opcodePlaySound = 0x2000004;
        const std::int32_t opcodePlaySoundVP = 0x2000005;
        const std::int32_t opcodePlaySound3D = 0x2000006;
        const std::int32_t opcodePlaySound3DVP = 0x2000007;
        const std::int32_t opcodePlayLoopSound3D = 0x2000008;
        const std::int32_t opcodePlayLoopSound3DVP = 0x2000009;
        const std::int32_t opcodeStopSound = 0x200000a;
        const std::int32_t opcodeGetSoundPlaying = 0x200000b;

        const std::int32_t opcodeSayExplicit = 0x2000019;
        const std::int32_t opcodeSayDoneExplicit = 0x200001a;
        const std::int32_t opcodePlaySound3DExplicit = 0x200001b;
        const std::int32_t opcodePlaySound3DVPExplicit = 0x200001c;
        const std::int32_t opcodePlayLoopSound3DExplicit = 0x200001d;
        const std::int32_t opcodePlayLoopSound3DVPExplicit = 0x200001e;
        const std::int32_t opcodeStopSoundExplicit = 0x200001f;
        const std::int32_t opcodeGetSoundPlayingExplicit = 0x2000020;
    }

    namespace Stats
    {
        const std::int32_t numberOfAttributes = 8;
        const std::int32_t numberOfDynamics = 3;
        const std::int32_t numberOfSkills = 27;

        const std::int32_t numberOfMagicEffects = 24;

        const std::int32_t opcodeGetAttribute = 0x2000027;
        const std::int32_t opcodeGetAttributeExplicit = 0x200002f;
        const std::int32_t opcodeSetAttribute = 0x2000037;
        const std::int32_t opcodeSetAttributeExplicit = 0x200003f;
        const std::int32_t opcodeModAttribute = 0x2000047;
        const std::int32_t opcodeModAttributeExplicit = 0x200004f;

        const std::int32_t opcodeGetDynamic = 0x2000057;
        const std::int32_t opcodeGetDynamicExplicit = 0x200005a;
        const std::int32_t opcodeSetDynamic = 0x200005d;
        const std::int32_t opcodeSetDynamicExplicit = 0x2000060;
        const std::int32_t opcodeModDynamic = 0x2000063;
        const std::int32_t opcodeModDynamicExplicit = 0x2000066;
        const std::int32_t opcodeModCurrentDynamic = 0x2000069;
        const std::int32_t opcodeModCurrentDynamicExplicit = 0x200006c;
        const std::int32_t opcodeGetDynamicGetRatio = 0x200006f;
        const std::int32_t opcodeGetDynamicGetRatioExplicit = 0x2000072;

        const std::int32_t opcodeGetSkill = 0x200008e;
        const std::int32_t opcodeGetSkillExplicit = 0x20000a9;
        const std::int32_t opcodeSetSkill = 0x20000c4;
        const std::int32_t opcodeSetSkillExplicit = 0x20000df;
        const std::int32_t opcodeModSkill = 0x20000fa;
        const std::int32_t opcodeModSkillExplicit = 0x2000115;

        const std::int32_t opcodeGetMagicEffect = 0x2000264;
        const std::int32_t opcodeGetMagicEffectExplicit = 0x200027c;
        const std::int32_t opcodeSetMagicEffect = 0x2000294;
        const std::int32_t opcodeSetMagicEffectExplicit = 0x20002ac;
        const std::int32_t opcodeModMagicEffect = 0x20002c4;
        const std::int32_t opcodeModMagicEffectExplicit = 0x20002dc;

        const std::int32_t opcodeGetPCCrimeLevel = 0x20001ec;
        const std::int32_t opcodeSetPCCrimeLevel = 0x20001ed;
        const std::int32_t opcodeModPCCrimeLevel = 0x20001ee;

        const std::int32_t opcodeAddSpell = 0x2000147;
        const std::int32_t opcodeAddSpellExplicit = 0x2000148;
        const std::int32_t opcodeRemoveSpell = 0x2000149;
        const std::int32_t opcodeRemoveSpellExplicit = 0x200014a;
        const std::int32_t opcodeGetSpell = 0x200014b;
        const std::int32_t opcodeGetSpellExplicit = 0x200014c;

        const std::int32_t opcodePCRaiseRank = 0x2000b;
        const std::int32_t opcodePCLowerRank = 0x2000c;
        const std::int32_t opcodePCJoinFaction = 0x2000d;
        const std::int32_t opcodePCRaiseRankExplicit = 0x20029;
        const std::int32_t opcodePCLowerRankExplicit = 0x2002a;
        const std::int32_t opcodePCJoinFactionExplicit = 0x2002b;

        const std::int32_t opcodeGetPCRank = 0x2000e;
        const std::int32_t opcodeGetPCRankExplicit = 0x2000f;
        const std::int32_t opcodeModDisposition = 0x200014d;
        const std::int32_t opcodeModDispositionExplicit = 0x200014e;
        const std::int32_t opcodeSetDisposition = 0x20001a4;
        const std::int32_t opcodeSetDispositionExplicit = 0x20001a5;
        const std::int32_t opcodeGetDisposition = 0x20001a6;
        const std::int32_t opcodeGetDispositionExplicit = 0x20001a7;

        const std::int32_t opcodeGetLevel = 0x200018c;
        const std::int32_t opcodeGetLevelExplicit = 0x200018d;
        const std::int32_t opcodeSetLevel = 0x200018e;
        const std::int32_t opcodeSetLevelExplicit = 0x200018f;

        const std::int32_t opcodeGetDeadCount = 0x20001a3;

        const std::int32_t opcodeGetPCFacRep = 0x20012;
        const std::int32_t opcodeGetPCFacRepExplicit = 0x20013;
        const std::int32_t opcodeSetPCFacRep = 0x20014;
        const std::int32_t opcodeSetPCFacRepExplicit = 0x20015;
        const std::int32_t opcodeModPCFacRep = 0x20016;
        const std::int32_t opcodeModPCFacRepExplicit = 0x20017;

        const std::int32_t opcodeGetCommonDisease = 0x20001a8;
        const std::int32_t opcodeGetCommonDiseaseExplicit = 0x20001a9;
        const std::int32_t opcodeGetBlightDisease = 0x20001aa;
        const std::int32_t opcodeGetBlightDiseaseExplicit = 0x20001ab;

        const std::int32_t opcodeGetRace = 0x20001d9;
        const std::int32_t opcodeGetRaceExplicit = 0x20001da;

        const std::int32_t opcodePcExpelled = 0x20018;
        const std::int32_t opcodePcExpelledExplicit = 0x20019;
        const std::int32_t opcodePcExpell = 0x2001a;
        const std::int32_t opcodePcExpellExplicit = 0x2001b;
        const std::int32_t opcodePcClearExpelled = 0x2001c;
        const std::int32_t opcodePcClearExpelledExplicit = 0x2001d;
        const std::int32_t opcodeRaiseRank = 0x20001e8;
        const std::int32_t opcodeRaiseRankExplicit = 0x20001e9;
        const std::int32_t opcodeLowerRank = 0x20001ea;
        const std::int32_t opcodeLowerRankExplicit = 0x20001eb;
        const std::int32_t opcodeOnDeath = 0x20001fc;
        const std::int32_t opcodeOnDeathExplicit = 0x2000205;
        const std::int32_t opcodeOnMurder = 0x2000249;
        const std::int32_t opcodeOnMurderExplicit = 0x200024a;
        const std::int32_t opcodeOnKnockout = 0x2000240;
        const std::int32_t opcodeOnKnockoutExplicit = 0x2000241;

        const std::int32_t opcodeBecomeWerewolf = 0x2000217;
        const std::int32_t opcodeBecomeWerewolfExplicit = 0x2000218;
        const std::int32_t opcodeUndoWerewolf = 0x2000219;
        const std::int32_t opcodeUndoWerewolfExplicit = 0x200021a;
        const std::int32_t opcodeSetWerewolfAcrobatics = 0x200021b;
        const std::int32_t opcodeSetWerewolfAcrobaticsExplicit = 0x200021c;
        const std::int32_t opcodeIsWerewolf = 0x20001fd;
        const std::int32_t opcodeIsWerewolfExplicit = 0x20001fe;

        const std::int32_t opcodeGetWerewolfKills = 0x20001e2;

        const std::int32_t opcodeRemoveSpellEffects = 0x200022b;
        const std::int32_t opcodeRemoveSpellEffectsExplicit = 0x200022c;
        const std::int32_t opcodeRemoveEffects = 0x200022d;
        const std::int32_t opcodeRemoveEffectsExplicit = 0x200022e;
        const std::int32_t opcodeResurrect = 0x200022f;
        const std::int32_t opcodeResurrectExplicit = 0x2000230;

        const std::int32_t opcodeGetStat = 0x200024e;
        const std::int32_t opcodeGetStatExplicit = 0x200024f;

        const std::int32_t opcodeGetPCVisionBonus = 0x2000322;
        const std::int32_t opcodeSetPCVisionBonus = 0x2000323;
        const std::int32_t opcodeModPCVisionBonus = 0x2000324;
    }

    namespace Transformation
    {
        const std::int32_t opcodeSetScale = 0x2000164;
        const std::int32_t opcodeSetScaleExplicit = 0x2000165;
        const std::int32_t opcodeSetAngle = 0x2000166;
        const std::int32_t opcodeSetAngleExplicit = 0x2000167;
        const std::int32_t opcodeGetScale = 0x2000168;
        const std::int32_t opcodeGetScaleExplicit = 0x2000169;
        const std::int32_t opcodeGetAngle = 0x200016a;
        const std::int32_t opcodeGetAngleExplicit = 0x200016b;
        const std::int32_t opcodeGetPos = 0x2000190;
        const std::int32_t opcodeGetPosExplicit = 0x2000191;
        const std::int32_t opcodeSetPos = 0x2000192;
        const std::int32_t opcodeSetPosExplicit = 0x2000193;
        const std::int32_t opcodeGetStartingPos = 0x2000194;
        const std::int32_t opcodeGetStartingPosExplicit = 0x2000195;
        const std::int32_t opcodeGetStartingAngle = 0x2000210;
        const std::int32_t opcodeGetStartingAngleExplicit = 0x2000211;
        const std::int32_t opcodePosition = 0x2000196;
        const std::int32_t opcodePositionExplicit = 0x2000197;
        const std::int32_t opcodePositionCell = 0x2000198;
        const std::int32_t opcodePositionCellExplicit = 0x2000199;

        const std::int32_t opcodePlaceItemCell = 0x200019a;
        const std::int32_t opcodePlaceItem = 0x200019b;
        const std::int32_t opcodePlaceAtPc = 0x200019c;
        const std::int32_t opcodePlaceAtMe = 0x200019d;
        const std::int32_t opcodePlaceAtMeExplicit = 0x200019e;
        const std::int32_t opcodeModScale = 0x20001e3;
        const std::int32_t opcodeModScaleExplicit = 0x20001e4;
        const std::int32_t opcodeRotate = 0x20001ff;
        const std::int32_t opcodeRotateExplicit = 0x2000200;
        const std::int32_t opcodeRotateWorld = 0x2000201;
        const std::int32_t opcodeRotateWorldExplicit = 0x2000202;
        const std::int32_t opcodeSetAtStart = 0x2000203;
        const std::int32_t opcodeSetAtStartExplicit = 0x2000204;
        const std::int32_t opcodeMove = 0x2000206;
        const std::int32_t opcodeMoveExplicit = 0x2000207;
        const std::int32_t opcodeMoveWorld = 0x2000208;
        const std::int32_t opcodeMoveWorldExplicit = 0x2000209;
        const std::int32_t opcodeResetActors = 0x20002f4;
        const std::int32_t opcodeFixme = 0x2000302;
        const std::int32_t opcodeGetDistance = 0x200031e;
        const std::int32_t opcodeGetDistanceExplicit = 0x200031f;
    }

    namespace User
    {
        const std::int32_t opcodeUser1 = 0x200016c;
        const std::int32_t opcodeUser2 = 0x200016d;
        const std::int32_t opcodeUser3 = 0x200016e;
        const std::int32_t opcodeUser3Explicit = 0x200016f;
        const std::int32_t opcodeUser4 = 0x2000170;
        const std::int32_t opcodeUser4Explicit = 0x2000171;
    }
}
