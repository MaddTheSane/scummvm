/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011, 2012, 2013 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//#include <cerrno>
//#include <cmath>
//#include <cstdlib>
//#include <cstring>

#define FORBIDDEN_SYMBOL_EXCEPTION_printf
#define FORBIDDEN_SYMBOL_EXCEPTION_vprintf

#include "mt32emu.h"
#include "mmath.h"
#include "PartialManager.h"
#include "BReverbModel.h"

namespace MT32Emu {

static const ControlROMMap ControlROMMaps[7] = {
	// ID    IDc IDbytes                     PCMmap  PCMc  tmbrA   tmbrAO, tmbrAC tmbrB   tmbrBO, tmbrBC tmbrR   trC  rhythm  rhyC  rsrv    panpot  prog    rhyMax  patMax  sysMax  timMax
	{0x4014, 22, "\000 ver1.04 14 July 87 ", 0x3000,  128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200,  30, 0x73A6,  85,  0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A},
	{0x4014, 22, "\000 ver1.05 06 Aug, 87 ", 0x3000,  128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200,  30, 0x7414,  85,  0x57C7, 0x57E2, 0x57D0, 0x5252, 0x525E, 0x526E, 0x520A},
	{0x4014, 22, "\000 ver1.06 31 Aug, 87 ", 0x3000,  128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200,  30, 0x7414,  85,  0x57D9, 0x57F4, 0x57E2, 0x5264, 0x5270, 0x5280, 0x521C},
	{0x4010, 22, "\000 ver1.07 10 Oct, 87 ", 0x3000,  128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200,  30, 0x73fe,  85,  0x57B1, 0x57CC, 0x57BA, 0x523C, 0x5248, 0x5258, 0x51F4}, // MT-32 revision 1
	{0x4010, 22, "\000verX.XX  30 Sep, 88 ", 0x3000,  128, 0x8000, 0x0000, false, 0xC000, 0x4000, false, 0x3200,  30, 0x741C,  85,  0x57E5, 0x5800, 0x57EE, 0x5270, 0x527C, 0x528C, 0x5228}, // MT-32 Blue Ridge mod
	{0x2205, 22, "\000CM32/LAPC1.00 890404", 0x8100,  256, 0x8000, 0x8000, false, 0x8080, 0x8000, false, 0x8500,  64, 0x8580,  85,  0x4F65, 0x4F80, 0x4F6E, 0x48A1, 0x48A5, 0x48BE, 0x48D5},
	{0x2205, 22, "\000CM32/LAPC1.02 891205", 0x8100,  256, 0x8000, 0x8000, true,  0x8080, 0x8000, true,  0x8500,  64, 0x8580,  85,  0x4F93, 0x4FAE, 0x4F9C, 0x48CB, 0x48CF, 0x48E8, 0x48FF}  // CM-32L
	// (Note that all but CM-32L ROM actually have 86 entries for rhythmTemp)
};

static inline void muteStream(Sample *stream, Bit32u len) {
	if (stream == NULL) return;

#if MT32EMU_USE_FLOAT_SAMPLES
	// FIXME: Use memset() where compatibility is guaranteed (if this turns out to be a win)
	while (len--) {
		*stream++ = 0.0f;
	}
#else
	memset(stream, 0, len * sizeof(Sample));
#endif
}

static inline void advanceStreamPosition(Sample *&stream, Bit32u posDelta) {
	if (stream != NULL) {
		stream += posDelta;
	}
}

Bit8u Synth::calcSysexChecksum(const Bit8u *data, Bit32u len, Bit8u checksum) {
	for (unsigned int i = 0; i < len; i++) {
		checksum = checksum + data[i];
	}
	checksum = checksum & 0x7f;
	if (checksum) {
		checksum = 0x80 - checksum;
	}
	return checksum;
}

Synth::Synth(ReportHandler *useReportHandler) {
	isOpen = false;
	reverbEnabled = true;
	reverbOverridden = false;

	if (useReportHandler == NULL) {
		reportHandler = new ReportHandler;
		isDefaultReportHandler = true;
	} else {
		reportHandler = useReportHandler;
		isDefaultReportHandler = false;
	}

	reverbModels[REVERB_MODE_ROOM] = new BReverbModel(REVERB_MODE_ROOM);
	reverbModels[REVERB_MODE_HALL] = new BReverbModel(REVERB_MODE_HALL);
	reverbModels[REVERB_MODE_PLATE] = new BReverbModel(REVERB_MODE_PLATE);
	reverbModels[REVERB_MODE_TAP_DELAY] = new BReverbModel(REVERB_MODE_TAP_DELAY);

	reverbModel = NULL;
	setDACInputMode(DACInputMode_NICE);
	setMIDIDelayMode(MIDIDelayMode_DELAY_SHORT_MESSAGES_ONLY);
	setOutputGain(1.0f);
	setReverbOutputGain(1.0f);
	partialManager = NULL;
	midiQueue = NULL;
	lastReceivedMIDIEventTimestamp = 0;
	memset(parts, 0, sizeof(parts));
	renderedSampleCount = 0;
}

Synth::~Synth() {
	close(); // Make sure we're closed and everything is freed
	for (int i = 0; i < 4; i++) {
		delete reverbModels[i];
	}
	if (isDefaultReportHandler) {
		delete reportHandler;
	}
}

void ReportHandler::showLCDMessage(const char *data) {
	printf("WRITE-LCD: %s", data);
	printf("\n");
}

void ReportHandler::printDebug(const char *fmt, va_list list) {
	vprintf(fmt, list);
	printf("\n");
}

void Synth::polyStateChanged(int partNum) {
	reportHandler->onPolyStateChanged(partNum);
}

void Synth::newTimbreSet(int partNum, Bit8u timbreGroup, const char patchName[]) {
	reportHandler->onProgramChanged(partNum, timbreGroup, patchName);
}

void Synth::printDebug(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
#if MT32EMU_DEBUG_SAMPLESTAMPS > 0
	reportHandler->printDebug("[%u] ", renderedSampleCount);
#endif
	reportHandler->printDebug(fmt, ap);
	va_end(ap);
}

void Synth::setReverbEnabled(bool newReverbEnabled) {
	reverbEnabled = newReverbEnabled;
}

bool Synth::isReverbEnabled() const {
	return reverbEnabled;
}

void Synth::setReverbOverridden(bool newReverbOverridden) {
	reverbOverridden = newReverbOverridden;
}

bool Synth::isReverbOverridden() const {
	return reverbOverridden;
}

void Synth::setDACInputMode(DACInputMode mode) {
	dacInputMode = mode;
}

DACInputMode Synth::getDACInputMode() const {
	return dacInputMode;
}

void Synth::setMIDIDelayMode(MIDIDelayMode mode) {
	midiDelayMode = mode;
}

MIDIDelayMode Synth::getMIDIDelayMode() const {
	return midiDelayMode;
}

void Synth::setOutputGain(float newOutputGain) {
	outputGain = newOutputGain;
}

float Synth::getOutputGain() const {
	return outputGain;
}

void Synth::setReverbOutputGain(float newReverbOutputGain) {
	reverbOutputGain = newReverbOutputGain;
}

float Synth::getReverbOutputGain() const {
	return reverbOutputGain;
}

bool Synth::loadControlROM(const ROMImage &controlROMImage) {
	if (&controlROMImage == NULL) return false;
	Common::File *file = controlROMImage.getFile();
	const ROMInfo *controlROMInfo = controlROMImage.getROMInfo();
	if ((controlROMInfo == NULL)
			|| (controlROMInfo->type != ROMInfo::Control)
			|| (controlROMInfo->pairType != ROMInfo::Full)) {
		return false;
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Found Control ROM: %s, %s", controlROMInfo->shortName, controlROMInfo->description);
#endif
	file->read(controlROMData, CONTROL_ROM_SIZE);

	// Control ROM successfully loaded, now check whether it's a known type
	controlROMMap = NULL;
	for (unsigned int i = 0; i < sizeof(ControlROMMaps) / sizeof(ControlROMMaps[0]); i++) {
		if (memcmp(&controlROMData[ControlROMMaps[i].idPos], ControlROMMaps[i].idBytes, ControlROMMaps[i].idLen) == 0) {
			controlROMMap = &ControlROMMaps[i];
			return true;
		}
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Control ROM failed to load");
#endif
	return false;
}

bool Synth::loadPCMROM(const ROMImage &pcmROMImage) {
	if (&pcmROMImage == NULL) return false;
	Common::File *file = pcmROMImage.getFile();
	const ROMInfo *pcmROMInfo = pcmROMImage.getROMInfo();
	if ((pcmROMInfo == NULL)
			|| (pcmROMInfo->type != ROMInfo::PCM)
			|| (pcmROMInfo->pairType != ROMInfo::Full)) {
		return false;
	}
#if MT32EMU_MONITOR_INIT
	printDebug("Found PCM ROM: %s, %s", pcmROMInfo->shortName, pcmROMInfo->description);
#endif
	size_t fileSize = file->size();
	if (fileSize != (2 * pcmROMSize)) {
#if MT32EMU_MONITOR_INIT
		printDebug("PCM ROM file has wrong size (expected %d, got %d)", 2 * pcmROMSize, fileSize);
#endif
		return false;
	}

	byte *buffer = new byte[file->size()];
	file->read(buffer, file->size());
	const byte *fileData = buffer;
	for (size_t i = 0; i < pcmROMSize; i++) {
		Bit8u s = *(fileData++);
		Bit8u c = *(fileData++);

		int order[16] = {0, 9, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 8};

		signed short log = 0;
		for (int u = 0; u < 15; u++) {
			int bit;
			if (order[u] < 8) {
				bit = (s >> (7 - order[u])) & 0x1;
			} else {
				bit = (c >> (7 - (order[u] - 8))) & 0x1;
			}
			log = log | (short)(bit << (15 - u));
		}
		pcmROMData[i] = log;
	}

	delete[] buffer;

	return true;
}

bool Synth::initPCMList(Bit16u mapAddress, Bit16u count) {
	ControlROMPCMStruct *tps = (ControlROMPCMStruct *)&controlROMData[mapAddress];
	for (int i = 0; i < count; i++) {
		Bit32u rAddr = tps[i].pos * 0x800;
		Bit32u rLenExp = (tps[i].len & 0x70) >> 4;
		Bit32u rLen = 0x800 << rLenExp;
		if (rAddr + rLen > pcmROMSize) {
			printDebug("Control ROM error: Wave map entry %d points to invalid PCM address 0x%04X, length 0x%04X", i, rAddr, rLen);
			return false;
		}
		pcmWaves[i].addr = rAddr;
		pcmWaves[i].len = rLen;
		pcmWaves[i].loop = (tps[i].len & 0x80) != 0;
		pcmWaves[i].controlROMPCMStruct = &tps[i];
		//int pitch = (tps[i].pitchMSB << 8) | tps[i].pitchLSB;
		//bool unaffectedByMasterTune = (tps[i].len & 0x01) == 0;
		//printDebug("PCM %d: pos=%d, len=%d, pitch=%d, loop=%s, unaffectedByMasterTune=%s", i, rAddr, rLen, pitch, pcmWaves[i].loop ? "YES" : "NO", unaffectedByMasterTune ? "YES" : "NO");
	}
	return false;
}

bool Synth::initCompressedTimbre(int timbreNum, const Bit8u *src, unsigned int srcLen) {
	// "Compressed" here means that muted partials aren't present in ROM (except in the case of partial 0 being muted).
	// Instead the data from the previous unmuted partial is used.
	if (srcLen < sizeof(TimbreParam::CommonParam)) {
		return false;
	}
	TimbreParam *timbre = &mt32ram.timbres[timbreNum].timbre;
	timbresMemoryRegion->write(timbreNum, 0, src, sizeof(TimbreParam::CommonParam), true);
	unsigned int srcPos = sizeof(TimbreParam::CommonParam);
	unsigned int memPos = sizeof(TimbreParam::CommonParam);
	for (int t = 0; t < 4; t++) {
		if (t != 0 && ((timbre->common.partialMute >> t) & 0x1) == 0x00) {
			// This partial is muted - we'll copy the previously copied partial, then
			srcPos -= sizeof(TimbreParam::PartialParam);
		} else if (srcPos + sizeof(TimbreParam::PartialParam) >= srcLen) {
			return false;
		}
		timbresMemoryRegion->write(timbreNum, memPos, src + srcPos, sizeof(TimbreParam::PartialParam));
		srcPos += sizeof(TimbreParam::PartialParam);
		memPos += sizeof(TimbreParam::PartialParam);
	}
	return true;
}

bool Synth::initTimbres(Bit16u mapAddress, Bit16u offset, int count, int startTimbre, bool compressed) {
	const Bit8u *timbreMap = &controlROMData[mapAddress];
	for (Bit16u i = 0; i < count * 2; i += 2) {
		Bit16u address = (timbreMap[i + 1] << 8) | timbreMap[i];
		if (!compressed && (address + offset + sizeof(TimbreParam) > CONTROL_ROM_SIZE)) {
			printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre address 0x%04x", i, startTimbre, address);
			return false;
		}
		address += offset;
		if (compressed) {
			if (!initCompressedTimbre(startTimbre, &controlROMData[address], CONTROL_ROM_SIZE - address)) {
				printDebug("Control ROM error: Timbre map entry 0x%04x for timbre %d points to invalid timbre at 0x%04x", i, startTimbre, address);
				return false;
			}
		} else {
			timbresMemoryRegion->write(startTimbre, 0, &controlROMData[address], sizeof(TimbreParam), true);
		}
		startTimbre++;
	}
	return true;
}

bool Synth::open(const ROMImage &controlROMImage, const ROMImage &pcmROMImage, unsigned int usePartialCount) {
	if (isOpen) {
		return false;
	}
	partialCount = usePartialCount;
	abortingPoly = NULL;
#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Constant Tables");
#endif
#if !MT32EMU_REDUCE_REVERB_MEMORY
	for (int i = REVERB_MODE_ROOM; i <= REVERB_MODE_TAP_DELAY; i++) {
		reverbModels[i]->open();
	}
#endif

	// This is to help detect bugs
	memset(&mt32ram, '?', sizeof(mt32ram));

#if MT32EMU_MONITOR_INIT
	printDebug("Loading Control ROM");
#endif
	if (!loadControlROM(controlROMImage)) {
		printDebug("Init Error - Missing or invalid Control ROM image");
		reportHandler->onErrorControlROM();
		return false;
	}

	initMemoryRegions();

	// 512KB PCM ROM for MT-32, etc.
	// 1MB PCM ROM for CM-32L, LAPC-I, CM-64, CM-500
	// Note that the size below is given in samples (16-bit), not bytes
	pcmROMSize = controlROMMap->pcmCount == 256 ? 512 * 1024 : 256 * 1024;
	pcmROMData = new Bit16s[pcmROMSize];

#if MT32EMU_MONITOR_INIT
	printDebug("Loading PCM ROM");
#endif
	if (!loadPCMROM(pcmROMImage)) {
		printDebug("Init Error - Missing PCM ROM image");
		reportHandler->onErrorPCMROM();
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank A");
#endif
	if (!initTimbres(controlROMMap->timbreAMap, controlROMMap->timbreAOffset, 0x40, 0, controlROMMap->timbreACompressed)) {
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank B");
#endif
	if (!initTimbres(controlROMMap->timbreBMap, controlROMMap->timbreBOffset, 0x40, 64, controlROMMap->timbreBCompressed)) {
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank R");
#endif
	if (!initTimbres(controlROMMap->timbreRMap, 0, controlROMMap->timbreRCount, 192, true)) {
		return false;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Timbre Bank M");
#endif
	// CM-64 seems to initialise all bytes in this bank to 0.
	memset(&mt32ram.timbres[128], 0, sizeof(mt32ram.timbres[128]) * 64);

	partialManager = new PartialManager(this, parts);

	pcmWaves = new PCMWaveEntry[controlROMMap->pcmCount];

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising PCM List");
#endif
	initPCMList(controlROMMap->pcmTable, controlROMMap->pcmCount);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Rhythm Temp");
#endif
	memcpy(mt32ram.rhythmTemp, &controlROMData[controlROMMap->rhythmSettings], controlROMMap->rhythmSettingsCount * 4);

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising Patches");
#endif
	for (Bit8u i = 0; i < 128; i++) {
		PatchParam *patch = &mt32ram.patches[i];
		patch->timbreGroup = i / 64;
		patch->timbreNum = i % 64;
		patch->keyShift = 24;
		patch->fineTune = 50;
		patch->benderRange = 12;
		patch->assignMode = 0;
		patch->reverbSwitch = 1;
		patch->dummy = 0;
	}

#if MT32EMU_MONITOR_INIT
	printDebug("Initialising System");
#endif
	// The MT-32 manual claims that "Standard pitch" is 442Hz.
	mt32ram.system.masterTune = 0x4A; // Confirmed on CM-64
	mt32ram.system.reverbMode = 0; // Confirmed
	mt32ram.system.reverbTime = 5; // Confirmed
	mt32ram.system.reverbLevel = 3; // Confirmed
	memcpy(mt32ram.system.reserveSettings, &controlROMData[controlROMMap->reserveSettings], 9); // Confirmed
	for (Bit8u i = 0; i < 9; i++) {
		// This is the default: {1, 2, 3, 4, 5, 6, 7, 8, 9}
		// An alternative configuration can be selected by holding "Master Volume"
		// and pressing "PART button 1" on the real MT-32's frontpanel.
		// The channel assignment is then {0, 1, 2, 3, 4, 5, 6, 7, 9}
		mt32ram.system.chanAssign[i] = i + 1;
	}
	mt32ram.system.masterVol = 100; // Confirmed
	refreshSystem();

	for (int i = 0; i < 9; i++) {
		MemParams::PatchTemp *patchTemp = &mt32ram.patchTemp[i];

		// Note that except for the rhythm part, these patch fields will be set in setProgram() below anyway.
		patchTemp->patch.timbreGroup = 0;
		patchTemp->patch.timbreNum = 0;
		patchTemp->patch.keyShift = 24;
		patchTemp->patch.fineTune = 50;
		patchTemp->patch.benderRange = 12;
		patchTemp->patch.assignMode = 0;
		patchTemp->patch.reverbSwitch = 1;
		patchTemp->patch.dummy = 0;

		patchTemp->outputLevel = 80;
		patchTemp->panpot = controlROMData[controlROMMap->panSettings + i];
		memset(patchTemp->dummyv, 0, sizeof(patchTemp->dummyv));
		patchTemp->dummyv[1] = 127;

		if (i < 8) {
			parts[i] = new Part(this, i);
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[i] = new RhythmPart(this, i);
		}
	}

	// For resetting mt32 mid-execution
	mt32default = mt32ram;

	midiQueue = new MidiEventQueue();

	isOpen = true;
	isEnabled = false;

#if MT32EMU_MONITOR_INIT
	printDebug("*** Initialisation complete ***");
#endif
	return true;
}

void Synth::close() {
	if (!isOpen) {
		return;
	}

	delete midiQueue;
	midiQueue = NULL;

	delete partialManager;
	partialManager = NULL;

	for (int i = 0; i < 9; i++) {
		delete parts[i];
		parts[i] = NULL;
	}

	delete[] pcmWaves;
	delete[] pcmROMData;

	deleteMemoryRegions();

	for (int i = 0; i < 4; i++) {
		reverbModels[i]->close();
	}
	reverbModel = NULL;
	isOpen = false;
}

void Synth::flushMIDIQueue() {
	if (midiQueue != NULL) {
		for (;;) {
			const MidiEvent *midiEvent = midiQueue->peekMidiEvent();
			if (midiEvent == NULL) break;
			if (midiEvent->sysexData == NULL) {
				playMsgNow(midiEvent->shortMessageData);
			} else {
				playSysexNow(midiEvent->sysexData, midiEvent->sysexLength);
			}
			midiQueue->dropMidiEvent();
		}
		lastReceivedMIDIEventTimestamp = renderedSampleCount;
	}
}

void Synth::setMIDIEventQueueSize(Bit32u useSize) {
	if (midiQueue != NULL) {
		flushMIDIQueue();
		delete midiQueue;
		midiQueue = new MidiEventQueue(useSize);
	}
}

Bit32u Synth::getShortMessageLength(Bit32u msg) {
	if ((msg & 0xF0) == 0xF0) return 1;
	// NOTE: This calculation isn't quite correct
	// as it doesn't consider the running status byte
	return ((msg & 0xE0) == 0xC0) ? 2 : 3;
}

Bit32u Synth::addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp) {
	Bit32u transferTime =  Bit32u((double)len * MIDI_DATA_TRANSFER_RATE);
	// Dealing with wrapping
	if (Bit32s(timestamp - lastReceivedMIDIEventTimestamp) < 0) {
		timestamp = lastReceivedMIDIEventTimestamp;
	}
	timestamp += transferTime;
	lastReceivedMIDIEventTimestamp = timestamp;
	return timestamp;
}

bool Synth::playMsg(Bit32u msg) {
	return playMsg(msg, renderedSampleCount);
}

bool Synth::playMsg(Bit32u msg, Bit32u timestamp) {
	if (midiQueue == NULL) return false;
	if (midiDelayMode != MIDIDelayMode_IMMEDIATE) {
		timestamp = addMIDIInterfaceDelay(getShortMessageLength(msg), timestamp);
	}
	return midiQueue->pushShortMessage(msg, timestamp);
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len) {
	return playSysex(sysex, len, renderedSampleCount);
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp) {
	if (midiQueue == NULL) return false;
	if (midiDelayMode == MIDIDelayMode_DELAY_ALL) {
		timestamp = addMIDIInterfaceDelay(len, timestamp);
	}
	return midiQueue->pushSysex(sysex, len, timestamp);
}

void Synth::playMsgNow(Bit32u msg) {
	// FIXME: Implement active sensing
	unsigned char code     = (unsigned char)((msg & 0x0000F0) >> 4);
	unsigned char chan     = (unsigned char)(msg & 0x00000F);
	unsigned char note     = (unsigned char)((msg & 0x007F00) >> 8);
	unsigned char velocity = (unsigned char)((msg & 0x7F0000) >> 16);
	isEnabled = true;

	//printDebug("Playing chan %d, code 0x%01x note: 0x%02x", chan, code, note);

	char part = chantable[chan];
	if (part < 0 || part > 8) {
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Play msg on unreg chan %d (%d): code=0x%01x, vel=%d", chan, part, code, velocity);
#endif
		return;
	}
	playMsgOnPart(part, code, note, velocity);
}

void Synth::playMsgOnPart(unsigned char part, unsigned char code, unsigned char note, unsigned char velocity) {
	Bit32u bend;

	//printDebug("Synth::playMsgOnPart(%02x, %02x, %02x, %02x)", part, code, note, velocity);
	switch (code) {
	case 0x8:
		//printDebug("Note OFF - Part %d", part);
		// The MT-32 ignores velocity for note off
		parts[part]->noteOff(note);
		break;
	case 0x9:
		//printDebug("Note ON - Part %d, Note %d Vel %d", part, note, velocity);
		if (velocity == 0) {
			// MIDI defines note-on with velocity 0 as being the same as note-off with velocity 40
			parts[part]->noteOff(note);
		} else {
			parts[part]->noteOn(note, velocity);
		}
		break;
	case 0xB: // Control change
		switch (note) {
		case 0x01:  // Modulation
			//printDebug("Modulation: %d", velocity);
			parts[part]->setModulation(velocity);
			break;
		case 0x06:
			parts[part]->setDataEntryMSB(velocity);
			break;
		case 0x07:  // Set volume
			//printDebug("Volume set: %d", velocity);
			parts[part]->setVolume(velocity);
			break;
		case 0x0A:  // Pan
			//printDebug("Pan set: %d", velocity);
			parts[part]->setPan(velocity);
			break;
		case 0x0B:
			//printDebug("Expression set: %d", velocity);
			parts[part]->setExpression(velocity);
			break;
		case 0x40: // Hold (sustain) pedal
			//printDebug("Hold pedal set: %d", velocity);
			parts[part]->setHoldPedal(velocity >= 64);
			break;

		case 0x62:
		case 0x63:
			parts[part]->setNRPN();
			break;
		case 0x64:
			parts[part]->setRPNLSB(velocity);
			break;
		case 0x65:
			parts[part]->setRPNMSB(velocity);
			break;

		case 0x79: // Reset all controllers
			//printDebug("Reset all controllers");
			parts[part]->resetAllControllers();
			break;

		case 0x7B: // All notes off
			//printDebug("All notes off");
			parts[part]->allNotesOff();
			break;

		case 0x7C:
		case 0x7D:
		case 0x7E:
		case 0x7F:
			// CONFIRMED:Mok: A real LAPC-I responds to these controllers as follows:
			parts[part]->setHoldPedal(false);
			parts[part]->allNotesOff();
			break;

		default:
#if MT32EMU_MONITOR_MIDI > 0
			printDebug("Unknown MIDI Control code: 0x%02x - vel 0x%02x", note, velocity);
#endif
			return;
		}

		break;
	case 0xC: // Program change
		//printDebug("Program change %01x", note);
		parts[part]->setProgram(note);
		break;
	case 0xE: // Pitch bender
		bend = (velocity << 7) | (note);
		//printDebug("Pitch bender %02x", bend);
		parts[part]->setBend(bend);
		break;
	default:
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Unknown Midi code: 0x%01x - %02x - %02x", code, note, velocity);
#endif
		return;
	}
	reportHandler->onMIDIMessagePlayed();
}

void Synth::playSysexNow(const Bit8u *sysex, Bit32u len) {
	if (len < 2) {
		printDebug("playSysex: Message is too short for sysex (%d bytes)", len);
	}
	if (sysex[0] != 0xF0) {
		printDebug("playSysex: Message lacks start-of-sysex (0xF0)");
		return;
	}
	// Due to some programs (e.g. Java) sending buffers with junk at the end, we have to go through and find the end marker rather than relying on len.
	Bit32u endPos;
	for (endPos = 1; endPos < len; endPos++) {
		if (sysex[endPos] == 0xF7) {
			break;
		}
	}
	if (endPos == len) {
		printDebug("playSysex: Message lacks end-of-sysex (0xf7)");
		return;
	}
	playSysexWithoutFraming(sysex + 1, endPos - 1);
}

void Synth::playSysexWithoutFraming(const Bit8u *sysex, Bit32u len) {
	if (len < 4) {
		printDebug("playSysexWithoutFraming: Message is too short (%d bytes)!", len);
		return;
	}
	if (sysex[0] != SYSEX_MANUFACTURER_ROLAND) {
		printDebug("playSysexWithoutFraming: Header not intended for this device manufacturer: %02x %02x %02x %02x", (int)sysex[0], (int)sysex[1], (int)sysex[2], (int)sysex[3]);
		return;
	}
	if (sysex[2] == SYSEX_MDL_D50) {
		printDebug("playSysexWithoutFraming: Header is intended for model D-50 (not yet supported): %02x %02x %02x %02x", (int)sysex[0], (int)sysex[1], (int)sysex[2], (int)sysex[3]);
		return;
	} else if (sysex[2] != SYSEX_MDL_MT32) {
		printDebug("playSysexWithoutFraming: Header not intended for model MT-32: %02x %02x %02x %02x", (int)sysex[0], (int)sysex[1], (int)sysex[2], (int)sysex[3]);
		return;
	}
	playSysexWithoutHeader(sysex[1], sysex[3], sysex + 4, len - 4);
}

void Synth::playSysexWithoutHeader(unsigned char device, unsigned char command, const Bit8u *sysex, Bit32u len) {
	if (device > 0x10) {
		// We have device ID 0x10 (default, but changeable, on real MT-32), < 0x10 is for channels
		printDebug("playSysexWithoutHeader: Message is not intended for this device ID (provided: %02x, expected: 0x10 or channel)", (int)device);
		return;
	}
	// This is checked early in the real devices (before any sysex length checks or further processing)
	// FIXME: Response to SYSEX_CMD_DAT reset with partials active (and in general) is untested.
	if ((command == SYSEX_CMD_DT1 || command == SYSEX_CMD_DAT) && sysex[0] == 0x7F) {
		reset();
		return;
	}
	if (len < 4) {
		printDebug("playSysexWithoutHeader: Message is too short (%d bytes)!", len);
		return;
	}
	unsigned char checksum = calcSysexChecksum(sysex, len - 1, 0);
	if (checksum != sysex[len - 1]) {
		printDebug("playSysexWithoutHeader: Message checksum is incorrect (provided: %02x, expected: %02x)!", sysex[len - 1], checksum);
		return;
	}
	len -= 1; // Exclude checksum
	switch (command) {
	case SYSEX_CMD_DAT:
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_DAT but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		// Deliberate fall-through
	case SYSEX_CMD_DT1:
		writeSysex(device, sysex, len);
		break;
	case SYSEX_CMD_RQD:
		if (hasActivePartials()) {
			printDebug("playSysexWithoutHeader: Got SYSEX_CMD_RQD but partials are active - ignoring");
			// FIXME: We should send SYSEX_CMD_RJC in this case
			break;
		}
		// Deliberate fall-through
	case SYSEX_CMD_RQ1:
		readSysex(device, sysex, len);
		break;
	default:
		printDebug("playSysexWithoutHeader: Unsupported command %02x", command);
		return;
	}
}

void Synth::readSysex(unsigned char /*device*/, const Bit8u * /*sysex*/, Bit32u /*len*/) const {
	// NYI
}

void Synth::writeSysex(unsigned char device, const Bit8u *sysex, Bit32u len) {
	reportHandler->onMIDIMessagePlayed();
	Bit32u addr = (sysex[0] << 16) | (sysex[1] << 8) | (sysex[2]);
	addr = MT32EMU_MEMADDR(addr);
	sysex += 3;
	len -= 3;
	//printDebug("Sysex addr: 0x%06x", MT32EMU_SYSEXMEMADDR(addr));
	// NOTE: Please keep both lower and upper bounds in each check, for ease of reading

	// Process channel-specific sysex by converting it to device-global
	if (device < 0x10) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-CHANNEL: Channel %d temp area 0x%06x", device, MT32EMU_SYSEXMEMADDR(addr));
#endif
		if (/*addr >= MT32EMU_MEMADDR(0x000000) && */addr < MT32EMU_MEMADDR(0x010000)) {
			int offset;
			if (chantable[device] == -1) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
				offset = 0;
			} else if (chantable[device] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
				offset = 0;
			} else {
				offset = chantable[device] * sizeof(MemParams::PatchTemp);
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Setting extra offset to %d)", offset);
#endif
			}
			addr += MT32EMU_MEMADDR(0x030000) + offset;
		} else if (/*addr >= MT32EMU_MEMADDR(0x010000) && */ addr < MT32EMU_MEMADDR(0x020000)) {
			addr += MT32EMU_MEMADDR(0x030110) - MT32EMU_MEMADDR(0x010000);
		} else if (/*addr >= MT32EMU_MEMADDR(0x020000) && */ addr < MT32EMU_MEMADDR(0x030000)) {
			int offset;
			if (chantable[device] == -1) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel not mapped to a part... 0 offset)");
#endif
				offset = 0;
			} else if (chantable[device] == 8) {
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Channel mapped to rhythm... 0 offset)");
#endif
				offset = 0;
			} else {
				offset = chantable[device] * sizeof(TimbreParam);
#if MT32EMU_MONITOR_SYSEX > 0
				printDebug(" (Setting extra offset to %d)", offset);
#endif
			}
			addr += MT32EMU_MEMADDR(0x040000) - MT32EMU_MEMADDR(0x020000) + offset;
		} else {
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug(" Invalid channel");
#endif
			return;
		}
	}

