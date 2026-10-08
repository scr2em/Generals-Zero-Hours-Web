// Decodes WAV files with the engine's own decoder (Core/GameEngineDevice/Source/WebDevice/Audio/WebAudioDecoder.cpp)
// and writes the 16 bit samples next to them, so the tests can compare them with the Python reference decoder.
//
//   engine_decode_check <in.wav> <out.s16>      prints:  codec rate channels frames
#include "WebDevice/Audio/WebAudioDecoder.h"

#include <stdio.h>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 3;
	std::vector<uint8_t> data;
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		data.insert(data.end(), buf, buf + n);
	fclose(f);
	std::vector<int16_t> pcm;
	WebAudio::StreamInfo info;
	if (!WebAudio::decodeAll(data.data(), data.size(), &pcm, &info))
	{
		printf("FAIL\n");
		return 1;
	}
	FILE *o = fopen(argv[2], "wb");
	fwrite(pcm.data(), 2, pcm.size(), o);
	fclose(o);
	printf("%d %u %u %llu\n", (int)info.codec, info.sampleRate, info.channels, (unsigned long long)info.totalFrames);
	return 0;
}
