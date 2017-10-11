/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2017 CERN
 * @author Alejandro García Montoro <alejandro.garciamontoro@gmail.com>
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 * @author Russell Oliver <roliver8143@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <properties.h>
#include <kiway.h>

#include <wx/filename.h>
#include <memory>
#include <string>
#include <unordered_map>

#include <sch_junction.h>
#include <sch_sheet.h>
#include <schframe.h>
#include <template_fieldnames.h>
#include <wildcards_and_files_ext.h>
#include <class_sch_screen.h>
#include <class_library.h>
#include <class_libentry.h>
#include <lib_draw_item.h>
#include <sch_component.h>
#include <sch_sheet_path.h>
#include <lib_arc.h>
#include <lib_circle.h>
#include <lib_rectangle.h>
#include <lib_polyline.h>
#include <lib_pin.h>
#include <lib_text.h>
#include <sch_text.h>
#include <drawtxt.h>
#include <sch_marker.h>
#include <sch_bus_entry.h>
#include <eagle_parser.h>
#include <sch_eagle_plugin.h>


using std::string;


// Eagle schematic axes are aligned with x increasing left to right and Y increasing bottom to top
// Kicad schematic axes are aligned with x increasing left to right and Y increasing top to bottom.

using namespace std;

/**
 * Provides an easy access to the children of an XML node via their names.
 * @param aCurrentNode is a pointer to a wxXmlNode, whose children will be mapped.
 * @param aName the name of the specific child names to be counted.
 * @return number of children with the give node name.
 */
static int countChildren( wxXmlNode* aCurrentNode, const std::string& aName )
{
    // Map node_name -> node_pointer
    int count = 0;

    // Loop through all children counting them if they match the given name
    aCurrentNode = aCurrentNode->GetChildren();

    while( aCurrentNode )
    {
        if( aCurrentNode->GetName().ToStdString() == aName )
            count++;

        // Get next child
        aCurrentNode = aCurrentNode->GetNext();
    }

    return count;
}


void SCH_EAGLE_PLUGIN::loadLayerDefs( wxXmlNode* aLayers )
{
    std::vector<ELAYER> eagleLayers;

    // Get the first layer and iterate
    wxXmlNode* layerNode = aLayers->GetChildren();

    while( layerNode )
    {
        ELAYER elayer( layerNode );
        eagleLayers.push_back( elayer );

        layerNode = layerNode->GetNext();
    }

    // match layers based on their names
    for( const auto& elayer : eagleLayers )
    {
        /**
         * Layers in Kicad schematics are not actually layers, but abstract groups mainly used to
         * decide item colours.
         *
         * <layers>
         *     <layer number="90" name="Modules" color="5" fill="1" visible="yes" active="yes"/>
         *     <layer number="91" name="Nets" color="2" fill="1" visible="yes" active="yes"/>
         *     <layer number="92" name="Busses" color="1" fill="1" visible="yes" active="yes"/>
         *     <layer number="93" name="Pins" color="2" fill="1" visible="no" active="yes"/>
         *     <layer number="94" name="Symbols" color="4" fill="1" visible="yes" active="yes"/>
         *     <layer number="95" name="Names" color="7" fill="1" visible="yes" active="yes"/>
         *     <layer number="96" name="Values" color="7" fill="1" visible="yes" active="yes"/>
         *     <layer number="97" name="Info" color="7" fill="1" visible="yes" active="yes"/>
         *     <layer number="98" name="Guide" color="6" fill="1" visible="yes" active="yes"/>
         * </layers>
         */


        if( elayer.name == "Nets" )
        {
            m_layerMap[elayer.number] = LAYER_WIRE;
        }
        else if( elayer.name == "Info" || elayer.name == "Guide" )
        {
            m_layerMap[elayer.number] = LAYER_NOTES;
        }
        else if( elayer.name == "Busses" )
        {
            m_layerMap[elayer.number] = LAYER_BUS;
        }
    }
}


SCH_LAYER_ID SCH_EAGLE_PLUGIN::kiCadLayer( int aEagleLayer )
{
    auto it = m_layerMap.find( aEagleLayer );
    return it == m_layerMap.end() ? LAYER_NOTES : it->second;
}


// Return the kicad component orientation based on eagle rotation degrees.
static COMPONENT_ORIENTATION_T kiCadComponentRotation( float eagleDegrees )
{
    int roti = int( eagleDegrees );

    switch( roti )
    {
    default:
        wxASSERT_MSG( false, wxString::Format( "Unhandled orientation (%d degrees)", roti ) );

    case 0:
        return CMP_ORIENT_0;

    case 90:
        return CMP_ORIENT_90;

    case 180:
        return CMP_ORIENT_180;

    case 270:
        return CMP_ORIENT_270;
    }

    return CMP_ORIENT_0;
}


// Calculate text alignment based on the given Eagle text alignment parameters.
static void eagleToKicadAlignment( EDA_TEXT* aText, int aEagleAlignment,
        int aRelDegress, bool aMirror, bool aSpin, int aAbsDegress )
{
    int align = aEagleAlignment;

    if( aRelDegress == 90 )
    {
        aText->SetTextAngle( 900 );
    }
    else if( aRelDegress == 180 )
        align = -align;
    else if( aRelDegress == 270 )
    {
        aText->SetTextAngle( 900 );
        align = -align;
    }

    if( aMirror == true )
    {
        if( aAbsDegress == 90 || aAbsDegress == 270 )
        {
            if( align == ETEXT::BOTTOM_RIGHT )
                align = ETEXT::TOP_RIGHT;
            else if( align == ETEXT::BOTTOM_LEFT )
                align = ETEXT::TOP_LEFT;
            else if( align == ETEXT::TOP_LEFT )
                align = ETEXT::BOTTOM_LEFT;
            else if( align == ETEXT::TOP_RIGHT )
                align = ETEXT::BOTTOM_RIGHT;
        }
        else if( aAbsDegress == 0 || aAbsDegress == 180 )
        {
            if( align == ETEXT::BOTTOM_RIGHT )
                align = ETEXT::BOTTOM_LEFT;
            else if( align == ETEXT::BOTTOM_LEFT )
                align = ETEXT::BOTTOM_RIGHT;
            else if( align == ETEXT::TOP_LEFT )
                align = ETEXT::TOP_RIGHT;
            else if( align == ETEXT::TOP_RIGHT )
                align = ETEXT::TOP_LEFT;
            else if( align == ETEXT::CENTER_LEFT )
                align = ETEXT::CENTER_RIGHT;
            else if( align == ETEXT::CENTER_RIGHT )
                align = ETEXT::CENTER_LEFT;
        }
    }

    switch( align )
    {
    case ETEXT::CENTER:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_CENTER );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_CENTER );
        break;

    case ETEXT::CENTER_LEFT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_CENTER );
        break;

    case ETEXT::CENTER_RIGHT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_RIGHT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_CENTER );
        break;

    case ETEXT::TOP_CENTER:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_CENTER );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_TOP );
        break;

    case ETEXT::TOP_LEFT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_TOP );
        break;

    case ETEXT::TOP_RIGHT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_RIGHT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_TOP );
        break;

    case ETEXT::BOTTOM_CENTER:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_CENTER );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_BOTTOM );
        break;

    case ETEXT::BOTTOM_LEFT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_LEFT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_BOTTOM );
        break;

    case ETEXT::BOTTOM_RIGHT:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_RIGHT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_BOTTOM );
        break;

    default:
        aText->SetHorizJustify( GR_TEXT_HJUSTIFY_RIGHT );
        aText->SetVertJustify( GR_TEXT_VJUSTIFY_BOTTOM );
    }
}


SCH_EAGLE_PLUGIN::SCH_EAGLE_PLUGIN()
{
    m_rootSheet = nullptr;
}


SCH_EAGLE_PLUGIN::~SCH_EAGLE_PLUGIN()
{
}


const wxString SCH_EAGLE_PLUGIN::GetName() const
{
    return wxT( "EAGLE" );
}


const wxString SCH_EAGLE_PLUGIN::GetFileExtension() const
{
    return wxT( "sch" );
}


int SCH_EAGLE_PLUGIN::GetModifyHash() const
{
    return 0;
}