	// Process device-global sysex (possibly converted from channel-specific sysex above)
	for (;;) {
		// Find the appropriate memory region
		const MemoryRegion *region = findMemoryRegion(addr);

		if (region == NULL) {
			printDebug("Sysex write to unrecognised address %06x, len %d", MT32EMU_SYSEXMEMADDR(addr), len);
			break;
		}
		writeMemoryRegion(region, addr, region->getClampedLen(addr, len), sysex);

		Bit32u next = region->next(addr, len);
		if (next == 0) {
			break;
		}
		addr += next;
		sysex += next;
		len -= next;
	}
}

void Synth::readMemory(Bit32u addr, Bit32u len, Bit8u *data) {
	const MemoryRegion *region = findMemoryRegion(addr);
	if (region != NULL) {
		readMemoryRegion(region, addr, len, data);
	}
}

void Synth::initMemoryRegions() {
	// Timbre max tables are slightly more complicated than the others, which are used directly from the ROM.
	// The ROM (sensibly) just has maximums for TimbreParam.commonParam followed by just one TimbreParam.partialParam,
	// so we produce a table with all partialParams filled out, as well as padding for PaddedTimbre, for quick lookup.
	paddedTimbreMaxTable = new Bit8u[sizeof(MemParams::PaddedTimbre)];
	memcpy(&paddedTimbreMaxTable[0], &controlROMData[controlROMMap->timbreMaxTable], sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam)); // commonParam and one partialParam
	int pos = sizeof(TimbreParam::CommonParam) + sizeof(TimbreParam::PartialParam);
	for (int i = 0; i < 3; i++) {
		memcpy(&paddedTimbreMaxTable[pos], &controlROMData[controlROMMap->timbreMaxTable + sizeof(TimbreParam::CommonParam)], sizeof(TimbreParam::PartialParam));
		pos += sizeof(TimbreParam::PartialParam);
	}
	memset(&paddedTimbreMaxTable[pos], 0, 10); // Padding
	patchTempMemoryRegion = new PatchTempMemoryRegion(this, (Bit8u *)&mt32ram.patchTemp[0], &controlROMData[controlROMMap->patchMaxTable]);
	rhythmTempMemoryRegion = new RhythmTempMemoryRegion(this, (Bit8u *)&mt32ram.rhythmTemp[0], &controlROMData[controlROMMap->rhythmMaxTable]);
	timbreTempMemoryRegion = new TimbreTempMemoryRegion(this, (Bit8u *)&mt32ram.timbreTemp[0], paddedTimbreMaxTable);
	patchesMemoryRegion = new PatchesMemoryRegion(this, (Bit8u *)&mt32ram.patches[0], &controlROMData[controlROMMap->patchMaxTable]);
	timbresMemoryRegion = new TimbresMemoryRegion(this, (Bit8u *)&mt32ram.timbres[0], paddedTimbreMaxTable);
	systemMemoryRegion = new SystemMemoryRegion(this, (Bit8u *)&mt32ram.system, &controlROMData[controlROMMap->systemMaxTable]);
	displayMemoryRegion = new DisplayMemoryRegion(this);
	resetMemoryRegion = new ResetMemoryRegion(this);
}

