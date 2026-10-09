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

// AssistUI.cpp
// The client side of the player assists.  See AssistUI.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "Common/ThingTemplate.h"
#include "GameClient/AssistHooks.h"
#include "GameClient/AssistUI.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameFont.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindowManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

//=================================================================================================
// shared drawing helpers
//=================================================================================================

// Strings are drawn the moment they are asked for, but each draw call gets a string of its own that lives until the
// next frame, so a batch of 2D drawing never sees a string change under it.  Every panel and the world overlays have a
// pool; the pool in use is the one of the window being drawn.
struct AssistPooledText
{
	DisplayString	*m_string;
	Int						m_pointSize;
};
typedef AssistPooledText PooledText;

struct AssistTextPool
{
	std::vector<PooledText>	m_items;
	size_t									m_next;
	AssistTextPool() : m_next( 0 ) {}
};

namespace
{
	AssistTextPool s_overlayPool;
	AssistTextPool *s_pool = &s_overlayPool;
	AssistUI::Selection s_selection;

	// the drag that aims a formation
	Bool			s_aimEligible = FALSE;		// the right button went down where a drag would aim a formation
	Bool			s_aimDragging = FALSE;		// ... and has moved far enough
	ICoord2D	s_aimStartPixel;
	ICoord2D	s_aimPixel;
	Coord3D		s_aimStart;
	Bool			s_aimStartValid = FALSE;
	Bool			s_swallowRightClick = FALSE;

	std::vector<AssistPanel *> s_panels;
	Bool s_hooked = FALSE;

	Color white( UnsignedByte a = 255 ) { return GameMakeColor( 255, 255, 255, a ); }
}

//-------------------------------------------------------------------------------------------------
Real AssistUI::scale()
{
	Real base = 1.0f;
	if (TheDisplay)
	{
		const Real fromHeight = (Real)TheDisplay->getHeight() / 600.0f;
		const Real fromWidth = (Real)TheDisplay->getWidth() / 800.0f;
		base = fromHeight < fromWidth ? fromHeight : fromWidth;
		if (base < 1.0f)
			base = 1.0f;
	}
	return base * (Real)TheAssistOptions.m_uiScalePercent / 100.0f;
}

//-------------------------------------------------------------------------------------------------
Int AssistUI::px( Int designPixels )
{
	return (Int)((Real)designPixels * scale() + 0.5f);
}

//-------------------------------------------------------------------------------------------------
Int AssistUI::controlBarTop()
{
	if (TheWindowManager)
	{
		static NameKeyType key = NAMEKEY_INVALID;
		if (key == NAMEKEY_INVALID)
			key = TheNameKeyGenerator->nameToKey( "ControlBar.wnd:ControlBarParent" );
		GameWindow *bar = TheWindowManager->winGetWindowFromId( nullptr, key );
		if (bar && !bar->winIsHidden())
		{
			Int x, y;
			bar->winGetScreenPosition( &x, &y );
			if (y > 0 && y < (Int)TheDisplay->getHeight())
				return y;
		}
	}
	return (Int)(TheDisplay->getHeight() * 0.8f);
}

//-------------------------------------------------------------------------------------------------
Int AssistUI::stackY( const AssistPanel *panel, Int height )
{
	Int y = controlBarTop() - px( 6 ) - height;
	for (size_t i = 0; i < s_panels.size() && s_panels[i] != panel; ++i)
	{
		AssistPanel *p = s_panels[i];
		if (p->isStackLeft() && p->isVisible())
		{
			Int w, h;
			p->window()->winGetSize( &w, &h );
			y -= h + px( 4 );
		}
	}
	return y;
}

//-------------------------------------------------------------------------------------------------
void AssistUI::usePool( AssistTextPool *pool )
{
	s_pool = pool ? pool : &s_overlayPool;
}

//-------------------------------------------------------------------------------------------------
void AssistUI::beginFrame()
{
	s_overlayPool.m_next = 0;
	s_pool = &s_overlayPool;
}

//-------------------------------------------------------------------------------------------------
static PooledText &nextText( Int pointSize )
{
	if (s_pool->m_next >= s_pool->m_items.size())
	{
		PooledText p;
		p.m_string = TheDisplayStringManager->newDisplayString();
		p.m_pointSize = 0;
		s_pool->m_items.push_back( p );
	}
	PooledText &p = s_pool->m_items[s_pool->m_next++];
	if (p.m_pointSize != pointSize)
	{
		p.m_pointSize = pointSize;
		p.m_string->setFont( TheFontLibrary->getFont( AsciiString( "Arial" ), TheGlobalLanguageData->adjustFontSize( pointSize ), FALSE ) );
	}
	return p;
}

//-------------------------------------------------------------------------------------------------
Int AssistUI::textWidth( const UnicodeString &s, Int pointSize )
{
	PooledText &p = nextText( pointSize );
	--s_pool->m_next;	// measuring does not use up the string
	p.m_string->setText( s );
	return p.m_string->getWidth();
}

//-------------------------------------------------------------------------------------------------
void AssistUI::text( const UnicodeString &s, Int x, Int y, Color color, Int pointSize, Bool centeredInWidth, Int width )
{
	PooledText &p = nextText( pointSize );
	p.m_string->setText( s );
	if (centeredInWidth)
		x += (width - p.m_string->getWidth()) / 2;
	p.m_string->draw( x, y, color, GameMakeColor( 0, 0, 0, 255 ) );
}

//-------------------------------------------------------------------------------------------------
void AssistUI::box( Int x, Int y, Int w, Int h, Color fill, Color border )
{
	TheDisplay->drawFillRect( x, y, w, h, fill );
	TheDisplay->drawOpenRect( x, y, w, h, 1.0f, border );
}