SCH_SHEET* SCH_EAGLE_PLUGIN::Load( const wxString& aFileName, KIWAY* aKiway,
        SCH_SHEET* aAppendToMe, const PROPERTIES* aProperties )
{
    wxASSERT( !aFileName || aKiway != NULL );
    LOCALE_IO toggle;     // toggles on, then off, the C locale.

    // Load the document
    wxXmlDocument xmlDocument;

    m_filename = aFileName;
    m_kiway = aKiway;

    if( !xmlDocument.Load( m_filename.GetFullPath() ) )
        THROW_IO_ERROR( wxString::Format( _( "Unable to read file '%s'" ),
                        m_filename.GetFullPath() ) );

    // Delete on exception, if I own m_rootSheet, according to aAppendToMe
    unique_ptr<SCH_SHEET> deleter( aAppendToMe ? nullptr : m_rootSheet );

    if( aAppendToMe )
    {
        m_rootSheet = aAppendToMe->GetRootSheet();
    }
    else
    {
        m_rootSheet = new SCH_SHEET();
        m_rootSheet->SetFileName( aFileName );
    }

    if( !m_rootSheet->GetScreen() )
    {
        SCH_SCREEN* screen = new SCH_SCREEN( aKiway );
        screen->SetFileName( aFileName );
        m_rootSheet->SetScreen( screen );
    }

    // Create a schematic symbol library
    wxString projectpath = m_kiway->Prj().GetProjectPath();
    wxFileName libfn = m_kiway->Prj().AbsolutePath( m_kiway->Prj().GetProjectName() );

    libfn.SetExt( SchematicLibraryFileExtension );
    std::unique_ptr<PART_LIB> lib( new PART_LIB( LIBRARY_TYPE_EESCHEMA, libfn.GetFullPath() ) );
    lib->EnableBuffering();

    if( !wxFileName::FileExists( lib->GetFullFileName() ) )
    {
        lib->Create();
    }

    m_partlib = lib.release();

    // Retrieve the root as current node
    wxXmlNode* currentNode = xmlDocument.GetRoot();

    // If the attribute is found, store the Eagle version;
    // otherwise, store the dummy "0.0" version.
    m_version = currentNode->GetAttribute( "version", "0.0" );

    // Map all children into a readable dictionary
    NODE_MAP children = MapChildren( currentNode );

    // Load drawing
    loadDrawing( children["drawing"] );

    PART_LIBS* prjLibs = aKiway->Prj().SchLibs();

    // There are two ways to add a new library, the official one that requires creating a file:
    m_partlib->Save( false );
    // prjLibs->AddLibrary( m_partlib->GetFullFileName() );
    // or undocumented one:
    prjLibs->insert( prjLibs->begin(), m_partlib );

    deleter.release();
    return m_rootSheet;
}


void SCH_EAGLE_PLUGIN::loadDrawing( wxXmlNode* aDrawingNode )
{
    // Map all children into a readable dictionary
    NODE_MAP drawingChildren = MapChildren( aDrawingNode );

    // Board nodes should not appear in .sch files
    // wxXmlNode* board = drawingChildren["board"]

    // wxXmlNode* grid = drawingChildren["grid"]

    wxXmlNode* layers = drawingChildren["layers"];

    loadLayerDefs( layers );

    // wxXmlNode* library = drawingChildren["library"]

    // wxXmlNode* settings = drawingChildren["settings"]


    // Load schematic
    loadSchematic( drawingChildren["schematic"] );
}


void SCH_EAGLE_PLUGIN::countNets( wxXmlNode* aSchematicNode )
{
    // Map all children into a readable dictionary
    NODE_MAP schematicChildren = MapChildren( aSchematicNode );
    // Loop through all the sheets
    wxXmlNode* sheetNode = schematicChildren["sheets"]->GetChildren();

    while( sheetNode )
    {
        NODE_MAP sheetChildren = MapChildren( sheetNode );
        // Loop through all nets
        // From the DTD: "Net is an electrical connection in a schematic."
        wxXmlNode* netNode = getChildrenNodes( sheetChildren, "nets" );

        while( netNode )
        {
            std::string netName = netNode->GetAttribute( "name" ).ToStdString();

            if( m_netCounts.count( netName ) )
                m_netCounts[netName] = m_netCounts[netName] + 1;
            else
                m_netCounts[netName] = 1;

            // Get next net
            netNode = netNode->GetNext();
        }

        sheetNode = sheetNode->GetNext();
    }
}


void SCH_EAGLE_PLUGIN::loadSchematic( wxXmlNode* aSchematicNode )
{
    // Map all children into a readable dictionary
    NODE_MAP schematicChildren = MapChildren( aSchematicNode );

    wxXmlNode* partNode = schematicChildren["parts"]->GetChildren();

    while( partNode )
    {
        std::unique_ptr<EPART> epart( new EPART( partNode ) );
        string name = epart->name;
        m_partlist[name] = std::move( epart );
        partNode = partNode->GetNext();
    }


    // Loop through all the libraries
    wxXmlNode* libraryNode = schematicChildren["libraries"]->GetChildren();

    while( libraryNode )
    {
        // Read the library name
        wxString libName = libraryNode->GetAttribute( "name" );

        EAGLE_LIBRARY* elib = &m_eagleLibs[libName.ToStdString()];
        elib->name = libName.ToStdString();

        loadLibrary( libraryNode, &m_eagleLibs[libName.ToStdString()] );

        libraryNode = libraryNode->GetNext();
    }

    // find all nets and count how many sheets they appear on.
    // local labels will be used for nets found only on that sheet.
    countNets( aSchematicNode );

    // Loop through all the sheets
    wxXmlNode* sheetNode = schematicChildren["sheets"]->GetChildren();

    int sheet_count = countChildren( schematicChildren["sheets"], "sheet" );

    // If eagle schematic has multiple sheets.

    if( sheet_count > 1 )
    {
        int x, y, i;
        i   = 1;
        x   = 1;
        y   = 1;

        while( sheetNode )
        {
            wxPoint pos = wxPoint( x * 1000, y * 1000 );
            std::unique_ptr<SCH_SHEET> sheet( new SCH_SHEET( pos ) );
            SCH_SCREEN* screen = new SCH_SCREEN( m_kiway );

            sheet->SetTimeStamp( GetNewTimeStamp() - i );    // minus the sheet index to make it unique.
            sheet->SetParent( m_rootSheet->GetScreen() );
            sheet->SetScreen( screen );

            m_currentSheet = sheet.get();
            sheet->GetScreen()->SetFileName( sheet->GetFileName() );
            m_rootSheet->GetScreen()->Append( sheet.release() );
            loadSheet( sheetNode, i );


            sheetNode = sheetNode->GetNext();
            x += 2;

            if( x > 10 )
            {
                x   = 1;
                y   += 2;
            }

            i++;
        }
    }
    else
    {
        while( sheetNode )
        {
            m_currentSheet = m_rootSheet;
            loadSheet( sheetNode, 0 );
            sheetNode = sheetNode->GetNext();
        }
    }
}