void Synth::deleteMemoryRegions() {
	delete patchTempMemoryRegion;
	patchTempMemoryRegion = NULL;
	delete rhythmTempMemoryRegion;
	rhythmTempMemoryRegion = NULL;
	delete timbreTempMemoryRegion;
	timbreTempMemoryRegion = NULL;
	delete patchesMemoryRegion;
	patchesMemoryRegion = NULL;
	delete timbresMemoryRegion;
	timbresMemoryRegion = NULL;
	delete systemMemoryRegion;
	systemMemoryRegion = NULL;
	delete displayMemoryRegion;
	displayMemoryRegion = NULL;
	delete resetMemoryRegion;
	resetMemoryRegion = NULL;

	delete[] paddedTimbreMaxTable;
	paddedTimbreMaxTable = NULL;
}

MemoryRegion *Synth::findMemoryRegion(Bit32u addr) {
	MemoryRegion *regions[] = {
		patchTempMemoryRegion,
		rhythmTempMemoryRegion,
		timbreTempMemoryRegion,
		patchesMemoryRegion,
		timbresMemoryRegion,
		systemMemoryRegion,
		displayMemoryRegion,
		resetMemoryRegion,
		NULL
	};
	for (int pos = 0; regions[pos] != NULL; pos++) {
		if (regions[pos]->contains(addr)) {
			return regions[pos];
		}
	}
	return NULL;
}

