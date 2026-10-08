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

// FILE: WebVideoPlayer.cpp ///////////////////////////////////////////////////
//
// The video player of the WebAssembly build, see WebVideoPlayer.h.
//
///////////////////////////////////////////////////////////////////////////////

#include "Lib/BaseType.h"
#include "WebDevice/Video/WebVideoPlayer.h"
#include "WebDevice/Audio/WebAudioBackend.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"

#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <deque>
#include <vector>

namespace
{
	// Decoding ahead. A frame of a 640x480 video is 450 KB; the audio lead is what bridges a slow
	// frame of the game, the frames are what the lead needs (the sound of a frame comes with it).
	constexpr size_t kMinQueuedFrames = 3;						///< decode at least this many frames ahead
	constexpr size_t kMaxQueuedFrames = 24;						///< and never more than this many
	constexpr Int64 kAudioLeadUs = 600000;						///< decode this much sound ahead of the clock
	constexpr Int64 kAudioMinLeadUs = 250000;					///< hand sound to the device before the lead is less than this
	constexpr Int64 kAudioChunkUs = 100000;						///< otherwise in chunks of this length
	constexpr Int64 kAudioResyncUs = 30000;						///< a voice that starts later or earlier than this is moved into sync
	constexpr Int kMaxDecodeErrors = 16;						///< damaged packets in a row after which the video ends

	Int64 nowUs()
	{
		using namespace std::chrono;
		return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
	}

	/// Bink's volume rule: never 0 (Bink would play at full volume), 80% of the speech volume.
	float gainForSpeechVolume( Real volume )
	{
		const Int percent = static_cast<Int>(volume * 0.8f * 100.0f) + 1;
		return static_cast<float>(percent) / 100.0f;
	}

	inline int16_t toS16( float sample )
	{
		const float scaled = sample * 32768.0f;
		return static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, scaled)));
	}
}

//----------------------------------------------------------------------------
// WebVideoStream
//----------------------------------------------------------------------------

class WebVideoStream : public VideoStream
{
	friend class WebVideoPlayer;

	public:

		explicit WebVideoStream( FFmpegFile *file );							///< takes the file
		virtual ~WebVideoStream() override;

		Bool isValid() const { return m_valid; }
		void setVolume( Real speechVolume );

		virtual void update() override;
		virtual Bool isFrameReady() override;
		virtual void frameDecompress() override { }								///< frames are decoded ahead of time
		virtual void frameRender( VideoBuffer *buffer ) override;
		virtual void frameNext() override;
		virtual Int frameIndex() override { return m_current; }
		virtual Int frameCount() override { return m_frameCount; }
		virtual void frameGoto( Int index ) override;
		virtual Int height() override { return m_height; }
		virtual Int width() override { return m_width; }

	private:

		static void onFrame( AVFrame *frame, int streamIndex, int streamType, void *userData );
		void onVideoFrame( AVFrame *frame );
		void onAudioFrame( AVFrame *frame );

		/// The position in the movie: where it was set to (frameGoto), plus the time since the first request for a frame.
		Int64 clockUs() const { return m_clockBaseUs + (m_clockStarted ? nowUs() - m_clockStartUs : 0); }
		Int64 frameDueUs( Int index ) const { return (static_cast<Int64>(index) * 1000000 * m_rateDen) / m_rateNum; }
		Int64 audioDecodedUs() const { return (m_audioDecodedFrames * 1000000) / m_audioRate; }

		void pump();																			///< decodes ahead, feeds the sound
		void submitAudio( Bool force );
		void dropFrame();																		///< the current frame is over, the next one is current
		void restart();

		FFmpegFile *m_file;
		Bool m_valid = false;
		Bool m_failed = false;																///< too many damaged packets
		Int m_errors = 0;

		Int m_width = 0;
		Int m_height = 0;
		Int m_rateNum = 15;																		///< frames per second = num / den
		Int m_rateDen = 1;
		Int m_frameCount = INT_MAX;														///< from the header; the real end found while decoding wins
		Int m_current = 0;																		///< index of the frame in m_frames.front()
		std::deque<AVFrame *> m_frames;												///< decoded, not yet shown: the current frame and those after it
		SwsContext *m_sws = nullptr;
		struct SwsKey																					///< what the converter was made for
		{
			int srcWidth, srcHeight, srcFormat, srcRange, dstWidth, dstHeight, dstFormat;
		} m_swsKey = { };

		Bool m_clockStarted = false;													///< set by the first isFrameReady()
		Int64 m_clockStartUs = 0;
		Int64 m_clockBaseUs = 0;															///< the position of the movie when the clock started