void SCH_EAGLE_PLUGIN::loadSheet( wxXmlNode* aSheetNode, int aSheetIndex )
{
    // Map all children into a readable dictionary
    NODE_MAP sheetChildren = MapChildren( aSheetNode );

    // Get description node
    wxXmlNode* descriptionNode = getChildrenNodes( sheetChildren, "description" );

    wxString des;
    std::string filename;

    if( descriptionNode )
    {
        des = descriptionNode->GetContent();
        m_currentSheet->SetName( des );
        filename = des.ToStdString();
    }
    else
    {
        filename = m_filename.GetName().ToStdString() + "_" + std::to_string( aSheetIndex );
        m_currentSheet->SetName( filename );
    }

    ReplaceIllegalFileNameChars( &filename );
    replace( filename.begin(), filename.end(), ' ', '_' );

    wxString fn = wxString( filename + ".sch" );
    m_currentSheet->SetFileName( fn );
    wxFileName fileName = m_currentSheet->GetFileName();
    m_currentSheet->GetScreen()->SetFileName( fileName.GetFullPath() );

    // Loop through all busses
    // From the DTD: "Buses receive names which determine which signals they include.
    // A bus is a drawing object. It does not create any electrical connections.
    // These are always created by means of the nets and their names."
    wxXmlNode* busNode = getChildrenNodes( sheetChildren, "busses" );

    while( busNode )
    {
        // Get the bus name
        wxString busName = busNode->GetAttribute( "name" );

        // Load segments of this bus
        loadSegments( busNode, busName, wxString() );

        // Get next bus
        busNode = busNode->GetNext();
    }

    // Loop through all nets
    // From the DTD: "Net is an electrical connection in a schematic."
    wxXmlNode* netNode = getChildrenNodes( sheetChildren, "nets" );

    while( netNode )
    {
        // Get the net name and class
        wxString    netName     = netNode->GetAttribute( "name" );
        wxString    netClass    = netNode->GetAttribute( "class" );

        // Load segments of this net
        loadSegments( netNode, netName, netClass );

        // Get next net
        netNode = netNode->GetNext();
    }

    addBusEntries();

    // Loop through all instances
    wxXmlNode* instanceNode = getChildrenNodes( sheetChildren, "instances" );

    while( instanceNode )
    {
        loadInstance( instanceNode );
        instanceNode = instanceNode->GetNext();
    }

    /*  moduleinst is a design block definition and is an EagleCad 8 feature,
     *
     *  // Loop through all moduleinsts
     *  wxXmlNode* moduleinstNode = getChildrenNodes( sheetChildren, "moduleinsts" );
     *
     *  while( moduleinstNode )
     *  {
     *   loadModuleinst( moduleinstNode );
     *   moduleinstNode = moduleinstNode->GetNext();
     *  }
     */

    wxXmlNode* plainNode = getChildrenNodes( sheetChildren, "plain" );

    while( plainNode )
    {
        wxString nodeName = plainNode->GetName();

        if( nodeName == "text" )
        {
            m_currentSheet->GetScreen()->Append( loadPlainText( plainNode ) );
        }
        else if( nodeName == "wire" )
        {
            m_currentSheet->GetScreen()->Append( loadWire( plainNode ) );
        }

        plainNode = plainNode->GetNext();
    }

    // Find the bounding box of the imported items.
    EDA_RECT sheetBoundingBox;

    SCH_ITEM* item = m_currentSheet->GetScreen()->GetDrawItems();
    sheetBoundingBox = item->GetBoundingBox();
    item = item->Next();

    while( item )
    {
        sheetBoundingBox.Merge( item->GetBoundingBox() );
        item = item->Next();
    }

    // Calculate the new sheet size.

    wxSize targetSheetSize = sheetBoundingBox.GetSize();
    targetSheetSize.IncBy( 1500, 1500 );

    // Get current Eeschema sheet size.
    wxSize pageSizeIU   = m_currentSheet->GetScreen()->GetPageSettings().GetSizeIU();
    PAGE_INFO pageInfo  = m_currentSheet->GetScreen()->GetPageSettings();

    // Increase if necessary
    if( pageSizeIU.x<targetSheetSize.x )
        pageInfo.SetWidthMils( targetSheetSize.x );

    if( pageSizeIU.y<targetSheetSize.y )
        pageInfo.SetHeightMils( targetSheetSize.y );

    // Set the new sheet size.
    m_currentSheet->GetScreen()->SetPageSettings( pageInfo );

    pageSizeIU = m_currentSheet->GetScreen()->GetPageSettings().GetSizeIU();
    wxPoint sheetcentre( pageSizeIU.x / 2, pageSizeIU.y / 2 );
    wxPoint itemsCentre = sheetBoundingBox.Centre();

    // round the translation to nearest 100mil to place it on the grid.
    wxPoint translation = sheetcentre - itemsCentre;
    translation.x   = translation.x - translation.x % 100;
    translation.y   = translation.y - translation.y % 100;

    // Translate the items.
    item = m_currentSheet->GetScreen()->GetDrawItems();

    while( item )
    {
        item->SetPosition( item->GetPosition() + translation );
        item->ClearFlags();
        item = item->Next();
    }
}


void SCH_EAGLE_PLUGIN::loadSegments( wxXmlNode* aSegmentsNode, const wxString& netName,
        const wxString& aNetClass )
{
    // Loop through all segments
    wxXmlNode* currentSegment = aSegmentsNode->GetChildren();
    SCH_SCREEN* screen = m_currentSheet->GetScreen();

    int segmentCount = countChildren( aSegmentsNode, "segment" );

    // wxCHECK( screen, [>void<] );
    while( currentSegment )
    {
        bool labelled = false;    // has a label been added to this continously connected segment
        NODE_MAP segmentChildren = MapChildren( currentSegment );

        // Loop through all segment children
        wxXmlNode* segmentAttribute = currentSegment->GetChildren();

        // load wire nodes first
        // label positions will then be tested for an underlying wire, since eagle labels can be seperated from the wire

        DLIST<SCH_LINE> segmentWires;
        segmentWires.SetOwnership( false );

        while( segmentAttribute )
        {
            if( segmentAttribute->GetName() == "wire" )
            {
                segmentWires.Append( loadWire( segmentAttribute ) );
            }

            segmentAttribute = segmentAttribute->GetNext();
        }

        segmentAttribute = currentSegment->GetChildren();

        while( segmentAttribute )
        {
            wxString nodeName = segmentAttribute->GetName();

            if( nodeName == "junction" )
            {
                screen->Append( loadJunction( segmentAttribute ) );
            }
            else if( nodeName == "label" )
            {
                screen->Append( loadLabel( segmentAttribute, netName, segmentWires ) );
                labelled = true;
            }
            else if( nodeName == "pinref" )
            {
                segmentAttribute->GetAttribute( "gate" );   // REQUIRED
                segmentAttribute->GetAttribute( "part" );   // REQUIRED
                segmentAttribute->GetAttribute( "pin" );    // REQUIRED
            }
            else if( nodeName == "wire" )
            {
                // already handled;
            }
            else    // DEFAULT
            {
                // THROW_IO_ERROR( wxString::Format( _( "XML node '%s' unknown" ), nodeName ) );
            }

            // Get next segment attribute
            segmentAttribute = segmentAttribute->GetNext();
        }

        SCH_LINE* wire = segmentWires.begin();

        // Add a small label to the net segment if it hasn't been labelled already
        // this preserves the named net feature of Eagle schematics.
        if( labelled == false && wire != NULL )
        {
            wxString netname = escapeName( netName );

            // Add a global label if the net appears on more than one Eagle sheet
            if( m_netCounts[netName.ToStdString()]>1 )
            {
                std::unique_ptr<SCH_GLOBALLABEL> glabel( new SCH_GLOBALLABEL );
                glabel->SetPosition( wire->MidPoint() );
                glabel->SetText( netname );
                glabel->SetTextSize( wxSize( 10, 10 ) );
                glabel->SetLabelSpinStyle( 0 );
                screen->Append( glabel.release() );
            }
            else if( segmentCount > 1 )
            {
                std::unique_ptr<SCH_LABEL> label( new SCH_LABEL );
                label->SetPosition( wire->MidPoint() );
                label->SetText( netname );
                label->SetTextSize( wxSize( 10, 10 ) );
                label->SetLabelSpinStyle( 0 );
                screen->Append( label.release() );
            }
        }


        SCH_LINE* next_wire;

        while( wire != NULL )
        {
            next_wire = wire->Next();
            screen->Append( wire );
            wire = next_wire;
        }

        currentSegment = currentSegment->GetNext();
    }
}


SCH_LINE* SCH_EAGLE_PLUGIN::loadWire( wxXmlNode* aWireNode )
{
    std::unique_ptr<SCH_LINE> wire( new SCH_LINE );

    auto ewire = EWIRE( aWireNode );

    wire->SetLayer( kiCadLayer( ewire.layer ) );

    wxPoint begin, end;

    begin.x = ewire.x1 * EUNIT_TO_MIL;
    begin.y = -ewire.y1 * EUNIT_TO_MIL;
    end.x   = ewire.x2 * EUNIT_TO_MIL;
    end.y   = -ewire.y2 * EUNIT_TO_MIL;

    wire->SetStartPoint( begin );
    wire->SetEndPoint( end );

    return wire.release();
}


SCH_JUNCTION* SCH_EAGLE_PLUGIN::loadJunction( wxXmlNode* aJunction )
{
    std::unique_ptr<SCH_JUNCTION> junction( new SCH_JUNCTION );

    auto ejunction = EJUNCTION( aJunction );
    wxPoint pos( ejunction.x * EUNIT_TO_MIL, -ejunction.y * EUNIT_TO_MIL );

    junction->SetPosition( pos  );

    return junction.release();
}


