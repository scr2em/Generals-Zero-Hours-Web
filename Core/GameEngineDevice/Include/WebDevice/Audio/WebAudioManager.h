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

// FILE: WebAudioManager.h ////////////////////////////////////////////////////
//
// The AudioManager of the WebAssembly build. It is the counterpart of MilesAudioManager and
// behaves the same towards the rest of the game: the same request processing, event lifetimes,
// priorities and limits, category volumes, music tracks, speech, looping with attack and decay,
// fading, pausing, getFileLengthMS. What differs is how sound reaches the speakers:
//
//  - Miles plays on its own timer thread and calls back when a sample ends. Here everything runs
//    on the engine thread, which looks at its voices once per update (pollPlayingAudio). To keep
//    the portions of a sound (attack, loops, decay) gapless the next portion is queued on the
//    voice shortly before the current one ends instead of after it has ended.
//  - Miles' sample handles are slots of a pool here; the voices are those of WebAudioBackend.
//  - Miles decodes IMA ADPCM when a sound is loaded into its cache; here all sounds are decoded
//    to PCM by WebAudio::Decoder and uploaded to the browser as a buffer, the cache keeps track
//    of the buffers and the footprint exactly like AudioFileCache.
//  - Music and speech are streams decoded on a worker thread (WebAudio_StreamOpen).
//  - Miles' providers are 3D positioning algorithms; the web device offers equal power panning
//    and HRTF (headphones).
//
// See WebAudioBackend.h for the design of the browser side and why it is not OpenAL/miniaudio.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/GameAudio.h"
#include "WebDevice/Audio/WebAudioBackend.h"

#include <list>
#include <vector>

class AudioEventRTS;
class DynamicAudioEventRTS;

enum WebPlayingAudioType CPP_11(: Int)
{
	WPAT_Sample,
	WPAT_3DSample,
	WPAT_Stream,
	WPAT_INVALID
};

enum WebPlayingStatus CPP_11(: Int)
{
	WPS_Playing,
	WPS_Stopping, ///< Is about to be stopped
	WPS_Stopped, ///< Is about to be released
};

/// A sound effect that has been decoded and uploaded to the browser.
struct WebCachedAudio
{
	WebAudioBuffer m_buffer;
	UnsignedInt m_channels;
	UnsignedInt m_sampleRate;
	UnsignedInt m_frames;
	UnsignedInt m_openCount;
	UnsignedInt m_fileSize;	///< size of the decoded sound, which is what counts against the audio footprint

	// Note: does not own the m_eventInfo, and should not delete it.
	const AudioEventInfo *m_eventInfo;	// Not mutable, unlike the one on AudioEventRTS.
};

typedef std::hash_map< AsciiString, WebCachedAudio, rts::hash<AsciiString>, rts::equal_to<AsciiString>/**/> WebOpenFilesHash;
typedef WebOpenFilesHash::iterator WebOpenFilesHashIt;

/// The counterpart of Miles' AudioFileCache. Used from the engine thread only.
class WebAudioFileCache
{
	public:
		WebAudioFileCache();
		virtual ~WebAudioFileCache();

		WebCachedAudio *openFile( AudioEventRTS *eventToOpenFrom );
		void closeFile( WebCachedAudio *fileToClose );
		void setMaxSize( UnsignedInt size );
		void releaseAll();	///< forgets all sounds (for when the device goes away); none may be open

		UnsignedInt getCurrentlyUsedSize() const { return m_currentlyUsedSize; }
		UnsignedInt getMaxSize() const { return m_maxSize; }

	protected:
		void releaseOpenAudioFile( WebCachedAudio *fileToRelease );

		// This function will return TRUE if it was able to free enough space, and FALSE otherwise.
		Bool freeEnoughSpaceForSample(const WebCachedAudio& sampleThatNeedsSpace);

		WebOpenFilesHash m_openFiles;
		UnsignedInt m_currentlyUsedSize;
		UnsignedInt m_maxSize;
};

struct WebPlayingAudio
{
	RefCountPtr<DynamicAudioEventRTS> m_audioEventRTS;
	WebAudioVoice m_voice;
	WebAudioStream m_stream;
	WebCachedAudio *m_file; // The sound that is playing, or was queued last
	WebPlayingAudioType m_type;
	WebPlayingStatus m_status;
	Int m_slot; // Number of the slot of the sample pool this occupies
	Short m_framesFaded;
	Bool m_fade;
	Bool m_rerequestOnNextUpdate;
	Bool m_requestStop; // Let the audio finish but stop looping if it is looping
	Bool m_lastPortionQueued; // Nothing follows what is queued on the voice, so when it has played the audio is complete
	Bool m_chainedLoop; // The portion queued behind the playing one is the next iteration of a loop
	Real m_lastGain; // What was last sent to the voice
	Coord3D m_lastPosition;