//-------------------------------------------------------------------------------------------------
void AssistUI::line( Int x0, Int y0, Int x1, Int y1, Int width, Color color )
{
	TheDisplay->drawLine( x0, y0, x1, y1, (Real)width, color );
}

//=================================================================================================
// the selection
//=================================================================================================

void AssistUI::invalidateSelection()
{
	s_selection.m_valid = FALSE;
}

//-------------------------------------------------------------------------------------------------
const AssistUI::Selection &AssistUI::selection()
{
	if (s_selection.m_valid)
		return s_selection;

	s_selection.m_ids.clear();
	s_selection.m_formation = AFORM_NONE;
	s_selection.m_valid = TRUE;
	if (TheInGameUI == nullptr)
		return s_selection;

	const DrawableList *list = TheInGameUI->getAllSelectedLocalDrawables();
	if (list == nullptr)
		return s_selection;
	for (DrawableListCIt it = list->begin(); it != list->end(); ++it)
	{
		const Object *obj = (*it)->getObject();
		if (obj == nullptr || !obj->isLocallyControlled() || obj->getAI() == nullptr || obj->isKindOf( KINDOF_IMMOBILE ))
			continue;
		s_selection.m_ids.push_back( obj->getID() );
	}
	if (ThePlayerAssist)
		s_selection.m_formation = ThePlayerAssist->sharedFormation( s_selection.m_ids );
	return s_selection;
}

//-------------------------------------------------------------------------------------------------
Bool AssistUI::playing()
{
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return FALSE;
	if (TheRecorder && TheRecorder->isPlaybackMode())
		return FALSE;
	Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	return local != nullptr && local->isPlayerActive();
}

//-------------------------------------------------------------------------------------------------
/// The assists that give orders work when the match allows them.
Bool AssistUI::active()
{
	return ThePlayerAssist != nullptr && ThePlayerAssist->allowed() && playing();
}

//=================================================================================================
// panels
//=================================================================================================

AssistPanel::AssistPanel( const char *name ) : m_name( name )
{
	m_window = nullptr;
	m_hover = -1;
	m_pressed = -1;
	m_texts = nullptr;
	m_fillAlpha = 190;
	m_manual = FALSE;
	m_stackLeft = FALSE;
	s_panels.push_back( this );
}