SCH_TEXT* SCH_EAGLE_PLUGIN::loadLabel( wxXmlNode* aLabelNode,
        const wxString& aNetName,
        const DLIST<SCH_LINE>& segmentWires )
{
    auto elabel = ELABEL( aLabelNode, aNetName );

    wxPoint elabelpos( elabel.x * EUNIT_TO_MIL, -elabel.y * EUNIT_TO_MIL );

    wxString netname = escapeName( elabel.netname );


    // Determine if the Label is a local and global label based on the number of sheets the net appears on.
    if( m_netCounts[aNetName.ToStdString()]>1 )
    {
        std::unique_ptr<SCH_GLOBALLABEL> glabel( new SCH_GLOBALLABEL );
        glabel->SetPosition( elabelpos );
        glabel->SetText( netname );
        glabel->SetTextSize( wxSize( elabel.size * EUNIT_TO_MIL, elabel.size * EUNIT_TO_MIL ) );

        glabel->SetLabelSpinStyle( 0 );

        if( elabel.rot )
        {
            glabel->SetLabelSpinStyle( int(elabel.rot->degrees / 90) % 4 );

            if( elabel.rot->mirror
                && ( glabel->GetLabelSpinStyle() == 0 || glabel->GetLabelSpinStyle() == 2 ) )
                glabel->SetLabelSpinStyle( (glabel->GetLabelSpinStyle() + 2) % 4 );
        }

        SCH_LINE*   wire;
        SCH_LINE*   next_wire;

        bool    labelOnWire = false;
        auto    glabelPosition = glabel->GetPosition();

        // determine if the segment has been labelled.
        for( wire = segmentWires.begin(); wire; wire = next_wire )
        {
            next_wire = wire->Next();

            if( wire->HitTest( glabelPosition, 0 ) )
            {
                labelOnWire = true;
                break;
            }
        }

        wire = segmentWires.begin();

        // Reposition label if necessary
        if( labelOnWire == false )
        {
            wxPoint newLabelPos = findNearestLinePoint( elabelpos, segmentWires );

            if( wire )
            {
                glabel->SetPosition( newLabelPos );
            }
        }

        return glabel.release();
    }
    else
    {
        std::unique_ptr<SCH_LABEL> label( new SCH_LABEL );
        label->SetPosition( elabelpos );
        label->SetText( netname );
        label->SetTextSize( wxSize( elabel.size * EUNIT_TO_MIL, elabel.size * EUNIT_TO_MIL ) );

        label->SetLabelSpinStyle( 0 );

        if( elabel.rot )
        {
            label->SetLabelSpinStyle( int(elabel.rot->degrees / 90) % 4 );

            if( elabel.rot->mirror
                && ( label->GetLabelSpinStyle() == 0 || label->GetLabelSpinStyle() == 2 ) )
                label->SetLabelSpinStyle( (label->GetLabelSpinStyle() + 2) % 4 );
        }

        SCH_LINE*   wire;
        SCH_LINE*   next_wire;

        bool    labelOnWire = false;
        auto    labelPosition = label->GetPosition();

        for( wire = segmentWires.begin(); wire; wire = next_wire )
        {
            next_wire = wire->Next();

            if( wire->HitTest( labelPosition, 0 ) )
            {
                labelOnWire = true;
                break;
            }
        }

        wire = segmentWires.begin();

        // Reposition label if necessary
        if( labelOnWire == false )
        {
            if( wire )
            {
                wxPoint newLabelPos = findNearestLinePoint( elabelpos, segmentWires );
                label->SetPosition( newLabelPos );
            }
        }

        return label.release();
    }
}


wxPoint SCH_EAGLE_PLUGIN::findNearestLinePoint( const wxPoint& aPoint, const DLIST<SCH_LINE>& aLines )
{
    wxPoint nearestPoint;

    float   mindistance = std::numeric_limits<float>::max();
    float   d;
    SCH_LINE* line = aLines.begin();

    // Find the nearest start, middle or end of a line from the list of lines.
    while( line != NULL )
    {
        auto testpoint = line->GetStartPoint();
        d = sqrt( abs( ( (aPoint.x - testpoint.x) ^ 2 ) + ( (aPoint.y - testpoint.y) ^ 2 ) ) );

        if( d < mindistance )
        {
            mindistance     = d;
            nearestPoint    = testpoint;
        }

        testpoint = line->MidPoint();
        d = sqrt( abs( ( (aPoint.x - testpoint.x) ^ 2 ) + ( (aPoint.y - testpoint.y) ^ 2 ) ) );

        if( d < mindistance )
        {
            mindistance     = d;
            nearestPoint    = testpoint;
        }

        testpoint = line->GetEndPoint();
        d = sqrt( abs( ( (aPoint.x - testpoint.x) ^ 2 ) + ( (aPoint.y - testpoint.y) ^ 2 ) ) );

        if( d < mindistance )
        {
            mindistance     = d;
            nearestPoint    = testpoint;
        }

        line = line->Next();
    }

    return nearestPoint;
}


void SCH_EAGLE_PLUGIN::loadInstance( wxXmlNode* aInstanceNode )
{
    auto einstance = EINSTANCE( aInstanceNode );

    bool smashed = false;

    SCH_SCREEN* screen = m_currentSheet->GetScreen();

    // Find the part in the list for the sheet.
    // Assign the component its value from the part entry
    // Calculate the unit number from the gate entry of the instance
    // Assign the the LIB_ID from deviceset and device names

    EPART* epart = m_partlist[einstance.part].get();

    std::string libraryname = epart->library;
    std::string gatename = epart->deviceset + epart->device + einstance.gate;
    wxString sntemp = wxString( epart->deviceset + epart->device );

    sntemp.Replace( "*", "" );
    std::string symbolname = sntemp.ToStdString();

    int unit = m_eagleLibs[libraryname].GateUnit[gatename];

    std::string package;
    EAGLE_LIBRARY* elib = &m_eagleLibs[libraryname];

    auto p = elib->package.find( symbolname );

    if( p != elib->package.end() )
    {
        package = p->second;
    }

    LIB_ID libId( wxEmptyString, symbolname );

    LIB_PART* part = m_partlib->FindPart( symbolname );

    if( !part )
        return;

    std::unique_ptr<SCH_COMPONENT> component( new SCH_COMPONENT() );
    component->SetLibId( libId );
    component->SetUnit( unit );
    component->SetPosition( wxPoint( einstance.x * EUNIT_TO_MIL, -einstance.y * EUNIT_TO_MIL ) );
    component->GetField( FOOTPRINT )->SetText( wxString( package ) );
    component->SetTimeStamp( EagleModuleTstamp( einstance.part, epart->value ? *epart->value : "",
                    unit ) );

    if( einstance.rot )
    {
        component->SetOrientation( kiCadComponentRotation( einstance.rot->degrees ) );

        if( einstance.rot->mirror )
        {
            component->MirrorY( einstance.x * EUNIT_TO_MIL );
        }
    }


    LIB_FIELDS partFields;
    part->GetFields( partFields );

    for( auto const& field : partFields )
    {
        component->GetField( field.GetId() )->ImportValues( field );
        component->GetField( field.GetId() )->SetTextPos(
                component->GetPosition() + field.GetTextPos() );
    }

    component->GetField( REFERENCE )->SetText( einstance.part );

    SCH_SHEET_PATH sheetpath;
    m_rootSheet->LocatePathOfScreen( screen, &sheetpath );
    wxString current_sheetpath = sheetpath.Path();

    wxString tstamp;
    tstamp.Printf( "%8.8lX", (unsigned long) component->GetTimeStamp() );
    current_sheetpath += tstamp;

    component->AddHierarchicalReference( current_sheetpath, wxString( einstance.part ), unit );

    if( epart->value )
    {
        component->GetField( VALUE )->SetText( *epart->value );
    }
    else
    {
        component->GetField( VALUE )->SetText( symbolname );
    }

    // Set the visibility of fields.
    if( part->GetField( REFERENCE )->IsVisible() )
        component->GetField( REFERENCE )->SetVisible( true );
    else
        component->GetField( REFERENCE )->SetVisible( false );

    if( part->GetField( VALUE )->IsVisible() )
        component->GetField( VALUE )->SetVisible( true );
    else
        component->GetField( VALUE )->SetVisible( false );

    if( einstance.smashed )
    {
        smashed = einstance.smashed.Get();
    }

    bool valueAttributeFound = false;
    bool nameAttributeFound  = false;


    wxXmlNode* attributeNode = aInstanceNode->GetChildren();

    // Parse attributes for the instance
    //
    while( attributeNode )
    {
        if( attributeNode->GetName() == "attribute" )
        {
            auto attr = EATTR( attributeNode );

            SCH_FIELD* field;

            if( attr.name == "NAME" || attr.name == "VALUE" )
            {
                if( attr.name == "NAME" )
                {
                    field = component->GetField( REFERENCE );
                    nameAttributeFound = true;
                }
                else
                {
                    field = component->GetField( VALUE );
                    valueAttributeFound = true;
                }

                field->SetPosition( wxPoint( *attr.x * EUNIT_TO_MIL, *attr.y * -EUNIT_TO_MIL ) );
                int align = attr.align ? *attr.align : ETEXT::BOTTOM_LEFT;
                int absdegrees = attr.rot ? attr.rot->degrees : 0;
                bool mirror = attr.rot ? attr.rot->mirror : false;

                if( einstance.rot && einstance.rot->mirror )
                {
                    mirror = !mirror;
                }


                bool spin = attr.rot ? attr.rot->spin : false;

                if( attr.display == EATTR::Off )
                {
                    field->SetVisible( false );
                }

                int rotation = einstance.rot ? einstance.rot->degrees : 0;
                int reldegrees = ( absdegrees - rotation + 360.0);
                reldegrees %= 360;

                eagleToKicadAlignment( (EDA_TEXT*) field, align, reldegrees, mirror, spin,
                        absdegrees );
            }
        }

        attributeNode = attributeNode->GetNext();
    }

    if( smashed )
    {
        if( !valueAttributeFound )
            component->GetField( VALUE )->SetVisible( false );

        if( !nameAttributeFound )
            component->GetField( REFERENCE )->SetVisible( false );
    }

    component->ClearFlags();

    screen->Append( component.release() );
}


