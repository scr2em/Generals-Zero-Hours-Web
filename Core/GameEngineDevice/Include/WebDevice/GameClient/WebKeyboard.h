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

// FILE: WebKeyboard.h ////////////////////////////////////////////////////////
//
// Keyboard of the WebAssembly build. Takes the key transitions that
// WebPlatform collected from the browser, already expressed as DirectInput
// scan codes, and hands them to the engine's Keyboard base class like
// DirectInputKeyboard does on Windows.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/Keyboard.h"

class WebKeyboard : public Keyboard
{

public:

	WebKeyboard();
	virtual ~WebKeyboard() override;

	virtual void init() override;				///< initialize the keyboard, extending init functionality
	virtual void reset() override;			///< reset the keyboard system
	virtual void update() override;			///< update call, extending update functionality
	virtual Bool getCapsState() override;	///< get state of caps lock key, return TRUE if down

protected:

	virtual void getKey( KeyboardIO *key ) override;	///< get a single key event

};