	WebPlayingAudio()
		: m_audioEventRTS(nullptr)
		, m_voice(0)
		, m_stream(0)
		, m_file(nullptr)
		, m_type(WPAT_INVALID)
		, m_status(WPS_Playing)
		, m_slot(0)
		, m_framesFaded(0)
		, m_fade(false)
		, m_rerequestOnNextUpdate(false)
		, m_requestStop(false)
		, m_lastPortionQueued(false)
		, m_chainedLoop(false)
		, m_lastGain(-1.0f)
	{
		m_lastPosition.zero();
	}

	Bool isPlaying() const
	{
		return m_status == WPS_Playing;
	}

	Bool isPlayingOrRequested() const
	{
		return m_status == WPS_Playing || m_rerequestOnNextUpdate;
	}
};

class WebAudioManager : public AudioManager
{

	public:
#if defined(RTS_DEBUG)
		virtual void audioDebugDisplay(DebugDisplayInterface *dd, void *, FILE *fp = nullptr ) override;
		virtual AudioHandle addAudioEvent( const AudioEventRTS *eventToAdd ) override; ///< Add an audio event (event must be declared in an INI file)
#endif

		// from AudioDevice
		virtual void init() override;
		virtual void postProcessLoad() override;
		virtual void reset() override;
		virtual void update() override;

		WebAudioManager();
		virtual ~WebAudioManager() override;

		virtual AsciiString nextMusicTrack() override;
		virtual AsciiString prevMusicTrack() override;
		virtual Bool isMusicPlaying() const override;
		virtual Bool hasMusicTrackCompleted( const AsciiString& trackName, Int numberOfTimes ) const override;

		virtual void openDevice() override;
		virtual void closeDevice() override;
		virtual void *getDevice() override { return m_deviceOpened && m_deviceWorks ? this : nullptr; }

		virtual void stopAudio( AudioAffect which ) override;
		virtual void pauseAudio( AudioAffect which ) override;
		virtual void resumeAudio( AudioAffect which ) override;
		virtual void pauseAmbient( Bool shouldPause ) override;

		virtual void killAudioEventImmediately( AudioHandle audioEvent ) override;

		///< Return whether the current audio is playing or not.
		///< NOTE NOTE NOTE !!DO NOT USE THIS IN FOR GAMELOGIC PURPOSES!! NOTE NOTE NOTE
		virtual Bool isCurrentlyPlaying( AudioHandle handle ) override;

		// There are no completion callbacks from another thread: the engine thread notices that
		// a voice has run dry in pollPlayingAudio. This is the entry point of that, kept with its
		// Miles name and meaning (the flags are a WebPlayingAudioType).
		virtual void notifyOfAudioCompletion( UnsignedInt handle, UnsignedInt flags ) override;

		virtual UnsignedInt getProviderCount() const override;
		virtual AsciiString getProviderName( UnsignedInt providerNum ) const override;
		virtual UnsignedInt getProviderIndex( AsciiString providerName ) const override;
		virtual void selectProvider( UnsignedInt providerNdx ) override;
		virtual void unselectProvider() override;
		virtual UnsignedInt getSelectedProvider() const override;
		virtual void setSpeakerType( UnsignedInt speakerType ) override;
		virtual UnsignedInt getSpeakerType() override;

		// The movie player of Bink played through Miles' DirectSound; there is no such thing here.
 		virtual void *getHandleForBink() override;
 		virtual void releaseHandleForBink() override;

		virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override;

		virtual UnsignedInt getNum2DSamples() const override;
		virtual UnsignedInt getNum3DSamples() const override;
		virtual UnsignedInt getNumStreams() const override;
		virtual UnsignedInt getNumAvailable2DSamples() const override;
		virtual UnsignedInt getNumAvailable3DSamples() const override;

		virtual Bool doesViolateLimit( AudioEventRTS *event ) const override;
		virtual Bool isPlayingLowerPriority( AudioEventRTS *event ) const override;
		virtual Bool isPlayingAlready( AudioEventRTS *event ) const override;
		virtual Bool isObjectPlayingVoice( UnsignedInt objID ) const override;
		Bool killLowestPrioritySoundImmediately( AudioEventRTS *event );
		AudioEventRTS* findLowestPrioritySound( AudioEventRTS *event );

		virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override;

		virtual void removePlayingAudio( AsciiString eventName ) override;
		virtual void removeAllDisabledAudio() override;

		virtual void processRequestList() override;
		virtual void processPlayingList();
		virtual void processFadingList();

		Bool shouldProcessRequestThisFrame( AudioRequest *req ) const;
		void adjustRequest( AudioRequest *req );
		Bool checkForSample( AudioRequest *req );

		virtual void setHardwareAccelerated(Bool accel) override;
		virtual void setSpeakerSurround(Bool surround) override;

