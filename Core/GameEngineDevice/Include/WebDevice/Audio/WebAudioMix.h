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

// FILE: WebAudioMix.h ////////////////////////////////////////////////////////
//
// The volume rules of the WebAudioManager, kept free of engine types so that they can be tested:
// category volumes (music, speech, sound effects, positional sound effects) and the distance
// fade of positional sounds. It is exactly the formula of MilesAudioManager::getEffectiveVolume.
//
// Miles also attenuated positional sounds by distance on its own. The game passes its minimum
// and maximum distance in the maximum/minimum parameter slots of AIL_set_3D_sample_distances,
// swapped, which leaves Miles' own attenuation without effect inside the audible range; the
// distance fade of the game (AudioSettings::m_use3DSoundRangeVolumeFade) is the only distance
// attenuation. The browser device does the same: the panner only pans, it never attenuates.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <math.h>

namespace WebAudio
{

enum class MixCategory
{
	Music,				///< AT_Music
	Speech,				///< AT_Streaming
	Sound,				///< AT_SoundEffect, not positional (user interface, global)
	Sound3D,			///< AT_SoundEffect, positional
};

struct MixSettings
{
	float musicVolume = 1.0f;
	float speechVolume = 1.0f;
	float soundVolume = 1.0f;
	float sound3DVolume = 1.0f;
	bool use3DSoundRangeVolumeFade = true;
	float fadeExponent = 4.0f;
};

/// @param eventVolume  AudioEventRTS::getVolume() * getVolumeShift()
/// @param hasPosition  false if the sound has no position (then no distance fade applies)
/// @param distance     distance of the sound to the listener
/// @param minDistance  full volume closer than this
/// @param maxDistance  silent from this distance on
inline float effectiveVolume(MixCategory category, float eventVolume, const MixSettings &mix,
	bool hasPosition = false, float distance = 0.0f, float minDistance = 0.0f, float maxDistance = 0.0f)
{
	float volume = eventVolume;
	switch (category)
	{
		case MixCategory::Music:
			volume *= mix.musicVolume;
			break;
		case MixCategory::Speech:
			volume *= mix.speechVolume;
			break;
		case MixCategory::Sound:
			volume *= mix.soundVolume;
			break;
		case MixCategory::Sound3D:
			volume *= mix.sound3DVolume;
			if (hasPosition)
			{
				if (distance >= maxDistance)
				{
					volume = 0.0f;
				}
				else if (mix.use3DSoundRangeVolumeFade && distance > minDistance)
				{
					float attenuation = (distance - minDistance) / (maxDistance - minDistance);
					attenuation = powf(attenuation, mix.fadeExponent);
					volume *= 1.0f - attenuation;
				}
			}
			break;
	}
	return volume;
}

} // namespace WebAudio