void Synth::readMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	//unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	len = region->getClampedLen(addr, len);

	unsigned int m;

	if (region->isReadable()) {
		region->read(first, off, data, len);
	} else {
		// FIXME: We might want to do these properly in future
		for (m = 0; m < len; m += 2) {
			data[m] = 0xff;
			if (m + 1 < len) {
				data[m+1] = (Bit8u)region->type;
			}
		}
	}
}

void Synth::writeMemoryRegion(const MemoryRegion *region, Bit32u addr, Bit32u len, const Bit8u *data) {
	unsigned int first = region->firstTouched(addr);
	unsigned int last = region->lastTouched(addr, len);
	unsigned int off = region->firstTouchedOffset(addr);
	switch (region->type) {
	case MR_PatchTemp:
		region->write(first, off, data, len);
		//printDebug("Patch temp: Patch %d, offset %x, len %d", off/16, off % 16, len);

		for (unsigned int i = first; i <= last; i++) {
			int absTimbreNum = mt32ram.patchTemp[i].patch.timbreGroup * 64 + mt32ram.patchTemp[i].patch.timbreNum;
			char timbreName[11];
			memcpy(timbreName, mt32ram.timbres[absTimbreNum].timbre.common.name, 10);
			timbreName[10] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-PARTPATCH (%d-%d@%d..%d): %d; timbre=%d (%s), outlevel=%d", first, last, off, off + len, i, absTimbreNum, timbreName, mt32ram.patchTemp[i].outputLevel);
#endif
			if (parts[i] != NULL) {
				if (i != 8) {
					// Note: Confirmed on CM-64 that we definitely *should* update the timbre here,
					// but only in the case that the sysex actually writes to those values
					if (i == first && off > 2) {
#if MT32EMU_MONITOR_SYSEX > 0
						printDebug(" (Not updating timbre, since those values weren't touched)");
#endif
					} else {
						parts[i]->setTimbre(&mt32ram.timbres[parts[i]->getAbsTimbreNum()].timbre);
					}
				}
				parts[i]->refresh();
			}
		}
		break;
	case MR_RhythmTemp:
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
			int timbreNum = mt32ram.rhythmTemp[i].timbre;
			char timbreName[11];
			if (timbreNum < 94) {
				memcpy(timbreName, mt32ram.timbres[128 + timbreNum].timbre.common.name, 10);
				timbreName[10] = 0;
			} else {
				strcpy(timbreName, "[None]");
			}
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-RHYTHM (%d-%d@%d..%d): %d; level=%02x, panpot=%02x, reverb=%02x, timbre=%d (%s)", first, last, off, off + len, i, mt32ram.rhythmTemp[i].outputLevel, mt32ram.rhythmTemp[i].panpot, mt32ram.rhythmTemp[i].reverbSwitch, mt32ram.rhythmTemp[i].timbre, timbreName);
#endif
		}
		if (parts[8] != NULL) {
			parts[8]->refresh();
		}
		break;
	case MR_TimbreTemp:
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbreTemp[i].common.name, 10);
			instrumentName[10] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
			printDebug("WRITE-PARTTIMBRE (%d-%d@%d..%d): timbre=%d (%s)", first, last, off, off + len, i, instrumentName);
