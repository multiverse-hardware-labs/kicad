/*
* This program source code file is part of KiCad, a free EDA CAD application.
*
* Copyright (C) 2017 CERN
* @author Alejandro García Montoro <alejandro.garciamontoro@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
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

#include <wx/filename.h>
#include <memory>
#include <string>
#include <unordered_map>

#include <sch_junction.h>
#include <sch_sheet.h>
#include <sch_eagle_plugin.h>

#include <wildcards_and_files_ext.h>
#include <class_sch_screen.h>
#include <class_library.h>
#include <class_libentry.h>
#include <lib_circle.h>
#include <lib_rectangle.h>
#include <lib_polyline.h>
#include <lib_pin.h>
#include <lib_text.h>

#include <eagle_parser.h>

// Eagle schematic internal units are millimeters
// Kicad schematic units are thousandths of an inch
#define EUNIT_TO_MIL 1000.0/25.4

// Eagle schematic axes are aligned with x increasing left to right and Y increasing bottom to top
// Kicad schematic axes are algigned with x increasing left to rigth and Y increasing top to bottom.

using namespace std;

NODE_MAP mapChildren( wxXmlNode* aCurrentNode )
{
    // Map node_name -> node_pointer
    NODE_MAP nodesMap;

    // Loop through all children mapping them in nodesMap
    aCurrentNode = aCurrentNode->GetChildren();
    while( aCurrentNode )
    {
        // Create a new pair in the map
        //      key: current node name
        //      value: current node pointer
        nodesMap[aCurrentNode->GetName().ToStdString()] = aCurrentNode;

        // Get next child
        aCurrentNode = aCurrentNode->GetNext();
    }

    return nodesMap;
}

int countChildren( wxXmlNode* aCurrentNode, std::string name)
{
    // Map node_name -> node_pointer
    int count = 0;

    // Loop through all children counting them if they match the given name
    aCurrentNode = aCurrentNode->GetChildren();
    while( aCurrentNode )
    {
        if(aCurrentNode->GetName().ToStdString() == name) count++;
        // Get next child
        aCurrentNode = aCurrentNode->GetNext();
    }

    return count;
}


void kicadLayer( int aEagleLayer )
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

    switch( aEagleLayer )
    {
        case 90: break;
        case 91: break;
        case 92: break;
        case 93: break;
        case 94: break;
        case 95: break;
        case 96: break;
        case 97: break;
        case 98: break;
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


void SCH_EAGLE_PLUGIN::SaveLibrary( const wxString& aFileName, const PROPERTIES* aProperties )
{
}


SCH_SHEET* SCH_EAGLE_PLUGIN::Load( const wxString& aFileName, KIWAY* aKiway,
                                   SCH_SHEET* aAppendToMe, const PROPERTIES* aProperties )
{
    // TODO: Handle Kiway and uncomment next line.
    // wxASSERT( !aFileName || aKiway != NULL );

    // Load the document
    wxXmlDocument xmlDocument;
    wxFileName fn = aFileName;
    m_kiway = aKiway;

    if( !xmlDocument.Load( fn.GetFullPath() ) )
        THROW_IO_ERROR( wxString::Format( _( "Unable to read file '%s'" ), fn.GetFullPath() ) );

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

    // TODO change to loadSheet, so it can handle multiple sheets
    if( !m_rootSheet->GetScreen() )
    {
        SCH_SCREEN* screen = new SCH_SCREEN( aKiway );
        screen->SetFileName( aFileName );
        m_rootSheet->SetScreen( screen );
    }

    // Create a schematic symbol library
    wxFileName libfn = fn;
    libfn.SetName(libfn.GetName()+wxString("-cache"));
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
    NODE_MAP children = mapChildren( currentNode );

    // TODO: handle compatibility nodes
    // wxXmlNode* compatibility = children["compatibility"];

    // Load drawing
    loadDrawing( children["drawing"] );

    m_partlib->Save(false);
    deleter.release();
    return m_rootSheet;
}


void SCH_EAGLE_PLUGIN::loadDrawing( wxXmlNode* aDrawingNode )
{
    // Map all children into a readable dictionary
    NODE_MAP drawingChildren = mapChildren( aDrawingNode );

    // Board nodes should not appear in .sch files
    // wxXmlNode* board = drawingChildren["board"]

    // TODO: handle grid nodes
    // wxXmlNode* grid = drawingChildren["grid"]

    // TODO: handle layers nodes
    // wxXmlNode* layers = drawingChildren["layers"]

    // TODO: handle library nodes
    // wxXmlNode* library = drawingChildren["library"]

    // TODO: handle settings nodes
    // wxXmlNode* settings = drawingChildren["settings"]

    // Load schematic
    loadSchematic( drawingChildren["schematic"] );
}


void SCH_EAGLE_PLUGIN::loadSchematic( wxXmlNode* aSchematicNode )
{
    // Map all children into a readable dictionary
    NODE_MAP schematicChildren = mapChildren( aSchematicNode );

    // TODO : handle classes nodes
    //wxXmlNode* classes = schematicChildren["classes"];

    // TODO : handle description nodes
    // wxXmlNode* description = schematicChildren["description"];

    // TODO : handle errors nodes
    // wxXmlNode* errors = schematicChildren["errors"];

    // TODO : handle modules nodes
    // wxXmlNode* modules = schematicChildren["modules"];

    // TODO : handle parts nodes
    // wxXmlNode* parts = schematicChildren["parts"];

    // TODO : handle variantdefs nodes
    // wxXmlNode* variantdefs = schematicChildren["variantdefs"];

    // TODO: handle attributes node
    // wxXmlNode* attributes = schematicChildren["attributes"];
    // Possible children: constant, display, font, layer, name, ratio, rot, size, value, x, y

    // Loop through all the libraries
    wxXmlNode* libraryNode = schematicChildren["libraries"]->GetChildren();

    while( libraryNode )
    {
        loadLibrary( libraryNode );
        libraryNode = libraryNode->GetNext();
    }

    // Loop through all the sheets
    wxXmlNode* sheetNode = schematicChildren["sheets"]->GetChildren();

    int sheet_count = countChildren(schematicChildren["sheets"], "sheet" );

    // If eagle schematic has multiple sheets.

    if(sheet_count > 1){
        // TODO: set up a heirachical sheet for each Eagle sheet.
        int x, y;
        x = 1;
        y = 1;

        while( sheetNode ){
          wxPoint pos = wxPoint(x*1000, y*1000);
          std::unique_ptr<SCH_SHEET> sheet( new SCH_SHEET(pos) );
          SCH_SCREEN* screen = new SCH_SCREEN(m_kiway) ;

          sheet->SetTimeStamp( GetNewTimeStamp() );
          sheet->SetParent( m_rootSheet->GetScreen() );
          sheet->SetScreen(  screen );

          m_currentSheet = sheet.get();
          loadSheet( sheetNode );
          sheet->GetScreen()->SetFileName( sheet->GetFileName() );
          m_rootSheet->GetScreen()->Append(sheet.release());

          sheetNode = sheetNode->GetNext();
          x+=2;
          if(x>10)
          {
            x = 1;
            y+=2;
          }
        }

    } else {
        while( sheetNode )
        {
            m_currentSheet = m_rootSheet;
            loadSheet( sheetNode );
            sheetNode = sheetNode->GetNext();
        }
    }

}


void SCH_EAGLE_PLUGIN::loadSheet( wxXmlNode* aSheetNode  )
{
    // Map all children into a readable dictionary
    NODE_MAP sheetChildren = mapChildren( aSheetNode );

    // Get description node

    wxXmlNode* descriptionNode = getChildrenNodes( sheetChildren, "description" );
    if( descriptionNode )
    {
      wxString des = descriptionNode->GetContent();
      m_currentSheet->SetName(des);

      std::string filename =  des.ToStdString();
      ReplaceIllegalFileNameChars(&filename);
      replace(filename.begin(),filename.end(), ' ', '_');

      wxString fn = wxString(filename+".sch");
      m_currentSheet->SetFileName(fn);
      wxFileName fileName = m_currentSheet->GetFileName();
      m_currentSheet->GetScreen()->SetFileName( fileName.GetFullPath() );


    }



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
      //  loadSegments( busNode );

        // Get next bus
        busNode = busNode->GetNext();
    }

    // Loop through all nets
    // From the DTD: "Net is an electrical connection in a schematic."
    wxXmlNode* netNode = getChildrenNodes( sheetChildren, "nets" );

    while( netNode )
    {
        // Get the net name and class
        wxString netName = netNode->GetAttribute( "name" );
        wxString netClass = netNode->GetAttribute( "class" );

        // Load segments of this net
        loadSegments( netNode , netName, netClass);

        // Get next net
        netNode = netNode->GetNext();
    }

    // Loop through all instances
    wxXmlNode* instanceNode = getChildrenNodes( sheetChildren, "instances" );

    while( instanceNode )
    {
        loadInstance( instanceNode );
        instanceNode = instanceNode->GetNext();
    }

    // Loop through all moduleinsts
    wxXmlNode* moduleinstNode = getChildrenNodes( sheetChildren, "moduleinsts" );

    while( moduleinstNode )
    {
        loadModuleinst( moduleinstNode );
        moduleinstNode = moduleinstNode->GetNext();
    }

    // TODO: do something with the description
    // wxXmlNode* description = sheetChildren["description"];
    // wxString language = description->GetAttribute( "language", "en" ); // Defaults to "en"
    // wxString description = description->GetNodeContent();

    // TODO: do something with the plain
    wxXmlNode* plainNode = sheetChildren["plain"];

    while( plainNode )
    {
        //loadSegments( plainNode );
        plainNode = plainNode->GetNext();
    }
}


void SCH_EAGLE_PLUGIN::loadSegments( wxXmlNode* aSegmentsNode, wxString netName, wxString netClass)
{
    // Loop through all segments
    wxXmlNode* currentSegment = aSegmentsNode->GetChildren();
    SCH_SCREEN* screen = m_currentSheet->GetScreen();
    //wxCHECK( screen, [>void<] );
    while( currentSegment )
    {
        // Loop through all segment children
        wxXmlNode* segmentAttribute = currentSegment->GetChildren();

        while( segmentAttribute )
        {
            wxString nodeName = segmentAttribute->GetName();

            if( nodeName == "junction" )
            {
                // TODO: handle junctions attributes
                segmentAttribute->GetAttribute( "x" );
                segmentAttribute->GetAttribute( "y" );
                screen->Append(loadJunction(segmentAttribute));
            }
            else if( nodeName == "label" )
            {
                // TODO: handle labels attributes
                segmentAttribute->GetAttribute( "x" );      // REQUIRED
                segmentAttribute->GetAttribute( "y" );      // REQUIRED
                segmentAttribute->GetAttribute( "size" );   // REQUIRED
                segmentAttribute->GetAttribute( "layer" );  // REQUIRED
                segmentAttribute->GetAttribute( "font" );   // Defaults to "proportional"
                segmentAttribute->GetAttribute( "ratio" );  // Defaults to "8"
                segmentAttribute->GetAttribute( "rot" );    // Defaults to "R0"
                segmentAttribute->GetAttribute( "xref" );   // Defaults to "no"

                screen->Append(loadLabel(segmentAttribute, netName));

            }
            else if( nodeName == "pinref" )
            {
                // TODO: handle pinref attributes
                segmentAttribute->GetAttribute( "gate" );   // REQUIRED
                segmentAttribute->GetAttribute( "part" );   // REQUIRED
                segmentAttribute->GetAttribute( "pin" );    // REQUIRED
            }
            else if( nodeName == "portref" )
            {
                // TODO: handle portref attributes
                segmentAttribute->GetAttribute( "moduleinst" ); // REQUIRED
                segmentAttribute->GetAttribute( "port" );       // REQUIRED
            }
            else if( nodeName == "wire" )
            {
                screen->Append( loadSignalWire( segmentAttribute ) );
            }
            else if( nodeName == "segment" )
            {
                //loadSegments( segmentAttribute );
            }
            else if( nodeName == "text" )
            {
                // TODO
                //loadSegments( segmentAttribute );
            }
            else // DEFAULT
            {
                // TODO uncomment
                //THROW_IO_ERROR( wxString::Format( _( "XML node '%s' unknown" ), nodeName ) );
            }

            // Get next segment attribute
            segmentAttribute = segmentAttribute->GetNext();
        }

        currentSegment = currentSegment->GetNext();
    }
}


SCH_LINE* SCH_EAGLE_PLUGIN::loadSignalWire( wxXmlNode* aWireNode )
{
    std::unique_ptr<SCH_LINE> wire( new SCH_LINE );

    auto ewire = EWIRE( aWireNode );

    wire->SetLayer( LAYER_WIRE );

    wxPoint begin, end;

    begin.x = ewire.x1*EUNIT_TO_MIL;
    begin.y = -ewire.y1*EUNIT_TO_MIL;
    end.x   = ewire.x2*EUNIT_TO_MIL;
    end.y   = -ewire.y2*EUNIT_TO_MIL;

    wire->SetStartPoint( begin );
    wire->SetEndPoint( end );

    return wire.release();
}



SCH_JUNCTION* SCH_EAGLE_PLUGIN::loadJunction(wxXmlNode* aJunction){
  std::unique_ptr<SCH_JUNCTION> junction( new SCH_JUNCTION );

  auto ejunction = EJUNCTION(aJunction);

  junction->SetPosition( wxPoint( ejunction.x*EUNIT_TO_MIL, -ejunction.y*EUNIT_TO_MIL ) );

  return junction.release();
}


SCH_GLOBALLABEL* SCH_EAGLE_PLUGIN::loadLabel(wxXmlNode* aLabelNode, wxString aNetName){
  std::unique_ptr<SCH_GLOBALLABEL> glabel( new SCH_GLOBALLABEL );

  auto elabel = ELABEL(aLabelNode, aNetName);

  glabel->SetPosition( wxPoint( elabel.x*EUNIT_TO_MIL, -elabel.y*EUNIT_TO_MIL ) );
  glabel->SetText(elabel.netname);
  glabel->SetTextSize( wxSize( GetDefaultTextSize(), GetDefaultTextSize() ) );

  if( elabel.rot )
        glabel->SetLabelSpinStyle( int( elabel.rot.Get().degrees/90+2)%4 );

  return glabel.release();
}


void SCH_EAGLE_PLUGIN::loadInstance( wxXmlNode* aInstanceNode )
{
}


void SCH_EAGLE_PLUGIN::loadModuleinst( wxXmlNode* aModuleinstNode )
{
}


void SCH_EAGLE_PLUGIN::loadLibrary( wxXmlNode* aLibraryNode )
{
    // Read the library name
    wxString libName = aLibraryNode->GetAttribute( "name" );

    // Query all children and map them into a readable dictionary
    NODE_MAP libraryChildren = mapChildren( aLibraryNode );

    // TODO: Do something with the description
    // wxXmlNode* libraryChildren["description"];

    // Loop through the devicesets and load each of them
    // wxXmlNode* devicesetNode = libraryChildren["devicesets"]->GetChildren();
    // while( devicesetNode )
    // {
    //     loadDeviceset( devicesetNode );
    //     devicesetNode = devicesetNode->GetNext();
    // }

    // Loop through the packages and load each of them
    // wxXmlNode* packageNode = libraryChildren["packages"]->GetChildren();
    // while( packageNode )
    // {
    //     loadPackage( packageNode );
    //     packageNode = packageNode->GetNext();
    // }

    // Loop through the symbols and load each of them
    wxXmlNode* symbolNode = libraryChildren["symbols"]->GetChildren();

    while( symbolNode )
    {
        m_partlib->AddPart(loadSymbol( symbolNode ));
        symbolNode = symbolNode->GetNext();
    }
}


LIB_PART* SCH_EAGLE_PLUGIN::loadSymbol( wxXmlNode* aSymbolNode )
{
    // Create a new part with the symbol name
    wxString symbolName = aSymbolNode->GetAttribute( "name" );
    std::unique_ptr<LIB_PART> part( new LIB_PART( symbolName ) );

    wxXmlNode* currentNode = aSymbolNode->GetChildren();

    while( currentNode )
    {
        wxString nodeName = currentNode->GetName();

        if( nodeName == "description" )
        {
            // TODO
            //wxASSERT_MSG( false, "'description' nodes are not implemented yet" );
        }
        else if( nodeName == "dimension" )
        {
            // TODO
            //wxASSERT_MSG( false, "'description' nodes are not implemented yet" );
        }
        else if( nodeName == "frame" )
        {
        }
        else if( nodeName == "circle" )
        {
            part->AddDrawItem( loadSymbolCircle( part.get(), currentNode ) );
        }
        else if( nodeName == "pin" )
        {
             part->AddDrawItem( loadPin( part.get(), currentNode ) );
        }
        else if( nodeName == "polygon" )
        {
            //part->AddDrawItem( loadPolygon( part.get(), currentNode ) );
        }
        else if( nodeName == "rectangle" )
        {
            part->AddDrawItem( loadSymbolRectangle( part.get(), currentNode ) );
        }
        else if( nodeName == "text" )
        {
            LIB_TEXT* libtext = loadSymboltext( part.get(), currentNode );

            if( libtext->GetText() ==">NAME" )
            {
                auto field = part->GetReferenceField();
                field.SetOffset( libtext->GetPosition() );
                field.SetTextSize( libtext->GetTextSize() );
                field.SetTextAngle( libtext->GetTextAngle() );
            }
            else if( libtext->GetText() == ">VALUE" )
            {
                auto field = part->GetValueField();
                field.SetOffset( libtext->GetPosition() );
                field.SetTextSize( libtext->GetTextSize() );
                field.SetTextAngle( libtext->GetTextAngle() );
            }
            //else
            //    part->AddDrawItem( loadlibtext( part.get(), currentNode ) );
        }
        else if( nodeName == "wire" )
        {
            part->AddDrawItem( loadSymbolWire( part.get(), currentNode ) );
        }

        currentNode = currentNode->GetNext();
    }

    return part.release();
}


LIB_CIRCLE* SCH_EAGLE_PLUGIN::loadSymbolCircle( LIB_PART* aPart, wxXmlNode* aCircleNode )
{
    // Parse the circle properties
    ECIRCLE c( aCircleNode );

    unique_ptr<LIB_CIRCLE> circle( new LIB_CIRCLE( aPart ) );

    circle->SetPosition( wxPoint( c.x*EUNIT_TO_MIL, -c.y*EUNIT_TO_MIL ) );
    circle->SetRadius( c.radius*EUNIT_TO_MIL );
    circle->SetWidth( c.width*EUNIT_TO_MIL );

    return circle.release();
}


LIB_RECTANGLE* SCH_EAGLE_PLUGIN::loadSymbolRectangle( LIB_PART* aPart, wxXmlNode* aRectNode )
{
    ERECT rect( aRectNode );

    unique_ptr<LIB_RECTANGLE> rectangle( new LIB_RECTANGLE( aPart ) );

    rectangle->SetPosition( wxPoint( rect.x1*EUNIT_TO_MIL, -rect.y1*EUNIT_TO_MIL ) );
    rectangle->SetEnd( wxPoint( rect.x2*EUNIT_TO_MIL, -rect.y2*EUNIT_TO_MIL ) );

    // TODO: Manage rotation

    return rectangle.release();
}

LIB_POLYLINE* SCH_EAGLE_PLUGIN::loadSymbolWire( LIB_PART* aPart, wxXmlNode* aWireNode )
{
    // TODO: Layer map
    std::unique_ptr< LIB_POLYLINE > polyLine( new LIB_POLYLINE( aPart ) );

    auto ewire = EWIRE(aWireNode);
    wxPoint begin, end;

    begin.x = ewire.x1*EUNIT_TO_MIL;
    begin.y = -ewire.y1*EUNIT_TO_MIL;
    end.x   = ewire.x2*EUNIT_TO_MIL;
    end.y   = -ewire.y2*EUNIT_TO_MIL;

    polyLine->AddPoint( begin );
    polyLine->AddPoint( end );

    return polyLine.release();
}

LIB_POLYLINE* SCH_EAGLE_PLUGIN::loadSymbolPolyLine( LIB_PART* aPart, wxXmlNode* aPolygonNode )
{
    // TODO: Layer map
    std::unique_ptr< LIB_POLYLINE > polyLine( new LIB_POLYLINE( aPart ) );

    NODE_MAP polygonChildren = mapChildren( aPolygonNode );
    wxXmlNode* vertex = getChildrenNodes( polygonChildren, "vertex" );

    while(vertex) {
        auto evertex = EVERTEX( vertex);
        auto v = wxPoint(evertex.x*EUNIT_TO_MIL, -evertex.y*EUNIT_TO_MIL);
        polyLine->AddPoint( v );

        vertex->GetNext();
    }

    return polyLine.release();
}

LIB_PIN* SCH_EAGLE_PLUGIN::loadPin(  LIB_PART* aPart, wxXmlNode* aPin )
{
    std::unique_ptr<LIB_PIN> pin( new LIB_PIN( aPart ) );

    auto epin = EPIN( aPin );

    pin->SetPosition( wxPoint( epin.x * EUNIT_TO_MIL, -epin.y * EUNIT_TO_MIL ) );
    pin->SetName( epin.name );

    int roti = 0;

    if( epin.rot )
    {
        roti = int(epin.rot->degrees);
    }

    switch( roti )
    {
    case 0:
        pin->SetOrientation( 'R' );
        break;

    case 90:
        pin->SetOrientation( 'D' );
        break;

    case 180:
        pin->SetOrientation( 'L' );
        break;

    case 270:
        pin->SetOrientation( 'U' );
        break;
    }

    if( epin.length )
    {
        wxString length = epin.length.Get();

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

    return pin.release();
}

LIB_TEXT* SCH_EAGLE_PLUGIN::loadSymboltext(  LIB_PART* aPart, wxXmlNode* aLibText )
{
    std::unique_ptr<LIB_TEXT> libtext( new LIB_TEXT( aPart ) );

    auto etext = ETEXT( aLibText );

    libtext->SetPosition( wxPoint( etext.x * EUNIT_TO_MIL, -etext.y * EUNIT_TO_MIL ) );
    libtext->SetText( aLibText->GetNodeContent() );
    libtext->SetTextSize(wxSize(int(etext.size*EUNIT_TO_MIL), int(etext.size*EUNIT_TO_MIL)));

    return libtext.release();
}


void SCH_EAGLE_PLUGIN::Save( const wxString& aFileName, SCH_SCREEN* aSchematic, KIWAY* aKiway,
                             const PROPERTIES* aProperties )
{
}


size_t SCH_EAGLE_PLUGIN::GetSymbolLibCount( const wxString&   aLibraryPath,
                                            const PROPERTIES* aProperties )
{
    return 0;
}


void SCH_EAGLE_PLUGIN::EnumerateSymbolLib( wxArrayString&    aAliasNameList,
                                           const wxString&   aLibraryPath,
                                           const PROPERTIES* aProperties )
{
}


LIB_ALIAS* SCH_EAGLE_PLUGIN::LoadSymbol( const wxString& aLibraryPath, const wxString& aSymbolName,
                                         const PROPERTIES* aProperties )
{
    return nullptr;
}


void SCH_EAGLE_PLUGIN::SaveSymbol( const wxString& aLibraryPath, const LIB_PART* aSymbol,
                                   const PROPERTIES* aProperties )
{
}


void SCH_EAGLE_PLUGIN::DeleteAlias( const wxString& aLibraryPath, const wxString& aAliasName,
                                    const PROPERTIES* aProperties )
{
}


void SCH_EAGLE_PLUGIN::DeleteSymbol( const wxString& aLibraryPath, const wxString& aAliasName,
                                     const PROPERTIES* aProperties )
{
}


void SCH_EAGLE_PLUGIN::CreateSymbolLib( const wxString&   aLibraryPath,
                                        const PROPERTIES* aProperties )
{
}


bool SCH_EAGLE_PLUGIN::DeleteSymbolLib( const wxString&   aLibraryPath,
                                        const PROPERTIES* aProperties )
{
    return false;
}


bool SCH_EAGLE_PLUGIN::IsSymbolLibWritable( const wxString& aLibraryPath )
{
    return false;
}


void SCH_EAGLE_PLUGIN::SymbolLibOptions( PROPERTIES* aListToAppendTo ) const
{
}

// approved
// attribute
// circle
// clearance
// connect
// contactref
// description
// dimension
// frame
// gate
// grid
// hole
// layer
// note
// pad
// param
// pin
// pinref
// port
// portref
// rectangle
// setting
// smd
// textvariant
// variantdef
// vertex
// via
// wire
