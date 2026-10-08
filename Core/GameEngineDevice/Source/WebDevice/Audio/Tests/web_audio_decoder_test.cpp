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

// FILE: web_audio_decoder_test.cpp ///////////////////////////////////////////
//
// Native unit test of the audio decoders (no browser involved), meant to be built with the
// address and undefined behaviour sanitizers:
//
//   g++ -std=c++20 -g -fsanitize=address,undefined -I<repo>/Core/GameEngineDevice/Include
//       -I<repo>/Dependencies/minimp3 web_audio_decoder_test.cpp ../WebAudioDecoder.cpp -o decoder_test
//   ./decoder_test data [reference-dir]
//
// reference-dir optionally holds <name>.s16 files (raw interleaved 16 bit little endian) decoded
// with ffmpeg from the files of data/ for a sample accurate comparison; see run_decoder_test.sh.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/Audio/WebAudioDecoder.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace WebAudio;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...) \
	do { \
		++g_checks; \
		if (!(cond)) { ++g_failures; printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } \
	} while (0)

static std::vector<uint8_t> readFile(const std::string &path)
{
	std::vector<uint8_t> v;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return v;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	v.resize((size_t)n);
	if (fread(v.data(), 1, v.size(), f) != v.size())
		v.clear();
	fclose(f);
	return v;
}

/// Amplitude of the tone at freq in channel ch (the sine amplitude, 0..1).
static double toneAmplitude(const std::vector<int16_t> &pcm, uint32_t channels, uint32_t ch, double freq, double rate)
{
	const size_t n = pcm.size() / channels;
	double re = 0, im = 0;
	for (size_t i = 0; i < n; ++i)
	{
		const double s = pcm[i * channels + ch] / 32768.0;
		const double a = 2 * M_PI * freq * (double)i / rate;
		re += s * cos(a);
		im += s * sin(a);
	}
	return 2.0 * sqrt(re * re + im * im) / (double)n;
}

static double rms(const std::vector<int16_t> &pcm, uint32_t channels, uint32_t ch)
{
	const size_t n = pcm.size() / channels;
	double t = 0;
	for (size_t i = 0; i < n; ++i)
	{
		const double s = pcm[i * channels + ch] / 32768.0;
		t += s * s;
	}
	return sqrt(t / (double)n);
}

// Builds a minimal WAV file image.
static std::vector<uint8_t> makeWav(uint16_t tag, uint16_t channels, uint32_t rate, uint16_t bits, const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> v;
	auto p32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); };
	auto p16 = [&](uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); };
	v.insert(v.end(), { 'R', 'I', 'F', 'F' });
	p32(0);
	v.insert(v.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
	p32(16);
	p16(tag);
	p16(channels);
	p32(rate);
	p32(rate * channels * (bits / 8));
	p16((uint16_t)(channels * (bits / 8)));
	p16(bits);
	v.insert(v.end(), { 'd', 'a', 't', 'a' });
	p32((uint32_t)data.size());
	v.insert(v.end(), data.begin(), data.end());
	uint32_t riff = (uint32_t)v.size() - 8;
	memcpy(v.data() + 4, &riff, 4);
	return v;
}