#endif
			if (parts[i] != NULL) {
				parts[i]->refresh();
			}
		}
		break;
	case MR_Patches:
		region->write(first, off, data, len);
#if MT32EMU_MONITOR_SYSEX > 0
		for (unsigned int i = first; i <= last; i++) {
			PatchParam *patch = &mt32ram.patches[i];
			int patchAbsTimbreNum = patch->timbreGroup * 64 + patch->timbreNum;
			char instrumentName[11];
			memcpy(instrumentName, mt32ram.timbres[patchAbsTimbreNum].timbre.common.name, 10);
			instrumentName[10] = 0;
			Bit8u *n = (Bit8u *)patch;
			printDebug("WRITE-PATCH (%d-%d@%d..%d): %d; timbre=%d (%s) %02X%02X%02X%02X%02X%02X%02X%02X", first, last, off, off + len, i, patchAbsTimbreNum, instrumentName, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7]);
		}
#endif
		break;
	case MR_Timbres:
		// Timbres
		first += 128;
		last += 128;
		region->write(first, off, data, len);
		for (unsigned int i = first; i <= last; i++) {
#if MT32EMU_MONITOR_TIMBRES >= 1
			TimbreParam *timbre = &mt32ram.timbres[i].timbre;
			char instrumentName[11];
			memcpy(instrumentName, timbre->common.name, 10);
			instrumentName[10] = 0;
			printDebug("WRITE-TIMBRE (%d-%d@%d..%d): %d; name=\"%s\"", first, last, off, off + len, i, instrumentName);
#if MT32EMU_MONITOR_TIMBRES >= 2
#define DT(x) printDebug(" " #x ": %d", timbre->x)
			DT(common.partialStructure12);
			DT(common.partialStructure34);
			DT(common.partialMute);
			DT(common.noSustain);

#define DTP(x) \
			DT(partial[x].wg.pitchCoarse); \
			DT(partial[x].wg.pitchFine); \
			DT(partial[x].wg.pitchKeyfollow); \
			DT(partial[x].wg.pitchBenderEnabled); \
			DT(partial[x].wg.waveform); \
			DT(partial[x].wg.pcmWave); \
			DT(partial[x].wg.pulseWidth); \
			DT(partial[x].wg.pulseWidthVeloSensitivity); \
			DT(partial[x].pitchEnv.depth); \
			DT(partial[x].pitchEnv.veloSensitivity); \
			DT(partial[x].pitchEnv.timeKeyfollow); \
			DT(partial[x].pitchEnv.time[0]); \
			DT(partial[x].pitchEnv.time[1]); \
			DT(partial[x].pitchEnv.time[2]); \
			DT(partial[x].pitchEnv.time[3]); \
			DT(partial[x].pitchEnv.level[0]); \
			DT(partial[x].pitchEnv.level[1]); \
			DT(partial[x].pitchEnv.level[2]); \
			DT(partial[x].pitchEnv.level[3]); \
			DT(partial[x].pitchEnv.level[4]); \
			DT(partial[x].pitchLFO.rate); \
			DT(partial[x].pitchLFO.depth); \
			DT(partial[x].pitchLFO.modSensitivity); \
			DT(partial[x].tvf.cutoff); \
			DT(partial[x].tvf.resonance); \
			DT(partial[x].tvf.keyfollow); \
			DT(partial[x].tvf.biasPoint); \
			DT(partial[x].tvf.biasLevel); \
			DT(partial[x].tvf.envDepth); \
			DT(partial[x].tvf.envVeloSensitivity); \
			DT(partial[x].tvf.envDepthKeyfollow); \
			DT(partial[x].tvf.envTimeKeyfollow); \
			DT(partial[x].tvf.envTime[0]); \
			DT(partial[x].tvf.envTime[1]); \
			DT(partial[x].tvf.envTime[2]); \
			DT(partial[x].tvf.envTime[3]); \
			DT(partial[x].tvf.envTime[4]); \
			DT(partial[x].tvf.envLevel[0]); \
			DT(partial[x].tvf.envLevel[1]); \
			DT(partial[x].tvf.envLevel[2]); \
			DT(partial[x].tvf.envLevel[3]); \
			DT(partial[x].tva.level); \
			DT(partial[x].tva.veloSensitivity); \
			DT(partial[x].tva.biasPoint1); \
			DT(partial[x].tva.biasLevel1); \
			DT(partial[x].tva.biasPoint2); \
			DT(partial[x].tva.biasLevel2); \
			DT(partial[x].tva.envTimeKeyfollow); \
			DT(partial[x].tva.envTimeVeloSensitivity); \
			DT(partial[x].tva.envTime[0]); \
			DT(partial[x].tva.envTime[1]); \
			DT(partial[x].tva.envTime[2]); \
			DT(partial[x].tva.envTime[3]); \
			DT(partial[x].tva.envTime[4]); \
			DT(partial[x].tva.envLevel[0]); \
			DT(partial[x].tva.envLevel[1]); \
			DT(partial[x].tva.envLevel[2]); \
			DT(partial[x].tva.envLevel[3]);

			DTP(0);
			DTP(1);
			DTP(2);
			DTP(3);
#undef DTP
#undef DT
#endif
#endif
			// FIXME:KG: Not sure if the stuff below should be done (for rhythm and/or parts)...
			// Does the real MT-32 automatically do this?
			for (unsigned int part = 0; part < 9; part++) {
				if (parts[part] != NULL) {
					parts[part]->refreshTimbre(i);
				}
			}
		}
		break;
	case MR_System:
		region->write(0, off, data, len);

		reportHandler->onDeviceReconfig();
		// FIXME: We haven't properly confirmed any of this behaviour
		// In particular, we tend to reset things such as reverb even if the write contained
		// the same parameters as were already set, which may be wrong.
		// On the other hand, the real thing could be resetting things even when they aren't touched
		// by the write at all.
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-SYSTEM:");
#endif
		if (off <= SYSTEM_MASTER_TUNE_OFF && off + len > SYSTEM_MASTER_TUNE_OFF) {
			refreshSystemMasterTune();
		}
		if (off <= SYSTEM_REVERB_LEVEL_OFF && off + len > SYSTEM_REVERB_MODE_OFF) {
			refreshSystemReverbParameters();
		}
		if (off <= SYSTEM_RESERVE_SETTINGS_END_OFF && off + len > SYSTEM_RESERVE_SETTINGS_START_OFF) {
			refreshSystemReserveSettings();
		}
		if (off <= SYSTEM_CHAN_ASSIGN_END_OFF && off + len > SYSTEM_CHAN_ASSIGN_START_OFF) {
			int firstPart = off - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(firstPart < 0)
				firstPart = 0;
			int lastPart = off + len - SYSTEM_CHAN_ASSIGN_START_OFF;
			if(lastPart > 9)
				lastPart = 9;
			refreshSystemChanAssign(firstPart, lastPart);
		}
		if (off <= SYSTEM_MASTER_VOL_OFF && off + len > SYSTEM_MASTER_VOL_OFF) {
			refreshSystemMasterVol();
		}
		break;
	case MR_Display:
		char buf[MAX_SYSEX_SIZE];
		memcpy(&buf, &data[0], len);
		buf[len] = 0;
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug("WRITE-LCD: %s", buf);
#endif
		reportHandler->showLCDMessage(buf);
		break;
	case MR_Reset:
		reset();
		break;
	}
}