		// The sound
		Int m_audioStream = -1;																///< the first audio track of the file
		Int m_audioChannels = 0;
		Int m_audioRate = 1;
		Bool m_hasAudio = false;															///< a voice exists to play the sound
		WebAudioVoice m_voice = 0;
		float m_gain = 0.8f;
		std::vector<int16_t> m_audioPending;									///< decoded, not yet queued (interleaved)
		Int64 m_audioDecodedFrames = 0;												///< samples per channel decoded since the start
		Int64 m_audioSubmittedFrames = 0;											///< ... and handed to the voice (or skipped)
		Bool m_discardAudio = false;													///< skipping ahead (frameGoto)

		// What happened, written to the console when the movie ends
		UnsignedInt m_statRendered = 0;												///< frames shown
		UnsignedInt m_statSkipped = 0;												///< frames not shown because they were late
		UnsignedInt m_statChunks = 0;													///< pieces of sound handed to the device
		UnsignedInt m_statResyncs = 0;												///< times the sound had to skip ahead or wait to catch up with the picture
};

//============================================================================
// WebVideoStream::WebVideoStream
//============================================================================

WebVideoStream::WebVideoStream( FFmpegFile *file )
: m_file(file)
{
	m_width = file->getWidth();
	m_height = file->getHeight();

	Int num = 0, den = 1;
	file->getFrameRate( num, den );
	if ( num > 0 )
	{
		m_rateNum = num;
		m_rateDen = den;
	}

	const Int frames = file->getNumFrames();
	m_frameCount = frames > 0 ? frames : INT_MAX;

	file->setFrameCallback( onFrame );
	file->setUserData( this );

	if ( file->hasAudio() )
	{
		m_audioStream = file->getAudioStreamIndex();
		m_audioChannels = file->getNumChannels();
		m_audioRate = file->getSampleRate();
		// The device may not exist (no audio manager, the sound is off): the movie is silent then.
		if ( m_audioChannels > 0 && m_audioChannels <= 8 && m_audioRate > 0 && WebAudio_GetState() != WEBAUDIO_STATE_NONE )
		{
			m_voice = WebAudio_CreateVoice( WEBAUDIO_VOICE_2D );
			m_hasAudio = m_voice != 0;
		}
		if ( m_audioRate <= 0 )
		{
			m_audioRate = 1;
		}
	}
	if ( TheAudio )
	{
		m_gain = gainForSpeechVolume( TheAudio->getVolume( AudioAffect_Speech ) );
	}
	if ( m_hasAudio )
	{
		WebAudio_VoiceSetGain( m_voice, m_gain );
	}

	pump();
	m_valid = !m_frames.empty() && m_width > 0 && m_height > 0;

	printf( "WebVideoStream: %dx%d, %d frames at %d/%d fps, sound: %s (%d Hz, %d channels)%s\n",
		m_width, m_height, m_frameCount == INT_MAX ? -1 : m_frameCount, m_rateNum, m_rateDen,
		m_hasAudio ? "yes" : (m_audioStream >= 0 ? "no device" : "none"), m_audioRate, m_audioChannels,
		m_valid ? "" : ", no picture: skipped" );
}

//============================================================================
// WebVideoStream::~WebVideoStream
//============================================================================

WebVideoStream::~WebVideoStream()
{
	if ( m_valid )
	{
		printf( "WebVideoStream: closed at frame %d of %d: %u shown, %u skipped, sound %u chunks, %u resyncs\n",
			m_current + 1, m_frameCount == INT_MAX ? -1 : m_frameCount, m_statRendered, m_statSkipped, m_statChunks, m_statResyncs );
	}

	if ( m_voice != 0 )
	{
		WebAudio_DestroyVoice( m_voice, 0.05f );
		WebAudio_Flush();
		m_voice = 0;
	}

	for ( AVFrame *frame : m_frames )
	{
		av_frame_free( &frame );
	}
	m_frames.clear();

	sws_freeContext( m_sws );
	m_sws = nullptr;

	delete m_file;
	m_file = nullptr;
}

//============================================================================
// WebVideoStream::setVolume
//============================================================================

void WebVideoStream::setVolume( Real speechVolume )
{
	m_gain = gainForSpeechVolume( speechVolume );
	if ( m_hasAudio )
	{
		WebAudio_VoiceSetGain( m_voice, m_gain );
		WebAudio_Flush();
	}
}

//============================================================================
// WebVideoStream::onFrame
//============================================================================