static void testSynthWavs()
{
	const double rate = 16000, freq = 500;
	const int n = 8000;

	// 8 bit unsigned
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < n; ++i)
			d.push_back((uint8_t)(128 + 100 * sin(2 * M_PI * freq * i / rate)));
		std::vector<uint8_t> w = makeWav(1, 1, 16000, 8, d);
		std::vector<int16_t> pcm;
		StreamInfo si;
		CHECK(decodeAll(w.data(), w.size(), &pcm, &si), "pcm8 decode");
		CHECK(si.codec == Codec::Pcm && si.channels == 1 && si.sampleRate == 16000 && si.totalFrames == (uint64_t)n, "pcm8 info");
		CHECK(fabs(toneAmplitude(pcm, 1, 0, freq, rate) - 100.0 / 128.0) < 0.02, "pcm8 amplitude %f", toneAmplitude(pcm, 1, 0, freq, rate));
	}
	// 24 bit and 32 bit and float, stereo
	for (int variant = 0; variant < 3; ++variant)
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < n; ++i)
		{
			for (int c = 0; c < 2; ++c)
			{
				double s = 0.5 * sin(2 * M_PI * (freq + c * 100) * i / rate);
				if (variant == 0)
				{
					int32_t v = (int32_t)(s * 8388607.0);
					d.push_back((uint8_t)v); d.push_back((uint8_t)(v >> 8)); d.push_back((uint8_t)(v >> 16));
				}
				else if (variant == 1)
				{
					int32_t v = (int32_t)(s * 2147483647.0);
					for (int b = 0; b < 4; ++b) d.push_back((uint8_t)(v >> (8 * b)));
				}
				else
				{
					float f = (float)s;
					uint32_t u;
					memcpy(&u, &f, 4);
					for (int b = 0; b < 4; ++b) d.push_back((uint8_t)(u >> (8 * b)));
				}
			}
		}
		std::vector<uint8_t> w = makeWav(variant == 2 ? 3 : 1, 2, 16000, variant == 0 ? 24 : 32, d);
		std::vector<int16_t> pcm;
		StreamInfo si;
		CHECK(decodeAll(w.data(), w.size(), &pcm, &si), "pcm variant %d decode", variant);
		CHECK(si.channels == 2 && si.totalFrames == (uint64_t)n, "pcm variant %d info", variant);
		CHECK(fabs(toneAmplitude(pcm, 2, 0, freq, rate) - 0.5) < 0.01, "variant %d left %f", variant, toneAmplitude(pcm, 2, 0, freq, rate));
		CHECK(fabs(toneAmplitude(pcm, 2, 1, freq + 100, rate) - 0.5) < 0.01, "variant %d right", variant);
	}
	// A streamed WAV whose data chunk claims 0 / too many bytes is clamped to the file.
	{
		std::vector<uint8_t> d(1000, 128);
		std::vector<uint8_t> w = makeWav(1, 1, 8000, 8, d);
		uint32_t big = 0xFFFFFFFFu;
		memcpy(w.data() + 40, &big, 4);
		StreamInfo si;
		CHECK(probe(w.data(), w.size(), w.size(), &si) && si.totalFrames == 1000, "clamped data chunk %llu", (unsigned long long)si.totalFrames);
	}
}