//-------------------------------------------------------------------------------------------------
AssistPanel::~AssistPanel()
{
	destroy();
	for (size_t i = 0; i < s_panels.size(); ++i)
	{
		if (s_panels[i] == this)
		{
			s_panels.erase( s_panels.begin() + i );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::create()
{
	if (m_window || TheWindowManager == nullptr)
		return;
	m_window = TheWindowManager->winCreate( nullptr, WIN_STATUS_ENABLED | WIN_STATUS_NO_FOCUS | WIN_STATUS_HIDDEN | WIN_STATUS_ABOVE,
		0, 0, 100, 40, systemFunc, nullptr );
	if (m_window)
	{
		m_window->winSetUserData( this );
		m_window->winSetInputFunc( inputFunc );
		m_window->winSetDrawFunc( drawFunc );
	}
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::destroy()
{
	if (m_window && TheWindowManager)
		TheWindowManager->winDestroy( m_window );
	m_window = nullptr;
	m_hover = -1;
	m_pressed = -1;
}

//-------------------------------------------------------------------------------------------------
Bool AssistPanel::isVisible() const
{
	return m_window && !m_window->winIsHidden();
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::show( Bool visible )
{
	if (m_window == nullptr)
		return;
	if (visible)
	{
		Int x, y, w, h;
		place( &x, &y, &w, &h );
		m_window->winSetPosition( x, y );
		m_window->winSetSize( w, h );
	}
	if (m_window->winIsHidden() == visible)
		m_window->winHide( !visible );
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::addButton( Int id, Int glyph, const wchar_t *label, const wchar_t *tip )
{
	AssistButton b;
	b.m_id = id;
	b.m_glyph = glyph;
	b.m_label = label;
	b.m_tip = tip;
	b.m_on = FALSE;
	b.m_enabled = TRUE;
	b.m_visible = TRUE;
	b.m_flat = FALSE;
	b.m_x = b.m_y = b.m_w = b.m_h = 0;
	m_buttons.push_back( b );
}

//-------------------------------------------------------------------------------------------------
AssistButton *AssistPanel::find( Int id )
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		if (m_buttons[i].m_id == id)
			return &m_buttons[i];
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::flow( Int columns, Int cellW, Int cellH, Int gap, Int *outW, Int *outH )
{
	const Int titleH = AssistUI::px( 14 );
	Int col = 0;
	Int row = 0;
	Int maxCol = 0;
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		AssistButton &b = m_buttons[i];
		if (!b.m_visible)
			continue;
		b.m_x = gap + col * (cellW + gap);
		b.m_y = titleH + gap + row * (cellH + gap);
		b.m_w = cellW;
		b.m_h = cellH;
		++col;
		if (col > maxCol)
			maxCol = col;
		if (col >= columns)
		{
			col = 0;
			++row;
		}
	}
	const Int rows = row + (col > 0 ? 1 : 0);
	*outW = gap + maxCol * (cellW + gap);
	*outH = titleH + gap + rows * (cellH + gap);
}

//-------------------------------------------------------------------------------------------------
Int AssistPanel::hit( Int mx, Int my ) const
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		const AssistButton &b = m_buttons[i];
		if (b.m_visible && mx >= b.m_x && mx < b.m_x + b.m_w && my >= b.m_y && my < b.m_y + b.m_h)
			return (Int)i;
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::drawGlyph( const AssistButton &b, Int x, Int y, Color color )
{
	AssistUI::text( b.m_label, x + b.m_x, y + b.m_y + (b.m_h - AssistUI::px( 12 )) / 2, color, 9, TRUE, b.m_w );
}

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType AssistPanel::systemFunc( GameWindow *window, UnsignedInt msg, WindowMsgData m1, WindowMsgData m2 )
{
	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
WindowMsgHandledType AssistPanel::inputFunc( GameWindow *window, UnsignedInt msg, WindowMsgData m1, WindowMsgData m2 )
{
	AssistPanel *panel = (AssistPanel *)window->winGetUserData();
	if (panel == nullptr)
		return MSG_IGNORED;

	Int wx, wy;
	window->winGetScreenPosition( &wx, &wy );
	const Int mx = (Int)(m1 & 0xFFFF) - wx;
	const Int my = (Int)(m1 >> 16) - wy;

	switch (msg)
	{
		case GWM_MOUSE_ENTERING:
		case GWM_MOUSE_POS:
			panel->m_hover = panel->hit( mx, my );
			TheMouse->setCursor( Mouse::ARROW );
			return MSG_HANDLED;

		case GWM_MOUSE_LEAVING:
			panel->m_hover = -1;
			panel->m_pressed = -1;
			return MSG_HANDLED;

		case GWM_LEFT_DOWN:
			panel->m_pressed = panel->hit( mx, my );
			return MSG_HANDLED;

		case GWM_LEFT_UP:
		{
			const Int h = panel->hit( mx, my );
			const Int pressed = panel->m_pressed;
			panel->m_pressed = -1;
			if (h >= 0 && h == pressed && panel->m_buttons[h].m_enabled)
				panel->clicked( panel->m_buttons[h].m_id );
			return MSG_HANDLED;
		}

		case GWM_RIGHT_DOWN:
		case GWM_RIGHT_UP:
		case GWM_MIDDLE_DOWN:
		case GWM_MIDDLE_UP:
		case GWM_LEFT_DRAG:
		case GWM_RIGHT_DRAG:
			return MSG_HANDLED;

		default:
			break;
	}
	return MSG_IGNORED;
}

//-------------------------------------------------------------------------------------------------
void AssistPanel::drawFunc( GameWindow *window, WinInstanceData *data )
{
	AssistPanel *panel = (AssistPanel *)window->winGetUserData();
	if (panel == nullptr)
		return;

	Int x, y, w, h;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &w, &h );

	{
		static Int s_drawn = 0;
		if (s_drawn < 4 && panel->name().compare( "AssistOptions" ) == 0)
		{
			++s_drawn;
			ASSIST_DEBUG(( "ASSIST draw %s at %d,%d size %d,%d", panel->name().str(), x, y, w, h ));
		}
	}

	// this window's own strings
	if (panel->m_texts == nullptr)
		panel->m_texts = new AssistTextPool;
	panel->m_texts->m_next = 0;
	AssistUI::usePool( panel->m_texts );

	AssistUI::box( x, y, w, h, GameMakeColor( 12, 16, 24, (UnsignedByte)panel->m_fillAlpha ), GameMakeColor( 120, 150, 190, 230 ) );

	// the title line: the hovered button's tip, or the panel's title
	UnicodeString title = panel->m_title;
	if (panel->m_hover >= 0 && panel->m_hover < (Int)panel->m_buttons.size() && !panel->m_buttons[panel->m_hover].m_tip.isEmpty())
		title = panel->m_buttons[panel->m_hover].m_tip;
	AssistUI::text( title, x + AssistUI::px( 4 ), y + AssistUI::px( 1 ), GameMakeColor( 200, 220, 255, 255 ), 9 );

	for (size_t i = 0; i < panel->m_buttons.size(); ++i)
	{
		const AssistButton &b = panel->m_buttons[i];
		if (!b.m_visible)
			continue;
		const Bool hover = (Int)i == panel->m_hover;
		const Bool pressed = (Int)i == panel->m_pressed && hover;
		Color fill = GameMakeColor( 40, 52, 70, 220 );
		Color border = GameMakeColor( 90, 110, 140, 255 );
		if (b.m_on)
		{
			fill = GameMakeColor( 40, 100, 70, 235 );
			border = GameMakeColor( 120, 230, 150, 255 );
		}
		if (hover)
			border = GameMakeColor( 255, 255, 200, 255 );
		if (pressed)
			fill = GameMakeColor( 90, 100, 60, 235 );
		if (!b.m_enabled)
		{
			fill = GameMakeColor( 30, 30, 34, 200 );
			border = GameMakeColor( 70, 70, 76, 255 );
		}
		if (!b.m_flat)
			AssistUI::box( x + b.m_x, y + b.m_y, b.m_w, b.m_h, fill, border );
		panel->drawGlyph( b, x, y, b.m_enabled ? white() : GameMakeColor( 130, 130, 140, 255 ) );
	}

	AssistUI::usePool( nullptr );
}

//=================================================================================================
// the formation picker
//=================================================================================================

namespace
{
	const wchar_t *formationNames[AFORM_COUNT] =
	{
		L"No formation", L"Line", L"Column", L"Wedge", L"Box", L"Loose", L"Keep shape"
	};
	const wchar_t *formationShort[AFORM_COUNT] =
	{
		L"-", L"Line", L"Col", L"Wedge", L"Box", L"Loose", L"Keep"
	};
	const GameMessage::Type formationKeys[AFORM_COUNT] =
	{
		GameMessage::MSG_META_ASSIST_FORM_NONE, GameMessage::MSG_META_ASSIST_FORM_LINE, GameMessage::MSG_META_ASSIST_FORM_COLUMN,
		GameMessage::MSG_META_ASSIST_FORM_WEDGE, GameMessage::MSG_META_ASSIST_FORM_BOX, GameMessage::MSG_META_ASSIST_FORM_LOOSE,
		GameMessage::MSG_META_ASSIST_FORM_KEEP
	};

	/// "Alt+3" for the key a meta message is mapped to.
	UnicodeString hotkeyText( GameMessage::Type msg )
	{
		UnicodeString out;
		if (TheMetaMap == nullptr)
			return out;
		const MetaMapRec *rec = nullptr;
		for (const MetaMapRec *r = TheMetaMap->getFirstMetaMapRec(); r; r = r->m_next)
		{
			if (r->m_meta == msg)
			{
				rec = r;
				break;
			}
		}
		if (rec == nullptr || rec->m_key == MK_NONE)
			return out;
		const Int mods = (Int)rec->m_modState;
		if (mods & CTRL)
			out.concat( L"Ctrl+" );
		if (mods & ALT)
			out.concat( L"Alt+" );
		if (mods & SHIFT)
			out.concat( L"Shift+" );
		WideChar c = 0;
		if (rec->m_key >= MK_A && rec->m_key <= MK_Z)
			c = (WideChar)(L'A' + (rec->m_key - MK_A));
		else if (rec->m_key >= MK_1 && rec->m_key <= MK_9)
			c = (WideChar)(L'1' + (rec->m_key - MK_1));
		else if (rec->m_key == MK_0)
			c = L'0';
		if (c)
			out.concat( c );
		else
			out.concat( L"key" );
		return out;
	}
}

class FormationPanel : public AssistPanel
{
public:
	FormationPanel() : AssistPanel( "Formation" )
	{
		m_title = L"Formation";
		setStackLeft( TRUE );
		for (Int f = AFORM_NONE; f < AFORM_COUNT; ++f)
			addButton( f, f, formationShort[f], nullptr );
	}

	virtual Bool refresh() override
	{
		const AssistUI::Selection &sel = AssistUI::selection();
		if (!TheAssistOptions.m_formations || !AssistUI::active() || sel.m_ids.size() < 2)
			return FALSE;
		for (size_t i = 0; i < m_buttons.size(); ++i)
		{
			AssistButton &b = m_buttons[i];
			b.m_on = sel.m_formation == b.m_id;
			UnicodeString tip;
			tip.format( L"%ls", formationNames[b.m_id] );
			const UnicodeString key = hotkeyText( formationKeys[b.m_id] );
			if (!key.isEmpty())
				tip.format( L"%ls  (%ls)", formationNames[b.m_id], key.str() );
			b.m_tip = tip;
		}
		m_title = L"Formation";
		return TRUE;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		const Int cellW = AssistUI::px( 38 );
		const Int cellH = AssistUI::px( 30 );
		flow( AFORM_COUNT, cellW, cellH, AssistUI::px( 3 ), w, h );
		*x = AssistUI::px( 6 );
		*y = AssistUI::stackY( this, *h );
	}

	virtual void clicked( Int id ) override
	{
		GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_FORMATION );
		m->appendIntegerArgument( id );
	}

	virtual void drawGlyph( const AssistButton &b, Int x, Int y, Color color ) override
	{
		// a picture of the formation: dots in its shape
		const Int type = b.m_glyph;
		const Int bx = x + b.m_x;
		const Int by = y + b.m_y;
		const Int labelH = AssistUI::px( 11 );
		const Int areaX = bx + AssistUI::px( 3 );
		const Int areaY = by + AssistUI::px( 2 );
		const Int areaW = b.m_w - AssistUI::px( 6 );
		const Int areaH = b.m_h - labelH - AssistUI::px( 3 );
		const Int dot = AssistUI::px( 2 ) > 2 ? AssistUI::px( 2 ) : 2;

		if (type == AFORM_NONE)
		{
			static const Int sx[5] = { 10, 70, 35, 85, 20 };
			static const Int sy[5] = { 20, 10, 55, 60, 85 };
			for (Int i = 0; i < 5; ++i)
				TheDisplay->drawFillRect( areaX + sx[i] * areaW / 100, areaY + sy[i] * areaH / 100, dot, dot, color );
		}
		else if (type == AFORM_KEEP)
		{
			const Int l = areaX + areaW / 6;
			const Int r = areaX + areaW - areaW / 6;
			const Int t = areaY + areaH / 8;
			const Int d = areaY + areaH - areaH / 8;
			const Int a = areaW / 4 > 3 ? areaW / 4 : 3;
			TheDisplay->drawLine( l, t, l + a, t, 1.0f, color );
			TheDisplay->drawLine( l, t, l, t + a, 1.0f, color );
			TheDisplay->drawLine( r, t, r - a, t, 1.0f, color );
			TheDisplay->drawLine( r, t, r, t + a, 1.0f, color );
			TheDisplay->drawLine( l, d, l + a, d, 1.0f, color );
			TheDisplay->drawLine( l, d, l, d - a, 1.0f, color );
			TheDisplay->drawLine( r, d, r - a, d, 1.0f, color );
			TheDisplay->drawLine( r, d, r, d - a, 1.0f, color );
			TheDisplay->drawFillRect( (l + r) / 2 - 1, (t + d) / 2 - 1, dot, dot, color );
		}
		else
		{
			std::vector<AssistSlot> slots;
			PlayerAssist::layoutSlots( type, 9, 1.0f, 1.0f, 0.0f, slots );
			Real minX = 1e9f, maxX = -1e9f, maxD = 0.0f;
			for (size_t i = 0; i < slots.size(); ++i)
			{
				minX = slots[i].m_x < minX ? slots[i].m_x : minX;
				maxX = slots[i].m_x > maxX ? slots[i].m_x : maxX;
				maxD = slots[i].m_depth > maxD ? slots[i].m_depth : maxD;
			}
			const Real spanX = (maxX - minX) > 0.5f ? (maxX - minX) : 1.0f;
			const Real spanD = maxD > 0.5f ? maxD : 1.0f;
			// the front of the formation points up
			for (size_t i = 0; i < slots.size(); ++i)
			{
				const Int dx = areaX + (Int)((slots[i].m_x - minX) / spanX * (areaW - dot));
				const Int dy = areaY + (Int)(slots[i].m_depth / spanD * (areaH - dot));
				TheDisplay->drawFillRect( dx, dy, dot, dot, color );
			}
		}
		AssistUI::text( b.m_label, bx, by + b.m_h - labelH - 1, color, 8, TRUE, b.m_w );
	}
};

static FormationPanel *s_formationPanel = nullptr;

//=================================================================================================
// the toolbar: switches of the display assists
//=================================================================================================

namespace
{
	std::vector<AssistUI::ToolbarEntry> s_toolbarEntries;
}

//-------------------------------------------------------------------------------------------------
void AssistUI::addToolbarEntry( const ToolbarEntry &entry )
{
	for (size_t i = 0; i < s_toolbarEntries.size(); ++i)
	{
		if (s_toolbarEntries[i].m_id == entry.m_id)
			return;
	}
	s_toolbarEntries.push_back( entry );
}

//-------------------------------------------------------------------------------------------------
class ToolbarPanel : public AssistPanel
{
public:
	ToolbarPanel() : AssistPanel( "Toolbar" )
	{
		m_title = L"Assists";
	}

	virtual Bool refresh() override
	{
		if (!AssistUI::playing())
			return FALSE;
		// one button per entry whose option is switched on
		if (m_buttons.size() != s_toolbarEntries.size())
		{
			m_buttons.clear();
			for (size_t i = 0; i < s_toolbarEntries.size(); ++i)
				addButton( s_toolbarEntries[i].m_id, 0, s_toolbarEntries[i].m_label, s_toolbarEntries[i].m_tip );
		}
		Bool any = FALSE;
		for (size_t i = 0; i < s_toolbarEntries.size(); ++i)
		{
			const AssistUI::ToolbarEntry &e = s_toolbarEntries[i];
			m_buttons[i].m_visible = *e.m_option;
			m_buttons[i].m_on = e.m_isOn();
			any = any || *e.m_option;
		}
		return any;
	}

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		Int visible = 0;
		for (size_t i = 0; i < m_buttons.size(); ++i)
			visible += m_buttons[i].m_visible ? 1 : 0;
		flow( visible > 0 ? visible : 1, AssistUI::px( 74 ), AssistUI::px( 22 ), AssistUI::px( 3 ), w, h );
		*x = (Int)TheDisplay->getWidth() - *w - AssistUI::px( 6 );
		*y = AssistUI::px( 26 );
	}

	virtual void clicked( Int id ) override
	{
		for (size_t i = 0; i < s_toolbarEntries.size(); ++i)
		{
			if (s_toolbarEntries[i].m_id == id)
				s_toolbarEntries[i].m_toggle();
		}
	}
};

static ToolbarPanel *s_toolbar = nullptr;

//=================================================================================================
// the panels and overlays, once per frame
//=================================================================================================

void AssistUI::init()
{
	if (!s_hooked)
	{
		s_hooked = TRUE;
		TheAssistRightDragHook = &AssistUI::wantsRightDrag;
	}
}

//-------------------------------------------------------------------------------------------------
void AssistUI::reset()
{
	AssistProtectUI::reset();
	AssistCoverageUI::reset();
	AssistAlertUI::reset();
	AssistOddsUI::reset();
	for (size_t i = 0; i < s_panels.size(); ++i)
		s_panels[i]->destroy();
	s_selection.m_valid = FALSE;
	s_aimEligible = FALSE;
	s_aimDragging = FALSE;
	s_swallowRightClick = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AssistUI::update()
{
	init();
	invalidateSelection();

	if (!AssistUI::playing())
	{
		for (size_t i = 0; i < s_panels.size(); ++i)
		{
			if (!s_panels[i]->isManual() && s_panels[i]->isVisible())
				s_panels[i]->show( FALSE );
		}
		return;
	}

	if (s_formationPanel == nullptr)
		s_formationPanel = new FormationPanel;
	AssistProtectUI::init();
	AssistCoverageUI::init();
	AssistAlertUI::init();
	AssistOddsUI::init();
	if (s_toolbar == nullptr)
		s_toolbar = new ToolbarPanel;

	for (size_t i = 0; i < s_panels.size(); ++i)
	{
		AssistPanel *p = s_panels[i];
		if (p->isManual())
			continue;
		if (!p->exists())
			p->create();
		const Bool want = p->refresh();
		if (want)
			p->show( TRUE );
		else if (p->isVisible())
			p->show( FALSE );
	}
}

//-------------------------------------------------------------------------------------------------
void AssistUI::drawOverlays( View *view )
{
	beginFrame();
	if (!AssistUI::playing())
		return;
	drawAim();
	AssistProtectUI::drawOverlays( view );
	AssistCoverageUI::drawOverlays( view );
	AssistAlertUI::drawOverlays( view );
	AssistOddsUI::drawOverlays( view );
}

//=================================================================================================
// drag to aim a formation
//=================================================================================================

Bool AssistUI::wantsRightDrag()
{
	return s_aimEligible;
}

//-------------------------------------------------------------------------------------------------
void AssistUI::drawAim()
{
	if (!s_aimDragging || !s_aimStartValid || TheTacticalView == nullptr)
		return;

	Coord3D end;
	if (!TheTacticalView->screenToTerrain( &s_aimPixel, &end ))
		return;

	ICoord2D a;
	if (!TheTacticalView->worldToScreen( &s_aimStart, &a ))
		return;

	const Color front = GameMakeColor( 120, 255, 160, 255 );
	line( a.x, a.y, s_aimPixel.x, s_aimPixel.y, 2, front );

	// the way the formation will face, away from the group, from the middle of the line
	const AssistUI::Selection &sel = selection();
	Coord3D mid;
	mid.x = 0.5f * (s_aimStart.x + end.x);
	mid.y = 0.5f * (s_aimStart.y + end.y);
	mid.z = s_aimStart.z;
	Real cx = 0.0f, cy = 0.0f;
	Int n = 0;
	for (size_t i = 0; i < sel.m_ids.size(); ++i)
	{
		const Object *o = TheGameLogic->findObjectByID( sel.m_ids[i] );
		if (o)
		{
			cx += o->getPosition()->x;
			cy += o->getPosition()->y;
			++n;
		}
	}
	if (n == 0)
		return;
	cx /= (Real)n;
	cy /= (Real)n;
	Real dx = end.x - s_aimStart.x;
	Real dy = end.y - s_aimStart.y;
	const Real len = sqrtf( dx * dx + dy * dy );
	if (len < 1.0f)
		return;
	Real nx = -dy / len;
	Real ny = dx / len;
	if (nx * (mid.x - cx) + ny * (mid.y - cy) < 0.0f)
	{
		nx = -nx;
		ny = -ny;
	}

	// the slots of the formation along the line
	std::vector<AssistSlot> slots;
	const Int count = (Int)sel.m_ids.size();
	const Real spacing = 22.0f;
	PlayerAssist::layoutSlots( sel.m_formation, count, spacing, spacing, len, slots );
	Real maxDepth = 0.0f;
	for (size_t i = 0; i < slots.size(); ++i)
		maxDepth = slots[i].m_depth > maxDepth ? slots[i].m_depth : maxDepth;
	const Real lx = dx / len;
	const Real ly = dy / len;
	for (size_t i = 0; i < slots.size(); ++i)
	{
		Coord3D p;
		const Real ahead = 0.5f * maxDepth - slots[i].m_depth;
		p.x = mid.x + nx * ahead + lx * slots[i].m_x;
		p.y = mid.y + ny * ahead + ly * slots[i].m_x;
		p.z = TheTerrainLogic->getGroundHeight( p.x, p.y );
		ICoord2D s;
		if (TheTacticalView->worldToScreen( &p, &s ))
			TheDisplay->drawFillRect( s.x - 2, s.y - 2, 5, 5, GameMakeColor( 120, 255, 160, 255 ) );
	}

	// an arrow from the middle of the line towards the facing
	Coord3D tip;
	tip.x = mid.x + nx * 60.0f;
	tip.y = mid.y + ny * 60.0f;
	tip.z = TheTerrainLogic->getGroundHeight( tip.x, tip.y );
	ICoord2D m2, t2;
	if (TheTacticalView->worldToScreen( &mid, &m2 ) && TheTacticalView->worldToScreen( &tip, &t2 ))
		line( m2.x, m2.y, t2.x, t2.y, 2, GameMakeColor( 255, 255, 120, 255 ) );
}

//=================================================================================================
// the options dialog
//=================================================================================================

namespace
{
	enum { OPT_FORMATIONS = 1, OPT_PROTECT = 2, OPT_COVERAGE = 3, OPT_ALERT = 4, OPT_ODDS = 5, OPT_CLOSE = 100 };

	struct ToggleRow { Int id; const wchar_t *label; Bool *value; };
}

class AssistOptionsDialog : public AssistPanel
{
public:
	AssistOptionsDialog() : AssistPanel( "AssistOptions" )
	{
		setManual( TRUE );
		m_fillAlpha = 252;
		m_title = L"Player assists";
		addButton( OPT_FORMATIONS, 0, L"Formations: picker, hotkeys, drag to aim", nullptr );
		addButton( OPT_PROTECT, 0, L"Protect: units guard other units, buildings and groups", nullptr );
		addButton( OPT_COVERAGE, 0, L"Defence coverage view: range rings and gaps in the base edge", nullptr );
		addButton( OPT_ODDS, 0, L"Odds meter: point at an enemy group to see who would win", nullptr );
		addButton( OPT_ALERT, 0, L"Base under attack: one click sends idle army units to defend", nullptr );
		addButton( 200, 0, L"All assists are off until switched on here.", nullptr );
		addButton( 201, 0, L"Those that give orders also need \"Player assists allowed\" in the match setup.", nullptr );
		addButton( OPT_CLOSE, 0, L"Close", nullptr );
		find( 200 )->m_flat = TRUE;
		find( 200 )->m_enabled = FALSE;
		find( 201 )->m_flat = TRUE;
		find( 201 )->m_enabled = FALSE;
	}

	void open()
	{
		if (!exists())
			create();
		if (!exists())
			return;
		refreshValues();
		show( TRUE );
		TheWindowManager->winSetModal( window() );
		window()->winBringToTop();
	}

	void closeDialog()
	{
		if (!exists())
			return;
		TheWindowManager->winUnsetModal( window() );
		show( FALSE );
	}

	void refreshValues()
	{
		find( OPT_FORMATIONS )->m_on = TheAssistOptions.m_formations;
		find( OPT_PROTECT )->m_on = TheAssistOptions.m_protect;
		find( OPT_COVERAGE )->m_on = TheAssistOptions.m_coverage;
		find( OPT_ALERT )->m_on = TheAssistOptions.m_baseAlert;
		find( OPT_ODDS )->m_on = TheAssistOptions.m_odds;
	}

	virtual Bool refresh() override { return TRUE; }

	virtual void place( Int *x, Int *y, Int *w, Int *h ) override
	{
		const Int rowH = AssistUI::px( 26 );
		const Int gap = AssistUI::px( 4 );
		const Int titleH = AssistUI::px( 14 );
		*w = AssistUI::px( 460 );
		Int yy = titleH + gap + AssistUI::px( 6 );
		for (size_t i = 0; i < m_buttons.size(); ++i)
		{
			AssistButton &b = m_buttons[i];
			if (b.m_id == OPT_CLOSE)
			{
				yy += AssistUI::px( 10 );
				b.m_w = AssistUI::px( 140 );
				b.m_h = AssistUI::px( 30 );
				b.m_x = (*w - b.m_w) / 2;
				b.m_y = yy;
				yy += b.m_h + gap;
				continue;
			}
			b.m_x = AssistUI::px( 8 );
			b.m_w = *w - 2 * AssistUI::px( 8 );
			b.m_h = b.m_flat ? AssistUI::px( 14 ) : rowH;
			b.m_y = yy;
			yy += b.m_h + gap;
		}
		*h = yy + AssistUI::px( 4 );
		*x = ((Int)TheDisplay->getWidth() - *w) / 2;
		*y = ((Int)TheDisplay->getHeight() - *h) / 3;
	}

	virtual void clicked( Int id ) override
	{
		switch (id)
		{
			case OPT_FORMATIONS: TheAssistOptions.m_formations = !TheAssistOptions.m_formations; break;
			case OPT_PROTECT: TheAssistOptions.m_protect = !TheAssistOptions.m_protect; break;
			case OPT_COVERAGE: TheAssistOptions.m_coverage = !TheAssistOptions.m_coverage; break;
			case OPT_ALERT: TheAssistOptions.m_baseAlert = !TheAssistOptions.m_baseAlert; break;
			case OPT_ODDS: TheAssistOptions.m_odds = !TheAssistOptions.m_odds; break;
			case OPT_CLOSE: closeDialog(); return;
			default: return;
		}
		TheAssistOptions.save();
		refreshValues();
	}

	virtual void drawGlyph( const AssistButton &b, Int x, Int y, Color color ) override
	{
		const Int h = b.m_h;
		if (b.m_id == OPT_CLOSE || b.m_flat)
		{
			AssistUI::text( b.m_label, x + b.m_x, y + b.m_y + (b.m_flat ? 0 : (h - AssistUI::px( 12 )) / 2), b.m_flat ? GameMakeColor( 170, 185, 205, 255 ) : color, b.m_flat ? 8 : 11, !b.m_flat, b.m_w );
			return;
		}
		// a check box and its label
		const Int boxSize = h - AssistUI::px( 10 );
		const Int bx = x + b.m_x + AssistUI::px( 6 );
		const Int by = y + b.m_y + (h - boxSize) / 2;
		AssistUI::box( bx, by, boxSize, boxSize, GameMakeColor( 10, 14, 20, 255 ), GameMakeColor( 150, 170, 200, 255 ) );
		if (b.m_on)
		{
			TheDisplay->drawLine( bx + 3, by + boxSize / 2, bx + boxSize / 2 - 1, by + boxSize - 4, 2.0f, GameMakeColor( 120, 255, 150, 255 ) );
			TheDisplay->drawLine( bx + boxSize / 2 - 1, by + boxSize - 4, bx + boxSize - 3, by + 3, 2.0f, GameMakeColor( 120, 255, 150, 255 ) );
		}
		AssistUI::text( b.m_label, bx + boxSize + AssistUI::px( 8 ), y + b.m_y + (h - AssistUI::px( 12 )) / 2, color, 10 );
	}
};

static AssistOptionsDialog *s_optionsDialog = nullptr;

//-------------------------------------------------------------------------------------------------
void AssistUI::openOptionsDialog()
{
	if (s_optionsDialog == nullptr)
		s_optionsDialog = new AssistOptionsDialog;
	s_optionsDialog->open();
	ASSIST_DEBUG(( "ASSIST options dialog opened, window %s", s_optionsDialog->exists() ? "exists" : "missing" ));
}

//-------------------------------------------------------------------------------------------------
NameKeyType AssistUI::optionsButtonId( const char *layoutName )
{
	AsciiString name;
	name.format( "%s:ButtonAssistOptions", layoutName );
	return TheNameKeyGenerator->nameToKey( name );
}

//-------------------------------------------------------------------------------------------------
void AssistUI::setupOptionsButton( const char *layoutName, const char *likeButton )
{
	if (TheWindowManager == nullptr)
		return;
	const NameKeyType id = optionsButtonId( layoutName );
	if (TheWindowManager->winGetWindowFromId( nullptr, id ))
		return;

	AsciiString likeName;
	likeName.format( "%s:%s", layoutName, likeButton );
	GameWindow *like = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( likeName ) );
	if (like == nullptr)
		return;

	WinInstanceData inst = *like->winGetInstanceData();
	inst.m_id = id;
	Int x, y, w, h;
	like->winGetPosition( &x, &y );
	like->winGetSize( &w, &h );
	GameWindow *button = TheWindowManager->gogoGadgetPushButton( like->winGetParent(), like->winGetStatus(),
		x, y + h + 4, w, h, &inst, like->winGetFont(), FALSE );
	if (button)
	{
		button->winSetWindowId( id );
		GadgetButtonSetText( button, UnicodeString( L"Player assists..." ) );
	}
	ASSIST_DEBUG(( "ASSIST options button %s", button ? "made" : "not made" ));
}

//=================================================================================================
// the match setting on the setup screens
//=================================================================================================

NameKeyType AssistUI::setupCheckboxId( const char *layoutName )
{
	AsciiString name;
	name.format( "%s:CheckboxPlayerAssists", layoutName );
	return TheNameKeyGenerator->nameToKey( name );
}

//-------------------------------------------------------------------------------------------------
GameWindow *AssistUI::setupCheckbox( GameWindow *parent, const char *layoutName, GameWindow *like )
{
	if (TheWindowManager == nullptr)
		return nullptr;
	const NameKeyType id = setupCheckboxId( layoutName );
	GameWindow *existing = TheWindowManager->winGetWindowFromId( parent, id );
	if (existing)
		return existing;
	if (like == nullptr)
		return nullptr;

	// a copy of the "limit superweapons" check box, one row lower
	WinInstanceData inst = *like->winGetInstanceData();
	inst.m_id = id;
	Int x, y, w, h;
	like->winGetPosition( &x, &y );
	like->winGetSize( &w, &h );
	GameWindow *box = TheWindowManager->gogoGadgetCheckbox( like->winGetParent() ? like->winGetParent() : parent, like->winGetStatus(),
		x, y + h + 2, w, h, &inst, like->winGetFont(), FALSE );
	if (box)
	{
		box->winSetWindowId( id );
		GadgetCheckBoxSetText( box, UnicodeString( L"Player assists allowed" ) );
	}
	return box;
}

//=================================================================================================
// the translator: hotkeys and the drag
//=================================================================================================

GameMessageDisposition AssistTranslator::translateGameMessage( const GameMessage *msg )
{
	return AssistUI::translate( msg );
}

//-------------------------------------------------------------------------------------------------
static void sendFormation( Int type )
{
	GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_FORMATION );
	m->appendIntegerArgument( type );
}

//-------------------------------------------------------------------------------------------------
GameMessageDisposition AssistUI::translate( const GameMessage *msg )
{
	const GameMessage::Type t = msg->getType();
	if (t != GameMessage::MSG_RAW_MOUSE_POSITION)
		invalidateSelection();	// the selection may have changed since the last frame

	if (AssistProtectUI::translate( msg ) || AssistCoverageUI::translate( msg ) || AssistAlertUI::translate( msg ) || AssistOddsUI::translate( msg ))
		return DESTROY_MESSAGE;

	// ---- hotkeys -----------------------------------------------------------------------------
	if (t >= GameMessage::MSG_META_ASSIST_FORM_NONE && t <= GameMessage::MSG_META_ASSIST_FORM_CYCLE)
	{
		if (!TheAssistOptions.m_formations || !active() || selection().m_ids.size() < 1)
			return KEEP_MESSAGE;
		if (t == GameMessage::MSG_META_ASSIST_FORM_CYCLE)
		{
			Int next = selection().m_formation + 1;
			if (next >= AFORM_COUNT)
				next = AFORM_NONE;
			sendFormation( next );
		}
		else
			sendFormation( (Int)(t - GameMessage::MSG_META_ASSIST_FORM_NONE) );
		return DESTROY_MESSAGE;
	}

	// ---- the drag that aims a formation --------------------------------------------------------
	switch (t)
	{
		case GameMessage::MSG_RAW_MOUSE_POSITION:
		{
			s_aimPixel = msg->getArgument( 0 )->pixel;
			if (s_aimEligible && !s_aimDragging)
			{
				const Int dx = s_aimPixel.x - s_aimStartPixel.x;
				const Int dy = s_aimPixel.y - s_aimStartPixel.y;
				if (dx * dx + dy * dy > px( 12 ) * px( 12 ))
					s_aimDragging = TRUE;
			}
			return KEEP_MESSAGE;
		}

		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
		{
			s_aimEligible = FALSE;
			s_aimDragging = FALSE;
			if (!TheAssistOptions.m_formations || !active() || TheTacticalView == nullptr)
				return KEEP_MESSAGE;
			const Selection &sel = selection();
			if (sel.m_ids.size() < 2 || sel.m_formation <= AFORM_NONE || sel.m_formation >= AFORM_KEEP)
				return KEEP_MESSAGE;
			if (TheInGameUI->getGUICommand() != nullptr || TheInGameUI->getPendingPlaceSourceObjectID() != INVALID_ID)
				return KEEP_MESSAGE;
			s_aimStartPixel = msg->getArgument( 0 )->pixel;
			s_aimPixel = s_aimStartPixel;
			s_aimStartValid = TheTacticalView->screenToTerrain( &s_aimStartPixel, &s_aimStart );
			s_aimEligible = s_aimStartValid;
			return KEEP_MESSAGE;
		}

		case GameMessage::MSG_RAW_MOUSE_RIGHT_BUTTON_UP:
		{
			const Bool wasDragging = s_aimEligible && s_aimDragging;
			const Bool eligible = s_aimEligible;
			s_aimEligible = FALSE;
			s_aimDragging = FALSE;
			if (!eligible)
				return KEEP_MESSAGE;
			if (!wasDragging)
				return KEEP_MESSAGE;

			Coord3D end;
			const ICoord2D up = msg->getArgument( 0 )->pixel;
			if (!TheTacticalView->screenToTerrain( &up, &end ) || !s_aimStartValid)
				return DESTROY_MESSAGE;

			const Selection &sel = selection();
			if (sel.m_formation > AFORM_NONE && sel.m_formation < AFORM_KEEP)
			{
				GameMessage *m = TheMessageStream->appendMessage( GameMessage::MSG_ASSIST_FORMATION_MOVE );
				m->appendIntegerArgument( sel.m_formation );
				m->appendLocationArgument( s_aimStart );
				m->appendLocationArgument( end );
				m->appendBooleanArgument( FALSE );
			}
			return DESTROY_MESSAGE;
		}

		default:
			break;
	}
	return KEEP_MESSAGE;
}