		virtual void setPreferredProvider(AsciiString provider) override { m_pref3DProvider = provider; }
		virtual void setPreferredSpeaker(AsciiString speakerType) override { m_prefSpeaker = speakerType; }

		virtual Real getFileLengthMS( AsciiString strToLoad ) const override;

		virtual void closeAnySamplesUsingFile( const void *fileToClose ) override;

	protected:
		// 3-D functions
		virtual void setDeviceListenerPosition() override;
		const Coord3D *getCurrentPositionFromEvent( AudioEventRTS *event );
		Bool isOnScreen( const Coord3D *pos ) const;
		Real getEffectiveVolume(AudioEventRTS *event) const;

		// Looping functions. What happened when the next portion of a sound was asked for:
		enum PortionResult
		{
			PR_Queued,		///< another portion (a loop, the attack, the decay) was handed to the voice
			PR_Done,			///< the sound is complete
			PR_Rerequest,	///< the next loop has to wait a delay, a new request will be made when the voice has stopped
		};
		PortionResult startNextLoop( WebPlayingAudio *playing );
		PortionResult startNextPortion( WebPlayingAudio *playing );
		void playbackFinished( WebPlayingAudio *playing );
		void cancelQueuedLoop( WebPlayingAudio *playing );
		WebPlayingAudio *findPlayingAudioFromVoice( UnsignedInt voice, UnsignedInt type );

		Bool playStream( AudioEventRTS *event, WebPlayingAudio *playing );
		// Returns the file that is playing, for attachment to the WebPlayingAudio structure
		WebCachedAudio *playSample( AudioEventRTS *event, WebPlayingAudio *playing );
		WebCachedAudio *playSample3D( AudioEventRTS *event, WebPlayingAudio *playing );

		void buildProviderList();
		Bool isValidProvider();
		void initSamplePools();
		void processRequest( AudioRequest *req );

		void playAudioEvent( AudioRequest* req );
		void stopAudioEvent( AudioHandle handle );
		void pauseAudioEvent( AudioHandle handle );

		WebCachedAudio *loadFileForRead( AudioEventRTS *eventToLoadFrom );
		void closeFile( WebCachedAudio *fileRead );

		WebPlayingAudio *allocatePlayingAudio();
		void releaseWebHandles( WebPlayingAudio *playing );
		void releasePlayingAudio( WebPlayingAudio *playing );
		void stopPlayingAudio( WebPlayingAudio *playing );
		void rerequestPlayingAudio( WebPlayingAudio *playing );
		void rerequestPlayingAudioWhenSignalled( WebPlayingAudio *playing );
		void fadePlayingAudio( WebPlayingAudio *playing );

		WebPlayingAudio *findActiveMusic( const AsciiString *trackName = nullptr );
		const WebPlayingAudio *findActiveMusic( const AsciiString* trackName = nullptr ) const;

		void releasePlayingAudioInListIfStopped(std::list<WebPlayingAudio *> &list);
		void stopAllAudioImmediately();
		void freeAllWebHandles();

		Int getAvailable2DSample( AudioEventRTS *event );
		Int getAvailable3DSample( AudioEventRTS *event );

		void adjustPlayingVolume( WebPlayingAudio *audio );

		void stopAllSpeech();

		void pollPlayingAudio();
		void pollSamples( std::list<WebPlayingAudio *> &list );
		void pollStreams();
		void releaseFinishedForcePlayed();

		void initFilters( WebPlayingAudio *playing, AudioEventRTS *eventInfo );
		void initFilters3D( WebPlayingAudio *playing, AudioEventRTS *eventInfo, const Coord3D *pos );
		void setVoiceGain( WebPlayingAudio *playing, Real gain );

		static WebAudioVoiceKind voiceKindOf( WebPlayingAudioType type ) { return type == WPAT_3DSample ? WEBAUDIO_VOICE_3D : WEBAUDIO_VOICE_2D; }

	protected:
		struct ProviderInfo
		{
			AsciiString name;
			Bool m_hrtf;
		};
		enum { MAXPROVIDERS = 2 };

		ProviderInfo m_provider3D[MAXPROVIDERS];
		UnsignedInt m_providerCount;
		UnsignedInt m_selectedProvider;
		UnsignedInt m_lastProvider;
		UnsignedInt m_selectedSpeakerType;

		AsciiString m_pref3DProvider;
		AsciiString m_prefSpeaker;

		// This is a list of sounds that are forcibly played. They always play as UI sounds.
		struct ForcePlayed
		{
			WebAudioVoice m_voice;
			WebAudioBuffer m_buffer;
		};
		std::list<ForcePlayed> m_audioForcePlayed;

		// Available slots for play. Note that there aren't slots in advance for
		// streaming things, only 2-D and 3-D sounds.
		std::vector<Int> m_availableSamples;
		std::vector<Int> m_available3DSamples;