static void testFile(const std::string &dir, const std::string &refDir, const char *name, Codec codec, uint32_t channels, uint32_t rate,
	double seconds, double secondsTolerance, double freqL, double freqR)
{
	std::vector<uint8_t> file = readFile(dir + "/" + name);
	CHECK(!file.empty(), "%s: cannot read", name);
	if (file.empty())
		return;

	std::vector<int16_t> pcm;
	StreamInfo si;
	CHECK(decodeAll(file.data(), file.size(), &pcm, &si), "%s: decode", name);
	CHECK(si.codec == codec, "%s: codec", name);
	CHECK(si.channels == channels, "%s: channels %u", name, si.channels);
	CHECK(si.sampleRate == rate, "%s: rate %u", name, si.sampleRate);
	const double len = (double)si.totalFrames / rate;
	CHECK(fabs(len - seconds) <= secondsTolerance, "%s: length %f (want %f)", name, len, seconds);

	const double ampL = toneAmplitude(pcm, channels, 0, freqL, rate);
	CHECK(ampL > 0.40 && ampL < 0.55, "%s: left tone %.0f Hz amplitude %f", name, freqL, ampL);
	if (channels == 2)
	{
		const double ampR = toneAmplitude(pcm, 2, 1, freqR, rate);
		CHECK(ampR > 0.40 && ampR < 0.55, "%s: right tone %.0f Hz amplitude %f", name, freqR, ampR);
		CHECK(toneAmplitude(pcm, 2, 0, freqR, rate) < 0.02, "%s: left channel leaks the right tone", name);
	}
	// The tone must be the main content: noise from a wrong decode would raise the error floor.
	const double a = ampL / sqrt(2.0);
	CHECK(fabs(rms(pcm, channels, 0) - a) < 0.03, "%s: rms %f vs tone %f", name, rms(pcm, channels, 0), a);

	// probe() agrees with the decoder (also from a short prefix, like getFileLengthMS does)
	StreamInfo p1, p2;
	CHECK(probe(file.data(), file.size(), file.size(), &p1), "%s: probe", name);
	CHECK(p1.totalFrames == si.totalFrames, "%s: probe frames %llu vs %llu", name, (unsigned long long)p1.totalFrames, (unsigned long long)si.totalFrames);
	const size_t prefix = std::min<size_t>(file.size(), 2048);
	CHECK(probe(file.data(), prefix, file.size(), &p2), "%s: probe prefix", name);
	CHECK(llabs((long long)p2.totalFrames - (long long)si.totalFrames) <= (long long)rate / 10, "%s: probe prefix frames %llu vs %llu", name,
		(unsigned long long)p2.totalFrames, (unsigned long long)si.totalFrames);

	// rewind produces the same samples
	{
		std::unique_ptr<Decoder> dec = Decoder::create(file.data(), file.size());
		CHECK(dec != nullptr, "%s: create", name);
		if (dec)
		{
			std::vector<int16_t> a1(channels * 3000), a2(channels * 3000), tmp(channels * 5000);
			size_t n1 = dec->read(a1.data(), 3000);
			dec->read(tmp.data(), 5000);
			CHECK(dec->rewind(), "%s: rewind", name);
			size_t n2 = dec->read(a2.data(), 3000);
			CHECK(n1 == 3000 && n2 == 3000 && a1 == a2, "%s: rewind repeats (%zu, %zu)", name, n1, n2);
			// reading in odd sized pieces gives the same samples as decodeAll
			dec->rewind();
			std::vector<int16_t> pieces;
			std::vector<int16_t> buf(channels * 777);
			size_t sizes[] = { 1, 7, 777, 333, 64 };
			for (size_t k = 0;; ++k)
			{
				size_t want = sizes[k % 5];
				size_t n = dec->read(buf.data(), want);
				pieces.insert(pieces.end(), buf.begin(), buf.begin() + n * channels);
				if (n == 0)
					break;
			}
			CHECK(pieces == pcm, "%s: piecewise read differs (%zu vs %zu samples)", name, pieces.size(), pcm.size());
		}
	}

	// reference decode from ffmpeg
	if (!refDir.empty())
	{
		std::vector<uint8_t> ref = readFile(refDir + "/" + name + ".s16");
		if (!ref.empty())
		{
			const int16_t *r = (const int16_t *)ref.data();
			const size_t rn = ref.size() / 2;
			// Find the best alignment offset (ffmpeg may or may not trim the MP3 delay the same way).
			size_t n = std::min(rn, pcm.size());
			double err = 0, sig = 0;
			int maxd = 0;
			for (size_t i = 0; i < n; ++i)
			{
				int d = abs((int)r[i] - (int)pcm[i]);
				maxd = std::max(maxd, d);
				err += (double)d * d;
				sig += (double)r[i] * r[i];
			}
			const double snr = sig > 0 ? 10.0 * log10(sig / (err + 1e-9)) : 0;
			printf("  %s: vs ffmpeg: %zu/%zu samples, max diff %d, SNR %.1f dB\n", name, pcm.size(), rn, maxd, snr);
			if (codec == Codec::Mp3)
			{
				CHECK(llabs((long long)rn - (long long)pcm.size()) <= (long long)channels * 2, "%s: length differs from ffmpeg (%zu vs %zu)", name, rn, pcm.size());
				CHECK(snr > 40.0, "%s: SNR against ffmpeg %f", name, snr);
			}
			else if (codec == Codec::ImaAdpcm)
			{
				// ffmpeg expands the nibbles as ((2 * delta + 1) * step) >> 3, the reference algorithm (which the
				// encoder of gen_test_audio.py and Miles use) adds the shifted steps up; the results differ in
				// the last bits of the steps.
				// (ffmpeg ignores the fact chunk and returns the padding of the last block, too)
				CHECK(rn >= pcm.size() && rn - pcm.size() <= 1017 * channels && snr > 50.0, "%s: ADPCM differs from ffmpeg (SNR %f)", name, snr);
			}
			else
			{
				CHECK(rn == pcm.size() && maxd == 0, "%s: not identical to ffmpeg (max diff %d)", name, maxd);
			}
		}
	}
}

