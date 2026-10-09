/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WebAudioManager.cpp //////////////////////////////////////////////////
//
// The AudioManager of the WebAssembly build, see WebAudioManager.h. This follows
// MilesAudioManager.cpp function by function (the request processing, the limits and priorities,
// the lifetime of an event, the 3D bookkeeping); what differs is marked where it happens.
//
///////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "WebDevice/Audio/WebAudioManager.h"
#include "WebDevice/Audio/WebAudioDecoder.h"
#include "WebDevice/Audio/WebAudioMix.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include "Common/file.h"

#include <math.h>
#include <string.h>

enum
{
	// How long before the end of what is queued on a voice the next portion of a sound (the next
	// loop, the decay) is queued behind it. Longer than a few frames, so that a slow frame cannot
	// open a gap; short enough that the state of the event does not run ahead of what is heard.
	CHAIN_LOOKAHEAD_MS = 250,

	// The speaker type values of the audio settings (the index into the table of GameAudio.cpp).
	SPEAKER_TYPE_HEADPHONES = 1,
};

// Gain changes below this are not sent to the browser.
static const Real GAIN_EPSILON = 0.001f;

static const char *const PROVIDER_NAME_STEREO = "Miles Fast 2D Positional Audio";	// the name the game settings know the software provider by
static const char *const PROVIDER_NAME_HRTF = "Web Audio HRTF";

//-------------------------------------------------------------------------------------------------
WebAudioManager::WebAudioManager() :
	m_providerCount(0),
	m_selectedProvider(PROVIDER_ERROR),
	m_lastProvider(PROVIDER_ERROR),
	m_selectedSpeakerType(0),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString),
	m_num2DSamples(0),
	m_num3DSamples(0),
	m_numStreams(0),
	m_deviceOpened(false),
	m_deviceWorks(false),
	m_listenerSent(false)
{
	m_audioCache = NEW WebAudioFileCache;
	m_lastListenerPosition.zero();
	m_lastListenerOrientation.zero();
}

//-------------------------------------------------------------------------------------------------
WebAudioManager::~WebAudioManager()
{
	releaseHandleForBink();
	closeDevice();
	delete m_audioCache;

	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm..."));
	TheAudio = nullptr;
}

//-------------------------------------------------------------------------------------------------
#if defined(RTS_DEBUG)
AudioHandle WebAudioManager::addAudioEvent( const AudioEventRTS *eventToAdd )
{
	if (TheGlobalData->m_preloadReport) {
		if (!eventToAdd->getEventName().isEmpty()) {
			m_allEventsLoaded.insert(eventToAdd->getEventName());
		}
	}

	return AudioManager::addAudioEvent(eventToAdd);
}
#endif

#if defined(RTS_DEBUG)
//-------------------------------------------------------------------------------------------------
void WebAudioManager::audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp )
{
	std::list<WebPlayingAudio *>::iterator it;

	Coord3D lookPos = TheTacticalView->getPosition();
	const Coord3D *mikePos = TheAudio->getListenerPosition();
	Coord3D distanceVector = TheTacticalView->get3DCameraPosition();
	distanceVector.sub( *mikePos );

	// One block of text goes to the debug display and to the file, so build the lines once.
	AsciiString text;
	text.format("Web Audio: context %s, %d Hz    Memory Usage : %u/%u\n",
		WebAudio_GetState() == WEBAUDIO_STATE_RUNNING ? "running" : (WebAudio_GetState() == WEBAUDIO_STATE_SUSPENDED ? "waiting for a user gesture" : "off"),
		WebAudio_GetSampleRate(), m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
	AsciiString line;
	line.format("Sound: %s    3DSound: %s    Speech: %s    Music: %s\n",
		isOn(AudioAffect_Sound) ? "Yes" : "No", isOn(AudioAffect_Sound3D) ? "Yes" : "No",
		isOn(AudioAffect_Speech) ? "Yes" : "No", isOn(AudioAffect_Music) ? "Yes" : "No");
	text.concat(line);
	line.format("Channels Available: %u Sounds    %u 3D Sounds\n", getNumAvailable2DSamples(), getNumAvailable3DSamples());
	text.concat(line);
	line.format("Volume: Sound: %d    3DSound: %d    Speech: %d    Music: %d\n",
		REAL_TO_INT(m_soundVolume * 100.0f), REAL_TO_INT(m_sound3DVolume * 100.0f), REAL_TO_INT(m_speechVolume * 100.0f), REAL_TO_INT(m_musicVolume * 100.0f));
	text.concat(line);
	line.format("Current 3D Provider: %s    Current Speaker Type: %s\n",
		TheAudio->getProviderName(m_selectedProvider).str(), TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());
	text.concat(line);
	line.format("Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n",
		(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z );
	text.concat(line);
	line.format("Camera distance from microphone: %d -- Zoom Volume: %d%%\n", (Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume()*100.0f) );
	text.concat(line);
	text.concat("-----------------------------------------------------------\nPlaying Audio\n");

	if( dd )
		dd->printf("%s", text.str());
	if( fp )
		fprintf( fp, "%s", text.str() );

	const Coord3D *microphonePos = TheAudio->getListenerPosition();

	for (Int section = 0; section < 3; ++section)
	{
		std::list<WebPlayingAudio *> &list = section == 0 ? m_playingSounds : (section == 1 ? m_playing3DSounds : m_playingStreams);
		const Int channelCount = section == 0 ? TheAudio->getNum2DSamples() : (section == 1 ? TheAudio->getNum3DSamples() : TheAudio->getNumStreams());
		const char *title = section == 0 ? "-----------------------------------------------------Sounds\n"
			: (section == 1 ? "--------------------------------------------------3D Sounds\n" : "----------------------------------------------------Streams\n");
		if( dd )
			dd->printf("%s", title);
		if( fp )
			fprintf( fp, "%s", title );

		Int channel = 1;
		for (it = list.begin(); it != list.end(); ++it) {
			WebPlayingAudio *playing = *it;
			AudioEventRTS *event = playing->m_audioEventRTS.Peek();
			AsciiString filenameNoSlashes = event->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			Real volume = 100.0f * getEffectiveVolume(event);
			AsciiString entry;
			if (section == 1) {
				Real dist = -1.0f;
				if (const Coord3D *pos = event->getPosition()) {
					Coord3D vector = *microphonePos;
					vector.sub( *pos );
					dist = vector.length();
				}
				const char *kind = "";
				switch( event->getOwnerType() )
				{
					case OT_Positional: kind = "(3D)"; break;
					case OT_Object: kind = "(3DObj)"; break;
					case OT_Drawable: kind = "(3DDraw)"; break;
					case OT_Dead: kind = "(3DDead)"; break;
					default: break;
				}
				entry.format("%2d: %-20s - (%s) Volume: %d, Dist: %d, %s\n", channel, event->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume), REAL_TO_INT(dist), kind);
			} else {
				entry.format("%2d: %-20s - (%s) Volume: %d (%s)\n", channel, event->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume), section == 0 ? "2D" : "Stream");
			}
			++channel;
			if (dd)
				dd->printf("%s", entry.str());
			if (fp)
				fprintf(fp, "%s", entry.str());
		}
		for (Int i = channel; i <= channelCount; ++i) {
			if (dd)
				dd->printf("%2d: Silence\n", i);
			if (fp)
				fprintf(fp, "%2d: Silence\n", i);
		}
	}
	if (dd)
		dd->printf("===========================================================\n");
	if (fp)
		fprintf(fp, "===========================================================\n");
}
#endif