EAGLE_LIBRARY* SCH_EAGLE_PLUGIN::loadLibrary( wxXmlNode* aLibraryNode,
        EAGLE_LIBRARY* aEagleLibrary )
{
    NODE_MAP libraryChildren = MapChildren( aLibraryNode );

    // Loop through the symbols and load each of them
    wxXmlNode* symbolNode = libraryChildren["symbols"]->GetChildren();

    while( symbolNode )
    {
        string symbolName = symbolNode->GetAttribute( "name" ).ToStdString();
        aEagleLibrary->SymbolNodes[symbolName] = symbolNode;
        symbolNode = symbolNode->GetNext();
    }

    // Loop through the devicesets and load each of them
    wxXmlNode* devicesetNode = libraryChildren["devicesets"]->GetChildren();

    while( devicesetNode )
    {
        // Get Device set information
        EDEVICE_SET edeviceset = EDEVICE_SET( devicesetNode );

        wxString prefix = edeviceset.prefix ? edeviceset.prefix.Get() : "";

        NODE_MAP aDeviceSetChildren = MapChildren( devicesetNode );
        wxXmlNode* deviceNode = getChildrenNodes( aDeviceSetChildren, "devices" );

        // For each device in the device set:
        while( deviceNode )
        {
            // Get device information
            EDEVICE edevice = EDEVICE( deviceNode );

            // Create symbol name from deviceset and device names.
            wxString symbolName = wxString( edeviceset.name + edevice.name );
            symbolName.Replace( "*", "" );

            if( edevice.package )
                aEagleLibrary->package[symbolName.ToStdString()] = edevice.package.Get();

            // Create KiCad symbol.
            unique_ptr<LIB_PART> kpart( new LIB_PART( symbolName ) );

            // Process each gate in the deviceset for this device.
            wxXmlNode* gateNode = getChildrenNodes( aDeviceSetChildren, "gates" );
            int gates_count = countChildren( aDeviceSetChildren["gates"], "gate" );
            kpart->SetUnitCount( gates_count );

            LIB_FIELD* reference = kpart->GetField( REFERENCE );

            if(  prefix.length() ==0  )
            {
                reference->SetVisible( false );
            }
            else
            {
                reference->SetText( prefix );
            }

            int gateindex = 1;
            bool ispower = false;

            while( gateNode )
            {
                EGATE egate = EGATE( gateNode );

                aEagleLibrary->GateUnit[edeviceset.name + edevice.name + egate.name] = gateindex;

                ispower = loadSymbol( aEagleLibrary->SymbolNodes[egate.symbol],
                        kpart, &edevice, gateindex, egate.name );

                gateindex++;
                gateNode = gateNode->GetNext();
            }    // gateNode

            kpart->SetUnitCount( gates_count );

            if( gates_count == 1 && ispower )
                kpart->SetPower();

            string name = kpart->GetName().ToStdString();
            m_partlib->AddPart( kpart.get() );
            aEagleLibrary->KiCadSymbols.insert( name, kpart.release() );

            deviceNode = deviceNode->GetNext();
        }    // devicenode

        devicesetNode = devicesetNode->GetNext();
    }    // devicesetNode

    return aEagleLibrary;
}


bool SCH_EAGLE_PLUGIN::loadSymbol( wxXmlNode* aSymbolNode,
        std::unique_ptr<LIB_PART>& aPart,
        EDEVICE* aDevice,
        int aGateNumber,
        string aGateName )
{
    wxString symbolName = aSymbolNode->GetAttribute( "name" );
    std::vector<LIB_ITEM*> items;

    wxXmlNode* currentNode = aSymbolNode->GetChildren();

    bool    foundName   = false;
    bool    foundValue  = false;
    bool    ispower     = false;
    int     pincount    = 0;

    while( currentNode )
    {
        wxString nodeName = currentNode->GetName();

        if( nodeName == "circle" )
        {
            aPart->AddDrawItem( loadSymbolCircle( aPart, currentNode, aGateNumber ) );
        }
        else if( nodeName == "pin" )
        {
            EPIN ePin = EPIN( currentNode );
            std::unique_ptr<LIB_PIN> pin( loadPin( aPart, currentNode, &ePin, aGateNumber ) );
            pincount++;

            if( ePin.direction )
            {
                if( wxString( *ePin.direction ).Lower()== "sup" )
                {
                    ispower = true;
                    pin->SetType( PIN_POWER_IN );
                }
                else if( wxString( *ePin.direction ).Lower()== "pas" )
                {
                    pin->SetType( PIN_PASSIVE );
                }
                else if( wxString( *ePin.direction ).Lower()== "out" )
                {
                    pin->SetType( PIN_OUTPUT );
                }
                else if( wxString( *ePin.direction ).Lower()== "in" )
                {
                    pin->SetType( PIN_INPUT );
                }
                else if( wxString( *ePin.direction ).Lower()== "nc" )
                {
                    pin->SetType( PIN_NC );
                }
                else if( wxString( *ePin.direction ).Lower()== "io" )
                {
                    pin->SetType( PIN_BIDI );
                }
                else if( wxString( *ePin.direction ).Lower()== "oc" )
                {
                    pin->SetType( PIN_OPENEMITTER );
                }
                else if( wxString( *ePin.direction ).Lower()== "hiz" )
                {
                    pin->SetType( PIN_TRISTATE );
                }
                else
                {
                    pin->SetType( PIN_UNSPECIFIED );
                }
            }


            if( aDevice->connects.size() != 0 )
            {
                for( auto connect : aDevice->connects )
                {
                    if( connect.gate == aGateName and pin->GetName().ToStdString() == connect.pin )
                    {
                        wxArrayString pads = wxSplit( wxString(connect.pad), ' ');

                        pin->SetPartNumber( aGateNumber );
                        pin->SetUnit( aGateNumber );
                        pin->SetName( escapeName( pin->GetName() ) );

                        if( pads.GetCount() > 1)
                        {
                            pin->SetNumberTextSize( 0 );
                        }

                        for( int i = 0; i < pads.GetCount(); i++)
                        {
                            LIB_PIN* apin = new LIB_PIN( *pin );

                            wxString padname( pads[i] );
                            apin->SetNumber( padname );
                            aPart->AddDrawItem( apin);
                        }
                        break;

                    }
                }
            }
            else
            {
                pin->SetPartNumber( aGateNumber );
                pin->SetUnit( aGateNumber );
                wxString stringPinNum = wxString::Format( wxT( "%i" ), pincount );
                pin->SetNumber( stringPinNum );
                aPart->AddDrawItem( pin.release() );
            }
        }
        else if( nodeName == "polygon" )
        {
            aPart->AddDrawItem( loadSymbolPolyLine( aPart, currentNode, aGateNumber ) );
        }
        else if( nodeName == "rectangle" )
        {
            aPart->AddDrawItem( loadSymbolRectangle( aPart, currentNode, aGateNumber ) );
        }
        else if( nodeName == "text" )
        {
            std::unique_ptr<LIB_TEXT> libtext( loadSymbolText( aPart, currentNode, aGateNumber ) );

            LIB_FIELD* field;

            if( libtext->GetText().Upper() ==">NAME" || libtext->GetText().Upper() == ">VALUE" )
            {
                if( libtext->GetText().Upper() ==">NAME" )
                {
                    field = aPart->GetField( REFERENCE );
                    foundName = true;
                }
                else
                {
                    field = aPart->GetField( REFERENCE );
                    foundValue = true;
                }

                field->SetTextPos( libtext->GetPosition() );
                field->SetTextSize( libtext->GetTextSize() );
                field->SetTextAngle( libtext->GetTextAngle() );
                field->SetBold( libtext->IsBold() );
                field->SetVertJustify( libtext->GetVertJustify() );
                field->SetHorizJustify( libtext->GetHorizJustify() );
                field->SetVisible( true );
            }
            else
            {
                aPart->AddDrawItem( libtext.release() );
            }
        }
        else if( nodeName == "wire" )
        {
            aPart->AddDrawItem( loadSymbolWire( aPart, currentNode, aGateNumber ) );
        }

        /*
         *  else if( nodeName == "description" )
         *  {
         *  }
         *  else if( nodeName == "dimension" )
         *  {
         *  }
         *  else if( nodeName == "frame" )
         *  {
         *  }
         */

        currentNode = currentNode->GetNext();
    }

    if( foundName == false )
        aPart->GetField( REFERENCE )->SetVisible( false );

    if( foundValue == false )
        aPart->GetField( VALUE )->SetVisible( false );

    return pincount == 1 ? ispower : false;
}


