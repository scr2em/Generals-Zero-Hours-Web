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

// FILE: WebVideoPlayer.h /////////////////////////////////////////////////////
//
// The video player of the WebAssembly build: the game's Bink videos (.bik) decoded by the small
// FFmpeg of Dependencies/FFmpegWeb, played through the engine's VideoPlayer / VideoStream / VideoBuffer
// interfaces like the Bink player does on Windows.
//
// It builds on the fork's FFmpeg player: the movie is found like FFmpegVideoPlayer finds it
// (Data\<language>\Movies, then Data\Movies, the mod directory first), read through the engine's
// File API (so it works on the web file systems like every other game file) and demuxed and
// decoded by FFmpegFile. What the web build adds, in WebVideoStream:
//
//  - the playback clock. Frames are due at their frame number / frame rate after the first call of
//    isFrameReady(), by the browser's monotonic clock. A game that renders too slowly drops frames
//    to stay in time instead of playing in slow motion.
//  - the sound, which goes to the Web Audio device (WebAudioBackend.h) on a voice of its own: the
//    decoded Bink audio is queued in chunks ahead of the clock, gapless. If the audio stalls (the
//    engine thread was blocked for longer than the queued lead) it skips ahead on resuming to stay
//    in sync with the picture. The movie plays silently if there is no sound device (or the browser
//    has not allowed sound yet) and the picture does not wait for it.
//  - decoding ahead: a few frames and a fraction of a second of audio are decoded before they are
//    due, so a slow frame of the game does not starve the audio.
//
// A movie that cannot be opened (missing, corrupt, not Bink, a video without any frame) gives
// no stream, the callers then skip it.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"

class WebVideoPlayer : public FFmpegVideoPlayer
{
	public:

		/// Sets the volume of the sound of the playing videos (the speech volume of the game).
		virtual void setVolume( Real volume ) override;

	protected:

		virtual VideoStreamInterface* createStream( File* file ) override;
};
