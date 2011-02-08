/****************************************************/
/*          block_libedit.cpp                       */
/****************************************************/

#include "fctsys.h"
#include "gr_basic.h"
#include "common.h"
#include "class_drawpanel.h"
#include "confirm.h"
#include "block_commande.h"

#include "general.h"
#include "class_library.h"
#include "protos.h"
#include "libeditframe.h"


static void DrawMovingBlockOutlines( EDA_DRAW_PANEL* aPanel, wxDC* aDC, const wxPoint& aPosition,
                                     bool aErase );


/*
 * Return the block command (BLOCK_MOVE, BLOCK_COPY...) corresponding to
 *  the key (ALT, SHIFT ALT ..)
 */
int LIB_EDIT_FRAME::ReturnBlockCommand( int key )
{
    int cmd;

    switch( key )
    {
    default:
        cmd = key & 0x255;
        break;

    case - 1:
        cmd = BLOCK_PRESELECT_MOVE;
        break;

    case 0:
        cmd = BLOCK_MOVE;
        break;

    case GR_KB_ALT:
    case GR_KB_SHIFT:
        cmd = BLOCK_COPY;
        break;

    case GR_KB_SHIFTCTRL:
        cmd = BLOCK_DELETE;
        break;

    case GR_KB_CTRL:
        cmd = BLOCK_MIRROR_Y;
        break;

    case MOUSE_MIDDLE:
        cmd = BLOCK_ZOOM;
        break;
    }

    return cmd;
}


/*
 * Command BLOCK END (end of block sizing)
 *  return :
 *  0 if command finished (zoom, delete ...)
 *  1 if HandleBlockPlace must follow (items found, and a block place
 * command must follow)
 */
bool LIB_EDIT_FRAME::HandleBlockEnd( wxDC* DC )
{
    int ItemCount = 0;
    int nextCmd = false;
    wxPoint pt;

    if( GetScreen()->m_BlockLocate.GetCount() )
    {
        BlockState state     = GetScreen()->m_BlockLocate.m_State;
        CmdBlockType command = GetScreen()->m_BlockLocate.m_Command;
        DrawPanel->ForceCloseManageCurseur( DrawPanel, DC );
        GetScreen()->m_BlockLocate.m_State   = state;
        GetScreen()->m_BlockLocate.m_Command = command;
        DrawPanel->ManageCurseur = DrawAndSizingBlockOutlines;
        DrawPanel->ForceCloseManageCurseur = AbortBlockCurrentCommand;
        GetScreen()->m_Curseur.x = GetScreen()->m_BlockLocate.GetRight();
        GetScreen()->m_Curseur.y = GetScreen()->m_BlockLocate.GetBottom();
        DrawPanel->MouseToCursorSchema();
    }

    switch( GetScreen()->m_BlockLocate.m_Command )
    {
    case  BLOCK_IDLE:
        DisplayError( this, wxT( "Error in HandleBlockPLace" ) );
        break;

    case BLOCK_DRAG:        /* Drag */
    case BLOCK_MOVE:        /* Move */
    case BLOCK_COPY:        /* Copy */
        if ( m_component )
            ItemCount = m_component->SelectItems( GetScreen()->m_BlockLocate,
                                                  m_unit, m_convert,
                                                  g_EditPinByPinIsOn );
        if( ItemCount )
        {
            nextCmd = true;
            if( DrawPanel->ManageCurseur != NULL )
            {
                DrawPanel->ManageCurseur( DrawPanel, DC, wxDefaultPosition, false );
                DrawPanel->ManageCurseur = DrawMovingBlockOutlines;
                DrawPanel->ManageCurseur( DrawPanel, DC, wxDefaultPosition, false );
            }
            GetScreen()->m_BlockLocate.m_State = STATE_BLOCK_MOVE;
            DrawPanel->Refresh( true );
        }
        break;

    case BLOCK_PRESELECT_MOVE:     /* Move with preselection list*/
        nextCmd = true;
        DrawPanel->ManageCurseur = DrawMovingBlockOutlines;
        GetScreen()->m_BlockLocate.m_State = STATE_BLOCK_MOVE;
        break;

    case BLOCK_DELETE:     /* Delete */
        if ( m_component )
            ItemCount = m_component->SelectItems( GetScreen()->m_BlockLocate,
                                                  m_unit, m_convert,
                                                  g_EditPinByPinIsOn );
        if( ItemCount )
            SaveCopyInUndoList( m_component );
        if ( m_component )
        {
            m_component->DeleteSelectedItems();
            OnModify();
        }
        break;

    case BLOCK_SAVE:     /* Save */
    case BLOCK_PASTE:
    case BLOCK_ROTATE:
    case BLOCK_MIRROR_X:
    case BLOCK_FLIP:
        break;


    case BLOCK_MIRROR_Y:
        if ( m_component )
            ItemCount = m_component->SelectItems( GetScreen()->m_BlockLocate,
                                                  m_unit, m_convert,
                                                  g_EditPinByPinIsOn );
        if( ItemCount )
            SaveCopyInUndoList( m_component );
        pt = GetScreen()->m_BlockLocate.Centre();
        pt.y *= -1;
        if ( m_component )
        {
            OnModify();
            m_component->MirrorSelectedItemsH( pt );
        }
        break;

    case BLOCK_ZOOM:     /* Window Zoom */
        Window_Zoom( GetScreen()->m_BlockLocate );
        break;

    case BLOCK_ABORT:
        break;

    case BLOCK_SELECT_ITEMS_ONLY:
        break;
    }

    if( ! nextCmd )
    {
        if( GetScreen()->m_BlockLocate.m_Command != BLOCK_SELECT_ITEMS_ONLY )
            if ( m_component )
                m_component->ClearSelectedItems();

        GetScreen()->m_BlockLocate.m_Flags   = 0;
        GetScreen()->m_BlockLocate.m_State   = STATE_NO_BLOCK;
        GetScreen()->m_BlockLocate.m_Command = BLOCK_IDLE;
        DrawPanel->ManageCurseur = NULL;
        DrawPanel->ForceCloseManageCurseur = NULL;
        GetScreen()->SetCurItem( NULL );
        SetToolID( m_ID_current_state, DrawPanel->GetDefaultCursor(), wxEmptyString );
        DrawPanel->Refresh( true );
    }


    return nextCmd;
}