LIB_CIRCLE* SCH_EAGLE_PLUGIN::loadSymbolCircle( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aCircleNode,
        int aGateNumber )
{
    // Parse the circle properties
    ECIRCLE c( aCircleNode );

    unique_ptr<LIB_CIRCLE> circle( new LIB_CIRCLE( aPart.get() ) );

    circle->SetPosition( wxPoint( c.x * EUNIT_TO_MIL, c.y * EUNIT_TO_MIL ) );
    circle->SetRadius( c.radius * EUNIT_TO_MIL );
    circle->SetWidth( c.width * EUNIT_TO_MIL );
    circle->SetUnit( aGateNumber );

    return circle.release();
}


LIB_RECTANGLE* SCH_EAGLE_PLUGIN::loadSymbolRectangle( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aRectNode,
        int aGateNumber )
{
    ERECT rect( aRectNode );

    unique_ptr<LIB_RECTANGLE> rectangle( new LIB_RECTANGLE( aPart.get() ) );

    rectangle->SetPosition( wxPoint( rect.x1 * EUNIT_TO_MIL, rect.y1 * EUNIT_TO_MIL ) );
    rectangle->SetEnd( wxPoint( rect.x2 * EUNIT_TO_MIL, rect.y2 * EUNIT_TO_MIL ) );

    rectangle->SetUnit( aGateNumber );
    // Eagle rectangles are filled by definition.
    rectangle->SetFillMode( FILLED_SHAPE );

    return rectangle.release();
}


LIB_ITEM* SCH_EAGLE_PLUGIN::loadSymbolWire( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aWireNode,
        int aGateNumber )
{
    auto ewire = EWIRE( aWireNode );

    wxRealPoint begin, end;

    begin.x = ewire.x1 * EUNIT_TO_MIL;
    begin.y = ewire.y1 * EUNIT_TO_MIL;
    end.x   = ewire.x2 * EUNIT_TO_MIL;
    end.y   = ewire.y2 * EUNIT_TO_MIL;

    // if the wire is an arc
    if( ewire.curve )
    {
        std::unique_ptr<LIB_ARC> arc( new LIB_ARC( aPart.get() ) );
        wxRealPoint center = ConvertArcCenter( begin, end, *ewire.curve * -1 );

        arc->SetPosition( center );

        if( *ewire.curve >0 )
        {
            arc->SetStart( begin );
            arc->SetEnd( end );
        }
        else
        {
            arc->SetStart( end );
            arc->SetEnd( begin );
        }

        arc->SetWidth( ewire.width * EUNIT_TO_MIL );


        double radius = sqrt( abs( ( ( center.x - begin.x ) * ( center.x - begin.x ) )
                        + ( ( center.y - begin.y ) * ( center.y - begin.y ) ) ) ) * 2;

        arc->SetRadius( radius );
        arc->CalcRadiusAngles();

        // this emulates the filled semicircles created by a thick arc with flat ends caps.
        if( ewire.width * 2 * EUNIT_TO_MIL > radius )
        {
            wxRealPoint centerStartVector = begin - center;

            wxRealPoint centerEndVector = end - center;
            centerStartVector.x = centerStartVector.x / radius;
            centerStartVector.y = centerStartVector.y / radius;


            centerEndVector.x   = centerEndVector.x / radius;
            centerEndVector.y   = centerEndVector.y / radius;
            centerStartVector.x = centerStartVector.x * (ewire.width * EUNIT_TO_MIL + radius);
            centerStartVector.y = centerStartVector.y * (ewire.width * EUNIT_TO_MIL + radius);

            centerEndVector.x   = centerEndVector.x * (ewire.width * EUNIT_TO_MIL + radius);
            centerEndVector.y   = centerEndVector.y * (ewire.width * EUNIT_TO_MIL + radius);


            begin = center + centerStartVector;
            end = center + centerEndVector;
            radius = sqrt( abs( ( ( center.x - begin.x ) * ( center.x - begin.x ) )
                            + ( ( center.y - begin.y ) * ( center.y - begin.y ) ) ) ) * 2;


            arc->SetPosition( center );

            if( *ewire.curve >0 )
            {
                arc->SetStart( begin );
                arc->SetEnd( end );
            }
            else
            {
                arc->SetStart( end );
                arc->SetEnd( begin );
            }

            arc->SetRadius( radius );
            arc->CalcRadiusAngles();
            arc->SetWidth( 1 );
            arc->SetFillMode( FILLED_SHAPE );
        }


        arc->SetUnit( aGateNumber );

        return (LIB_ITEM*) arc.release();
    }
    else
    {
        std::unique_ptr<LIB_POLYLINE> polyLine( new LIB_POLYLINE( aPart.get() ) );

        polyLine->AddPoint( begin );
        polyLine->AddPoint( end );
        polyLine->SetUnit( aGateNumber );
        return (LIB_ITEM*) polyLine.release();
    }
}


LIB_POLYLINE* SCH_EAGLE_PLUGIN::loadSymbolPolyLine( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aPolygonNode,
        int aGateNumber )
{
    std::unique_ptr<LIB_POLYLINE> polyLine( new LIB_POLYLINE( aPart.get() ) );

    EPOLYGON epoly( aPolygonNode );
    wxXmlNode* vertex = aPolygonNode->GetChildren();


    wxPoint pt;

    while( vertex )
    {
        if( vertex->GetName() == "vertex" )     // skip <xmlattr> node
        {
            EVERTEX evertex( vertex );
            pt = wxPoint( evertex.x * EUNIT_TO_MIL, evertex.y * EUNIT_TO_MIL );
            polyLine->AddPoint( pt );
        }

        vertex = vertex->GetNext();
    }

    polyLine->SetFillMode( FILLED_SHAPE );
    polyLine->SetUnit( aGateNumber );

    return polyLine.release();
}


