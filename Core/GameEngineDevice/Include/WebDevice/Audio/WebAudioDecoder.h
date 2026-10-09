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

// FILE: WebAudioDecoder.h ////////////////////////////////////////////////////
//
// Audio file decoders of the WebAssembly audio device: RIFF/WAVE (PCM of 8, 16,
// 24 and 32 bits, 32/64 bit float, IMA ADPCM) and MP3 (minimp3, with the
// LAME/Xing gapless information). These are the formats of the game data: the
// sound effects are WAV (PCM or IMA ADPCM, which Miles decompresses in
// AIL_decompress_ADPCM), the music and some of the speech are MP3.
//
// Everything here is plain portable C++ that knows nothing about the engine
// or the browser, so it runs on any thread (the music streamer decodes on a
// worker thread) and is unit tested natively.
//
// The decoders produce interleaved signed 16 bit samples, as Miles does.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <vector>

namespace WebAudio
{

enum class Codec : uint8_t
{
	Unknown,
	Pcm,			///< RIFF/WAVE, uncompressed
	ImaAdpcm,	///< RIFF/WAVE, WAVE_FORMAT_IMA_ADPCM
	Mp3,			///< MPEG audio layer 1, 2 or 3
};

struct StreamInfo
{
	Codec codec = Codec::Unknown;
	uint32_t sampleRate = 0;
	uint32_t channels = 0;
	uint64_t totalFrames = 0;	///< length in sample frames (one sample of every channel), 0 if unknown
	bool lengthExact = true;	///< false if totalFrames is an estimate (probe() of the start of an MP3 without a Xing tag)

	/// Length in milliseconds like Miles' AIL_stream_ms_position reports it, 0 if unknown.
	uint32_t durationMs() const
	{
		return sampleRate ? (uint32_t)((totalFrames * 1000u) / sampleRate) : 0u;
	}
};

/// A decoder over a memory image of a file. The memory must stay valid and unchanged while
/// the decoder is in use.
class Decoder
{
public:
	virtual ~Decoder() {}

	/// Opens the file image, or returns null if the format is not recognized or damaged.
	static std::unique_ptr<Decoder> create(const uint8_t *data, size_t size);

	const StreamInfo &info() const { return m_info; }

	/// Decodes up to maxFrames sample frames into out (interleaved, info().channels per frame).
	/// Returns the number of frames decoded, 0 when the end of the stream is reached.
	virtual size_t read(int16_t *out, size_t maxFrames) = 0;

	/// Restarts decoding at the first frame. Returns false if that is not possible.
	virtual bool rewind() = 0;

protected:
	StreamInfo m_info;
};

/// Reads the format of the file from the start of the file image only, without decoding.
/// data/size may be just the beginning of the file (a RIFF header or the first MP3 frames) as
/// long as fileSize says how long the entire file is. The length of an MP3 without a Xing/Info
/// tag is estimated from the bit rate of the first frame (exact for constant bit rate) unless the
/// entire file is given, in which case the frames are counted.
/// Returns false if the format is not recognized or the header is incomplete.
bool probe(const uint8_t *data, size_t size, size_t fileSize, StreamInfo *info);

/// Decodes the entire file image into out. Returns false (and clears out) on failure.
bool decodeAll(const uint8_t *data, size_t size, std::vector<int16_t> *out, StreamInfo *info);

} // namespace WebAudio
