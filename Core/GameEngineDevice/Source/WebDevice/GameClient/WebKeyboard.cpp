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

// FILE: WebKeyboard.cpp //////////////////////////////////////////////////////
//
// Keyboard of the WebAssembly build, see WebKeyboard.h.
//
///////////////////////////////////////////////////////////////////////////////

#include "WebDevice/GameClient/WebKeyboard.h"
#include "WebDevice/Platform/WebPlatform.h"

enum { VK_CAPITAL_ = 0x14 };

//-------------------------------------------------------------------------------------------------
WebKeyboard::WebKeyboard()
{

	if( WebPlatform_GetKeyState( VK_CAPITAL_ ) & 0x01 )
		m_modifiers |= KEY_STATE_CAPSLOCK;
	else
		m_modifiers &= ~KEY_STATE_CAPSLOCK;

}

//-------------------------------------------------------------------------------------------------
WebKeyboard::~WebKeyboard()
{
}

//-------------------------------------------------------------------------------------------------
void WebKeyboard::init()
{

	Keyboard::init();

	// anything typed before the game was listening is not for it
	WebPlatform_ResetKeys();

}

//-------------------------------------------------------------------------------------------------
void WebKeyboard::reset()
{

	Keyboard::reset();

}

//-------------------------------------------------------------------------------------------------
void WebKeyboard::update()
{

	Keyboard::update();

}

//-------------------------------------------------------------------------------------------------
Bool WebKeyboard::getCapsState()
{

	return ( WebPlatform_GetKeyState( VK_CAPITAL_ ) & 0x01 ) != 0;

}

//-------------------------------------------------------------------------------------------------
/** Get a single key transition from the platform layer */
//-------------------------------------------------------------------------------------------------
void WebKeyboard::getKey( KeyboardIO *key )
{
	WebKeyEvent event;

	key->key = KEY_NONE;

	if( !WebPlatform_PopKeyEvent( &event ) )
		return;

	key->key = event.dik;

	if( event.down )
	{
		key->state = KEY_STATE_DOWN;
		key->keyDownTimeMsec = event.time;
	}
	else
	{
		key->state = KEY_STATE_UP;
	}

	key->status = KeyboardIO::STATUS_UNUSED;

}