void WebVideoStream::onFrame( AVFrame *frame, int streamIndex, int streamType, void *userData )
{
	WebVideoStream *stream = static_cast<WebVideoStream *>(userData);
	if ( streamType == AVMEDIA_TYPE_VIDEO )
	{
		stream->onVideoFrame( frame );
	}
	else if ( streamType == AVMEDIA_TYPE_AUDIO && streamIndex == stream->m_audioStream )
	{
		stream->onAudioFrame( frame );
	}
}

//============================================================================
// WebVideoStream::onVideoFrame
//============================================================================

void WebVideoStream::onVideoFrame( AVFrame *frame )
{
	// The decoder reuses its frame; the reference keeps the picture.
	AVFrame *copy = av_frame_clone( frame );
	if ( copy != nullptr )
	{
		m_frames.push_back( copy );
	}
}

//============================================================================
// WebVideoStream::onAudioFrame
//============================================================================

void WebVideoStream::onAudioFrame( AVFrame *frame )
{
	const Int samples = frame->nb_samples;
	const Int channels = frame->ch_layout.nb_channels;
	if ( samples <= 0 || channels != m_audioChannels )
	{
		return;
	}

	m_audioDecodedFrames += samples;
	if ( !m_hasAudio || m_discardAudio )
	{
		return;
	}

	const size_t oldSize = m_audioPending.size();
	m_audioPending.resize( oldSize + static_cast<size_t>(samples) * channels );
	int16_t *dst = m_audioPending.data() + oldSize;

	switch ( frame->format )
	{
		case AV_SAMPLE_FMT_FLT:
		{
			const float *src = reinterpret_cast<const float *>(frame->data[0]);
			for ( Int i = 0; i < samples * channels; ++i )
			{
				dst[i] = toS16( src[i] );
			}
			break;
		}
		case AV_SAMPLE_FMT_FLTP:
		{
			for ( Int c = 0; c < channels; ++c )
			{
				const float *src = reinterpret_cast<const float *>(frame->extended_data[c]);
				for ( Int i = 0; i < samples; ++i )
				{
					dst[i * channels + c] = toS16( src[i] );
				}
			}
			break;
		}
		case AV_SAMPLE_FMT_S16:
		{
			memcpy( dst, frame->data[0], static_cast<size_t>(samples) * channels * sizeof(int16_t) );
			break;
		}
		case AV_SAMPLE_FMT_S16P:
		{
			for ( Int c = 0; c < channels; ++c )
			{
				const int16_t *src = reinterpret_cast<const int16_t *>(frame->extended_data[c]);
				for ( Int i = 0; i < samples; ++i )
				{
					dst[i * channels + c] = src[i];
				}
			}
			break;
		}
		default:
			// Bink's decoders give nothing else.
			m_audioPending.resize( oldSize );
			break;
	}
}

//============================================================================
// WebVideoStream::pump
//============================================================================
/** Decodes packets until the next few frames and the sound of the next fraction of a second
	* are there. The packets of a Bink file hold the sound of a frame in front of the frame. */
//============================================================================

void WebVideoStream::pump()
{
	const Int64 clock = clockUs();

	while ( !m_failed && !m_file->atEnd() && m_frames.size() < kMaxQueuedFrames )
	{
		const Bool needVideo = m_frames.size() < kMinQueuedFrames;
		const Bool needAudio = m_hasAudio && audioDecodedUs() - clock < kAudioLeadUs;
		if ( !needVideo && !needAudio )
		{
			break;
		}

		if ( m_file->decodePacket() )
		{
			m_errors = 0;
		}
		else if ( !m_file->atEnd() && ++m_errors >= kMaxDecodeErrors )
		{
			m_failed = true;
		}
	}

	submitAudio( m_file->atEnd() || m_failed );
}

//============================================================================
// WebVideoStream::submitAudio
//============================================================================
/** Hands the decoded sound to the voice in chunks, gapless behind what is queued already. */
//============================================================================

