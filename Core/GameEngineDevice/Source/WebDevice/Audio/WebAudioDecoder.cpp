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

// FILE: WebAudioDecoder.cpp //////////////////////////////////////////////////
//
// See WebAudioDecoder.h.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Audio/WebAudioDecoder.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <string.h>
#include <algorithm>

namespace WebAudio
{

namespace
{

//-------------------------------------------------------------------------------------------------
// Little endian readers
//-------------------------------------------------------------------------------------------------

inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

inline int16_t clamp16(int v)
{
	return (int16_t)(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

enum
{
	WAVE_FORMAT_PCM_TAG = 0x0001,
	WAVE_FORMAT_IMA_ADPCM_TAG = 0x0011,
	WAVE_FORMAT_IEEE_FLOAT_TAG = 0x0003,
	WAVE_FORMAT_EXTENSIBLE_TAG = 0xFFFE,
};

//-------------------------------------------------------------------------------------------------
// RIFF/WAVE
//-------------------------------------------------------------------------------------------------

struct WavLayout
{
	uint16_t tag = 0;
	uint16_t channels = 0;
	uint32_t sampleRate = 0;
	uint16_t blockAlign = 0;
	uint16_t bits = 0;
	uint16_t samplesPerBlock = 0;	// ADPCM
	uint32_t factFrames = 0;			// 0 if there is no fact chunk
	size_t dataOffset = 0;
	size_t dataSize = 0;					// clamped to the file
};

/// Walks the chunks of a RIFF/WAVE image. data/size is what is available, fileSize the length of
/// the entire file.
bool parseWav(const uint8_t *data, size_t size, size_t fileSize, WavLayout *out, StreamInfo *info)
{
	if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
		return false;

	WavLayout w;
	bool haveFmt = false;
	size_t pos = 12;
	while (pos + 8 <= size)
	{
		const uint8_t *ck = data + pos;
		uint32_t ckSize = rd32(ck + 4);
		size_t body = pos + 8;

		if (memcmp(ck, "fmt ", 4) == 0)
		{
			if (ckSize < 16 || body + 16 > size)
				return false;
			const uint8_t *f = data + body;
			w.tag = rd16(f);
			w.channels = rd16(f + 2);
			w.sampleRate = rd32(f + 4);
			w.blockAlign = rd16(f + 12);
			w.bits = rd16(f + 14);
			if (w.tag == WAVE_FORMAT_EXTENSIBLE_TAG && ckSize >= 40 && body + 40 <= size)
				w.tag = rd16(f + 24);	// the first two bytes of the sub format GUID are the format tag
			if (w.tag == WAVE_FORMAT_IMA_ADPCM_TAG && ckSize >= 20 && body + 20 <= size)
				w.samplesPerBlock = rd16(f + 18);
			haveFmt = true;
		}
		else if (memcmp(ck, "fact", 4) == 0)
		{
			if (ckSize >= 4 && body + 4 <= size)
				w.factFrames = rd32(data + body);
		}
		else if (memcmp(ck, "data", 4) == 0)
		{
			if (!haveFmt)
				return false;
			w.dataOffset = body;
			size_t avail = fileSize > body ? fileSize - body : 0;
			// Streamed files carry 0 or 0xFFFFFFFF here, damaged files claim more than there is.
			w.dataSize = (ckSize == 0 || ckSize > avail) ? avail : ckSize;
			break;
		}

		size_t next = body + (size_t)ckSize + (ckSize & 1u);
		if (next <= pos)	// overflow
			return false;
		pos = next;
	}
	if (w.dataOffset == 0 || w.channels == 0 || w.channels > 8 || w.sampleRate == 0)
		return false;

	StreamInfo si;
	si.sampleRate = w.sampleRate;
	si.channels = w.channels;

	if (w.tag == WAVE_FORMAT_PCM_TAG || w.tag == WAVE_FORMAT_IEEE_FLOAT_TAG)
	{
		const bool isFloat = (w.tag == WAVE_FORMAT_IEEE_FLOAT_TAG);
		if (isFloat ? (w.bits != 32 && w.bits != 64) : (w.bits != 8 && w.bits != 16 && w.bits != 24 && w.bits != 32))
			return false;
		w.blockAlign = (uint16_t)(w.channels * (w.bits / 8));
		si.codec = Codec::Pcm;
		si.totalFrames = w.dataSize / w.blockAlign;
	}
	else if (w.tag == WAVE_FORMAT_IMA_ADPCM_TAG)
	{
		if (w.bits != 4 || w.blockAlign < 4u * w.channels)
			return false;
		const uint32_t computed = ((uint32_t)w.blockAlign - 4u * w.channels) * 2u / w.channels + 1u;
		if (w.samplesPerBlock == 0 || w.samplesPerBlock > computed)
			w.samplesPerBlock = (uint16_t)computed;
		si.codec = Codec::ImaAdpcm;
		const size_t fullBlocks = w.dataSize / w.blockAlign;
		const size_t rest = w.dataSize % w.blockAlign;
		uint64_t frames = (uint64_t)fullBlocks * w.samplesPerBlock;
		if (rest >= 4u * w.channels)
			frames += 1u + ((rest - 4u * w.channels) / (4u * w.channels)) * 8u;
		if (w.factFrames != 0 && w.factFrames <= frames)
			frames = w.factFrames;
		si.totalFrames = frames;
	}
	else
	{
		return false;
	}

	if (out)
		*out = w;
	if (info)
		*info = si;
	return true;
}

//-------------------------------------------------------------------------------------------------
class PcmDecoder : public Decoder
{
public:
	PcmDecoder(const uint8_t *data, size_t size, const WavLayout &w, const StreamInfo &si)
		: m_data(data + w.dataOffset), m_layout(w), m_pos(0)
	{
		m_info = si;
		(void)size;
	}

	virtual size_t read(int16_t *out, size_t maxFrames) override
	{
		size_t n = (size_t)std::min<uint64_t>(maxFrames, m_info.totalFrames - m_pos);
		const uint32_t ch = m_layout.channels;
		const uint8_t *src = m_data + m_pos * m_layout.blockAlign;
		const size_t samples = n * ch;
		switch (m_layout.tag == WAVE_FORMAT_IEEE_FLOAT_TAG ? 100 + m_layout.bits : m_layout.bits)
		{
			case 8:
				for (size_t i = 0; i < samples; ++i)
					out[i] = (int16_t)(((int)src[i] - 128) << 8);
				break;
			case 16:
				for (size_t i = 0; i < samples; ++i)
					out[i] = (int16_t)rd16(src + i * 2);
				break;
			case 24:
				for (size_t i = 0; i < samples; ++i)
					out[i] = (int16_t)rd16(src + i * 3 + 1);
				break;
			case 32:
				for (size_t i = 0; i < samples; ++i)
					out[i] = (int16_t)rd16(src + i * 4 + 2);
				break;
			case 132:
				for (size_t i = 0; i < samples; ++i)
				{
					uint32_t u = rd32(src + i * 4);
					float f;
					memcpy(&f, &u, 4);
					out[i] = clamp16((int)(f * 32768.0f + (f < 0 ? -0.5f : 0.5f)));
				}
				break;
			case 164:
				for (size_t i = 0; i < samples; ++i)
				{
					uint64_t u = (uint64_t)rd32(src + i * 8) | ((uint64_t)rd32(src + i * 8 + 4) << 32);
					double f;
					memcpy(&f, &u, 8);
					out[i] = clamp16((int)(f * 32768.0 + (f < 0 ? -0.5 : 0.5)));
				}
				break;
		}
		m_pos += n;
		return n;
	}

	virtual bool rewind() override { m_pos = 0; return true; }

private:
	const uint8_t *m_data;
	WavLayout m_layout;
	uint64_t m_pos;
};

//-------------------------------------------------------------------------------------------------
// IMA ADPCM as found in WAV files (Microsoft flavour, WAVE_FORMAT_IMA_ADPCM)
//-------------------------------------------------------------------------------------------------

const int16_t ImaStepTable[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
	1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
	24623, 27086, 29794, 32767
};

const int8_t ImaIndexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

struct ImaChannel
{
	int predictor;
	int index;

	int16_t decode(unsigned nibble)
	{
		const int step = ImaStepTable[index];
		int diff = step >> 3;
		if (nibble & 1) diff += step >> 2;
		if (nibble & 2) diff += step >> 1;
		if (nibble & 4) diff += step;
		if (nibble & 8) diff = -diff;
		predictor = clamp16(predictor + diff);
		index = std::max(0, std::min(88, index + ImaIndexTable[nibble]));
		return (int16_t)predictor;
	}
};

class ImaAdpcmDecoder : public Decoder
{
public:
	ImaAdpcmDecoder(const uint8_t *data, size_t size, const WavLayout &w, const StreamInfo &si)
		: m_data(data + w.dataOffset), m_dataSize(w.dataSize), m_layout(w), m_block(0), m_blockFrames(0), m_blockPos(0), m_framesOut(0)
	{
		m_info = si;
		m_pcm.resize((size_t)w.samplesPerBlock * w.channels);
		(void)size;
	}

	virtual size_t read(int16_t *out, size_t maxFrames) override
	{
		const uint32_t ch = m_layout.channels;
		size_t done = 0;
		while (done < maxFrames && m_framesOut < m_info.totalFrames)
		{
			if (m_blockPos >= m_blockFrames && !decodeBlock())
				break;
			size_t n = std::min(maxFrames - done, m_blockFrames - m_blockPos);
			n = (size_t)std::min<uint64_t>(n, m_info.totalFrames - m_framesOut);
			memcpy(out + done * ch, m_pcm.data() + m_blockPos * ch, n * ch * sizeof(int16_t));
			m_blockPos += n;
			m_framesOut += n;
			done += n;
		}
		return done;
	}

	virtual bool rewind() override
	{
		m_block = 0;
		m_blockFrames = 0;
		m_blockPos = 0;
		m_framesOut = 0;
		return true;
	}

private:
	bool decodeBlock()
	{
		const uint32_t ch = m_layout.channels;
		const size_t start = m_block * m_layout.blockAlign;
		if (start + 4u * ch > m_dataSize)
			return false;
		const size_t avail = std::min<size_t>(m_layout.blockAlign, m_dataSize - start);
		const uint8_t *b = m_data + start;

		ImaChannel st[8];
		for (uint32_t c = 0; c < ch; ++c)
		{
			st[c].predictor = (int16_t)rd16(b + c * 4);
			st[c].index = std::min<int>(b[c * 4 + 2], 88);
			m_pcm[c] = (int16_t)st[c].predictor;
		}

		// After the headers come groups of 4 bytes (8 samples) per channel, channels interleaved.
		size_t frames = 1;
		const size_t groups = (avail - 4u * ch) / (4u * ch);
		const uint8_t *g = b + 4u * ch;
		for (size_t grp = 0; grp < groups && frames + 8 <= (size_t)m_layout.samplesPerBlock + 7; ++grp)
		{
			for (uint32_t c = 0; c < ch; ++c)
			{
				const uint8_t *src = g + (grp * ch + c) * 4;
				for (int i = 0; i < 4; ++i)
				{
					const size_t f = frames + (size_t)i * 2;
					if (f < m_pcm.size() / ch)
						m_pcm[f * ch + c] = st[c].decode(src[i] & 0x0F);
					if (f + 1 < m_pcm.size() / ch)
						m_pcm[(f + 1) * ch + c] = st[c].decode(src[i] >> 4);
				}
			}
			frames += 8;
		}
		m_blockFrames = std::min<size_t>(frames, m_layout.samplesPerBlock);
		m_blockPos = 0;
		++m_block;
		return true;
	}

	const uint8_t *m_data;
	size_t m_dataSize;
	WavLayout m_layout;
	std::vector<int16_t> m_pcm;
	size_t m_block;
	size_t m_blockFrames;
	size_t m_blockPos;
	uint64_t m_framesOut;
};

//-------------------------------------------------------------------------------------------------
// MP3
//-------------------------------------------------------------------------------------------------

struct Mp3Layout
{
	size_t firstFrame = 0;		// offset of the first frame (after an ID3v2 tag)
	size_t audioStart = 0;		// offset of the first audio frame (after a Xing/Info/VBRI frame)
	size_t audioEnd = 0;			// end of the audio data (before an ID3v1 tag)
	uint32_t sampleRate = 0;
	uint32_t channels = 0;
	uint32_t samplesPerFrame = 0;
	uint32_t firstFrameBytes = 0;
	uint32_t bitrateKbps = 0;
	bool hasTag = false;			// the first frame is a Xing/Info/VBRI frame
	uint32_t tagFrames = 0;		// frame count of the tag, 0 if absent
	bool hasGapless = false;
	uint32_t delay = 0;
	uint32_t padding = 0;
};

/// Size of an ID3v2 tag at the start of the data, 0 if there is none.
size_t id3v2Size(const uint8_t *data, size_t size)
{
	if (size >= 10 && memcmp(data, "ID3", 3) == 0 && data[3] != 0xFF && data[4] != 0xFF
		&& !((data[6] | data[7] | data[8] | data[9]) & 0x80))
	{
		size_t tag = ((size_t)data[6] << 21) | ((size_t)data[7] << 14) | ((size_t)data[8] << 7) | (size_t)data[9];
		tag += 10;
		if (data[5] & 0x10)	// footer present
			tag += 10;
		return tag;
	}
	return 0;
}

/// Parses a Xing/Info/VBRI tag in the frame at p (frame_bytes long).
void parseMp3Tag(const uint8_t *p, size_t frameBytes, Mp3Layout *m)
{
	const bool mpeg1 = ((p[1] >> 3) & 3) == 3;
	const bool mono = ((p[3] >> 6) & 3) == 3;
	const bool crc = !(p[1] & 1);
	const size_t sideInfo = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
	const size_t xingAt = 4 + (crc ? 2 : 0) + sideInfo;

	if (frameBytes >= xingAt + 8 && (memcmp(p + xingAt, "Xing", 4) == 0 || memcmp(p + xingAt, "Info", 4) == 0))
	{
		m->hasTag = true;
		const uint32_t flags = rd32be(p + xingAt + 4);
		size_t q = xingAt + 8;
		if ((flags & 1) && q + 4 <= frameBytes)
		{
			m->tagFrames = rd32be(p + q);
			q += 4;
		}
		if (flags & 2) q += 4;
		if (flags & 4) q += 100;
		if (flags & 8) q += 4;
		// The LAME extension: 9 bytes encoder version, then at +21 the 12 bit encoder delay and padding.
		if (q + 24 <= frameBytes && m->tagFrames != 0 &&
			(memcmp(p + q, "LAME", 4) == 0 || memcmp(p + q, "Lavf", 4) == 0 || memcmp(p + q, "Lavc", 4) == 0 ||
			 memcmp(p + q, "GOGO", 4) == 0 || memcmp(p + q, "L3.9", 4) == 0))
		{
			const uint32_t v = ((uint32_t)p[q + 21] << 16) | ((uint32_t)p[q + 22] << 8) | (uint32_t)p[q + 23];
			m->delay = v >> 12;
			m->padding = v & 0xFFF;
			m->hasGapless = true;
		}
		return;
	}

	if (frameBytes >= 4 + 32 + 18 && memcmp(p + 4 + 32, "VBRI", 4) == 0)
	{
		m->hasTag = true;
		m->tagFrames = rd32be(p + 4 + 32 + 14);
	}
}

/// Locates the first frame, reads the stream parameters and the optional tag.
bool parseMp3(const uint8_t *data, size_t size, Mp3Layout *m)
{
	size_t skip = id3v2Size(data, size);
	if (skip >= size)
		return false;

	// ID3v1 at the end
	size_t end = size;
	if (end >= skip + 128 && memcmp(data + end - 128, "TAG", 3) == 0)
		end -= 128;

	int freeFormat = 0, frameBytes = 0;
	int off = mp3d_find_frame(data + skip, (int)std::min<size_t>(end - skip, 0x7FFFFFFF), &freeFormat, &frameBytes);
	if (frameBytes <= 0 || skip + (size_t)off + 4 > end)
		return false;

	const uint8_t *h = data + skip + off;
	m->firstFrame = skip + (size_t)off;
	m->audioEnd = end;
	m->sampleRate = hdr_sample_rate_hz(h);
	m->channels = HDR_IS_MONO(h) ? 1 : 2;
	m->samplesPerFrame = hdr_frame_samples(h);
	m->firstFrameBytes = (uint32_t)frameBytes;
	m->bitrateKbps = hdr_bitrate_kbps(h);
	m->audioStart = m->firstFrame;
	if (m->sampleRate == 0 || m->samplesPerFrame == 0)
		return false;

	if (HDR_GET_LAYER(h) == 1)	// layer III
	{
		parseMp3Tag(h, (size_t)frameBytes, m);
		if (m->hasTag)
			m->audioStart = m->firstFrame + (size_t)frameBytes;
	}
	return true;
}

/// Counts the frames of the entire stream by walking the frame headers.
uint64_t countMp3Frames(const uint8_t *data, const Mp3Layout &m)
{
	uint64_t frames = 0;
	size_t pos = m.audioStart;
	int freeFormat = 0;
	while (pos + 4 <= m.audioEnd)
	{
		const uint8_t *h = data + pos;
		if (!hdr_valid(h))
		{
			// resync
			int fb = 0;
			int off = mp3d_find_frame(h, (int)std::min<size_t>(m.audioEnd - pos, 0x7FFFFFFF), &freeFormat, &fb);
			if (fb <= 0)
				break;
			pos += (size_t)off;
			continue;
		}
		const int fb = hdr_frame_bytes(h, freeFormat) + hdr_padding(h);
		if (fb <= 0 || pos + (size_t)fb > m.audioEnd)
			break;
		++frames;
		pos += (size_t)fb;
	}
	return frames;
}

class Mp3Decoder : public Decoder
{
public:
	Mp3Decoder(const uint8_t *data, size_t /*size*/, const Mp3Layout &m)
		: m_data(data), m_layout(m), m_pos(0), m_pcmFrames(0), m_pcmPos(0), m_skip(0), m_emitted(0), m_limit(0), m_limited(false)
	{
		m_info.codec = Codec::Mp3;
		m_info.sampleRate = m.sampleRate;
		m_info.channels = m.channels;

		uint64_t frames;
		if (m.hasTag && m.tagFrames != 0)
			frames = m.tagFrames;
		else
			frames = countMp3Frames(data, m);
		uint64_t total = frames * m.samplesPerFrame;
		if (m.hasGapless)
		{
			// The same arithmetic as ffmpeg: skip the encoder delay and the decoder delay (529 samples),
			// and drop the padding of the last frame.
			m_skip = (uint64_t)m.delay + 529;
			if (m_skip + m.padding < total)
				total -= (uint64_t)m.delay + m.padding;
			else
				m_skip = 0;
			m_limited = true;
			m_limit = total;
		}
		m_info.totalFrames = total;
		restart();
	}

	virtual size_t read(int16_t *out, size_t maxFrames) override
	{
		const uint32_t ch = m_layout.channels;
		size_t done = 0;
		while (done < maxFrames)
		{
			if (m_limited && m_emitted >= m_limit)
				break;
			if (m_pcmPos >= m_pcmFrames && !decodeFrame())
				break;
			size_t n = std::min(maxFrames - done, m_pcmFrames - m_pcmPos);
			if (m_limited)
				n = (size_t)std::min<uint64_t>(n, m_limit - m_emitted);
			memcpy(out + done * ch, m_pcm + m_pcmPos * ch, n * ch * sizeof(int16_t));
			m_pcmPos += n;
			m_emitted += n;
			done += n;
		}
		return done;
	}

	virtual bool rewind() override
	{
		restart();
		return true;
	}

private:
	void restart()
	{
		mp3dec_init(&m_dec);
		m_pos = m_layout.audioStart;
		m_pcmFrames = 0;
		m_pcmPos = 0;
		m_emitted = 0;
		m_toSkip = m_skip;
	}

	/// Decodes the next frame into m_pcm, applying the start trim.
	bool decodeFrame()
	{
		for (;;)
		{
			if (m_pos >= m_layout.audioEnd)
				return false;
			mp3dec_frame_info_t fi;
			const int samples = mp3dec_decode_frame(&m_dec, m_data + m_pos, (int)std::min<size_t>(m_layout.audioEnd - m_pos, 0x7FFFFFFF), m_pcm, &fi);
			if (fi.frame_bytes <= 0)
				return false;
			m_pos += (size_t)fi.frame_bytes;
			if (samples <= 0 || (uint32_t)fi.channels != m_layout.channels)
				continue;	// skipped garbage, a frame without bit reservoir, or a stream change we do not follow
			size_t skip = (size_t)std::min<uint64_t>(m_toSkip, (uint64_t)samples);
			m_toSkip -= skip;
			if ((int)skip >= samples)
				continue;
			m_pcmFrames = (size_t)samples;
			m_pcmPos = skip;
			return true;
		}
	}

	const uint8_t *m_data;
	Mp3Layout m_layout;
	mp3dec_t m_dec;
	size_t m_pos;
	int16_t m_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	size_t m_pcmFrames;
	size_t m_pcmPos;
	uint64_t m_skip;
	uint64_t m_toSkip = 0;
	uint64_t m_emitted;
	uint64_t m_limit;
	bool m_limited;
};

} // namespace

//-------------------------------------------------------------------------------------------------
std::unique_ptr<Decoder> Decoder::create(const uint8_t *data, size_t size)
{
	if (!data || size < 12)
		return nullptr;

	if (memcmp(data, "RIFF", 4) == 0)
	{
		WavLayout w;
		StreamInfo si;
		if (!parseWav(data, size, size, &w, &si))
			return nullptr;
		if (si.codec == Codec::ImaAdpcm)
			return std::unique_ptr<Decoder>(new ImaAdpcmDecoder(data, size, w, si));
		return std::unique_ptr<Decoder>(new PcmDecoder(data, size, w, si));
	}

	Mp3Layout m;
	if (parseMp3(data, size, &m))
		return std::unique_ptr<Decoder>(new Mp3Decoder(data, size, m));
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
bool probe(const uint8_t *data, size_t size, size_t fileSize, StreamInfo *info)
{
	if (!data || size < 12)
		return false;
	if (fileSize < size)
		fileSize = size;

	StreamInfo si;
	if (memcmp(data, "RIFF", 4) == 0)
	{
		if (!parseWav(data, size, fileSize, nullptr, &si))
			return false;
		if (info)
			*info = si;
		return true;
	}

	Mp3Layout m;
	if (!parseMp3(data, size, &m))
		return false;
	si.codec = Codec::Mp3;
	si.sampleRate = m.sampleRate;
	si.channels = m.channels;

	uint64_t frames = 0;
	if (m.hasTag && m.tagFrames != 0)
	{
		frames = m.tagFrames;
	}
	else if (size == fileSize)
	{
		frames = countMp3Frames(data, m);
	}
	else if (m.bitrateKbps != 0)
	{
		// Constant bit rate estimate from the size of the audio data.
		si.lengthExact = false;
		const uint64_t audioBytes = fileSize > m.audioStart ? fileSize - m.audioStart : 0;
		frames = (audioBytes * 8ull * m.sampleRate) / ((uint64_t)m.bitrateKbps * 1000ull * m.samplesPerFrame);
	}
	uint64_t total = frames * m.samplesPerFrame;
	if (m.hasGapless && total > (uint64_t)m.delay + m.padding)
		total -= (uint64_t)m.delay + m.padding;
	si.totalFrames = total;
	if (info)
		*info = si;
	return true;
}

//-------------------------------------------------------------------------------------------------
bool decodeAll(const uint8_t *data, size_t size, std::vector<int16_t> *out, StreamInfo *info)
{
	out->clear();
	std::unique_ptr<Decoder> dec = Decoder::create(data, size);
	if (!dec)
		return false;

	const StreamInfo &si = dec->info();
	const size_t ch = si.channels;
	if (si.totalFrames > 0 && si.totalFrames < (1u << 28))
		out->reserve((size_t)si.totalFrames * ch);

	const size_t chunk = 4096;
	size_t have = 0;
	for (;;)
	{
		out->resize((have + chunk) * ch);
		size_t n = dec->read(out->data() + have * ch, chunk);
		have += n;
		if (n == 0)
			break;
	}
	out->resize(have * ch);
	if (info)
	{
		*info = si;
		info->totalFrames = have;
	}
	if (have == 0)
	{
		out->clear();
		return false;
	}
	return true;
}

} // namespace WebAudio
