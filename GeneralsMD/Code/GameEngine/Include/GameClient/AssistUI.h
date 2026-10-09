/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// AssistUI.h
// The client side of the player assists: small panels, overlays on the world view, hotkeys and the mouse gestures.
// Everything is drawn by the engine with plain rectangles, lines and text (no art files), so it looks the same
// with the starter pack and with retail data.  Nothing here changes the simulation directly: the panels and
// hotkeys send assist commands (GameMessage MSG_ASSIST_...) that the logic carries out, see GameLogic/PlayerAssist.h.

#pragma once

#include <vector>

#include "Common/GameCommon.h"
#include "Common/MessageStream.h"
#include "GameClient/Color.h"
#include "GameClient/GameWindow.h"

class DisplayString;
class Object;
struct AssistTextPool;
class View;

//-------------------------------------------------------------------------------------------------
/// One button of a panel.
struct AssistButton
{
	Int						m_id;
	Int						m_glyph;			///< what the button shows (a panel decides what the numbers mean)
	UnicodeString	m_label;
	UnicodeString	m_tip;				///< shown in the title line while the mouse is on the button
	Bool					m_on;					///< toggled on / the current choice
	Bool					m_enabled;
	Bool					m_visible;
	Bool					m_flat;				///< no box: the button is only a place to draw a label
	Int						m_x, m_y, m_w, m_h;	///< inside the panel, in screen pixels
};

//-------------------------------------------------------------------------------------------------
/// A small window with buttons, drawn by AssistUI.
class AssistPanel
{
public:
	AssistPanel( const char *name );
	virtual ~AssistPanel();

	void create();
	void destroy();
	Bool exists() const { return m_window != nullptr; }
	void show( Bool visible );
	Bool isVisible() const;

	/// Refresh the buttons from the game state; return false to hide the panel.
	virtual Bool refresh() = 0;
	/// Place the panel on the screen (called when it is shown or the screen changes).
	virtual void place( Int *x, Int *y, Int *w, Int *h ) = 0;
	virtual void clicked( Int buttonId ) = 0;
	virtual void drawGlyph( const AssistButton &b, Int x, Int y, Color color );

	const AsciiString	&name() const { return m_name; }
	UnicodeString			m_title;
	std::vector<AssistButton> m_buttons;
	Int								m_hover;			///< index of the button under the mouse, -1 none
	Int								m_pressed;
	Int								m_fillAlpha;			///< 0..255
	AssistTextPool		*m_texts;			///< this panel's display strings

	void	addButton( Int id, Int glyph, const wchar_t *label, const wchar_t *tip );
	void	setManual( Bool manual ) { m_manual = manual; }
	void	setStackLeft( Bool stack ) { m_stackLeft = stack; }
	Bool	isStackLeft() const { return m_stackLeft; }
	Bool	isManual() const { return m_manual; }
	AssistButton *find( Int id );
	/// Lay the buttons out in a row/grid: 'columns' per row, cells of w x h, a gap, below a title line.
	void	flow( Int columns, Int cellW, Int cellH, Int gap, Int *outW, Int *outH );

	GameWindow *window() { return m_window; }

private:
	static void drawFunc( GameWindow *window, WinInstanceData *data );
	static WindowMsgHandledType inputFunc( GameWindow *window, UnsignedInt msg, WindowMsgData m1, WindowMsgData m2 );
	static WindowMsgHandledType systemFunc( GameWindow *window, UnsignedInt msg, WindowMsgData m1, WindowMsgData m2 );
	Int  hit( Int mx, Int my ) const;

	AsciiString	m_name;
	GameWindow	*m_window;
	Bool				m_manual;				///< shown and hidden by its owner, not by AssistUI::update
	Bool				m_stackLeft;			///< one of the panels stacked above the command bar at the left
};

//-------------------------------------------------------------------------------------------------
class AssistUI
{
public:
	static void init();
	static void reset();																///< a game ends (or a new one starts): panels go
	static void update();																///< once per client frame while in a game
	static void drawOverlays( View *view );							///< on top of the world view, below the windows

	// ---- shared helpers ----------------------------------------------------------------------
	/// True when the assists can be used in this game (the match allows them and the local player takes part).
	static Bool active();
	/// True while the local player plays a match (not a replay, not as observer): what needs no rule of the match (display, selection) works.
	static Bool playing();
	/// Scale of the assist graphics: from the screen height, times the player's setting.
	static Real scale();
	static Int  px( Int designPixels );								///< designPixels * scale()
	static Int  controlBarTop();											///< y of the top of the command bar (the panels sit above)
	static Int  belowToolbar();												///< y just below the toolbar of the display assists (top right)
	/// "Alt+3" for the key a meta message is mapped to (empty if none).
	static UnicodeString hotkeyText( GameMessage::Type msg );
	/// y of a panel of the left stack: above the command bar and the stack panels that were made before it and are shown.
	static Int  stackY( const AssistPanel *panel, Int height );