void Synth::refreshSystemMasterTune() {
#if MT32EMU_MONITOR_SYSEX > 0
	//FIXME:KG: This is just an educated guess.
	// The LAPC-I documentation claims a range of 427.5Hz-452.6Hz (similar to what we have here)
	// The MT-32 documentation claims a range of 432.1Hz-457.6Hz
	float masterTune = 440.0f * EXP2F((mt32ram.system.masterTune - 64.0f) / (128.0f * 12.0f));
	printDebug(" Master Tune: %f", masterTune);
#endif
}

void Synth::refreshSystemReverbParameters() {
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Reverb: mode=%d, time=%d, level=%d", mt32ram.system.reverbMode, mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
#endif
	if (reverbOverridden && reverbModel != NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		printDebug(" (Reverb overridden - ignoring)");
#endif
		return;
	}
	reportHandler->onNewReverbMode(mt32ram.system.reverbMode);
	reportHandler->onNewReverbTime(mt32ram.system.reverbTime);
	reportHandler->onNewReverbLevel(mt32ram.system.reverbLevel);

	BReverbModel *newReverbModel = reverbModels[mt32ram.system.reverbMode];
#if MT32EMU_REDUCE_REVERB_MEMORY
	if (reverbModel != newReverbModel) {
		if (reverbModel != NULL) {
			reverbModel->close();
		}
		newReverbModel->open();
	}
#endif
	reverbModel = newReverbModel;
	reverbModel->setParameters(mt32ram.system.reverbTime, mt32ram.system.reverbLevel);
}

