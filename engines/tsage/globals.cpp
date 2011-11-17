/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "tsage/globals.h"
#include "tsage/tsage.h"
#include "tsage/blue_force/blueforce_logic.h"
#include "tsage/ringworld/ringworld_demo.h"
#include "tsage/ringworld/ringworld_logic.h"
#include "tsage/ringworld2/ringworld2_logic.h"

namespace TsAGE {

Globals *g_globals = NULL;
ResourceManager *g_resourceManager = NULL;

/*--------------------------------------------------------------------------*/

/**
 * Instantiates a saved object that can be instantiated
 */
static SavedObject *classFactoryProc(const Common::String &className) {
	if (className == "ObjectMover") return new ObjectMover();
	if (className == "NpcMover") return new NpcMover();
	if (className == "ObjectMover2") return new ObjectMover2();
	if (className == "ObjectMover3") return new ObjectMover3();
	if (className == "PlayerMover") return new PlayerMover();
	if (className == "SceneObjectWrapper") return new SceneObjectWrapper();
	if (className == "PaletteRotation") return new PaletteRotation();
	if (className == "PaletteFader") return new PaletteFader();
	return NULL;
}

/*--------------------------------------------------------------------------*/

Globals::Globals() : _dialogCenter(160, 140), _gfxManagerInstance(_screenSurface),
		_randomSource("tsage"), _color1(0), _color2(255), _color3(255) {
	reset();
	_stripNum = 0;
	_gfxEdgeAdjust = 3;

	if (g_vm->getFeatures() & GF_DEMO) {
		_gfxFontNumber = 0;
		_gfxColors.background = 6;
		_gfxColors.foreground = 0;
		_fontColors.background = 255;
		_fontColors.foreground = 6;
		_dialogCenter.y = 80;
		// Workaround in order to use later version of the engine
		_color1 = _gfxColors.foreground;
		_color2 = _gfxColors.foreground;
		_color3 = _gfxColors.foreground;
	} else if (g_vm->getGameID() == GType_BlueForce) {
		// Blue Force
		_gfxFontNumber = 0;
		_gfxColors.background = 89;
		_gfxColors.foreground = 83;
		_fontColors.background = 88;
		_fontColors.foreground = 92;
		_dialogCenter.y = 140;
	} else if (g_vm->getGameID() == GType_Ringworld2) {
		// Return to Ringworld
		_gfxFontNumber = 50;
		_gfxColors.background = 0;
		_gfxColors.foreground = 59;
		_fontColors.background = 4;
		_fontColors.foreground = 15;
		_color1 = 59;
		_color2 = 15;
		_color3 = 4;
		_dialogCenter.y = 100;
	} else if ((g_vm->getGameID() == GType_Ringworld) &&  (g_vm->getFeatures() & GF_CD)) {
		_gfxFontNumber = 50;
		_gfxColors.background = 53;
		_gfxColors.foreground = 0;
		_fontColors.background = 51;
		_fontColors.foreground = 54;
		_color1 = 18;
		_color2 = 18;
		_color3 = 18;
	} else {
		// Ringworld
		_gfxFontNumber = 50;
		_gfxColors.background = 53;
		_gfxColors.foreground = 18;
		_fontColors.background = 51;
		_fontColors.foreground = 54;
		// Workaround in order to use later version of the engine
		_color1 = _gfxColors.foreground;
		_color2 = _gfxColors.foreground;
		_color3 = _gfxColors.foreground;
	}
	_screenSurface.setScreenSurface();
	_gfxManagers.push_back(&_gfxManagerInstance);

	_sceneObjects = &_sceneObjectsInstance;
	_sceneObjects_queue.push_front(_sceneObjects);

	_prevSceneOffset = Common::Point(-1, -1);
	_sounds.push_back(&_soundHandler);
	_sounds.push_back(&_sequenceManager._soundHandler);

	_scrollFollower = NULL;
	_inventory = NULL;

	switch (g_vm->getGameID()) {
	case GType_Ringworld:
		if (!(g_vm->getFeatures() & GF_DEMO)) {
			_inventory = new Ringworld::RingworldInvObjectList();
			_game = new Ringworld::RingworldGame();
		} else {
			_game = new Ringworld::RingworldDemoGame();
		}
		_sceneHandler = new SceneHandler();
		break;

	case GType_BlueForce:
		_game = new BlueForce::BlueForceGame();
		_inventory = new BlueForce::BlueForceInvObjectList();
		_sceneHandler = new BlueForce::SceneHandlerExt();
		break;

	case GType_Ringworld2:
		_inventory = new Ringworld2::Ringworld2InvObjectList();
		_game = new Ringworld2::Ringworld2Game();
		_sceneHandler = new Ringworld2::SceneHandlerExt();
		break;
	}
}

Globals::~Globals() {
	_scenePalette.clearListeners();
	delete _inventory;
	delete _sceneHandler;
	delete _game;
	g_globals = NULL;
}

void Globals::reset() {
	Common::fill(&_flags[0], &_flags[MAX_FLAGS], false);
	g_saver->addFactory(classFactoryProc);
}

void Globals::synchronize(Serializer &s) {
	if (s.getVersion() >= 2)
		SavedObject::synchronize(s);
	assert(_gfxManagers.size() == 1);

	_sceneItems.synchronize(s);
	SYNC_POINTER(_sceneObjects);
	_sceneObjects_queue.synchronize(s);
	s.syncAsSint32LE(_gfxFontNumber);
	s.syncAsSint32LE(_gfxColors.background);
	s.syncAsSint32LE(_gfxColors.foreground);
	s.syncAsSint32LE(_fontColors.background);
	s.syncAsSint32LE(_fontColors.foreground);

	if (s.getVersion() >= 4) {
		s.syncAsByte(_color1);
		s.syncAsByte(_color2);
		s.syncAsByte(_color3);
	}

	s.syncAsSint16LE(_dialogCenter.x); s.syncAsSint16LE(_dialogCenter.y);
	_sounds.synchronize(s);
	for (int i = 0; i < 256; ++i)
		s.syncAsByte(_flags[i]);

	s.syncAsSint16LE(_sceneOffset.x); s.syncAsSint16LE(_sceneOffset.y);
	s.syncAsSint16LE(_prevSceneOffset.x); s.syncAsSint16LE(_prevSceneOffset.y);
	SYNC_POINTER(_scrollFollower);
	s.syncAsSint32LE(_stripNum);

	if (s.getVersion() >= 8)
		_walkRegions.synchronize(s);
}

void Globals::dispatchSound(ASound *obj) {
	obj->dispatch();
}

void Globals::dispatchSounds() {
	Common::for_each(_sounds.begin(), _sounds.end(), Globals::dispatchSound);
}

/*--------------------------------------------------------------------------*/

void TsAGE2Globals::reset() {
	Globals::reset();
	
	// Reset the inventory
	T2_GLOBALS._uiElements.updateInventory();
	T2_GLOBALS._uiElements._scoreValue = 0;
	T2_GLOBALS._uiElements._active = false;
}

void TsAGE2Globals::synchronize(Serializer &s) {
	Globals::synchronize(s);

	s.syncAsSint16LE(_interfaceY);
}

/*--------------------------------------------------------------------------*/

namespace BlueForce {

BlueForceGlobals::BlueForceGlobals(): TsAGE2Globals() {
}

void BlueForceGlobals::synchronize(Serializer &s) {
	TsAGE2Globals::synchronize(s);

	s.syncAsSint16LE(_dayNumber);
	if (s.getVersion() < 9) {
		int tmpVar;
		s.syncAsSint16LE(tmpVar);
	}
	s.syncAsSint16LE(_tonyDialogCtr);
	s.syncAsSint16LE(_marinaWomanCtr);
	s.syncAsSint16LE(_kateDialogCtr);
	s.syncAsSint16LE(_v4CEB6);
	s.syncAsSint16LE(_safeCombination);
	s.syncAsSint16LE(_gateStatus);
	s.syncAsSint16LE(_greenDay5TalkCtr);
	s.syncAsSint16LE(_v4CEC4);
	s.syncAsSint16LE(_v4CEC8);
	s.syncAsSint16LE(_v4CECA);
	s.syncAsSint16LE(_v4CECC);
	for (int i = 0; i < 18; i++)
		s.syncAsByte(_breakerBoxStatusArr[i]);
	s.syncAsSint16LE(_v4CEE0);
	s.syncAsSint16LE(_v4CEE2);
	s.syncAsSint16LE(_v4CEE4);
	s.syncAsSint16LE(_v4CEE6);
	s.syncAsSint16LE(_v4CEE8);
	s.syncAsSint16LE(_deziTopic);
	s.syncAsSint16LE(_deathReason);
	s.syncAsSint16LE(_driveFromScene);
	s.syncAsSint16LE(_driveToScene);
	s.syncAsSint16LE(_v501F8);
	s.syncAsSint16LE(_v501FA);
	s.syncAsSint16LE(_v501FC);
	s.syncAsSint16LE(_v5020C);
	s.syncAsSint16LE(_v50696);
	s.syncAsSint16LE(_subFlagBitArr1);
	s.syncAsSint16LE(_subFlagBitArr2);
	s.syncAsSint16LE(_v50CC2);
	s.syncAsSint16LE(_v50CC4);
	s.syncAsSint16LE(_v50CC6);
	s.syncAsSint16LE(_v50CC8);
	s.syncAsSint16LE(_v51C42);
	s.syncAsSint16LE(_v51C44);
	s.syncAsSint16LE(_bookmark);
	s.syncAsSint16LE(_mapLocationId);
	s.syncAsSint16LE(_clip1Bullets);
	s.syncAsSint16LE(_clip2Bullets);
}

void BlueForceGlobals::reset() {
	TsAGE2Globals::reset();
	_scenePalette.clearListeners();
	
	_scrollFollower = &_player;
	_bookmark = bNone;

	// Reset the inventory
	((BlueForceInvObjectList *)_inventory)->reset();

	_mapLocationId = 1;
	_driveFromScene = 300;
	_driveToScene = 0;

	_interfaceY = UI_INTERFACE_Y;
	_dayNumber = 0;
	_tonyDialogCtr = 0;
	_marinaWomanCtr = 0;
	_kateDialogCtr = 0;
	_v4CEB6 = 0;
	_safeCombination = 0;
	_gateStatus = 0;
	_greenDay5TalkCtr = 0;
	_v4CEC4 = 0;
	_v4CEC8 = 1;
	_v4CECA = 0;
	_v4CECC = 0;
	_breakerBoxStatusArr[0] = 2;
	_breakerBoxStatusArr[1] = 2;
	_breakerBoxStatusArr[2] = 2;
	_breakerBoxStatusArr[3] = 1;
	_breakerBoxStatusArr[4] = 2;
	_breakerBoxStatusArr[5] = 2;
	_breakerBoxStatusArr[6] = 2;
	_breakerBoxStatusArr[7] = 2;
	_breakerBoxStatusArr[8] = 2;
	_breakerBoxStatusArr[9] = 2;
	_breakerBoxStatusArr[10] = 2;
	_breakerBoxStatusArr[11] = 2;
	_breakerBoxStatusArr[12] = 1;
	_breakerBoxStatusArr[13] = 1;
	_breakerBoxStatusArr[14] = 2;
	_breakerBoxStatusArr[15] = 2;
	_breakerBoxStatusArr[16] = 3;
	_breakerBoxStatusArr[17] = 0;
	_v4CEE0 = 0;
	_v4CEE2 = 0;
	_v4CEE4 = 0;
	_v4CEE6 = 0;
	_v4CEE8 = 0;
	_deziTopic = 0;
	_deathReason = 0;
	_v501F8 = 0;
	_v501FA = 0;
	_v501FC = 0;
	_v5020C = 0;
	_v50696 = 0;
	_subFlagBitArr1 = 0;
	_subFlagBitArr2 = 0;
	_v50CC2 = 0;
	_v50CC4 = 0;
	_v50CC6 = 0;
	_v50CC8 = 0;
	_v51C42 = 0;
	_v51C44 = 1;
	_clip1Bullets = 8;
	_clip2Bullets = 8;
}

bool BlueForceGlobals::getHasBullets() {
	if (!getFlag(fGunLoaded))
		return false;
	return BF_GLOBALS.getFlag(fLoadedSpare) ? (_clip2Bullets > 0) : (_clip1Bullets > 0);
}

void BlueForceGlobals::set2Flags(int flagNum) {
	if (!getFlag(flagNum + 1)) {
		setFlag(flagNum + 1);
		setFlag(flagNum);
	}
}

bool BlueForceGlobals::removeFlag(int flagNum) {
	bool result = getFlag(flagNum);
	clearFlag(flagNum);
	return result;
}

} // end of namespace BlueForce

namespace Ringworld2 {

void Ringworld2Globals::reset() {
	Globals::reset();
	
	// Reset the inventory
	R2_INVENTORY.reset();
	T2_GLOBALS._uiElements.updateInventory();
	T2_GLOBALS._uiElements._active = false;

	// Reset fields
	_v5657C = 0;
	_v565F5 = 0;
	_v57C2C = 0;
	_v58CE2 = 0;
	Common::fill(&_v565F1[0], &_v565F1[MAX_CHARACTERS], 0);
	_speechSubtitles = 0;
	_insetUp = 0;

	Common::fill(&_v565F1[0], &_v565F1[MAX_CHARACTERS], 0);
	Common::fill(&_stripManager_lookupList[0], &_stripManager_lookupList[12], 0);
	_stripManager_lookupList[0] = 1;
	_stripManager_lookupList[1] = 1;
	_stripManager_lookupList[2] = 1;
	_stripManager_lookupList[3] = 1;
	_stripManager_lookupList[4] = 1;
	_stripManager_lookupList[5] = 1;
	_stripManager_lookupList[8] = 1;
	_stripManager_lookupList[9] = 1;
	_stripManager_lookupList[10] = 1;
	_stripManager_lookupList[11] = 1;

	// Reset fields stored in the player class
	_player._characterIndex = 1;
	_player._characterScene[1] = 100;
	_player._characterScene[2] = 300;
	_player._characterScene[3] = 300;
}

void Ringworld2Globals::synchronize(Serializer &s) {
	TsAGE2Globals::synchronize(s);
	int i;

	s.syncAsSint16LE(_v5657C);
	s.syncAsSint16LE(_v565F5);
	s.syncAsSint16LE(_v57C2C);
	s.syncAsSint16LE(_v58CE2);
	s.syncAsSint16LE(_speechSubtitles);

	for (i = 0; i < MAX_CHARACTERS; ++i)
		s.syncAsSint16LE(_v565F1[i]);
	for (i = 0; i < 12; ++i)
		s.syncAsByte(_stripManager_lookupList[i]);

	s.syncAsSint16LE(_insetUp);
}

} // end of namespace Ringworld2

} // end of namespace TsAGE