/*
 * Routine to handle the BLOCK PLACE command
 *  Last routine for block operation for:
 *  - block move & drag
 *  - block copy & paste
 */
void LIB_EDIT_FRAME::HandleBlockPlace( wxDC* DC )
{
    bool err = false;
    wxPoint pt;

    if( DrawPanel->ManageCurseur == NULL )
    {
        err = true;
        DisplayError( this, wxT( "HandleBlockPLace : ManageCurseur = NULL" ) );
    }

    GetScreen()->m_BlockLocate.m_State = STATE_BLOCK_STOP;

    switch( GetScreen()->m_BlockLocate.m_Command )
    {
    case  BLOCK_IDLE:
        err = true;
        break;

    case BLOCK_DRAG:                /* Drag */
    case BLOCK_MOVE:                /* Move */
    case BLOCK_PRESELECT_MOVE:      /* Move with preselection list*/
        GetScreen()->m_BlockLocate.ClearItemsList();
        if ( m_component )
            SaveCopyInUndoList( m_component );
        pt = GetScreen()->m_BlockLocate.m_MoveVector;
        pt.y *= -1;
        if ( m_component )
            m_component->MoveSelectedItems( pt );
        DrawPanel->Refresh( true );
        break;

    case BLOCK_COPY:     /* Copy */
        GetScreen()->m_BlockLocate.ClearItemsList();
        if ( m_component )
            SaveCopyInUndoList( m_component );
        pt = GetScreen()->m_BlockLocate.m_MoveVector;
        pt.y *= -1;
        if ( m_component )
            m_component->CopySelectedItems( pt );
        break;

    case BLOCK_PASTE:     /* Paste (recopy the last block saved) */
        GetScreen()->m_BlockLocate.ClearItemsList();
        break;

    case BLOCK_MIRROR_Y:      /* Invert by popup menu, from block move */
        if ( m_component )
            SaveCopyInUndoList( m_component );
        pt = GetScreen()->m_BlockLocate.Centre();
        pt.y *= -1;
        if ( m_component )
            m_component->MirrorSelectedItemsH( pt );
        break;

    case BLOCK_ZOOM:        // Handled by HandleBlockEnd
    case BLOCK_DELETE:
    case BLOCK_SAVE:
    case BLOCK_ROTATE:
    case BLOCK_ABORT:
    default:
        break;
    }

    OnModify();

    DrawPanel->ManageCurseur             = NULL;
    DrawPanel->ForceCloseManageCurseur   = NULL;
    GetScreen()->m_BlockLocate.m_Flags   = 0;
    GetScreen()->m_BlockLocate.m_State   = STATE_NO_BLOCK;
    GetScreen()->m_BlockLocate.m_Command = BLOCK_IDLE;
    GetScreen()->SetCurItem( NULL );
    DrawPanel->Refresh( true );

    SetToolID( m_ID_current_state, DrawPanel->GetDefaultCursor(), wxEmptyString );
}


/*
 * Traces the outline of the search block structures
 * The entire block follows the cursor
 */
void DrawMovingBlockOutlines( EDA_DRAW_PANEL* aPanel, wxDC* aDC, const wxPoint& aPosition,
                              bool aErase )
{
    BLOCK_SELECTOR* PtBlock;
    BASE_SCREEN* screen = aPanel->GetScreen();
    wxPoint move_offset;
    PtBlock = &screen->m_BlockLocate;

    LIB_EDIT_FRAME* parent = ( LIB_EDIT_FRAME* ) aPanel->GetParent();
    wxASSERT( parent != NULL );

    LIB_COMPONENT* component = parent->GetComponent();

    if( component == NULL )
        return;

    int unit = parent->GetUnit();
    int convert = parent->GetConvert();

    if( aErase )
    {
        PtBlock->Draw( aPanel, aDC, PtBlock->m_MoveVector, g_XorMode, PtBlock->m_Color );

        component->Draw( aPanel, aDC, PtBlock->m_MoveVector, unit, convert,
                         g_XorMode, -1, DefaultTransform, true, true, true );
    }

    /* Repaint new view */
    PtBlock->m_MoveVector.x = screen->m_Curseur.x - PtBlock->m_BlockLastCursorPosition.x;
    PtBlock->m_MoveVector.y = screen->m_Curseur.y - PtBlock->m_BlockLastCursorPosition.y;

    GRSetDrawMode( aDC, g_XorMode );
    PtBlock->Draw( aPanel, aDC, PtBlock->m_MoveVector, g_XorMode, PtBlock->m_Color );

    component->Draw( aPanel, aDC, PtBlock->m_MoveVector, unit, convert,
                     g_XorMode, -1, DefaultTransform, true, true, true );
}