void Synth::refreshSystemReserveSettings() {
	Bit8u *rset = mt32ram.system.reserveSettings;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Partial reserve: 1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
	partialManager->setReserve(rset);
}

void Synth::refreshSystemChanAssign(unsigned int firstPart, unsigned int lastPart) {
	memset(chantable, -1, sizeof(chantable));

	// CONFIRMED: In the case of assigning a channel to multiple parts, the lower part wins.
	for (unsigned int i = 0; i <= 8; i++) {
		if (parts[i] != NULL && i >= firstPart && i <= lastPart) {
			// CONFIRMED: Decay is started for all polys, and all controllers are reset, for every part whose assignment was touched by the sysex write.
			parts[i]->allSoundOff();
			parts[i]->resetAllControllers();
		}
		int chan = mt32ram.system.chanAssign[i];
		if (chan != 16 && chantable[chan] == -1) {
			chantable[chan] = i;
		}
	}

#if MT32EMU_MONITOR_SYSEX > 0
	Bit8u *rset = mt32ram.system.chanAssign;
	printDebug(" Part assign:     1=%02d 2=%02d 3=%02d 4=%02d 5=%02d 6=%02d 7=%02d 8=%02d Rhythm=%02d", rset[0], rset[1], rset[2], rset[3], rset[4], rset[5], rset[6], rset[7], rset[8]);
#endif
}

void Synth::refreshSystemMasterVol() {
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug(" Master volume: %d", mt32ram.system.masterVol);
#endif
}

void Synth::refreshSystem() {
	refreshSystemMasterTune();
	refreshSystemReverbParameters();
	refreshSystemReserveSettings();
	refreshSystemChanAssign(0, 8);
	refreshSystemMasterVol();
}

void Synth::reset() {
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug("RESET");
#endif
	reportHandler->onDeviceReset();
	partialManager->deactivateAll();
	mt32ram = mt32default;
	for (int i = 0; i < 9; i++) {
		parts[i]->reset();
		if (i != 8) {
			parts[i]->setProgram(controlROMData[controlROMMap->programSettings + i]);
		} else {
			parts[8]->refresh();
		}
	}
	refreshSystem();
	isEnabled = false;
}

MidiEvent::~MidiEvent() {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
}

void MidiEvent::setShortMessage(Bit32u useShortMessageData, Bit32u useTimestamp) {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
	shortMessageData = useShortMessageData;
	timestamp = useTimestamp;
	sysexData = NULL;
	sysexLength = 0;
}

void MidiEvent::setSysex(const Bit8u *useSysexData, Bit32u useSysexLength, Bit32u useTimestamp) {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
	shortMessageData = 0;
	timestamp = useTimestamp;
	sysexLength = useSysexLength;
	Bit8u *dstSysexData = new Bit8u[sysexLength];
	sysexData = dstSysexData;
	memcpy(dstSysexData, useSysexData, sysexLength);
}

MidiEventQueue::MidiEventQueue(Bit32u useRingBufferSize) : ringBufferSize(useRingBufferSize) {
	ringBuffer = new MidiEvent[ringBufferSize];
	memset(ringBuffer, 0, ringBufferSize * sizeof(MidiEvent));
	reset();
}

MidiEventQueue::~MidiEventQueue() {
	delete[] ringBuffer;
}

void MidiEventQueue::reset() {
	startPosition = 0;
	endPosition = 0;
}

bool MidiEventQueue::pushShortMessage(Bit32u shortMessageData, Bit32u timestamp) {
	unsigned int newEndPosition = (endPosition + 1) % ringBufferSize;
	// Is ring buffer full?
	if (startPosition == newEndPosition) return false;
	ringBuffer[endPosition].setShortMessage(shortMessageData, timestamp);
	endPosition = newEndPosition;
	return true;
}

bool MidiEventQueue::pushSysex(const Bit8u *sysexData, Bit32u sysexLength, Bit32u timestamp) {
	unsigned int newEndPosition = (endPosition + 1) % ringBufferSize;
	// Is ring buffer full?
	if (startPosition == newEndPosition) return false;
	ringBuffer[endPosition].setSysex(sysexData, sysexLength, timestamp);
	endPosition = newEndPosition;
	return true;
}

const MidiEvent *MidiEventQueue::peekMidiEvent() {
	return (startPosition == endPosition) ? NULL : &ringBuffer[startPosition];
}

void MidiEventQueue::dropMidiEvent() {
	// Is ring buffer empty?
	if (startPosition != endPosition) {
		startPosition = (startPosition + 1) % ringBufferSize;
	}
}