LIB_PIN* SCH_EAGLE_PLUGIN::loadPin( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aPin,
        EPIN* aEPin,
        int aGateNumber )
{
    std::unique_ptr<LIB_PIN> pin( new LIB_PIN( aPart.get() ) );
    pin->SetPosition( wxPoint( aEPin->x * EUNIT_TO_MIL, aEPin->y * EUNIT_TO_MIL ) );
    pin->SetName( aEPin->name );
    pin->SetUnit( aGateNumber );

    int roti = aEPin->rot ? aEPin->rot->degrees : 0;

    switch( roti )
    {
    default:
        wxASSERT_MSG( false, wxString::Format( "Unhandled orientation (%d degrees)", roti ) );

    // fall through
    case 0:
        pin->SetOrientation( 'R' );
        break;

    case 90:
        pin->SetOrientation( 'U' );
        break;

    case 180:
        pin->SetOrientation( 'L' );
        break;

    case 270:
        pin->SetOrientation( 'D' );
        break;
    }

    if( aEPin->length )
    {
        wxString length = aEPin->length.Get();

        if( length =="short" )
        {
            pin->SetLength( 100 );
        }
        else if( length =="middle" )
        {
            pin->SetLength( 200 );
        }
        else if( length == "long" )
        {
            pin->SetLength( 300 );
        }
        else if( length == "point" )
        {
            pin->SetLength( 0 );
        }
    }

    // emaulate the visibility of pin elements
    if( aEPin->visible )
    {
        wxString visible = aEPin->visible.Get();

        if( visible == "off" )
        {
            pin->SetNameTextSize( 0 );
            pin->SetNumberTextSize( 0 );
        }
        else if( visible == "pad" )
        {
            pin->SetNameTextSize( 0 );
        }
        else if( visible == "pin" )
        {
            pin->SetNumberTextSize( 0 );
        }

        /*
         *  else if( visible == "both" )
         *  {
         *  }
         */
    }

    if( aEPin->function )
    {
        wxString function = aEPin->function.Get();

        if( function == "dot" )
        {
            pin->SetShape( PINSHAPE_INVERTED );
        }
        else if( function == "clk" )
        {
            pin->SetShape( PINSHAPE_CLOCK );
        }
        else if( function == "dotclk" )
        {
            pin->SetShape( PINSHAPE_INVERTED_CLOCK );
        }
    }

    return pin.release();
}


LIB_TEXT* SCH_EAGLE_PLUGIN::loadSymbolText( std::unique_ptr<LIB_PART>& aPart,
        wxXmlNode* aLibText,
        int aGateNumber )
{
    std::unique_ptr<LIB_TEXT> libtext( new LIB_TEXT( aPart.get() ) );

    ETEXT etext( aLibText );

    libtext->SetUnit( aGateNumber );
    libtext->SetPosition( wxPoint( etext.x * EUNIT_TO_MIL, etext.y * EUNIT_TO_MIL ) );
    libtext->SetText( aLibText->GetNodeContent() );
    libtext->SetTextSize( etext.ConvertSize() );

    if( etext.ratio )
    {
        if( etext.ratio.Get()>12 )
        {
            libtext->SetBold( true );
            libtext->SetThickness( GetPenSizeForBold( libtext->GetTextWidth() ) );
        }
    }

    int align = etext.align ? *etext.align : ETEXT::BOTTOM_LEFT;
    int degrees = etext.rot ? etext.rot->degrees : 0;
    bool mirror = etext.rot ? etext.rot->mirror : false;
    bool spin = etext.rot ? etext.rot->spin : false;

    eagleToKicadAlignment( (EDA_TEXT*) libtext.get(), align, degrees, mirror, spin, 0 );

    return libtext.release();
}


SCH_TEXT* SCH_EAGLE_PLUGIN::loadPlainText( wxXmlNode* aSchText )
{
    std::unique_ptr<SCH_TEXT> schtext( new SCH_TEXT() );
    auto etext = ETEXT( aSchText );

    schtext->SetItalic( false );
    schtext->SetPosition( wxPoint( etext.x * EUNIT_TO_MIL, -etext.y * EUNIT_TO_MIL ) );

    wxString thetext = aSchText->GetNodeContent();
    schtext->SetText( aSchText->GetNodeContent().IsEmpty() ? "\" \"" : escapeName( thetext ) );

    if( etext.ratio )
    {
        if( etext.ratio.Get()>12 )
        {
            schtext->SetBold( true );
            schtext->SetThickness( GetPenSizeForBold( schtext->GetTextWidth() ) );
        }
    }

    schtext->SetTextSize( etext.ConvertSize() );

    int align = etext.align ? *etext.align : ETEXT::BOTTOM_LEFT;
    int degrees = etext.rot ? etext.rot->degrees : 0;
    bool mirror = etext.rot ? etext.rot->mirror : false;
    bool spin = etext.rot ? etext.rot->spin : false;

    eagleToKicadAlignment( (EDA_TEXT*) schtext.get(), align, degrees, mirror, spin, 0 );

    return schtext.release();
}


bool SCH_EAGLE_PLUGIN::CheckHeader( const wxString& aFileName )
{
    // Open file and check first line
    wxTextFile tempFile;

    tempFile.Open( aFileName );
    wxString firstline;
    // read the first line
    firstline = tempFile.GetFirstLine();
    wxString    secondline  = tempFile.GetNextLine();
    wxString    thirdline   = tempFile.GetNextLine();
    tempFile.Close();

    return firstline.StartsWith( "<?xml" ) && secondline.StartsWith( "<!DOCTYPE eagle SYSTEM" )
           && thirdline.StartsWith( "<eagle version" );
}


void SCH_EAGLE_PLUGIN::moveLabels( SCH_ITEM* aWire, const wxPoint& aNewEndPoint )
{
    for( SCH_ITEM* item = m_currentSheet->GetScreen()->GetDrawItems(); item; item = item->Next() )
    {
        if( item->Type() == SCH_LABEL_T || item->Type() == SCH_GLOBAL_LABEL_T )
        {
            if( TestSegmentHit( item->GetPosition(), ( (SCH_LINE*) aWire )->GetStartPoint(),
                        ( (SCH_LINE*) aWire )->GetEndPoint(), 0 ) )
            {
                item->SetPosition( aNewEndPoint );
            }
        }
    }
}