void WebVideoStream::submitAudio( Bool force )
{
	if ( !m_hasAudio || !m_clockStarted || m_audioPending.empty() )
	{
		return;
	}

	const Int channels = m_audioChannels;
	Int64 frames = static_cast<Int64>(m_audioPending.size()) / channels;
	const Int64 chunkStartUs = (m_audioSubmittedFrames * 1000000) / m_audioRate;
	const Int64 leadUs = chunkStartUs - clockUs();
	if ( !force && frames * 1000000 < kAudioChunkUs * m_audioRate && leadUs > kAudioMinLeadUs )
	{
		return;
	}

	Int64 skip = 0;
	float delaySeconds = 0.0f;
	if ( WebAudio_VoicePendingSegments( m_voice ) == 0 )
	{
		// The voice is idle, so the chunk starts now. At the start of the movie that is when it is due. If the
		// engine thread stalled for longer than the lead, the sound is late and skips ahead to catch up with the
		// picture; if it is early (the sound could not play before) it waits.
		const Int64 lagUs = -leadUs;
		if ( lagUs > kAudioResyncUs )
		{
			skip = std::min( frames, (lagUs * m_audioRate) / 1000000 );
			++m_statResyncs;
		}
		else if ( lagUs < -kAudioResyncUs )
		{
			delaySeconds = static_cast<float>(-lagUs) / 1000000.0f;
			++m_statResyncs;
		}
	}

	if ( skip < frames && WebAudio_GetState() == WEBAUDIO_STATE_RUNNING )
	{
		const WebAudioBuffer buffer = WebAudio_CreateBuffer( m_audioPending.data() + skip * channels,
			static_cast<uint32_t>(frames - skip), static_cast<uint32_t>(channels), static_cast<uint32_t>(m_audioRate) );
		if ( buffer != 0 )
		{
			WebAudio_VoiceQueue( m_voice, buffer, delaySeconds, 0 );
			++m_statChunks;
			// The voice keeps the data until it has played it; the buffer id can go now.
			WebAudio_DestroyBuffer( buffer );
			WebAudio_Flush();
		}
	}

	m_audioSubmittedFrames += frames;
	m_audioPending.clear();
}

//============================================================================
// WebVideoStream::update
//============================================================================

void WebVideoStream::update()
{
	// Keeps the sound fed while the owner of the movie is not asking for frames.
	if ( m_clockStarted )
	{
		pump();
	}
}

//============================================================================
// WebVideoStream::isFrameReady
//============================================================================

Bool WebVideoStream::isFrameReady()
{
	if ( !m_clockStarted )
	{
		// The movie starts when it is first asked for a frame.
		m_clockStarted = TRUE;
		m_clockStartUs = nowUs();
	}

	pump();
	return clockUs() >= frameDueUs( m_current );
}

//============================================================================
// WebVideoStream::frameRender
//============================================================================

void WebVideoStream::frameRender( VideoBuffer *buffer )
{
	if ( buffer == nullptr || m_frames.empty() )
	{
		return;
	}

	AVPixelFormat dstFormat;
	switch ( buffer->format() )
	{
		case VideoBuffer::TYPE_R8G8B8:
			dstFormat = AV_PIX_FMT_RGB24;
			break;
		case VideoBuffer::TYPE_X8R8G8B8:
			dstFormat = AV_PIX_FMT_BGR0;
			break;
		case VideoBuffer::TYPE_R5G6B5:
			dstFormat = AV_PIX_FMT_RGB565;
			break;
		case VideoBuffer::TYPE_X1R5G5B5:
			dstFormat = AV_PIX_FMT_RGB555;
			break;
		default:
			return;
	}

	AVFrame *frame = m_frames.front();

	// The converter is made again only when something about the pictures changes (sws_getCachedContext does it
	// for every frame in this version of FFmpeg, as it compares flags that sws_init_context has changed).
	const SwsKey key = { frame->width, frame->height, frame->format, frame->color_range,
		static_cast<Int>(buffer->width()), static_cast<Int>(buffer->height()), static_cast<Int>(dstFormat) };
	if ( m_sws == nullptr || memcmp( &key, &m_swsKey, sizeof(key) ) != 0 )
	{
		sws_freeContext( m_sws );
		m_sws = sws_getContext( frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
			buffer->width(), buffer->height(), dstFormat, SWS_BICUBIC, nullptr, nullptr, nullptr );
		m_swsKey = key;
		if ( m_sws != nullptr && frame->color_range == AVCOL_RANGE_JPEG )
		{
			// The Bink versions up to i are in the limited (MPEG) range, which is what swscale assumes. Revision k is full range.
			sws_setColorspaceDetails( m_sws, sws_getCoefficients( SWS_CS_ITU601 ), 1,
				sws_getCoefficients( SWS_CS_DEFAULT ), 1, 0, 1 << 16, 1 << 16 );
		}
	}
	if ( m_sws == nullptr )
	{
		return;
	}

	uint8_t *dst = static_cast<uint8_t *>( buffer->lock() );
	if ( dst == nullptr )
	{
		return;
	}

	uint8_t *dstData[] = { dst };
	int dstStride[] = { static_cast<int>(buffer->pitch()) };
	sws_scale( m_sws, frame->data, frame->linesize, 0, frame->height, dstData, dstStride );
	buffer->unlock();
	++m_statRendered;
}