void Synth::render(Sample *stream, Bit32u len) {
	Sample tmpNonReverbLeft[MAX_SAMPLES_PER_RUN];
	Sample tmpNonReverbRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbDryLeft[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbDryRight[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbWetLeft[MAX_SAMPLES_PER_RUN];
	Sample tmpReverbWetRight[MAX_SAMPLES_PER_RUN];

	while (len > 0) {
		Bit32u thisLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
		renderStreams(tmpNonReverbLeft, tmpNonReverbRight, tmpReverbDryLeft, tmpReverbDryRight, tmpReverbWetLeft, tmpReverbWetRight, thisLen);
		for (Bit32u i = 0; i < thisLen; i++) {
#if MT32EMU_USE_FLOAT_SAMPLES
			*(stream++) = tmpNonReverbLeft[i] + tmpReverbDryLeft[i] + tmpReverbWetLeft[i];
			*(stream++) = tmpNonReverbRight[i] + tmpReverbDryRight[i] + tmpReverbWetRight[i];
#else
			*(stream++) = clipBit16s((Bit32s)tmpNonReverbLeft[i] + (Bit32s)tmpReverbDryLeft[i] + (Bit32s)tmpReverbWetLeft[i]);
			*(stream++) = clipBit16s((Bit32s)tmpNonReverbRight[i] + (Bit32s)tmpReverbDryRight[i] + (Bit32s)tmpReverbWetRight[i]);
#endif
		}
		len -= thisLen;
	}
}

void Synth::renderStreams(Sample *nonReverbLeft, Sample *nonReverbRight, Sample *reverbDryLeft, Sample *reverbDryRight, Sample *reverbWetLeft, Sample *reverbWetRight, Bit32u len) {
	while (len > 0) {
		// We need to ensure zero-duration notes will play so add minimum 1-sample delay.
		Bit32u thisLen = 1;
		if (!isAbortingPoly()) {
			const MidiEvent *nextEvent = midiQueue->peekMidiEvent();
			Bit32s samplesToNextEvent = (nextEvent != NULL) ? Bit32s(nextEvent->timestamp - renderedSampleCount) : MAX_SAMPLES_PER_RUN;
			if (samplesToNextEvent > 0) {
				thisLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
				if (thisLen > (Bit32u)samplesToNextEvent) {
					thisLen = samplesToNextEvent;
				}
			} else {
				if (nextEvent->sysexData == NULL) {
					playMsgNow(nextEvent->shortMessageData);
					// If a poly is aborting we don't drop the event from the queue.
					// Instead, we'll return to it again when the abortion is done.
					if (!isAbortingPoly()) {
						midiQueue->dropMidiEvent();
					}
				} else {
					playSysexNow(nextEvent->sysexData, nextEvent->sysexLength);
					midiQueue->dropMidiEvent();
				}
			}
		}
		doRenderStreams(nonReverbLeft, nonReverbRight, reverbDryLeft, reverbDryRight, reverbWetLeft, reverbWetRight, thisLen);
		advanceStreamPosition(nonReverbLeft, thisLen);
		advanceStreamPosition(nonReverbRight, thisLen);
		advanceStreamPosition(reverbDryLeft, thisLen);
		advanceStreamPosition(reverbDryRight, thisLen);
		advanceStreamPosition(reverbWetLeft, thisLen);
		advanceStreamPosition(reverbWetRight, thisLen);
		len -= thisLen;
	}
}

void Synth::convertSamplesToOutput(Sample *target, const Sample *source, Bit32u len, bool reverb) {
	if (target == NULL) return;

	if (dacInputMode == DACInputMode_PURE) {
		memcpy(target, source, len * sizeof(Sample));
		return;
	}

#if MT32EMU_USE_FLOAT_SAMPLES
	float gain = reverb ? reverbOutputGain * CM32L_REVERB_TO_LA32_ANALOG_OUTPUT_GAIN_FACTOR : 2.0f * outputGain;
	while (len--) {
		*(target++) = *(source++) * gain;
	}
#else
	float gain = reverb ? reverbOutputGain * CM32L_REVERB_TO_LA32_ANALOG_OUTPUT_GAIN_FACTOR : outputGain;
	if (!reverb) {
		switch (dacInputMode) {
		case DACInputMode_NICE:
			// Since we're not shooting for accuracy here, don't worry about the rounding mode.
			gain *= 2.0f;
			break;
		case DACInputMode_GENERATION1:
			while (len--) {
				*target = clipBit16s(Bit32s(*source * gain));
				*target = (*target & 0x8000) | ((*target << 1) & 0x7FFE);
				source++;
				target++;
			}
			return;
		case DACInputMode_GENERATION2:
			while (len--) {
				*target = clipBit16s(Bit32s(*source * gain));
				*target = (*target & 0x8000) | ((*target << 1) & 0x7FFE) | ((*target >> 14) & 0x0001);
				source++;
				target++;
			}
			return;
		default:
			break;
		}
	}
	while (len--) {
		*(target++) = clipBit16s(Bit32s(*(source++) * gain));
	}
#endif
}

void Synth::doRenderStreams(Sample *nonReverbLeft, Sample *nonReverbRight, Sample *reverbDryLeft, Sample *reverbDryRight, Sample *reverbWetLeft, Sample *reverbWetRight, Bit32u len) {
	if (isEnabled) {
		Sample tmpBufMixLeft[MAX_SAMPLES_PER_RUN], tmpBufMixRight[MAX_SAMPLES_PER_RUN];
		muteStream(tmpBufMixLeft, len);
		muteStream(tmpBufMixRight, len);
		for (unsigned int i = 0; i < getPartialCount(); i++) {
			if (!reverbEnabled || !partialManager->shouldReverb(i)) {
				partialManager->produceOutput(i, tmpBufMixLeft, tmpBufMixRight, len);
			}
		}
		convertSamplesToOutput(nonReverbLeft, tmpBufMixLeft, len, false);
		convertSamplesToOutput(nonReverbRight, tmpBufMixRight, len, false);
	} else {
		muteStream(nonReverbLeft, len);
		muteStream(nonReverbRight, len);
	}

	if (isEnabled && reverbEnabled) {
		Sample tmpBufMixLeft[MAX_SAMPLES_PER_RUN], tmpBufMixRight[MAX_SAMPLES_PER_RUN];
		muteStream(tmpBufMixLeft, len);
		muteStream(tmpBufMixRight, len);
		for (unsigned int i = 0; i < getPartialCount(); i++) {
			if (partialManager->shouldReverb(i)) {
				partialManager->produceOutput(i, tmpBufMixLeft, tmpBufMixRight, len);
			}
		}
		convertSamplesToOutput(reverbDryLeft, tmpBufMixLeft, len, false);
		convertSamplesToOutput(reverbDryRight, tmpBufMixRight, len, false);

		Sample tmpBufReverbOutLeft[MAX_SAMPLES_PER_RUN], tmpBufReverbOutRight[MAX_SAMPLES_PER_RUN];
		reverbModel->process(tmpBufMixLeft, tmpBufMixRight, tmpBufReverbOutLeft, tmpBufReverbOutRight, len);
		convertSamplesToOutput(reverbWetLeft, tmpBufReverbOutLeft, len, true);
		convertSamplesToOutput(reverbWetRight, tmpBufReverbOutRight, len, true);
	} else {
		muteStream(reverbDryLeft, len);
		muteStream(reverbDryRight, len);
		muteStream(reverbWetLeft, len);
		muteStream(reverbWetRight, len);
	}

	partialManager->clearAlreadyOutputed();
	renderedSampleCount += len;
}

void Synth::printPartialUsage(unsigned long sampleOffset) {
	unsigned int partialUsage[9];
	partialManager->getPerPartPartialUsage(partialUsage);
	if (sampleOffset > 0) {
		printDebug("[+%lu] Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", sampleOffset, partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	} else {
		printDebug("Partial Usage: 1:%02d 2:%02d 3:%02d 4:%02d 5:%02d 6:%02d 7:%02d 8:%02d R: %02d  TOTAL: %02d", partialUsage[0], partialUsage[1], partialUsage[2], partialUsage[3], partialUsage[4], partialUsage[5], partialUsage[6], partialUsage[7], partialUsage[8], getPartialCount() - partialManager->getFreePartialCount());
	}
}

bool Synth::hasActivePartials() const {
	for (unsigned int partialNum = 0; partialNum < getPartialCount(); partialNum++) {
		if (partialManager->getPartial(partialNum)->isActive()) {
			return true;
		}
	}
	return false;
}

bool Synth::isAbortingPoly() const {
	return abortingPoly != NULL;
}

bool Synth::isActive() const {
	if (hasActivePartials()) {
		return true;
	}
	if (reverbEnabled) {
		return reverbModel->isActive();
	}
	return false;
}

const Partial *Synth::getPartial(unsigned int partialNum) const {
	return partialManager->getPartial(partialNum);
}

unsigned int Synth::getPartialCount() const {
	return partialCount;
}

const Part *Synth::getPart(unsigned int partNum) const {
	if (partNum > 8) {
		return NULL;
	}
	return parts[partNum];
}

void MemoryRegion::read(unsigned int entry, unsigned int off, Bit8u *dst, unsigned int len) const {
	off += entry * entrySize;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *src = getRealMemory();
	if (src == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("read[%d]: unreadable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	memcpy(dst, src + off, len);
}

void MemoryRegion::write(unsigned int entry, unsigned int off, const Bit8u *src, unsigned int len, bool init) const {
	unsigned int memOff = entry * entrySize + off;
	// This method should never be called with out-of-bounds parameters,
	// or on an unsupported region - seeing any of this debug output indicates a bug in the emulator
	if (off > entrySize * entries - 1) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters start out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		return;
	}
	if (off + len > entrySize * entries) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: parameters end out of bounds: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
		len = entrySize * entries - off;
	}
	Bit8u *dest = getRealMemory();
	if (dest == NULL) {
#if MT32EMU_MONITOR_SYSEX > 0
		synth->printDebug("write[%d]: unwritable region: entry=%d, off=%d, len=%d", type, entry, off, len);
#endif
	}

	for (unsigned int i = 0; i < len; i++) {
		Bit8u desiredValue = src[i];
		Bit8u maxValue = getMaxValue(memOff);
		// maxValue == 0 means write-protected unless called from initialisation code, in which case it really means the maximum value is 0.
		if (maxValue != 0 || init) {
			if (desiredValue > maxValue) {
#if MT32EMU_MONITOR_SYSEX > 0
				synth->printDebug("write[%d]: Wanted 0x%02x at %d, but max 0x%02x", type, desiredValue, memOff, maxValue);
#endif
				desiredValue = maxValue;
			}
			dest[memOff] = desiredValue;
		} else if (desiredValue != 0) {
#if MT32EMU_MONITOR_SYSEX > 0
			// Only output debug info if they wanted to write non-zero, since a lot of things cause this to spit out a lot of debug info otherwise.
			synth->printDebug("write[%d]: Wanted 0x%02x at %d, but write-protected", type, desiredValue, memOff);
#endif
		}
		memOff++;
	}
}

}