void SCH_EAGLE_PLUGIN::addBusEntries()
{
    // Add bus entry symbols

    // for each wire segment, compare each end with all busess.
    // If the wire end is found to end on a bus segment, place a bus entry symbol.

    for( SCH_ITEM* bus = m_currentSheet->GetScreen()->GetDrawItems(); bus; bus = bus->Next() )
    {
        // Check line type for line
        if( bus->Type() != SCH_LINE_T )
            continue;

        // Check line type for wire
        if( ( (SCH_LINE*) bus )->GetLayer() != LAYER_BUS )
            continue;


        wxPoint busstart = ( (SCH_LINE*) bus )->GetStartPoint();
        wxPoint busend = ( (SCH_LINE*) bus )->GetEndPoint();

        SCH_ITEM* nextline;

        for( SCH_ITEM* line = m_currentSheet->GetScreen()->GetDrawItems(); line; line = nextline )
        {
            nextline = line->Next();

            // Check line type for line
            if( line->Type() == SCH_LINE_T )
            {
                // Check line type for bus
                if( ( (SCH_LINE*) line )->GetLayer() == LAYER_WIRE )
                {
                    // Get points of both segments.

                    wxPoint linestart = ( (SCH_LINE*) line )->GetStartPoint();
                    wxPoint lineend = ( (SCH_LINE*) line )->GetEndPoint();


                    // Test for horizontal wire and         vertical bus
                    if( linestart.y == lineend.y && busstart.x == busend.x )
                    {
                        if( TestSegmentHit( linestart, busstart, busend, 0 ) )
                        {
                            // Wire start is on a bus.
                            // Wire start is on the vertical bus

                            // if the end of the wire is to the left of the bus
                            if( lineend.x < busstart.x )
                            {
                                // |
                                // ---|
                                // |
                                if( TestSegmentHit( linestart + wxPoint( 0, -100 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    -100,
                                                    0 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( -100, 0 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( -100, 0 ) );
                                }
                                else if( TestSegmentHit( linestart + wxPoint( 0, 100 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    -100,
                                                    0 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( -100, 0 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( -100, 0 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( linestart,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                            // else the wire end is to the right of the bus
                            // Wire is to the right of the bus
                            // |
                            // |----
                            // |
                            else
                            {
                                // test is bus exists above the wire
                                if( TestSegmentHit( linestart + wxPoint( 0, -100 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    0,
                                                    -100 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 100, 0 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart + wxPoint( 100,
                                                    0 ) );
                                }
                                // test is bus exists below the wire
                                else if( TestSegmentHit( linestart + wxPoint( 0, 100 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    0,
                                                    100 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 100, 0 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart + wxPoint( 100,
                                                    0 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( linestart,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                        }

                        // Same thing but test end of the wire instead.
                        if( TestSegmentHit( lineend, busstart, busend, 0 ) )
                        {
                            // Wire end is on the vertical bus

                            // if the start of the wire is to the left of the bus
                            if( linestart.x < busstart.x )
                            {
                                // Test if bus exists above the wire
                                if( TestSegmentHit( lineend + wxPoint( 0, 100 ), busstart, busend,
                                            0 ) )
                                {
                                    // |
                                    // ___/|
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    -100,
                                                    0 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( -100, 0 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( -100, 0 ) );
                                }
                                // Test if bus exists below the wire
                                else if( TestSegmentHit( lineend + wxPoint( 0, -100 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    -100,
                                                    0 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( -100, 0 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( -100, 0 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( lineend,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                            // else the start of the wire is to the right of the bus
                            // |
                            // |----
                            // |
                            else
                            {
                                // test if bus existed above the wire
                                if( TestSegmentHit( lineend + wxPoint( 0, -100 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    0,
                                                    -100 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 100, 0 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 100, 0 ) );
                                }
                                // test if bus existed below the wire
                                else if( TestSegmentHit( lineend + wxPoint( 0, 100 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    0,
                                                    100 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 100, 0 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 100, 0 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( lineend,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                        }
                    }    // if( linestart.y == lineend.y && busstart.x == busend.x)

                    // Test for horizontal wire and vertical bus
                    if( linestart.x == lineend.x && busstart.y == busend.y )
                    {
                        if( TestSegmentHit( linestart, busstart, busend, 0 ) )
                        {
                            // Wire start is on the bus
                            // If wire end is above the bus,
                            if( lineend.y < busstart.y )
                            {
                                // Test for bus existance to the left of the wire
                                if( TestSegmentHit( linestart + wxPoint( -100, 0 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    -100,
                                                    0 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 0, -100 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( 0, -100 ) );
                                }
                                else if( TestSegmentHit( linestart + wxPoint( 100, 0 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    0,
                                                    100 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 0, -100 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( 0, -100 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( linestart,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                            else    // wire end is below the bus.
                            {
                                // Test for bus existance to the left of the wire
                                if( TestSegmentHit( linestart + wxPoint( -100, 0 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    -100,
                                                    0 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 0, 100 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart + wxPoint( 0,
                                                    100 ) );
                                }
                                else if( TestSegmentHit( linestart + wxPoint( 100, 0 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart + wxPoint(
                                                    100,
                                                    0 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, linestart + wxPoint( 0, 100 ) );
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart + wxPoint( 0,
                                                    100 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( linestart,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                        }

                        if( TestSegmentHit( lineend, busstart, busend, 0 ) )
                        {
                            // Wire end is on the bus
                            // If wire start is above the bus,

                            if( linestart.y < busstart.y )
                            {
                                // Test for bus existance to the left of the wire
                                if( TestSegmentHit( lineend + wxPoint( -100, 0 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    -100,
                                                    0 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 0, -100 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 0, -100 ) );
                                }
                                else if( TestSegmentHit( lineend + wxPoint( 100, 0 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    0,
                                                    -100 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 0, -100 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 0, -100 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( lineend,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                            else    // wire end is below the bus.
                            {
                                // Test for bus existance to the left of the wire
                                if( TestSegmentHit( lineend + wxPoint( -100, 0 ), busstart,
                                            busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    -100,
                                                    0 ),
                                            '\\' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 0, 100 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 0, 100 ) );
                                }
                                else if( TestSegmentHit( lineend + wxPoint( 100, 0 ), busstart,
                                                 busend, 0 ) )
                                {
                                    SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( lineend + wxPoint(
                                                    0,
                                                    100 ),
                                            '/' );
                                    busEntry->SetFlags( IS_NEW );
                                    m_currentSheet->GetScreen()->Append( busEntry );
                                    moveLabels( line, lineend + wxPoint( 0, 100 ) );
                                    ( (SCH_LINE*) line )->SetEndPoint( lineend +
                                            wxPoint( 0, 100 ) );
                                }
                                else
                                {
                                    SCH_MARKER* marker = new SCH_MARKER( lineend,
                                            "Bus Entry neeeded" );

                                    m_currentSheet->GetScreen()->Append( marker );
                                }
                            }
                        }
                    }

                    linestart = ( (SCH_LINE*) line )->GetStartPoint();
                    lineend     = ( (SCH_LINE*) line )->GetEndPoint();
                    busstart    = ( (SCH_LINE*) bus )->GetStartPoint();
                    busend = ( (SCH_LINE*) bus )->GetEndPoint();


                    // bus entry wire isn't horizontal or vertical
                    if( TestSegmentHit( linestart, busstart, busend, 0 ) )
                    {
                        wxPoint wirevector = linestart - lineend;

                        if( wirevector.x > 0 )
                        {
                            if( wirevector.y > 0 )
                            {
                                wxPoint p = linestart + wxPoint( -100, -100 );
                                SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( p, '\\' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );
                                moveLabels( line, p );

                                if( p == lineend )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetStartPoint( p );
                                }
                            }
                            else
                            {
                                wxPoint p = linestart + wxPoint( -100, 100 );
                                SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( p, '/' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );

                                moveLabels( line, p );

                                if( p== lineend )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetStartPoint( p );
                                }
                            }
                        }
                        else
                        {
                            if( wirevector.y > 0 )
                            {
                                SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart,
                                        '/' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );

                                moveLabels( line, linestart + wxPoint( 100, -100 ) );

                                if( linestart + wxPoint( 100, -100 )== lineend )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( 100, -100 ) );
                                }
                            }
                            else
                            {
                                SCH_BUS_WIRE_ENTRY* busEntry = new SCH_BUS_WIRE_ENTRY( linestart,
                                        '\\' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );
                                moveLabels( line, linestart + wxPoint( 100, 100 ) );

                                if( linestart + wxPoint( 100, 100 )== lineend )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetStartPoint( linestart +
                                            wxPoint( 100, 100 ) );
                                }
                            }
                        }
                    }

                    if( TestSegmentHit( lineend, busstart, busend, 0 ) )
                    {
                        wxPoint wirevector = linestart - lineend;

                        if( wirevector.x > 0 )
                        {
                            if( wirevector.y > 0 )
                            {
                                wxPoint p = lineend + wxPoint( 100, 100 );
                                SCH_BUS_WIRE_ENTRY* busEntry =
                                    new SCH_BUS_WIRE_ENTRY( lineend, '\\' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );

                                moveLabels( line, p );

                                if( p == linestart )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetEndPoint( p );
                                }
                            }
                            else
                            {
                                wxPoint p = lineend + wxPoint( 100, -100 );
                                SCH_BUS_WIRE_ENTRY* busEntry =
                                    new SCH_BUS_WIRE_ENTRY( lineend, '/' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );

                                moveLabels( line, p );

                                if( p== linestart )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetEndPoint( p );
                                }
                            }
                        }
                        else
                        {
                            if( wirevector.y > 0 )
                            {
                                wxPoint p = lineend + wxPoint( -100, 100 );
                                SCH_BUS_WIRE_ENTRY* busEntry =
                                    new SCH_BUS_WIRE_ENTRY( p, '/' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );
                                moveLabels( line, p );

                                if( p == linestart )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetEndPoint( p );
                                }
                            }
                            else
                            {
                                wxPoint p = lineend + wxPoint( -100, -100 );
                                SCH_BUS_WIRE_ENTRY* busEntry =
                                    new SCH_BUS_WIRE_ENTRY( p, '\\' );
                                busEntry->SetFlags( IS_NEW );
                                m_currentSheet->GetScreen()->Append( busEntry );
                                moveLabels( line, p );

                                if( p == linestart )    // wire is overlapped by bus entry symbol
                                {
                                    m_currentSheet->GetScreen()->DeleteItem( line );
                                }
                                else
                                {
                                    ( (SCH_LINE*) line )->SetEndPoint( p );
                                }
                            }
                        }
                    }
                }
            }
        }   // for ( line ..
    }       // for ( bus ..
}


wxString SCH_EAGLE_PLUGIN::escapeName( const wxString& aNetName )
{
    wxString ret( aNetName );

    ret.Replace( "~", "~~" );
    ret.Replace( "!", "~" );

    return ret;
}