	/// Draw text with the shared string pool (call from draw code only).
	static void text( const UnicodeString &s, Int x, Int y, Color color, Int pointSize = 10, Bool centeredInWidth = FALSE, Int width = 0 );
	static Int  textWidth( const UnicodeString &s, Int pointSize = 10 );
	static void box( Int x, Int y, Int w, Int h, Color fill, Color border );
	static void line( Int x0, Int y0, Int x1, Int y1, Int width, Color color );
	static void beginFrame();
	static void usePool( AssistTextPool *pool );		///< the strings of the next text() calls come from this pool (null: the world overlay's)

	// ---- the toolbar: switches of the display assists, at the top right -------------------------
	struct ToolbarEntry
	{
		Int					m_id;
		const wchar_t	*m_label;
		const wchar_t	*m_tip;
		Bool				*m_option;				///< the entry shows when this option is on
		Bool				(*m_isOn)();
		void				(*m_toggle)();
	};
	static void addToolbarEntry( const ToolbarEntry &entry );

	// ---- the local selection ------------------------------------------------------------------
	struct Selection
	{
		std::vector<ObjectID>	m_ids;						///< own, mobile, controllable units
		Int										m_formation;			///< the formation they share (AFORM_NONE if none or mixed)
		Bool									m_valid;
	};
	static const Selection &selection();
	static void invalidateSelection();

	// ---- mouse gestures (called by the translator) -------------------------------------------
	static Bool wantsRightDrag();
	static void drawAim();

	static GameMessageDisposition translate( const GameMessage *msg );

	// ---- the match setting on the skirmish and LAN setup screens ---------------------------------
	/// The "Player assists" check box of a setup screen: the one the layout has (CheckboxPlayerAssists), or one made
	/// below 'like' (the layout of the original game has none).  Null if neither is possible.
	static GameWindow *setupCheckbox( GameWindow *parent, const char *layoutName, GameWindow *like );
	static NameKeyType setupCheckboxId( const char *layoutName );

	// ---- the Options screen ----------------------------------------------------------------------
	/// The "Player assists" button of the options screen: the layout's ButtonAssistOptions, or one made below the button 'likeButton'.
	static void setupOptionsButton( const char *layoutName, const char *likeButton );
	static NameKeyType optionsButtonId( const char *layoutName );
	static void openOptionsDialog();
};

//-------------------------------------------------------------------------------------------------
class AssistTranslator : public GameMessageTranslator
{
public:
	virtual GameMessageDisposition translateGameMessage( const GameMessage *msg ) override;
};


//-------------------------------------------------------------------------------------------------
/// The protect assist (AssistUIProtect.cpp).
class AssistProtectUI
{
public:
	static void init();
	static void reset();
	static Bool picking();										///< the player is choosing what to protect
	static void begin();											///< start choosing, with the selected units as protectors
	static void cancel();
	static void stop();												///< the selected units stop protecting
	static void useSelection();								///< the selected units are what to protect
	static Bool translate( const GameMessage *msg );	///< true when the message is used up
	static void drawOverlays( View *view );
};


//-------------------------------------------------------------------------------------------------
/// The defence coverage view (AssistUICoverage.cpp).
class AssistCoverageUI
{
public:
	static void init();
	static void reset();
	static Bool on();
	static void toggle();
	static Bool translate( const GameMessage *msg );
	static void drawOverlays( View *view );
};


//-------------------------------------------------------------------------------------------------
/// The "base under attack" response (AssistUIAlert.cpp).
class AssistAlertUI
{
public:
	static void init();
	static void reset();
	static void defend();											///< send the idle army units near the alert to attack-move there
	static void sendBack();										///< send them back
	static Bool translate( const GameMessage *msg );
	static void drawOverlays( View *view );
};


//-------------------------------------------------------------------------------------------------
/// The odds meter (AssistUIOdds.cpp).
class AssistOddsUI
{
public:
	static void init();
	static void reset();
	static void toggle();
	static Bool translate( const GameMessage *msg );
	static void drawOverlays( View *view );
};


//-------------------------------------------------------------------------------------------------
/// The unit stances (AssistUIStance.cpp).
class AssistStanceUI
{
public:
	static void init();
	static void reset();
	static Bool translate( const GameMessage *msg );	///< true when the message is used up
};


//-------------------------------------------------------------------------------------------------
/// The idle hotkeys and the idle counter (AssistUIIdle.cpp).
class AssistIdleUI
{
public:
	static void init();
	static void reset();
	static Bool translate( const GameMessage *msg );	///< true when the message is used up
};