//-------------------------------------------------------------------------------------------------
void WebAudioManager::init()
{
	AudioManager::init();

	// We should now know how many samples we want to load
	openDevice();
	m_audioCache->setMaxSize(getAudioSettings()->m_maxCacheSize);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::reset()
{
#if defined(RTS_DEBUG)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif

	AudioManager::reset();
	stopAllAudioImmediately();
	removeAllAudioRequests();
	// This must come after stopAllAudioImmediately() and removeAllAudioRequests(), to ensure that
	// sounds pointing to the temporary AudioEventInfo handles are deleted before their info is deleted
	removeLevelSpecificAudioEventInfos();
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::update()
{
	AudioManager::update();
	setDeviceListenerPosition();
	processRequestList();
	pollPlayingAudio();
	processPlayingList();
	processFadingList();
	releaseFinishedForcePlayed();

	if (m_deviceWorks) {
		if (!WebAudio_StreamsHaveThread()) {
			WebAudio_PumpStreams();
		}
		// All of this frame's changes go to the browser in one message.
		WebAudio_Flush();
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::stopAudio( AudioAffect which )
{
	// All we really need to do is:
	// 1) Stop the voice (so that when we later unload the sound, bad stuff doesn't happen)
	// 2) Set the status to stopped, so that when we next process the playing list, we will
	//		correctly clean up the sample.

	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			stopPlayingAudio(playing);
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			stopPlayingAudio(playing);
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
				if (!BitIsSet(which, AudioAffect_Music)) {
					continue;
				}
			} else {
				if (!BitIsSet(which, AudioAffect_Speech)) {
					continue;
				}
			}
			stopPlayingAudio(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::pauseAudio( AudioAffect which )
{
	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("Voice is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, TRUE);
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("3D Voice is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, TRUE);
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
				if (!BitIsSet(which, AudioAffect_Music)) {
					continue;
				}
			} else {
				if (!BitIsSet(which, AudioAffect_Speech)) {
					continue;
				}
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("Stream is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, TRUE);
		}
	}

	//Get rid of PLAY audio requests when pausing audio.
	std::list<AudioRequest*>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); )
	{
		AudioRequest *req = (*ait);
		if( req->m_request == AR_Play )
		{
			deleteInstance(req);
			ait = m_audioRequests.erase(ait);
		}
		else
		{
			ait++;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::resumeAudio( AudioAffect which )
{
	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("Voice is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, FALSE);
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("3D Voice is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, FALSE);
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (!playing->isPlaying()) {
				continue;
			}
			if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
				if (!BitIsSet(which, AudioAffect_Music)) {
					continue;
				}
			} else {
				if (!BitIsSet(which, AudioAffect_Speech)) {
					continue;
				}
			}
			DEBUG_ASSERTCRASH(playing->m_voice, ("Stream is not expected to be null"));
			WebAudio_VoicePause(playing->m_voice, FALSE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::pauseAmbient( Bool shouldPause )
{

}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::playAudioEvent( AudioRequest* req )
{
	DEBUG_ASSERTCRASH(req->m_pendingEvent != nullptr, ("audio request was expected to contain a valid audio event"));

	AudioEventRTS* event = req->m_pendingEvent.Peek();

#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("WEBAUDIO (%d) - Processing play request: %d (%s)", TheGameLogic->getFrame(), event->getPlayingHandle(), event->getEventName().str()));
#endif
	const AudioEventInfo *info = event->getAudioEventInfo();
	if (!info) {
		return;
	}

	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing = nullptr;

	AudioHandle handleToKill = event->getHandleToKill();

	WebPlayingAudio *newPlaying = allocatePlayingAudio();
	newPlaying->m_requestStop = req->m_requestStop;

	switch(info->m_soundType)
	{
		case AT_Music:
		case AT_Streaming:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Stream"));
		#endif

			if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
				stopAllSpeech();
			}

			Bool foundSoundToReplace = false;
			if (handleToKill) {
				for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
					playing = (*it);

					if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
					{
						//Release this streaming channel immediately because we are going to play another sound in it's place.
						stopPlayingAudio(playing);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			// Put this on here, so that the audio event RTS will be cleaned up regardless.
			newPlaying->m_audioEventRTS = req->m_pendingEvent;
			newPlaying->m_type = WPAT_Stream;

			Bool started = false;
			if (!handleToKill || foundSoundToReplace) {
				started = playStream(event, newPlaying);
			}

			if (started) {
				if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
					setDisallowSpeech(TRUE);
	 			}

				m_playingStreams.push_back(newPlaying);
				newPlaying = nullptr;
			}
			break;
		}

		case AT_SoundEffect:
		{
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG(("- Sound"));
		#endif


			if (event->isPositionalAudio()) {
				// Sounds that are non-global are positional 3-D sounds. Deal with them accordingly
			#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Positional"));
			#endif
				Bool foundSoundToReplace = false;
				if (handleToKill)
				{
					for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
						playing = (*it);

						if( playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill )
						{
							//Release this 3D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				Int sample3D;
				if( !handleToKill || foundSoundToReplace )
				{
					sample3D = getAvailable3DSample( event );
					if( !sample3D )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample3D = getAvailable3DSample( event );
						}
					}
				}
				else
				{
					sample3D = 0;
				}
				// Push it onto the list of playing things
				newPlaying->m_audioEventRTS = req->m_pendingEvent;
				newPlaying->m_slot = sample3D;
				newPlaying->m_file = nullptr;
				newPlaying->m_type = WPAT_3DSample;

				if (sample3D) {
					newPlaying->m_voice = WebAudio_CreateVoice(WEBAUDIO_VOICE_3D);
					if (newPlaying->m_voice) {
						newPlaying->m_file = playSample3D(event, newPlaying);
					}
				}

				if( !newPlaying->m_file )
				{
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)"));
					#endif
				}
				else
				{
					m_playing3DSounds.push_back(newPlaying);
					newPlaying = nullptr;
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Playing."));
					#endif
				}
			}
			else
			{
				// UI sounds are always 2-D. All other sounds should be Positional
				// Unit acknowledgment, etc, falls into the UI category of sound.
				Bool foundSoundToReplace = false;
				if (handleToKill) {
					for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
						playing = (*it);

						if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
						{
							//Release this 2D sound channel immediately because we are going to play another sound in it's place.
							stopPlayingAudio(playing);
							foundSoundToReplace = true;
							break;
						}
					}
				}

				Int sample;
				if( !handleToKill || foundSoundToReplace )
				{
					sample = getAvailable2DSample(event);
					if( !sample )
					{
						//If we don't have an available sample, kill the lowest priority assuming we have one that is lower
						//than the sound we are trying to add. One possibility for strangeness is when an interrupt sound
						//that wants to kill a handle to replace it, it's possible that another request already killed it,
						//in which case we need to attempt to find another sound to kill.
						if( killLowestPrioritySoundImmediately( event ) )
						{
							sample = getAvailable2DSample( event );
						}
					}
				}
				else
				{
					sample = 0;
				}

				// Push it onto the list of playing things
				newPlaying->m_audioEventRTS = req->m_pendingEvent;
				newPlaying->m_slot = sample;
				newPlaying->m_file = nullptr;
				newPlaying->m_type = WPAT_Sample;

				if (sample) {
					newPlaying->m_voice = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
					if (newPlaying->m_voice) {
						newPlaying->m_file = playSample(event, newPlaying);
					}
				}

				if (!newPlaying->m_file) {
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Killed (no handles available)"));
					#endif
				} else {
					m_playingSounds.push_back(newPlaying);
					newPlaying = nullptr;
					#ifdef INTENSIVE_AUDIO_DEBUG
						DEBUG_LOG((" Playing."));
					#endif
				}
			}
			break;
		}
	}

	// If we were able to successfully play audio, then we set it to null above. (And it will be freed
	// later. However, if audio is non-null at this point, then it must be freed.
	if (newPlaying) {
		stopPlayingAudio(newPlaying);
		releasePlayingAudio(newPlaying);
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::stopAudioEvent( AudioHandle handle )
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("WEBAUDIO (%d) - Processing stop request: %d", TheGameLogic->getFrame(), handle));
#endif

	std::list<WebPlayingAudio *>::iterator it;
	if ( handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade ) {
		// for music, find and stop the currently playing music stream(s).
		for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
			WebPlayingAudio *playing = (*it);

			if( playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music )
			{
				if( handle == AHSV_StopTheMusicFade )
				{
					fadePlayingAudio(playing);
				}
				else
				{
					stopPlayingAudio(playing);
				}
			}
		}
		return;
	}

	// Look for it in the request list.
	{
		std::list<AudioRequest*>::iterator it;
		for( it = m_audioRequests.begin(); it != m_audioRequests.end(); it++ )
		{
			AudioRequest *req = (*it);
			if( req->m_pendingEvent && req->m_pendingEvent->getPlayingHandle() == handle )
			{
				if (req->m_pendingEvent->getAudioEventInfo()->m_soundType == AT_SoundEffect)
				{
					req->m_requestStop = true;
				}
				else
				{
					deleteInstance(req);
					m_audioRequests.erase(it);
				}
				return;
			}
		}
	}

	for ( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it ) {
		WebPlayingAudio *playing = (*it);

		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
			stopPlayingAudio(playing);
			return;
		}
	}

	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		WebPlayingAudio *playing = (*it);

		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
			playing->m_requestStop = true;
			cancelQueuedLoop(playing);
			return;
		}
	}

	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		WebPlayingAudio *playing = (*it);

		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
		#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" (%s)", playing->m_audioEventRTS->getEventName()));
		#endif
			playing->m_requestStop = true;
			cancelQueuedLoop(playing);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The next iteration of a loop may already be queued behind the one that is playing (see
	* pollSamples). Stopping a loop lets the playing iteration finish but plays no more, so take the
	* queued one back and ask for what follows instead, which is the decay or the end. */
//-------------------------------------------------------------------------------------------------
void WebAudioManager::cancelQueuedLoop( WebPlayingAudio *playing )
{
	if (!playing->m_chainedLoop || !playing->m_voice || !playing->isPlaying()) {
		return;
	}
	playing->m_chainedLoop = false;
	if (WebAudio_VoicePendingSegments(playing->m_voice) < 2) {
		return;	// the queued iteration has begun, it is the one that plays out now
	}

	WebAudio_VoiceCancelPending(playing->m_voice);
	const PortionResult result = startNextPortion(playing);
	if (result != PR_Queued) {
		playing->m_lastPortionQueued = true;
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::killAudioEventImmediately( AudioHandle audioEvent )
{
	//First look for it in the request list.
	std::list<AudioRequest*>::iterator ait;
	for( ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ait++ )
	{
		AudioRequest *req = (*ait);
		if( req->m_pendingEvent && req->m_pendingEvent->getPlayingHandle() == audioEvent )
		{
			deleteInstance(req);
			m_audioRequests.erase(ait);
			return;
		}
	}

	//Look for matching 3D sound to kill
	std::list<WebPlayingAudio *>::iterator it;
	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); it++ )
	{
		WebPlayingAudio *playing = (*it);

		if( playing->m_audioEventRTS->getPlayingHandle() == audioEvent )
		{
			stopPlayingAudio(playing);
			return;
		}
	}

	//Look for matching 2D sound to kill
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); it++ )
	{
		WebPlayingAudio *playing = (*it);

		if( playing->m_audioEventRTS->getPlayingHandle() == audioEvent )
		{
			stopPlayingAudio(playing);
			return;
		}
	}

	//Look for matching steaming sound to kill
	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); it++ )
	{
		WebPlayingAudio *playing = (*it);

		if( playing->m_audioEventRTS->getPlayingHandle() == audioEvent )
		{
			stopPlayingAudio(playing);
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::pauseAudioEvent( AudioHandle handle )
{
	// pause audio
}

//-------------------------------------------------------------------------------------------------
WebCachedAudio *WebAudioManager::loadFileForRead( AudioEventRTS *eventToLoadFrom )
{
	return m_audioCache->openFile(eventToLoadFrom);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::closeFile( WebCachedAudio *fileRead )
{
	m_audioCache->closeFile(fileRead);
}


//-------------------------------------------------------------------------------------------------
WebPlayingAudio *WebAudioManager::allocatePlayingAudio()
{
	return NEW WebPlayingAudio; // poolify
}


//-------------------------------------------------------------------------------------------------
void WebAudioManager::releaseWebHandles( WebPlayingAudio *playing )
{
	switch (playing->m_type)
	{
		case WPAT_Sample:
		{
			if (playing->m_slot) {
				m_availableSamples.push_back(playing->m_slot);
				playing->m_slot = 0;
			}
			break;
		}
		case WPAT_3DSample:
		{
			if (playing->m_slot) {
				m_available3DSamples.push_back(playing->m_slot);
				playing->m_slot = 0;
			}
			break;
		}
		case WPAT_Stream:
		{
			if (playing->m_stream) {
				WebAudio_StreamClose(playing->m_stream);
				playing->m_stream = 0;
			}
			break;
		}
		default:
			break;
	}

	// A short fade out, an abrupt end of a waveform clicks.
	if (playing->m_voice) {
		WebAudio_DestroyVoice(playing->m_voice, 0.008f);
		playing->m_voice = 0;
	}
	playing->m_type = WPAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::releasePlayingAudio( WebPlayingAudio *playing )
{
	DEBUG_ASSERTCRASH(playing->m_status == WPS_Stopped, ("WebPlayingAudio must be stopped by now"));
	DEBUG_ASSERTCRASH(playing->m_type == WPAT_INVALID, ("WebPlayingAudio must be invalidated by now"));

	if (playing->m_file) {
		closeFile(playing->m_file);
		playing->m_file = nullptr;
	}

	delete playing;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::stopPlayingAudio( WebPlayingAudio *playing )
{
	// Playing or Stopping advance to Stopped, and the voice goes away. (Miles does this in two
	// interlocked steps because its timer thread can advance the first one at any time.)
	if (playing->m_status == WPS_Stopped) {
		return;
	}
	playing->m_status = WPS_Stopped;
	releaseWebHandles(playing);
	playing->m_rerequestOnNextUpdate = false;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::rerequestPlayingAudio( WebPlayingAudio *playing )
{
	AudioRequest *req = allocateAudioRequest();
	req->m_pendingEvent = playing->m_audioEventRTS;
	req->m_requiresCheckForSample = true;
	req->m_requestStop = playing->m_requestStop;
	appendAudioRequest(req);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::rerequestPlayingAudioWhenSignalled( WebPlayingAudio *playing )
{
	if (playing->m_rerequestOnNextUpdate) {
		rerequestPlayingAudio(playing);
		playing->m_rerequestOnNextUpdate = false;
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::fadePlayingAudio( WebPlayingAudio *playing )
{
	playing->m_fade = true;
}

//-------------------------------------------------------------------------------------------------
WebPlayingAudio *WebAudioManager::findActiveMusic( const AsciiString* trackName )
{
	std::list<WebPlayingAudio *>::const_iterator it;
	WebPlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (!playing->isPlaying()) {
			continue;
		}
		if (playing->m_fade) {
			continue;
		}
		if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType != AT_Music) {
			continue;
		}
		if (trackName && *trackName != playing->m_audioEventRTS->getEventName()) {
			continue;
		}
		return playing;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const WebPlayingAudio *WebAudioManager::findActiveMusic( const AsciiString* trackName ) const
{
	return const_cast<WebAudioManager*>(this)->findActiveMusic(trackName);
}


//-------------------------------------------------------------------------------------------------
void WebAudioManager::releasePlayingAudioInListIfStopped(std::list<WebPlayingAudio *> &list)
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	for (it = list.begin(); it != list.end(); )
	{
		playing = (*it);

		if (playing->m_status == WPS_Stopped)
		{
			releasePlayingAudio(playing);
			it = list.erase(it);
			continue;
		}

		++it;
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::stopAllAudioImmediately()
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = (*it);
		stopPlayingAudio(playing);
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = (*it);
		stopPlayingAudio(playing);
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		stopPlayingAudio(playing);
	}

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = (*it);
		stopPlayingAudio(playing);
	}

	releasePlayingAudioInListIfStopped(m_playingSounds);
	releasePlayingAudioInListIfStopped(m_playing3DSounds);
	releasePlayingAudioInListIfStopped(m_playingStreams);
	releasePlayingAudioInListIfStopped(m_fadingAudio);

	DEBUG_ASSERTCRASH(m_playingSounds.empty(), ("List is expected to be empty now"));
	DEBUG_ASSERTCRASH(m_playing3DSounds.empty(), ("List is expected to be empty now"));
	DEBUG_ASSERTCRASH(m_playingStreams.empty(), ("List is expected to be empty now"));
	DEBUG_ASSERTCRASH(m_fadingAudio.empty(), ("List is expected to be empty now"));

	std::list<ForcePlayed>::iterator fit;
	for (fit = m_audioForcePlayed.begin(); fit != m_audioForcePlayed.end(); ++fit) {
		WebAudio_DestroyVoice(fit->m_voice, 0.008f);
		WebAudio_DestroyBuffer(fit->m_buffer);
	}

	m_audioForcePlayed.clear();
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::freeAllWebHandles()
{
	// First, we need to ensure that we don't have any voices open. To that end, we must stop
	// all of our currently playing audio.
	stopAllAudioImmediately();

	m_availableSamples.clear();
	m_num2DSamples = 0;

	m_available3DSamples.clear();
	m_num3DSamples = 0;
	m_numStreams = 0;
}

//-------------------------------------------------------------------------------------------------
Int WebAudioManager::getAvailable2DSample( AudioEventRTS *event )
{
	if (!m_availableSamples.empty()) {
		Int retSample = m_availableSamples.back();
		m_availableSamples.pop_back();
		return retSample;
	}

	// Find the first sample of lower priority than my augmented priority that is interruptible and take its handle
	return 0;
}

//-------------------------------------------------------------------------------------------------
Int WebAudioManager::getAvailable3DSample( AudioEventRTS *event )
{
	if (!m_available3DSamples.empty()) {
		Int retSample = m_available3DSamples.back();
		m_available3DSamples.pop_back();
		return retSample;
	}

	// Find the first sample of lower priority than my augmented priority that is interruptible and take its handle
	return 0;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::setVoiceGain( WebPlayingAudio *playing, Real gain )
{
	if (!playing->m_voice) {
		return;
	}
	// Distance fade moves the gain of positional sounds a little every frame, a tiny step is not worth a message.
	if (playing->m_lastGain >= 0.0f && fabsf(gain - playing->m_lastGain) < GAIN_EPSILON) {
		return;
	}
	playing->m_lastGain = gain;
	WebAudio_VoiceSetGain(playing->m_voice, gain);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::adjustPlayingVolume( WebPlayingAudio *playing )
{
	setVoiceGain(playing, getEffectiveVolume(playing->m_audioEventRTS.Peek()));
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::stopAllSpeech()
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);
		if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Streaming) {
			stopPlayingAudio(playing);
		}
	}

}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::initFilters( WebPlayingAudio *playing, AudioEventRTS *event )
{
	// set the sample volume
	setVoiceGain(playing, getEffectiveVolume(event));

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		WebAudio_VoiceSetPitch(playing->m_voice, pitchShift);
	}

	// The delay filter of Miles has a mix of zero: the delay of an event is what its request waits (adjustRequest).
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::initFilters3D( WebPlayingAudio *playing, AudioEventRTS *event, const Coord3D *pos )
{
	// set the sample volume
	setVoiceGain(playing, getEffectiveVolume(event));

	// pitch shift
	Real pitchShift = event->getPitchShift();
	if (pitchShift == 0.0f) {
		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()) );
	} else {
		WebAudio_VoiceSetPitch(playing->m_voice, pitchShift);
	}

	// Low pass filter: sounds that are not in view are muffled. The setting is the highest
	// frequency that is still heard, as a part of half the sample rate.
	if (event->getAudioEventInfo()->m_lowPassFreq > 0 && !isOnScreen(pos) ) {
		const Real nyquist = (Real)WebAudio_GetSampleRate() * 0.5f;
		WebAudio_VoiceSetLowPass(playing->m_voice, MAX(200.0f, event->getAudioEventInfo()->m_lowPassFreq * (nyquist > 0 ? nyquist : 22050.0f)));
	} else {
		WebAudio_VoiceSetLowPass(playing->m_voice, 0.0f);
	}
}

//-------------------------------------------------------------------------------------------------
AsciiString WebAudioManager::nextMusicTrack()
{
	AsciiString trackName;

	if (WebPlayingAudio *playing = findActiveMusic()) {
		trackName = playing->m_audioEventRTS->getEventName();
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);

	return trackName;
}

//-------------------------------------------------------------------------------------------------
AsciiString WebAudioManager::prevMusicTrack()
{
	AsciiString trackName;

	if (WebPlayingAudio *playing = findActiveMusic()) {
		trackName = playing->m_audioEventRTS->getEventName();
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);

	return trackName;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isMusicPlaying() const
{
	return findActiveMusic() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const
{
	if (const WebPlayingAudio *playing = findActiveMusic(&trackName)) {
		// The stream counts the passes over the file that have been played to the end.
		if ((Int)WebAudio_VoiceLoopsDone(playing->m_voice) >= numberOfTimes) {
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::openDevice()
{
	if (!TheGlobalData->m_audioOn) {
		return;
	}

	m_deviceOpened = true;

	// Creates the Web Audio context (suspended until the browser sees a user gesture, which the
	// launcher's Play click usually was) and the stream decoder.
	const AudioSettings *audioSettings = getAudioSettings();
	m_selectedSpeakerType = TheAudio->translateSpeakerTypeToUnsignedInt(m_prefSpeaker);

	// The game mixed at the OutputRate of its settings (22 kHz in the retail files), but the music is 44.1 kHz:
	// the browser resamples to the speakers anyway, so never go below CD quality.
	m_deviceWorks = WebAudio_Init(MAX(44100, audioSettings->m_outputRate)) != 0;

	if (m_deviceWorks) {
		buildProviderList();
	} else {
		// if we couldn't initialize any devices, turn sound off (fail silently)
		DEBUG_LOG(("Web Audio is not available. Audio will be turned off."));
		setOn( false, AudioAffect_All );
	}

	UnsignedInt providerNdx = TheAudio->getProviderIndex(m_pref3DProvider);
	if (providerNdx >= m_providerCount) {
		// no (known) preference: positional audio for the speakers, or HRTF for headphones
		providerNdx = getProviderIndex( m_selectedSpeakerType == SPEAKER_TYPE_HEADPHONES ? PROVIDER_NAME_HRTF : PROVIDER_NAME_STEREO );
	}
	selectProvider(providerNdx);

	// Now that we're all done, update the cached variables so that everything is in sync.
	TheAudio->refreshCachedVariables();
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::closeDevice()
{
	if (m_deviceOpened)
	{
		if (m_deviceWorks)
		{
			freeAllWebHandles();
			unselectProvider();
			m_audioCache->releaseAll();	// the buffers belong to the device
			WebAudio_Shutdown();
			m_deviceWorks = false;
		}
		m_deviceOpened = false;
	}
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isCurrentlyPlaying( AudioHandle handle )
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (!playing->isPlayingOrRequested()) {
			continue;
		}
		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (!playing->isPlayingOrRequested()) {
			continue;
		}
		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (!playing->isPlayingOrRequested()) {
			continue;
		}
		if (playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	// if something is requested, it is also considered playing
	std::list<AudioRequest *>::iterator ait;
	AudioRequest *req = nullptr;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		req = *ait;
		if (req->m_pendingEvent && req->m_pendingEvent->getPlayingHandle() == handle) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
/** The voice of the audio has played everything that was queued on it. This is what Miles'
	* end of sample callback leads to: the next loop or the decay starts, or the audio is complete.
	* Normally the next portion is already queued before the voice runs dry (pollSamples), this
	* is the way it works out when that was too late or impossible. */
//-------------------------------------------------------------------------------------------------
void WebAudioManager::playbackFinished( WebPlayingAudio *playing )
{
	if (getDisallowSpeech() && playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Streaming) {
		setDisallowSpeech(FALSE);
	}

	switch (playing->m_type)
	{
	case WPAT_Sample:
	case WPAT_3DSample:
	{
		if (startNextPortion(playing) == PR_Queued) {
			return;
		}
		break;
	}
	default:
		break;
	}

	// it will be cleaned up on the next frame update
	if (playing->m_status == WPS_Playing) {
		playing->m_status = WPS_Stopping;
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::notifyOfAudioCompletion( UnsignedInt handle, UnsignedInt flags )
{
	WebPlayingAudio *playing = findPlayingAudioFromVoice(handle, flags);
	if (!playing) {
		DEBUG_CRASH(("Audio has completed playing, but we can't seem to find it. - jkmcd"));
		return;
	}

	playbackFinished(playing);
}

//-------------------------------------------------------------------------------------------------
WebPlayingAudio *WebAudioManager::findPlayingAudioFromVoice( UnsignedInt voice, UnsignedInt type )
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	if (type == WPAT_Sample) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing->m_voice == voice) {
				return playing;
			}
		}
	}

	if (type == WPAT_3DSample) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing->m_voice == voice) {
				return playing;
			}
		}
	}

	if (type == WPAT_Stream) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing->m_voice == voice) {
				return playing;
			}
		}
		for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
			playing = *it;
			if (playing->m_voice == voice) {
				return playing;
			}
		}
	}

	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/** Miles tells the game when a sample or stream has ended, from its timer thread. We have to ask:
	* once per update this looks at the voices. It also does what makes sounds gapless: a few frames
	* before the last queued portion of a sound ends, the portion after it (the next loop, the
	* decay) is handed to the voice, which plays it right behind. */
//-------------------------------------------------------------------------------------------------
void WebAudioManager::pollSamples( std::list<WebPlayingAudio *> &list )
{
	std::list<WebPlayingAudio *>::iterator it;
	for (it = list.begin(); it != list.end(); ++it) {
		WebPlayingAudio *playing = *it;

		if (playing->m_status != WPS_Playing || !playing->m_voice) {
			continue;
		}

		const UnsignedInt pending = WebAudio_VoicePendingSegments(playing->m_voice);
		if (pending < 2) {
			playing->m_chainedLoop = false;	// whatever was queued ahead is the one playing now
		}

		if (pending == 0) {
			if (playing->m_lastPortionQueued) {
				// Everything has been played, and nothing more is to follow.
				playing->m_status = WPS_Stopping;
			} else {
				playbackFinished(playing);
			}
			continue;
		}

		if (pending == 1 && !playing->m_lastPortionQueued) {
			// A voice reports no end time (0) until the browser has seen the segment, that is not "ending".
			const Int remaining = WebAudio_VoiceRemainingMs(playing->m_voice);
			if (remaining > 0 && remaining <= CHAIN_LOOKAHEAD_MS) {
				if (startNextPortion(playing) != PR_Queued) {
					playing->m_lastPortionQueued = true;
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::pollStreams()
{
	std::list<WebPlayingAudio *>::iterator it;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		WebPlayingAudio *playing = *it;

		if (playing->m_status != WPS_Playing || !playing->m_stream) {
			continue;
		}
		// Music loops inside the stream and only ends when the file cannot be decoded (a damaged file)
		if (WebAudio_StreamIsFinished(playing->m_stream)) {
			playbackFinished(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::pollPlayingAudio()
{
	if (!m_deviceWorks) {
		return;
	}
	pollSamples(m_playingSounds);
	pollSamples(m_playing3DSounds);
	pollStreams();
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::releaseFinishedForcePlayed()
{
	std::list<ForcePlayed>::iterator it;
	for (it = m_audioForcePlayed.begin(); it != m_audioForcePlayed.end(); ) {
		if (WebAudio_VoicePendingSegments(it->m_voice) == 0) {
			WebAudio_DestroyVoice(it->m_voice, 0.0f);
			WebAudio_DestroyBuffer(it->m_buffer);
			it = m_audioForcePlayed.erase(it);
		} else {
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getProviderCount() const
{
	return m_providerCount;
}

//-------------------------------------------------------------------------------------------------
AsciiString WebAudioManager::getProviderName( UnsignedInt providerNum ) const
{
	if (isOn(AudioAffect_Sound3D) && providerNum < m_providerCount) {
		return m_provider3D[providerNum].name;
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getProviderIndex( AsciiString providerName ) const
{
	for (UnsignedInt i = 0; i < m_providerCount; ++i) {
		if (providerName == m_provider3D[i].name) {
			return i;
		}
	}

	return PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::selectProvider( UnsignedInt providerNdx )
{
	if (!isOn(AudioAffect_Sound3D))
	{
		return;
	}

	if (providerNdx == m_selectedProvider)
	{
		return;
	}

	if (isValidProvider())
	{
		freeAllWebHandles();
		unselectProvider();
	}

	// Miles picks the "Dolby Surround" provider for headphones and surround speaker setups and the
	// plain stereo one otherwise, whatever was asked for. Likewise the browser device: headphones
	// get HRTF, everything else equal power panning, unless the caller names a provider.
	if (providerNdx >= m_providerCount)
	{
		providerNdx = getProviderIndex( m_selectedSpeakerType == SPEAKER_TYPE_HEADPHONES ? PROVIDER_NAME_HRTF : PROVIDER_NAME_STEREO );
	}

	if (providerNdx < m_providerCount)
	{
		m_selectedProvider = providerNdx;

		initSamplePools();

		setSpeakerType(m_selectedSpeakerType);
		if (TheVideoPlayer)
		{
			TheVideoPlayer->notifyVideoPlayerOfNewProvider(TRUE);
		}
	}
	else
	{
		m_selectedProvider = PROVIDER_ERROR;
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::unselectProvider()
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	if (TheVideoPlayer) {
		TheVideoPlayer->notifyVideoPlayerOfNewProvider(FALSE);
	}

	m_lastProvider = m_selectedProvider;

	m_selectedProvider = PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getSelectedProvider() const
{
	return m_selectedProvider;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::setSpeakerType( UnsignedInt speakerType )
{
	if (!isValidProvider()) {
		return;
	}

	// Of the speaker types only headphones change how the sound is positioned
	WebAudio_SetPanningModel(speakerType == SPEAKER_TYPE_HEADPHONES || m_provider3D[m_selectedProvider].m_hrtf);
	m_selectedSpeakerType = speakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getSpeakerType()
{
	if (!isValidProvider()) {
		return 0;
	}

	return m_selectedSpeakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getNum2DSamples() const
{
	return m_num2DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getNum3DSamples() const
{
	return m_num3DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getNumStreams() const
{
	return m_numStreams;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getNumAvailable2DSamples() const
{
	return (UnsignedInt)m_availableSamples.size();
}

//-------------------------------------------------------------------------------------------------
UnsignedInt WebAudioManager::getNumAvailable3DSamples() const
{
	return (UnsignedInt)m_available3DSamples.size();
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::doesViolateLimit( AudioEventRTS *event ) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) {
		return false;
	}

	Int totalCount = 0;
	Int totalRequestCount = 0;

	std::list<WebPlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if (!(*it)->isPlayingOrRequested()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if (!(*it)->isPlayingOrRequested()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	}

	// Also check the request list in case we've requested to play this sound.
	std::list<AudioRequest*>::const_iterator arIt;
	for (arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest *req = (*arIt);
		if( req->m_pendingEvent && req->m_pendingEvent->getEventName() == event->getEventName() )
		{
			totalRequestCount++;
			totalCount++;
		}
	}

	//If our event is an interrupting type, then normally we would always add it. The exception is when we have requested
	//multiple sounds in the same frame and those requests violate the limit. Because we don't have any "old" sounds to
	//remove in the case of an interrupt, we need to catch it early and prevent the sound from being added if we already
	//reached the limit
	if( event->getAudioEventInfo()->m_control & AC_INTERRUPT )
	{
		if( totalRequestCount < limit )
		{
			Int totalPlayingCount = totalCount - totalRequestCount;
			if( totalRequestCount + totalPlayingCount < limit )
			{
				//We aren't exceeding the actual limit, then clear the kill handle.
				event->setHandleToKill(0);
				return false;
			}

			//We are exceeding the limit - the kill handle will kill the
			//oldest playing sound to enforce the actual limit.
			return false;
		}
	}

	if( totalCount < limit )
	{
		event->setHandleToKill(0);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isPlayingAlready( AudioEventRTS *event ) const
{
	std::list<WebPlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if (!(*it)->isPlaying()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if (!(*it)->isPlaying()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isObjectPlayingVoice( UnsignedInt objID ) const
{
	if (objID == 0) {
		return false;
	}

	std::list<WebPlayingAudio *>::const_iterator it;
		// 2-D
	for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		if (!(*it)->isPlaying()) {
			continue;
		}
		if ((UnsignedInt)(*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	// 3-D
	for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		if (!(*it)->isPlaying()) {
			continue;
		}
		if ((UnsignedInt)(*it)->m_audioEventRTS->getObjectID() == objID && (*it)->m_audioEventRTS->getAudioEventInfo()->m_type & ST_VOICE) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
AudioEventRTS* WebAudioManager::findLowestPrioritySound( AudioEventRTS *event )
{
	const AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return nullptr;
	}

	AudioEventRTS *lowestPriorityEvent = nullptr;
	AudioPriority lowestPriority = priority;

	std::list<WebPlayingAudio *>::const_iterator it;
	if( event->isPositionalAudio() )
	{
		//3D
		for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it )
		{
			if (!(*it)->isPlaying()) {
				continue;
			}
			AudioEventRTS *itEvent = (*it)->m_audioEventRTS.Peek();
			const AudioPriority itPriority = itEvent->getAudioEventInfo()->m_priority;
			if( itPriority < lowestPriority )
			{
				lowestPriorityEvent = itEvent;
				lowestPriority = itPriority;
				if( lowestPriority == AP_LOWEST )
				{
					return lowestPriorityEvent;
				}
			}
		}
	}
	else
	{
		//2D
		for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
		{
			if (!(*it)->isPlaying()) {
				continue;
			}
			AudioEventRTS *itEvent = (*it)->m_audioEventRTS.Peek();
			const AudioPriority itPriority = itEvent->getAudioEventInfo()->m_priority;
			if( itPriority < lowestPriority )
			{
				lowestPriorityEvent = itEvent;
				lowestPriority = itPriority;
				if( lowestPriority == AP_LOWEST )
				{
					return lowestPriorityEvent;
				}
			}
		}
	}
	return lowestPriorityEvent;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isPlayingLowerPriority( AudioEventRTS *event ) const
{
	//We don't actually want to do anything to this CONST function. Remember, we're
	//just checking to see if there is a lower priority sound.
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if( priority == AP_LOWEST )
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return false;
	}
	std::list<WebPlayingAudio *>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for ( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
			if (!(*it)->isPlaying()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	} else {
		// 3-D
		for ( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
			if (!(*it)->isPlaying()) {
				continue;
			}
			if ((*it)->m_audioEventRTS->getAudioEventInfo()->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::killLowestPrioritySoundImmediately( AudioEventRTS *event )
{
	//Actually, we want to kill the LOWEST PRIORITY SOUND, not the first "lower" priority
	//sound we find, because it could easily be
	AudioEventRTS *lowestPriorityEvent = findLowestPrioritySound( event );
	if( lowestPriorityEvent )
	{
		std::list<WebPlayingAudio *>::iterator it;
		if( event->isPositionalAudio() )
		{
			for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it )
			{
				WebPlayingAudio *playing = (*it);
				if (!playing->isPlaying())
				{
					continue;
				}
				if( playing->m_audioEventRTS.Peek() == lowestPriorityEvent )
				{
					// Stop this 3D sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio(playing);
					return TRUE;
				}
			}
		}
		else
		{
			for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
			{
				WebPlayingAudio *playing = (*it);
				if (!playing->isPlaying())
				{
					continue;
				}
				if( playing->m_audioEventRTS.Peek() == lowestPriorityEvent )
				{
					// Stop this sound channel immediately because we are going to play another sound in it's place.
					stopPlayingAudio(playing);
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}


//-------------------------------------------------------------------------------------------------
void WebAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (!playing->isPlaying()) {
			continue;
		}
		AudioEventRTS *itEvent = playing->m_audioEventRTS.Peek();

		if (itEvent->getEventName() == eventName) {
			DEBUG_ASSERTCRASH(playing->m_voice, ("Voice is not expected to be null"));
			itEvent->setVolume(newVolume);
			setVoiceGain(playing, getEffectiveVolume(itEvent));
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (!playing->isPlaying()) {
			continue;
		}
		AudioEventRTS *itEvent = playing->m_audioEventRTS.Peek();

		if (itEvent->getEventName() == eventName) {
			DEBUG_ASSERTCRASH(playing->m_voice, ("3D Voice is not expected to be null"));
			itEvent->setVolume(newVolume);
			setVoiceGain(playing, getEffectiveVolume(itEvent));
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (!playing->isPlaying()) {
			continue;
		}
		AudioEventRTS *itEvent = playing->m_audioEventRTS.Peek();

		if (itEvent->getEventName() == eventName) {
			DEBUG_ASSERTCRASH(playing->m_voice, ("Stream is not expected to be null"));
			itEvent->setVolume(newVolume);
			setVoiceGain(playing, getEffectiveVolume(itEvent));
		}
	}
}


//-------------------------------------------------------------------------------------------------
void WebAudioManager::removePlayingAudio( AsciiString eventName )
{
	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio(playing);
		}
	}

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio(playing);
		}
	}

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getEventName() == eventName )
		{
			stopPlayingAudio(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::removeAllDisabledAudio()
{
	std::list<WebPlayingAudio *>::iterator it;

	WebPlayingAudio *playing = nullptr;
	for( it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio(playing);
		}
	}

	for( it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio(playing);
		}
	}

	for( it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it )
	{
		playing = *it;
		if( playing->m_audioEventRTS->getVolume() == 0.0f )
		{
			stopPlayingAudio(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::processRequestList()
{
	std::list<AudioRequest*>::iterator it;
	for (it = m_audioRequests.begin(); it != m_audioRequests.end(); ) {
		AudioRequest *req = (*it);
		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req)) {
			processRequest(req);
		}
		deleteInstance(req);
		it = m_audioRequests.erase(it);
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::processPlayingList()
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	//
	// Stop audio. Update the position of the audio if it is positional.
	//
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = (*it);

		if (playing->m_status == WPS_Stopping) {
			rerequestPlayingAudioWhenSignalled(playing);
			stopPlayingAudio(playing);
			continue;
		}

		if (playing->m_status == WPS_Stopped) {
			continue;
		}

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = (*it);

		if (playing->m_status == WPS_Stopping) {
			rerequestPlayingAudioWhenSignalled(playing);
			stopPlayingAudio(playing);
			continue;
		}

		if (playing->m_status == WPS_Stopped) {
			continue;
		}

		const Coord3D *pos = getCurrentPositionFromEvent(playing->m_audioEventRTS.Peek());
		if (pos)
		{
			if( playing->m_audioEventRTS->isDead() )
			{
				stopAudioEvent( playing->m_audioEventRTS->getPlayingHandle() );
			}
			else
			{
				Real volForConsideration = getEffectiveVolume(playing->m_audioEventRTS.Peek());
				const Real effectiveVolume = volForConsideration;
				volForConsideration /= (m_sound3DVolume > 0.0f ? m_soundVolume : 1.0f);
				Bool playAnyways = BitIsSet( playing->m_audioEventRTS->getAudioEventInfo()->m_type, ST_GLOBAL) ||
					playing->m_audioEventRTS->getAudioEventInfo()->m_priority == AP_CRITICAL;
				if( volForConsideration < m_audioSettings->m_minVolume && !playAnyways )
				{
					stopPlayingAudio(playing);
				}
				else
				{
					DEBUG_ASSERTCRASH(playing->m_voice, ("3D Voice is not expected to be null"));
					setVoiceGain(playing, effectiveVolume);
					if (pos->x != playing->m_lastPosition.x || pos->y != playing->m_lastPosition.y || pos->z != playing->m_lastPosition.z)
					{
						playing->m_lastPosition = *pos;
						WebAudio_VoiceSetPosition(playing->m_voice, pos->x, pos->y, pos->z);
					}
				}
			}
		}
		else
		{
			stopPlayingAudio(playing);
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = (*it);

		if (playing->m_status == WPS_Stopping) {
			rerequestPlayingAudioWhenSignalled(playing);
			stopPlayingAudio(playing);
			continue;
		}

		if (playing->m_status == WPS_Stopped) {
			continue;
		}

		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
	}

	releasePlayingAudioInListIfStopped(m_playingSounds);
	releasePlayingAudioInListIfStopped(m_playing3DSounds);
	releasePlayingAudioInListIfStopped(m_playingStreams);

	//
	// Transfer streams to fade list when signaled.
	//
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);

		if (playing->m_fade)
		{
			m_fadingAudio.push_back(playing);
			it = m_playingStreams.erase(it);
			continue;
		}

		++it;
	}

	if (m_volumeHasChanged) {
		m_volumeHasChanged = false;

		// TheSuperHackers @bugfix Push speech volume changes because movie audio does not go through the mixer of the audio manager.
		if (TheVideoPlayer) {
			TheVideoPlayer->setVolume(getVolume(AudioAffect_Speech));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::processFadingList()
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ++it) {
		playing = (*it);

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			stopPlayingAudio(playing);
			continue;
		}

		if (playing->m_status == WPS_Stopped) {
			continue;
		}

		++playing->m_framesFaded;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS.Peek());
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);

		if (playing->m_voice) {
			// not setVoiceGain: it does not remember the faded volume, the audio is going away
			WebAudio_VoiceSetGain(playing->m_voice, volume);
		}
	}

	releasePlayingAudioInListIfStopped(m_fadingAudio);
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::shouldProcessRequestThisFrame( AudioRequest *req ) const
{
	if (req->m_pendingEvent == nullptr) {
		return true;
	}

	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) {
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::adjustRequest( AudioRequest *req )
{
	if (req->m_pendingEvent == nullptr) {
		return;
	}

	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::checkForSample( AudioRequest *req )
{
	if (req->m_pendingEvent == nullptr) {
		return true;
	}

  if ( req->m_pendingEvent->getAudioEventInfo() == nullptr )
  {
    // Fill in event info
    getInfoForAudioEvent( req->m_pendingEvent.Peek() );
  }

	if (req->m_pendingEvent->getAudioEventInfo()->m_type != AT_SoundEffect)
  {
		return true;
	}

	return m_sound->canPlayNow(req->m_pendingEvent.Peek());
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::setHardwareAccelerated(Bool accel)
{
	// Extends
	Bool retEarly = (accel == m_hardwareAccel);
	AudioManager::setHardwareAccelerated(accel);

	if (retEarly) {
		return;
	}

	if (m_hardwareAccel) {
		for (Int i = 0; i < MAX_HW_PROVIDERS; ++i) {
			UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[i]);
			TheAudio->selectProvider(providerNdx);
			if (getSelectedProvider() == providerNdx) {
				return;
			}
		}
	}

	// set it false
	AudioManager::setHardwareAccelerated(FALSE);
	UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS]);
	TheAudio->selectProvider(providerNdx);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::setSpeakerSurround(Bool surround)
{
	// Extends
	Bool retEarly = (surround == m_surroundSpeakers);
	AudioManager::setSpeakerSurround(surround);

	if (retEarly) {
		return;
	}

	UnsignedInt speakerType;
	if (m_surroundSpeakers) {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType3D;
	} else {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType2D;
	}

	TheAudio->setSpeakerType(speakerType);
}

//-------------------------------------------------------------------------------------------------
/** The length of an audio file in milliseconds, which scripts use to wait for speech to end and
	* which therefore takes part in the game logic (and the CRC). It does not need the sound device:
	* the length comes from the header of the file, or from counting the frames of an MP3 that has
	* no length in its header. */
//-------------------------------------------------------------------------------------------------
Real WebAudioManager::getFileLengthMS( AsciiString strToLoad ) const
{
	if (strToLoad.isEmpty()) {
		return 0.0f;
	}

	std::hash_map< AsciiString, Real, rts::hash<AsciiString>, rts::equal_to<AsciiString>/**/>::const_iterator cached = m_fileLengthCache.find(strToLoad);
	if (cached != m_fileLengthCache.end()) {
		return cached->second;
	}

	File *file = TheFileSystem->openFile(strToLoad.str(), File::READ | File::STREAMING);
	if (!file) {
		return 0.0f;
	}

	Real result = 0.0f;
	const Int fileSize = file->size();
	if (fileSize > 0) {
		// The start of the file holds the format and, for WAV and most MP3, the length.
		const Int headSize = MIN(fileSize, 64 * 1024);
		std::vector<UnsignedByte> data((size_t)headSize);
		const Int got = file->read(data.data(), headSize);
		WebAudio::StreamInfo info;
		Bool known = got > 0 && WebAudio::probe(data.data(), (size_t)got, (size_t)fileSize, &info);
		if (got == headSize && headSize < fileSize && (!known || !info.lengthExact)) {
			// Needs the whole file: an MP3 without a length in its header is measured by counting its frames.
			data.resize((size_t)fileSize);
			const Int more = file->read(data.data() + headSize, fileSize - headSize);
			if (more > 0) {
				known = WebAudio::probe(data.data(), (size_t)(headSize + more), (size_t)fileSize, &info);
			}
		}
		if (known) {
			result = INT_TO_REAL((Int)info.durationMs());
		}
	}
	file->close();

	if (result > 0.0f) {
		m_fileLengthCache[strToLoad] = result;
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::closeAnySamplesUsingFile( const void *fileToClose )
{
	std::list<WebPlayingAudio *>::iterator it;
	WebPlayingAudio *playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it ) {
		playing = *it;

		if (playing->m_file == fileToClose) {
			stopPlayingAudio(playing);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it ) {
		playing = *it;

		if (playing->m_file == fileToClose) {
			stopPlayingAudio(playing);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The listener is the "microphone" of the game: where it hears from and the direction it faces.
	* In the game world X is east, Y north and Z up, which is a right handed system like the one of
	* Web Audio, so the world coordinates go to the browser as they are, with Z as the up vector.
	* (Miles' coordinates are left handed, which is why the Miles manager passes (0, 0, -1) as the
	* up vector.) */
//-------------------------------------------------------------------------------------------------
void WebAudioManager::setDeviceListenerPosition()
{
	if (!m_deviceWorks || !isValidProvider()) {
		return;
	}

	if (m_listenerSent
		&& m_lastListenerPosition.x == m_listenerPosition.x && m_lastListenerPosition.y == m_listenerPosition.y && m_lastListenerPosition.z == m_listenerPosition.z
		&& m_lastListenerOrientation.x == m_listenerOrientation.x && m_lastListenerOrientation.y == m_listenerOrientation.y && m_lastListenerOrientation.z == m_listenerOrientation.z) {
		return;
	}
	m_listenerSent = true;
	m_lastListenerPosition = m_listenerPosition;
	m_lastListenerOrientation = m_listenerOrientation;

	const float position[3] = { m_listenerPosition.x, m_listenerPosition.y, m_listenerPosition.z };
	const float facing[3] = { m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z };
	float up[3] = { 0.0f, 0.0f, 1.0f };
	// Looking straight up or down leaves "left" and "right" undefined
	if (fabsf(facing[2]) > 0.999f * sqrtf(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2])) {
		up[1] = 1.0f;
		up[2] = 0.0f;
	}
	WebAudio_SetListener(position, facing, up);
}

//-------------------------------------------------------------------------------------------------
const Coord3D *WebAudioManager::getCurrentPositionFromEvent( AudioEventRTS *event )
{
	if (!event->isPositionalAudio()) {
		return nullptr;
	}

	return event->getCurrentPosition();
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isOnScreen( const Coord3D *pos ) const
{
	static ICoord2D dummy;
	// WorldToScreen will return True if the point is onscreen and false if it is offscreen.
	return TheTacticalView->worldToScreen(pos, &dummy);
}

//-------------------------------------------------------------------------------------------------
Real WebAudioManager::getEffectiveVolume(AudioEventRTS *event) const
{
	WebAudio::MixSettings mix;
	mix.musicVolume = m_musicVolume;
	mix.speechVolume = m_speechVolume;
	mix.soundVolume = m_soundVolume;
	mix.sound3DVolume = m_sound3DVolume;

	const AudioSettings *audioSettings = TheAudio->getAudioSettings();
	mix.use3DSoundRangeVolumeFade = audioSettings->m_use3DSoundRangeVolumeFade;
	mix.fadeExponent = audioSettings->m_3DSoundRangeVolumeFadeExponent;

	const Real volume = event->getVolume() * event->getVolumeShift();

	switch (event->getAudioEventInfo()->m_soundType)
	{
	case AT_Music:
		return WebAudio::effectiveVolume(WebAudio::MixCategory::Music, volume, mix);

	case AT_Streaming:
		return WebAudio::effectiveVolume(WebAudio::MixCategory::Speech, volume, mix);

	case AT_SoundEffect:
	{
		if (event->isPositionalAudio())
		{
			const Coord3D *pos = event->getCurrentPosition();
			Real objDistance = 0.0f;
			Real objMinDistance = 0.0f;
			Real objMaxDistance = 0.0f;
			if (pos)
			{
				Coord3D distance = m_listenerPosition;
				distance.sub(*pos);

				if (event->getAudioEventInfo()->m_type & ST_GLOBAL)
				{
					objMinDistance = audioSettings->m_globalMinRange;
					objMaxDistance = audioSettings->m_globalMaxRange;
				}
				else
				{
					objMinDistance = event->getAudioEventInfo()->m_minDistance;
					objMaxDistance = event->getAudioEventInfo()->m_maxDistance;
				}

				objDistance = distance.length();
			}
			return WebAudio::effectiveVolume(WebAudio::MixCategory::Sound3D, volume, mix, pos != nullptr, objDistance, objMinDistance, objMaxDistance);
		}
		return WebAudio::effectiveVolume(WebAudio::MixCategory::Sound, volume, mix);
	}
	}

	return volume;
}

//-------------------------------------------------------------------------------------------------
/** Asks for the sound that follows the one that plays (or is queued) on this voice when the
	* event loops: another iteration of the loop, if the event has one. Returns what happened. */
//-------------------------------------------------------------------------------------------------
WebAudioManager::PortionResult WebAudioManager::startNextLoop( WebPlayingAudio *playing )
{
	closeFile(playing->m_file);
	playing->m_file = nullptr;

	if (!playing->isPlaying()) {
		return PR_Done;
	}

	if (playing->m_requestStop) {
		return PR_Done;
	}

	if (playing->m_audioEventRTS->hasMoreLoops()) {
		// generate a new filename, and test to see whether we can play with it now
		playing->m_audioEventRTS->generateFilename();

		if (playing->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
			playing->m_rerequestOnNextUpdate = true;
			return PR_Rerequest;
		}

		if (playing->m_type == WPAT_3DSample) {
			playing->m_file = playSample3D(playing->m_audioEventRTS.Peek(), playing);
		} else {
			playing->m_file = playSample(playing->m_audioEventRTS.Peek(), playing);
		}

		return playing->m_file != nullptr ? PR_Queued : PR_Done;
	}
	return PR_Done;
}

//-------------------------------------------------------------------------------------------------
/** The state machine of a sound that is made of an attack, a sound (looped, if asked for) and a
	* decay, advanced by one portion: this is the second half of Miles' notifyOfAudioCompletion. */
//-------------------------------------------------------------------------------------------------
WebAudioManager::PortionResult WebAudioManager::startNextPortion( WebPlayingAudio *playing )
{
	AudioEventRTS *event = playing->m_audioEventRTS.Peek();

	if (event->getAudioEventInfo()->m_control & AC_LOOP) {
		if (event->getNextPlayPortion() == PP_Attack) {
			event->setNextPlayPortion(PP_Sound);
		}
		if (event->getNextPlayPortion() == PP_Sound) {
			// First, decrease the loop count.
			event->decreaseLoopCount();

			// Now, try to start the next loop
			const PortionResult result = startNextLoop(playing);
			if (result != PR_Done) {
				// A queued iteration can still be taken back if the event is stopped before it starts
				playing->m_chainedLoop = (result == PR_Queued);
				return result;
			}
		}
	}

	event->advanceNextPlayPortion();

	if (event->getNextPlayPortion() != PP_Done) {
		closeFile(playing->m_file);	// close it so as not to leak it.
		if (playing->m_type == WPAT_3DSample) {
			playing->m_file = playSample3D(event, playing);
		} else {
			playing->m_file = playSample(event, playing);
		}

		// If we don't have a file now, then the audio is complete so that we correctly close this handle.
		if (playing->m_file) {
			// The sound portion after an attack is a loop that a stop request can still take back.
			playing->m_chainedLoop = (event->getAudioEventInfo()->m_control & AC_LOOP) && event->getNextPlayPortion() == PP_Sound;
			return PR_Queued;
		}
	}
	return PR_Done;
}

//-------------------------------------------------------------------------------------------------
/** Opens a music or speech file as a stream: reads the file and hands it to the decoder thread of
	* the backend, which decodes it ahead of the playback. Music loops until it is stopped. */
//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::playStream( AudioEventRTS *event, WebPlayingAudio *playing )
{
	if (!m_deviceWorks) {
		return false;
	}

	AsciiString fileToPlay = event->getFilename();
	File *file = TheFileSystem->openFile(fileToPlay.str());
	if (!file) {
		DEBUG_ASSERTLOG(fileToPlay.isEmpty(), ("Missing Audio File: '%s'", fileToPlay.str()));
		return false;
	}
	const Int fileSize = file->size();
	char *buffer = file->readEntireAndClose();
	if (!buffer || fileSize <= 0) {
		delete [] buffer;
		return false;
	}

	WebAudioVoice voice = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	if (!voice) {
		delete [] buffer;
		return false;
	}

	// The volume is in place before the first sample is heard.
	playing->m_voice = voice;
	setVoiceGain(playing, getEffectiveVolume(event));

	const Bool isMusic = event->getAudioEventInfo()->m_soundType == AT_Music;
	playing->m_stream = WebAudio_StreamOpen(buffer, (uint32_t)fileSize, voice, isMusic ? TRUE : FALSE);
	delete [] buffer;

	if (!playing->m_stream) {
		DEBUG_LOG(("Unexpected audio format in '%s'", fileToPlay.str()));
		WebAudio_DestroyVoice(voice, 0.0f);
		playing->m_voice = 0;
		return false;
	}
	return true;
}

//-------------------------------------------------------------------------------------------------
WebCachedAudio *WebAudioManager::playSample( AudioEventRTS *event, WebPlayingAudio *playing )
{
	// Load the file in
	WebCachedAudio *file = loadFileForRead(event);
	if (file) {
		// Prep any sort of filtering, etc, here
		initFilters(playing, event);

		// Start playback. If a portion plays on the voice this one follows right behind it.
		WebAudio_VoiceQueue(playing->m_voice, file->m_buffer, 0.0f, 0);
	}

	return file;
}

//-------------------------------------------------------------------------------------------------
WebCachedAudio *WebAudioManager::playSample3D( AudioEventRTS *event, WebPlayingAudio *playing )
{
	const Coord3D *pos = getCurrentPositionFromEvent(event);
	if (pos) {
		// Load the file in
		WebCachedAudio *file = loadFileForRead(event);
		if (file) {
			// Set the position of the sample here
			playing->m_lastPosition = *pos;
			WebAudio_VoiceSetPosition(playing->m_voice, pos->x, pos->y, pos->z);
			initFilters3D(playing, event, pos);

			// Start playback
			WebAudio_VoiceQueue(playing->m_voice, file->m_buffer, 0.0f, 0);
		}
		return file;
	}

	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::buildProviderList()
{
	m_provider3D[0].name.set(PROVIDER_NAME_STEREO);
	m_provider3D[0].m_hrtf = false;
	m_provider3D[1].name.set(PROVIDER_NAME_HRTF);
	m_provider3D[1].m_hrtf = true;
	m_providerCount = MAXPROVIDERS;
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioManager::isValidProvider()
{
	return (m_selectedProvider < m_providerCount);
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::initSamplePools()
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	m_availableSamples.reserve(getAudioSettings()->m_sampleCount2D);
	m_available3DSamples.reserve(getAudioSettings()->m_sampleCount3D);

	int i = 0;
	for (i = 0; i < getAudioSettings()->m_sampleCount2D; ++i) {
		m_availableSamples.push_back(i + 1);
		++m_num2DSamples;
	}

	for (i = 0; i < getAudioSettings()->m_sampleCount3D; ++i) {
		m_available3DSamples.push_back(i + 1);
		++m_num3DSamples;
	}

	// Streams are basically free, so we can just allocate the appropriate number
	m_numStreams = getAudioSettings()->m_streamCount;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::processRequest( AudioRequest *req )
{
	switch (req->m_request)
	{
		case AR_Play:
		{
			playAudioEvent(req);
			break;
		}
		case AR_Pause:
		{
			pauseAudioEvent(req->m_handleToInteractOn);
			break;
		}
		case AR_Stop:
		{
			stopAudioEvent(req->m_handleToInteractOn);
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Miles gives the Bink video player a DirectSound device to play the sound of movies through.
	* There is none here: the movie player of the web build has to play its audio itself. */
//-------------------------------------------------------------------------------------------------
void *WebAudioManager::getHandleForBink()
{
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::releaseHandleForBink()
{

}

//-------------------------------------------------------------------------------------------------
void WebAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
		case AT_Music:
			if (!isOn(AudioAffect_Music))
				return;
			break;
		case AT_SoundEffect:
			if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D))
				return;
			break;
		case AT_Streaming:
			if (!isOn(AudioAffect_Speech))
				return;
			break;
	}

	if (!m_deviceWorks) {
		return;
	}

	AudioEventRTS event = *eventToPlay;

	event.generateFilename();
	event.generatePlayInfo();

	std::list<std::pair<AsciiString, Real>/**/>::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == event.getEventName()) {
			event.setVolume(it->second);
			break;
		}
	}

	// Load the whole file and play it
	AsciiString fileToPlay = event.getFilename();
	File *file = TheFileSystem->openFile(fileToPlay.str());
	if (!file) {
		DEBUG_ASSERTLOG(fileToPlay.isEmpty(), ("Missing Audio File: '%s'", fileToPlay.str()));
		return;
	}
	const Int fileSize = file->size();
	char *fileData = file->readEntireAndClose();
	std::vector<int16_t> pcm;
	WebAudio::StreamInfo info;
	const Bool decoded = fileData && WebAudio::decodeAll(reinterpret_cast<const uint8_t *>(fileData), (size_t)fileSize, &pcm, &info);
	delete [] fileData;
	if (!decoded) {
		return;
	}

	ForcePlayed forced;
	forced.m_buffer = WebAudio_CreateBuffer(pcm.data(), (uint32_t)info.totalFrames, info.channels, info.sampleRate);
	forced.m_voice = WebAudio_CreateVoice(WEBAUDIO_VOICE_2D);
	if (!forced.m_buffer || !forced.m_voice) {
		WebAudio_DestroyBuffer(forced.m_buffer);
		WebAudio_DestroyVoice(forced.m_voice, 0.0f);
		return;
	}

	// Even though the event type is not Speech, this is used only for mission briefings, so use the
	// speech slider to adjust the volume.
	WebAudio_VoiceSetGain(forced.m_voice, event.getVolume() * getVolume(AudioAffect_Speech));
	WebAudio_VoiceQueue(forced.m_voice, forced.m_buffer, 0.0f, 0);
	m_audioForcePlayed.push_back(forced);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
WebAudioFileCache::WebAudioFileCache() : m_currentlyUsedSize(0), m_maxSize(0)
{
}

//-------------------------------------------------------------------------------------------------
WebAudioFileCache::~WebAudioFileCache()
{
	// Free all the samples that are open.
	WebOpenFilesHashIt it;
	for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
		if (it->second.m_openCount > 0) {
			DEBUG_CRASH(("Sample '%s' is still playing, and we're trying to quit.", it->second.m_eventInfo->m_audioName.str()));
		}

		releaseOpenAudioFile(&it->second);
		// Don't erase it from the map, cause it makes this whole process way more complicated, and
		// we're about to go away anyways.
	}
}

//-------------------------------------------------------------------------------------------------
WebCachedAudio *WebAudioFileCache::openFile( AudioEventRTS *eventToOpenFrom )
{
	AsciiString strToFind;
	switch (eventToOpenFrom->getNextPlayPortion())
	{
		case PP_Attack:
			strToFind = eventToOpenFrom->getAttackFilename();
			break;
		case PP_Sound:
			strToFind = eventToOpenFrom->getFilename();
			break;
		case PP_Decay:
			strToFind = eventToOpenFrom->getDecayFilename();
			break;
		case PP_Done:
			return nullptr;
	}

	WebOpenFilesHash::iterator it;
	it = m_openFiles.find(strToFind);

	if (it != m_openFiles.end()) {
		++it->second.m_openCount;
		return &it->second;
	}

	// Couldn't find the file, so actually open it.
	File *file = TheFileSystem->openFile(strToFind.str());
	if (!file) {
		DEBUG_ASSERTLOG(strToFind.isEmpty(), ("Missing Audio File: '%s'", strToFind.str()));
		return nullptr;
	}

	UnsignedInt fileSize = file->size();
	char* buffer = file->readEntireAndClose();

	// Decode to PCM (IMA ADPCM is decompressed, as Miles does when it loads a file)
	std::vector<int16_t> pcm;
	WebAudio::StreamInfo soundInfo;
	const Bool decoded = buffer && WebAudio::decodeAll(reinterpret_cast<const uint8_t *>(buffer), fileSize, &pcm, &soundInfo);
	delete [] buffer;

	if (!decoded) {
		DEBUG_CRASH(("Unexpected compression type in '%s'", strToFind.str()));
		return nullptr;
	}

	if (eventToOpenFrom->isPositionalAudio()) {
		if (soundInfo.channels > 1) {
			DEBUG_CRASH(("Requested Positional Play of audio '%s', but it is in stereo.", strToFind.str()));
			return nullptr;
		}
	}

	WebCachedAudio openedAudioFile;
	openedAudioFile.m_eventInfo = eventToOpenFrom->getAudioEventInfo();
	openedAudioFile.m_channels = soundInfo.channels;
	openedAudioFile.m_sampleRate = soundInfo.sampleRate;
	openedAudioFile.m_frames = (UnsignedInt)soundInfo.totalFrames;
	openedAudioFile.m_fileSize = (UnsignedInt)(pcm.size() * sizeof(int16_t));
	openedAudioFile.m_openCount = 1;
	openedAudioFile.m_buffer = WebAudio_CreateBuffer(pcm.data(), openedAudioFile.m_frames, openedAudioFile.m_channels, openedAudioFile.m_sampleRate);
	if (!openedAudioFile.m_buffer) {
		return nullptr;
	}

	m_currentlyUsedSize += openedAudioFile.m_fileSize;
	if (m_currentlyUsedSize > m_maxSize) {
		// We need to free some samples, or we're not going to be able to play this sound.
		if (!freeEnoughSpaceForSample(openedAudioFile)) {
			m_currentlyUsedSize -= openedAudioFile.m_fileSize;
			releaseOpenAudioFile(&openedAudioFile);
			return nullptr;
		}
	}

	return &(m_openFiles[strToFind] = openedAudioFile);
}

//-------------------------------------------------------------------------------------------------
void WebAudioFileCache::closeFile( WebCachedAudio *fileToClose )
{
	if (!fileToClose) {
		return;
	}

	WebOpenFilesHash::iterator it;
	for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
		if ( &it->second == fileToClose ) {
			--it->second.m_openCount;
			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void WebAudioFileCache::setMaxSize( UnsignedInt size )
{
	m_maxSize = size;
}

//-------------------------------------------------------------------------------------------------
void WebAudioFileCache::releaseAll()
{
	WebOpenFilesHashIt it;
	for ( it = m_openFiles.begin(); it != m_openFiles.end(); ++it ) {
		DEBUG_ASSERTCRASH(it->second.m_openCount == 0, ("Sample '%s' is still open", it->first.str()));
		it->second.m_openCount = 0;
		releaseOpenAudioFile(&it->second);
	}
	m_openFiles.clear();
	m_currentlyUsedSize = 0;
}

//-------------------------------------------------------------------------------------------------
void WebAudioFileCache::releaseOpenAudioFile( WebCachedAudio *fileToRelease )
{
	if (fileToRelease->m_openCount > 0) {
		// This thing needs to be terminated IMMEDIATELY.
		TheAudio->closeAnySamplesUsingFile(fileToRelease);
	}

	if (fileToRelease->m_buffer) {
		// A voice that is still playing the buffer keeps its own reference to it in the browser.
		WebAudio_DestroyBuffer(fileToRelease->m_buffer);
		fileToRelease->m_buffer = 0;
		fileToRelease->m_eventInfo = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
Bool WebAudioFileCache::freeEnoughSpaceForSample(const WebCachedAudio& sampleThatNeedsSpace)
{

	Int spaceRequired = m_currentlyUsedSize - m_maxSize;
	Int runningTotal = 0;

	std::list<AsciiString> filesToClose;
	// First, search for any samples that have ref counts of 0. They are low-hanging fruit, and
	// should be considered immediately.
	WebOpenFilesHashIt it;
	for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
		if (it->second.m_openCount == 0) {
			// This is said low-hanging fruit.
			filesToClose.push_back(it->first);

			runningTotal += it->second.m_fileSize;

			if (runningTotal >= spaceRequired) {
				break;
			}
		}
	}

	// If we don't have enough space yet, then search through the events who have a count of 1 or more
	// and who are lower priority than this sound.
	// Mical said that at this point, sounds shouldn't care if other sounds are interruptable or not.
	// Kill any files of lower priority necessary to clear our the buffer.
	if (runningTotal < spaceRequired) {
		for (it = m_openFiles.begin(); it != m_openFiles.end(); ++it) {
			if (it->second.m_openCount > 0) {
				if (it->second.m_eventInfo->m_priority < sampleThatNeedsSpace.m_eventInfo->m_priority) {
					filesToClose.push_back(it->first);
					runningTotal += it->second.m_fileSize;

					if (runningTotal >= spaceRequired) {
						break;
					}
				}
			}
		}
	}

	// We weren't able to find enough sounds to truncate. Therefore, this sound is not going to play.
	if (runningTotal < spaceRequired) {
		return FALSE;
	}

	std::list<AsciiString>::iterator ait;
	for (ait = filesToClose.begin(); ait != filesToClose.end(); ++ait) {
		WebOpenFilesHashIt itToErase = m_openFiles.find(*ait);
		if (itToErase != m_openFiles.end()) {
			releaseOpenAudioFile(&itToErase->second);
			m_currentlyUsedSize -= itToErase->second.m_fileSize;
			m_openFiles.erase(itToErase);
		}
	}

	return TRUE;
}


#if defined(RTS_DEBUG)
//-------------------------------------------------------------------------------------------------
void WebAudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) {
		return;
	}

	// Dump all the audio assets we've used.
	FILE *logfile=fopen("PreloadedAssets.txt","a+");	//append to log
	if (!logfile)
		return;

	std::list<AsciiString> missingEvents;
	std::list<AsciiString> usedFiles;

	std::list<AsciiString>::iterator lit;

	fprintf(logfile, "\nAudio Asset Report - BEGIN\n");
	{
		SetAsciiStringIt it;
		std::vector<AsciiString>::iterator asIt;
		for (it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
			AsciiString astr = *it;
			AudioEventInfo *aei = findAudioEventInfo(astr);
			if (!aei) {
				missingEvents.push_back(astr);
				continue;
			}

			for (asIt = aei->m_attackSounds.begin(); asIt != aei->m_attackSounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_sounds.begin(); asIt != aei->m_sounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_decaySounds.begin(); asIt != aei->m_decaySounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			if (!aei->m_filename.isEmpty()) {
				usedFiles.push_back(aei->m_filename);
			}
		}

		fprintf(logfile, "\nEvents Requested that are missing information - BEGIN\n");
		for (lit = missingEvents.begin(); lit != missingEvents.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nEvents Requested that are missing information - END\n");

		fprintf(logfile, "\nFiles Used - BEGIN\n");
		for (lit = usedFiles.begin(); lit != usedFiles.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nFiles Used - END\n");
	}
	fprintf(logfile, "\nAudio Asset Report - END\n");
	fclose(logfile);
	logfile = nullptr;
}
#endif