		// Currently Playing audio. Useful if we have to preempt it.
		// This should rarely if ever happen, as we mirror this in Sounds, and attempt to
		// keep preemption from taking place here.
		std::list<WebPlayingAudio *> m_playingSounds;
		std::list<WebPlayingAudio *> m_playing3DSounds;
		std::list<WebPlayingAudio *> m_playingStreams;

		// Currently fading music. We just let it finish fading, then release it.
		std::list<WebPlayingAudio *> m_fadingAudio;

		WebAudioFileCache *m_audioCache;
		UnsignedInt m_num2DSamples;
		UnsignedInt m_num3DSamples;
		UnsignedInt m_numStreams;

		Bool m_deviceOpened;
		Bool m_deviceWorks; ///< the browser offers Web Audio and the context was created

		Coord3D m_lastListenerPosition;
		Coord3D m_lastListenerOrientation;
		Bool m_listenerSent;

		mutable std::hash_map< AsciiString, Real, rts::hash<AsciiString>, rts::equal_to<AsciiString>/**/> m_fileLengthCache;

#if defined(RTS_DEBUG)
		typedef std::set<AsciiString> SetAsciiString;
		typedef SetAsciiString::iterator SetAsciiStringIt;
		SetAsciiString m_allEventsLoaded;
		void dumpAllAssetsUsed();
#endif

};

// TheSuperHackers @feature AudioManager that does almost nothing. Useful for headless mode.
// The scripts need the actual audio file length (getFileLengthMS) to function properly, which is important for the CRC
// computation, so that still works, without a device.
class WebAudioManagerDummy : public WebAudioManager
{
#if defined(RTS_DEBUG)
	virtual void audioDebugDisplay(DebugDisplayInterface* dd, void* userData, FILE* fp) override {}
#endif
	virtual void openDevice() override {}
	virtual void closeDevice() override {}
	virtual void* getDevice() override { return nullptr; }
	virtual void update() override { AudioManager::update(); removeAllAudioRequests(); }
	virtual void processRequestList() override { removeAllAudioRequests(); }
	virtual void stopAudio(AudioAffect which) override {}
	virtual void pauseAudio(AudioAffect which) override {}
	virtual void resumeAudio(AudioAffect which) override {}
	virtual void pauseAmbient(Bool shouldPause) override {}
	virtual void killAudioEventImmediately(AudioHandle audioEvent) override {}
	virtual AsciiString nextMusicTrack() override { return AsciiString::TheEmptyString; }
	virtual AsciiString prevMusicTrack() override { return AsciiString::TheEmptyString; }
	virtual Bool isMusicPlaying() const override { return false; }
	virtual Bool hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const override { return false; }
	virtual void notifyOfAudioCompletion(UnsignedInt audioCompleted, UnsignedInt flags) override {}
	virtual UnsignedInt getProviderCount() const override { return 0; }
	virtual AsciiString getProviderName(UnsignedInt providerNum) const override { return AsciiString::TheEmptyString; }
	virtual UnsignedInt getProviderIndex(AsciiString providerName) const override { return 0; }
	virtual void selectProvider(UnsignedInt providerNdx) override {}
	virtual void unselectProvider() override {}
	virtual UnsignedInt getSelectedProvider() const override { return 0; }
	virtual void setSpeakerType(UnsignedInt speakerType) override {}
	virtual UnsignedInt getSpeakerType() override { return 0; }
	virtual UnsignedInt getNum2DSamples() const override { return 0; }
	virtual UnsignedInt getNum3DSamples() const override { return 0; }
	virtual UnsignedInt getNumStreams() const override { return 0; }
	virtual UnsignedInt getNumAvailable2DSamples() const override { return 0; }
	virtual UnsignedInt getNumAvailable3DSamples() const override { return 0; }
	virtual Bool doesViolateLimit(AudioEventRTS* event) const override { return false; }
	virtual Bool isPlayingLowerPriority(AudioEventRTS* event) const override { return false; }
	virtual Bool isPlayingAlready(AudioEventRTS* event) const override { return false; }
	virtual Bool isObjectPlayingVoice(UnsignedInt objID) const override { return false; }
	virtual void adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume) override {}
	virtual void removePlayingAudio(AsciiString eventName) override {}
	virtual void removeAllDisabledAudio() override {}
	virtual void* getHandleForBink() override { return nullptr; }
	virtual void releaseHandleForBink() override {}
	virtual void friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay) override {}
	virtual void setPreferredProvider(AsciiString providerNdx) override {}
	virtual void setPreferredSpeaker(AsciiString speakerType) override {}
	virtual void closeAnySamplesUsingFile(const void* fileToClose) override {}
	virtual void setDeviceListenerPosition() override {}
};