static void testRobustness(const std::string &dir)
{
	const char *files[] = { "pcm16_mono_44100_440.wav", "adpcm_stereo_44100_660.wav", "music_stereo_44100_1320.mp3" };
	srand(1234);
	for (const char *name : files)
	{
		std::vector<uint8_t> orig = readFile(dir + "/" + name);
		if (orig.empty())
			continue;
		// truncations
		for (size_t cut : { (size_t)0, (size_t)3, (size_t)11, (size_t)12, (size_t)40, (size_t)45, (size_t)100, orig.size() / 2, orig.size() - 1 })
		{
			std::vector<uint8_t> v(orig.begin(), orig.begin() + std::min(cut, orig.size()));
			std::vector<int16_t> pcm;
			StreamInfo si;
			decodeAll(v.data(), v.size(), &pcm, &si);
			StreamInfo p;
			probe(v.data(), v.size(), orig.size(), &p);
		}
		// random byte corruption
		for (int iter = 0; iter < 60; ++iter)
		{
			std::vector<uint8_t> v = orig;
			int flips = 1 + rand() % 20;
			for (int k = 0; k < flips; ++k)
				v[(size_t)rand() % std::min<size_t>(v.size(), iter % 2 ? 80 : v.size())] = (uint8_t)rand();
			std::vector<int16_t> pcm;
			StreamInfo si;
			decodeAll(v.data(), v.size(), &pcm, &si);
		}
	}
	// pure garbage
	std::vector<uint8_t> junk(5000);
	for (auto &b : junk)
		b = (uint8_t)rand();
	std::vector<int16_t> pcm;
	StreamInfo si;
	CHECK(!decodeAll(junk.data(), junk.size(), &pcm, &si), "garbage must not decode");
	CHECK(Decoder::create(junk.data(), junk.size()) == nullptr, "garbage must not open");
	++g_checks;
}

int main(int argc, char **argv)
{
	std::string dir = argc > 1 ? argv[1] : "data";
	std::string ref = argc > 2 ? argv[2] : "";

	testSynthWavs();
	testFile(dir, ref, "pcm16_mono_44100_440.wav", Codec::Pcm, 1, 44100, 1.0, 0.0001, 440, 0);
	testFile(dir, ref, "pcm8_mono_22050_550.wav", Codec::Pcm, 1, 22050, 0.5, 0.0001, 550, 0);
	testFile(dir, ref, "adpcm_mono_22050_880.wav", Codec::ImaAdpcm, 1, 22050, 1.5, 0.0001, 880, 0);
	testFile(dir, ref, "adpcm_stereo_44100_660.wav", Codec::ImaAdpcm, 2, 44100, 1.0, 0.0001, 660, 990);
	// MP3 length: the LAME tag gives the exact (gapless) length of the source.
	testFile(dir, ref, "music_stereo_44100_1320.mp3", Codec::Mp3, 2, 44100, 4.0, 0.001, 1320, 1760);
	testFile(dir, ref, "music_mono_22050_1100.mp3", Codec::Mp3, 1, 22050, 2.0, 0.001, 1100, 0);
	testRobustness(dir);

	printf("%d checks, %d failures\n", g_checks, g_failures);
	printf(g_failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	return g_failures ? 1 : 0;
}