//============================================================================
// WebVideoStream::dropFrame
//============================================================================

void WebVideoStream::dropFrame()
{
	av_frame_free( &m_frames.front() );
	m_frames.pop_front();
	++m_current;
}

//============================================================================
// WebVideoStream::frameNext
//============================================================================

void WebVideoStream::frameNext()
{
	if ( m_current >= m_frameCount - 1 )
	{
		return;
	}

	// The next frame, decoded now if it is not there yet. The last frame stays the current one if there is no more.
	if ( m_frames.size() < 2 )
	{
		pump();
	}
	if ( m_frames.size() < 2 )
	{
		m_frameCount = m_current + 1;
		return;
	}
	dropFrame();

	// Frames that are already late are not shown: the movie keeps its time when the game renders slowly.
	while ( m_current < m_frameCount - 1 && clockUs() >= frameDueUs( m_current + 1 ) )
	{
		if ( m_frames.size() < 2 )
		{
			pump();
			if ( m_frames.size() < 2 )
			{
				break;
			}
		}
		dropFrame();
		++m_statSkipped;
	}

	pump();
}

//============================================================================
// WebVideoStream::restart
//============================================================================

void WebVideoStream::restart()
{
	for ( AVFrame *frame : m_frames )
	{
		av_frame_free( &frame );
	}
	m_frames.clear();
	m_current = 0;
	m_audioPending.clear();
	m_audioDecodedFrames = 0;
	m_audioSubmittedFrames = 0;
	m_errors = 0;
	m_failed = false;
	if ( m_hasAudio )
	{
		WebAudio_VoiceCancelPending( m_voice );
	}
}

//============================================================================
// WebVideoStream::frameGoto
//============================================================================

void WebVideoStream::frameGoto( Int index )
{
	index = std::max( 0, std::min( index, m_frameCount - 1 ) );
	if ( index < m_current )
	{
		if ( !m_file->rewind() )
		{
			return;
		}
		restart();
	}

	// Decode forward without keeping the sound.
	m_discardAudio = TRUE;
	while ( m_current < index )
	{
		while ( m_frames.size() < 2 && !m_failed && !m_file->atEnd() )
		{
			if ( m_file->decodePacket() )
			{
				m_errors = 0;
			}
			else if ( !m_file->atEnd() && ++m_errors >= kMaxDecodeErrors )
			{
				m_failed = TRUE;
			}
		}
		if ( m_frames.size() < 2 )
		{
			break;
		}
		dropFrame();
	}
	m_discardAudio = FALSE;

	// The sound goes on from here, the clock is set to the frame.
	m_audioPending.clear();
	m_audioSubmittedFrames = m_audioDecodedFrames;
	if ( m_hasAudio )
	{
		WebAudio_VoiceCancelPending( m_voice );
	}
	m_clockBaseUs = frameDueUs( m_current );
	m_clockStartUs = nowUs();
	pump();
}

//----------------------------------------------------------------------------
// WebVideoPlayer
//----------------------------------------------------------------------------

//============================================================================
// WebVideoPlayer::createStream
//============================================================================

VideoStreamInterface* WebVideoPlayer::createStream( File* file )
{
	if ( file == nullptr )
	{
		return nullptr;
	}

	FFmpegFile *ffmpegFile = NEW FFmpegFile();
	if ( !ffmpegFile->open( file ) )
	{
		// open() closed the file
		delete ffmpegFile;
		return nullptr;
	}
	// FFmpegFile::open turns the library's messages on in logging builds; its warnings (such as that swscale has no
	// accelerated conversion on WebAssembly) are not for the console of the browser.
	av_log_set_level( AV_LOG_ERROR );

	WebVideoStream *stream = NEW WebVideoStream( ffmpegFile );
	if ( !stream->isValid() )
	{
		delete stream;
		return nullptr;
	}

	stream->m_next = m_firstStream;
	stream->m_player = this;
	m_firstStream = stream;
	return stream;
}

//============================================================================
// WebVideoPlayer::setVolume
//============================================================================

void WebVideoPlayer::setVolume( Real volume )
{
	for ( VideoStream *stream = m_firstStream; stream != nullptr; stream = static_cast<VideoStream *>( stream->next() ) )
	{
		static_cast<WebVideoStream *>( stream )->setVolume( volume );
	}
}
