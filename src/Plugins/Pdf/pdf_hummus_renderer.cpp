
/******************************************************************************
* MODULE     : pdf_hummus_renderer.cpp
* DESCRIPTION: Renderer for printing pdf graphics using the PDFHummus library
* COPYRIGHT  : (C) 2012 Massimiliano Gubinelli
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "pdf_hummus_renderer.hpp"
#include "Metafont/tex_files.hpp"
#include "Freetype/tt_file.hpp"
#include "file.hpp"
#include "image_files.hpp"
#include "analyze.hpp"
#include "iterator.hpp"
#include "merge_sort.hpp"
#include "scheme.hpp"
#include "sys_utils.hpp"
#include "convert.hpp"
#include "ntuple.hpp"
#include "link.hpp"
#include "frame.hpp"
#include "Ghostscript/gs_utilities.hpp" // for gs_prefix

#include "PDFWriter/PDFWriter.h"
#include "PDFWriter/PDFPage.h"
#include "PDFWriter/PageContentContext.h"
#include "PDFWriter/DictionaryContext.h"
#include "PDFWriter/PDFImageXObject.h"
#include "PDFWriter/PDFStream.h"
#include "PDFWriter/PDFDocumentCopyingContext.h"

/******************************************************************************
 * pdf_hummus_renderer
 ******************************************************************************/

typedef triple<int,int,int> rgb;
typedef quartet<string,int,SI,SI> dest_data;
typedef quintuple<string,int,SI,SI,int> outline_data;

class pdf_image;
class pdf_raw_image;
class t3font;

class pdf_hummus_renderer_rep : public renderer_rep {
  
  static const int default_dpi= 72; // PDF initial coordinate system corresponds to 72 dpi
  
  url       pdf_file_name;
  int       dpi;
  int       nr_pages;
  string    page_type;
  bool      landscape;
  double    paper_w;
  double    paper_h;
  
  int       page_num;
  bool      inText;
  int       alpha;
  rgb       stroke_rgb;
  rgb       fill_rgb;
  color     fg, bg;
  SI        lw;
  double    current_width;
  int       clip_level;
  
  pencil   pen;
  brush    bgb;

  string    cfn;
  PDFUsedFont* cfid;
  double fsize;
  double prev_text_x, prev_text_y;
  
  double width, height;
  
  
  hashmap<string,PDFUsedFont*> pdf_fonts;
  //hashmap<string,ObjectIDType> image_resources;
  hashmap<string,pdf_raw_image> pdf_glyphs;
  hashmap<tree,pdf_image> image_pool;
  
  hashmap<int,ObjectIDType> alpha_id;
  hashmap<int,ObjectIDType> page_id;
  hashmap<string,t3font> t3font_list;
  
  // link annotation support
  hashmap<ObjectIDType,string> annot_list;
  list<dest_data> dests;
  ObjectIDType destId;
  hashmap<string,int> label_id;
  int label_count;

  // outline support
  ObjectIDType outlineId;
  list<outline_data> outlines;
  
  
  PDFWriter pdfWriter;
  PDFPage* page;
  PageContentContext* contentContext;
  
  // geometry
  
  double to_x (SI x) {
    x += ox;
    if (x>=0) x= x/pixel; else x= (x-pixel+1)/pixel;
    return x;
  };

  double to_y (SI y) {
    y += oy;
    if (y>=0) y= y/pixel; else y= (y-pixel+1)/pixel;
    return y;
  };
  
  // various internal routines

  void init_page_size ();
  void select_stroke_color (color c);
  void select_fill_color (color c);
  void select_alpha (int a);
  
  void select_line_width (SI w);
  void compile_glyph (scheme_tree t);
  void begin_text ();
  void end_text ();
  
  void begin_page();
  void end_page();
  
  int get_label_id(string label);

    
  // glyph positioning
  
  typedef quartet<int,int,int,glyph> drawn_glyph;
  list <drawn_glyph> drawn_glyphs;
  void draw_glyphs();

  
  // various internal routines
  
  void flush_images();
  void flush_glyphs();
  void flush_dests();
  void flush_outlines();
  void flush_fonts();
  PDFImageXObject *create_pdf_image_raw (string raw_data, SI width, SI height, ObjectIDType imageXObjectID);
  void make_pdf_font (string fontname);
  void draw_bitmap_glyph (int ch, font_glyphs fn, SI x, SI y);
  void  image (url u, SI w, SI h, SI x, SI y,
               double cx1, double cy1, double cx2, double cy2,
               int alpha);
  
  
  void bezier_arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta);

  // hooks for the Hummus library

  class DestinationsWriter : public DocumentContextExtenderAdapter
  {
    pdf_hummus_renderer_rep *ren;
  public:
    DestinationsWriter(pdf_hummus_renderer_rep *_ren) : ren(_ren) {}
    virtual ~DestinationsWriter(){}
    
    // IDocumentContextExtender implementation
    virtual PDFHummus::EStatusCode OnCatalogWrite(CatalogInformation* inCatalogInformation,
                                                  DictionaryContext* inCatalogDictionaryContext,
                                                  ObjectsContext* inPDFWriterObjectContext,
                                                  PDFHummus::DocumentContext* inDocumentContext)
    {
      ren->on_catalog_write (inCatalogInformation, inCatalogDictionaryContext, inPDFWriterObjectContext, inDocumentContext);
      return eSuccess;
    }
  };
  
  PDFHummus::EStatusCode on_catalog_write (CatalogInformation* inCatalogInformation,
                                           DictionaryContext* inCatalogDictionaryContext,
                                           ObjectsContext* inPDFWriterObjectContext,
                                           PDFHummus::DocumentContext* inDocumentContext);
  
  
  void recurse (ObjectsContext& objectsContext, list<outline_data>& it, ObjectIDType parentId,
                ObjectIDType& firstId, ObjectIDType& lastId, int &count);

  
public:
  pdf_hummus_renderer_rep (url pdf_file_name, int dpi, int nr_pages,
                           string ptype, bool landsc, double paper_w, double paper_h);
  ~pdf_hummus_renderer_rep ();
  bool is_printer ();
  void next_page ();
  
  void set_transformation (frame fr);
  void reset_transformation ();
  
  void  set_clipping (SI x1, SI y1, SI x2, SI y2, bool restore= false);
  
  pencil get_pencil ();
  brush get_background ();
  void  set_pencil (pencil pen2);
  void  set_background (brush b2);

  void  draw (int char_code, font_glyphs fn, SI x, SI y);
  void  line (SI x1, SI y1, SI x2, SI y2);
  void  lines (array<SI> x, array<SI> y);
  void  clear (SI x1, SI y1, SI x2, SI y2);
  void  fill (SI x1, SI y1, SI x2, SI y2);
  void  arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta);
  void  fill_arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta);
  void  polygon (array<SI> x, array<SI> y, bool convex=true);
  
  void draw_picture (picture p, SI x, SI y, int alpha);
  void draw_scalable (scalable im, SI x, SI y, int alpha);

  renderer shadow (picture& pic, SI x1, SI y1, SI x2, SI y2);
  void fetch (SI x1, SI y1, SI x2, SI y2, renderer ren, SI x, SI y);
  void new_shadow (renderer& ren);
  void delete_shadow (renderer& ren);
  void get_shadow (renderer ren, SI x1, SI y1, SI x2, SI y2);
  void put_shadow (renderer ren, SI x1, SI y1, SI x2, SI y2);
  void apply_shadow (SI x1, SI y1, SI x2, SI y2);

  /************************ subroutines hyperlinks ***************************/
  
  void anchor (string label, SI x, SI y);
  void href (string label, SI x1, SI y1, SI x2, SI y2);
  void toc_entry (string kind, string title, SI x, SI y);
  
};

/******************************************************************************
 * local utilities
 ******************************************************************************/


static void
write_indirect_obj(ObjectsContext&  objectsContext, ObjectIDType destId, string payload) {
  objectsContext.StartNewIndirectObject(destId);
  c_string buf (payload);
  objectsContext.StartFreeContext()->Write((unsigned char *)(char*)buf, N(payload));
  objectsContext.EndFreeContext();
  objectsContext.EndIndirectObject();
}



/******************************************************************************
* constructors and destructors
******************************************************************************/

pdf_hummus_renderer_rep::pdf_hummus_renderer_rep (
  url pdf_file_name2, int dpi2, int nr_pages2,
  string page_type2, bool landscape2, double paper_w2, double paper_h2):
    renderer_rep (false),
    pdf_file_name (pdf_file_name2), dpi (dpi2),
    nr_pages (nr_pages2), page_type (page_type2),
    landscape (landscape2), paper_w (paper_w2), paper_h (paper_h2),
    fg (-1), bg (-1), fill_rgb(-1,-1,-1), stroke_rgb(-1,-1,-1),
    lw (-1),  cfn (""), cfid (NULL),
    pdf_fonts (0), page_num(0), label_count(0),
    destId(0), outlineId(0),
    inText (false)
{
  if (landscape) { // consistent with printer
    width=  default_dpi * paper_h  / 2.54;
    height= default_dpi *paper_w  / 2.54;
  }
  else {
	  width=  default_dpi *  paper_w / 2.54;
    height= default_dpi * paper_h / 2.54;
  }
  
  
  // setup library

  EStatusCode status;
  {
    c_string _pdf_file_name (concretize (pdf_file_name));

    status = pdfWriter.StartPDF((char*)_pdf_file_name, ePDFVersion14 ); // PDF 1.4 for alpha
                               //   , LogConfiguration(true, true, "/Users/mgubi/Desktop/pdfwriter-x.log")
                               //   , PDFCreationSettings(false) ); // true = compression on
      if(status != PDFHummus::eSuccess)
      {
          cout << "failed to start PDF\n";
      }	
  }
  
  pdfWriter.GetDocumentContext().AddDocumentContextExtender (new DestinationsWriter(this));

  // start real work
  
  begin_page();
}

pdf_hummus_renderer_rep::~pdf_hummus_renderer_rep () {
  
  end_page();
  
  flush_images();
  flush_glyphs();
  flush_dests();
  flush_outlines();
  flush_fonts();
 
  {
    // flush alphas
    iterator<int> it = iterate(alpha_id);
    ObjectsContext& objectsContext = pdfWriter.GetObjectsContext();
    while (it->busy()) {
      int a = it->next();
      double da = ((double) a)/1000.0;
      objectsContext.StartNewIndirectObject(alpha_id(a));
      {
        std::stringstream buf;
        buf << "<< /Type /ExtGState /CA "<< da << "  /ca "<< da << " >>\r\n";
        objectsContext.StartFreeContext()->Write((const IOBasicTypes::Byte* )(buf.str().c_str()),buf.str().size());
        objectsContext.EndFreeContext();
      }
      objectsContext.EndIndirectObject();
    }
  }
  
  {
    // flush annotations
    iterator<ObjectIDType> it = iterate(annot_list);
    ObjectsContext& objectsContext = pdfWriter.GetObjectsContext();
    while (it->busy()) {
      ObjectIDType id = it->next();
      write_indirect_obj(objectsContext, id, annot_list(id));
    }
  }
  
  EStatusCode status = pdfWriter.EndPDF();
  if(status != PDFHummus::eSuccess)
  {
    cout << "failed in end PDF\n";
  }
}

bool
pdf_hummus_renderer_rep::is_printer () {
//  cerr << "is_printer\n";
  return true;
}

void
pdf_hummus_renderer_rep::next_page () {
  end_page();
  begin_page();
}

void
pdf_hummus_renderer_rep::begin_page() {
  EStatusCode status;
  

  page = new PDFPage();
  page->SetMediaBox(PDFRectangle(0,0,width,height));
  contentContext = pdfWriter.StartPageContentContext(page);
  if (NULL == contentContext)
  {
    status = PDFHummus::eFailure;
    cout << "failed to create content context for page\n";
  }
  
  alpha = 255;
  fg  = -1;
  bg  = -1;
  lw  = -1;
  current_width = -1.0;
  cfn= "";
  cfid = NULL;
  inText = false;
  clip_level = 0;

  // outmost save of the graphics state
  contentContext->q();
  // set scaling suitable for dpi (pdf default is 72)
  contentContext->cm((double)default_dpi / dpi, 0, 0, (double)default_dpi / dpi, 0, 0);
  
  set_origin (0, paper_h*dpi*pixel/2.54);
  set_clipping (0, (int) ((-dpi*pixel*paper_h)/2.54), (int) ((dpi*pixel*paper_w)/2.54), 0);
}

void
pdf_hummus_renderer_rep::end_page(){
  
  if (!page) return;
  EStatusCode status;

  end_text ();
  

  // reset set_clipping calls in order to have well formed PDF.
  while (clip_level--)
    contentContext->Q();
  
  // outmost restore for the graphics state (see begin_page)
  contentContext->Q();

  status = pdfWriter.EndPageContentContext(contentContext);
  if(status != PDFHummus::eSuccess)
  {
    cout<<"failed to end page content context\n";
  }
  
  EStatusCodeAndObjectIDType res = pdfWriter.GetDocumentContext().WritePageAndRelease(page);
  status = res.first;
  page_id (page_num) = res.second;
  page_num++;
}

void
pdf_hummus_renderer_rep::begin_text () {
  if (!inText) {
    contentContext->BT();
    inText = true;
    prev_text_x = to_x(0);
    prev_text_y = to_y(0);
    contentContext->Tm(1,0,0,1, prev_text_x, prev_text_y);
  }
}


void
pdf_hummus_renderer_rep::end_text () {
  if (inText) {
    draw_glyphs();
    contentContext->ET();
    inText = false;
  }
}

/******************************************************************************
 * Transformed rendering
 ******************************************************************************/

void
pdf_hummus_renderer_rep::set_transformation (frame fr) {
  ASSERT (fr->linear, "only linear transformations have been implemented");
  
  end_text ();

  SI cx1, cy1, cx2, cy2;
  get_clipping (cx1, cy1, cx2, cy2);
  rectangle oclip (cx1, cy1, cx2, cy2);
  frame cv= scaling (point (pixel, pixel),
                     point (-ox, -oy));
  frame tr= invert (cv) * fr * cv;
  point o = tr (point (0.0, 0.0));
  point ux= tr (point (1.0, 0.0)) - o;
  point uy= tr (point (0.0, 1.0)) - o;
  //cout << "Set transformation " << o << ", " << ux << ", " << uy << "\n";
  double tx= o[0];
  double ty= o[1];
  contentContext->q();
  contentContext->cm(ux[0],ux[1],uy[0],uy[1],tx,ty);
  
  rectangle nclip= fr [oclip];
  renderer_rep::clip (nclip->x1, nclip->y1, nclip->x2, nclip->y2);
}

void
pdf_hummus_renderer_rep::reset_transformation () {
  end_text ();
  renderer_rep::unclip ();
  contentContext->Q();
}

/******************************************************************************
* Clipping
******************************************************************************/

void
pdf_hummus_renderer_rep::set_clipping (SI x1, SI y1, SI x2, SI y2, bool restore) {
  renderer_rep::set_clipping (x1, y1, x2, y2, restore);

  end_text();
  
  outer_round (x1, y1, x2, y2);
  if (restore) {
    //cerr << "restore clipping\n";
    contentContext->Q();
    if (clip_level > 0) clip_level--;
    cfn= "";
  }
  else {
    //cerr << "set clipping\n";
    contentContext->q(); clip_level++;
    double xx1= to_x (min (x1, x2));
    double yy1= to_y (min (y1, y2));
    double xx2= to_x (max (x1, x2));
    double yy2= to_y (max (y1, y2));
    contentContext->re(xx1, yy1, xx2-xx1, yy2-yy1);
    contentContext->W();
    contentContext->n();
  }
}

/******************************************************************************
 * Graphic state management
 ******************************************************************************/

void
pdf_hummus_renderer_rep::select_alpha (int a) {
  if (alpha != a) {
    alpha = a;
    if (!alpha_id->contains(a)) {
      ObjectIDType temp = pdfWriter.GetObjectsContext().GetInDirectObjectsRegistry().AllocateNewObjectID();
      alpha_id(a) = temp;
    }
    std::string name = page->GetResourcesDictionary().AddExtGStateMapping(alpha_id(a));
    contentContext->gs(name);
  }
}

void
pdf_hummus_renderer_rep::select_stroke_color (color c) {;
  int r, g, b, a;
  get_rgb_color (c, r, g, b, a);
  r= ((r*1000)/255);
  g= ((g*1000)/255);
  b= ((b*1000)/255);
  a= ((a*1000)/255);
  rgb c1 = rgb(r,g,b);
  if (stroke_rgb != c1) {
    double dr= ((double) r) / 1000.0;
    double dg= ((double) g) / 1000.0;
    double db= ((double) b) / 1000.0;
    contentContext->RG(dr, dg, db); // stroke color;
    stroke_rgb = c1;
  }
  select_alpha(a);
}

void
pdf_hummus_renderer_rep::select_fill_color (color c) {;
  int r, g, b, a;
  get_rgb_color (c, r, g, b, a);
  r= ((r*1000)/255);
  g= ((g*1000)/255);
  b= ((b*1000)/255);
  a= ((a*1000)/255);
  rgb c1 = rgb(r,g,b);
  if (fill_rgb != c1) {
    double dr= ((double) r) / 1000.0;
    double dg= ((double) g) / 1000.0;
    double db= ((double) b) / 1000.0;
    contentContext->rg(dr, dg, db); // non-stroking color
    fill_rgb = c1;
  }
  select_alpha(a);
}

void
pdf_hummus_renderer_rep::select_line_width (SI w) {
  double pw = w /pixel;
  //if (pw < 1) pw= 1;
  if (pw != current_width) {
    contentContext->w(pw);
    current_width = pw;
  }
}

pencil
pdf_hummus_renderer_rep::get_pencil () {
//  cerr << "get_color\n";
  return pen;
}

brush
pdf_hummus_renderer_rep::get_background () {
//  cerr << "get_background\n";
  return bgb;
}

void
pdf_hummus_renderer_rep::set_pencil (pencil pen2) {
//  cerr << "set_color\n";
  pen= pen2;
  color c= pen->get_color ();
  if (fg!=c) {
    fg= c;
    draw_glyphs();
    select_fill_color (c);
    select_stroke_color (c);
  }
  //if (pen->w != lw) {
  // FIXME: apparently, the line width can be overidden by some of
  // the graphical constructs (example file: newimpl.tm, in which
  // the second dag was not printed using the right width)
  lw= pen->get_width ();
  select_line_width (lw);
  //}

}

void
pdf_hummus_renderer_rep::set_background (brush b) {
//  cerr << "set_background\n";
  //if (bgb==b) return;
  bgb= b;
  bg= b->get_color ();
}

/******************************************************************************
 * Raw images
 ******************************************************************************/

// obsolete support for direct placement of virtual glyphs without Type3 fonts.

static string
load_virtual_glyph (glyph gl) {
  string buf;
  int i, j;
  for (j= 0; j < gl->height; j++)
    for (i= 0; i < gl->width; i++) {
      if (gl->get_x (i, j) > 0) buf << (char)0;
      else buf << (char)255;
    }
 return buf;
}

static const std::string scType = "Type";
static const std::string scXObject = "XObject";
static const std::string scSubType = "Subtype";

static const std::string scImage = "Image";
static const std::string scWidth = "Width";
static const std::string scHeight = "Height";
static const std::string scColorSpace = "ColorSpace";
static const std::string scDeviceGray = "DeviceGray";
static const std::string scDeviceRGB = "DeviceRGB";
static const std::string scDeviceCMYK = "DeviceCMYK";
static const std::string scDecode = "Decode";
static const std::string scBitsPerComponent = "BitsPerComponent";
static const std::string scFilter = "Filter";
static const std::string scDCTDecode = "DCTDecode";
static const std::string KProcsetImageB = "ImageB";

static void
create_pdf_image_raw (PDFWriter& pdfw, string raw_data, SI width, SI height, ObjectIDType imageXObjectID)
{
  
	PDFImageXObject* imageXObject = NULL;
	//EStatusCode status = PDFHummus::eSuccess;

  ObjectsContext& objectsContext = pdfw.GetObjectsContext();
  objectsContext.StartNewIndirectObject(imageXObjectID);
	do {
    {
      // write stream dictionary
      DictionaryContext* imageContext = objectsContext.StartDictionary();
      // type
      imageContext->WriteKey(scType);
      imageContext->WriteNameValue(scXObject);
      // subtype
      imageContext->WriteKey(scSubType);
      imageContext->WriteNameValue(scImage);
      // Width
      imageContext->WriteKey(scWidth);
      imageContext->WriteIntegerValue(width);
      // Height
      imageContext->WriteKey(scHeight);
      imageContext->WriteIntegerValue(height);
      // Bits Per Component
      imageContext->WriteKey(scBitsPerComponent);
      imageContext->WriteIntegerValue(8);
      // Color Space and Decode Array if necessary
      imageContext->WriteKey(scColorSpace);
      imageContext->WriteNameValue(scDeviceGray);
      // Length
      imageContext->WriteKey("Length");
      imageContext->WriteIntegerValue(N(raw_data));
      objectsContext.EndDictionary(imageContext);
    }
    {
      // write stream
      objectsContext.WriteKeyword("stream");
      {
        c_string buf (raw_data);
        objectsContext.StartFreeContext()->Write((unsigned char*)(char *)buf, N(raw_data));
        objectsContext.EndFreeContext();
      }
      objectsContext.EndLine();
      objectsContext.WriteKeyword("endstream");
    }
    objectsContext.EndIndirectObject();
		imageXObject = new PDFImageXObject(imageXObjectID, KProcsetImageB);
	} while(false);
 
  if (imageXObject == NULL) {
    cerr <<  "pdf_hummus: failed to include glyph." << LF;
  } else {
    delete imageXObject;
  }
}



class pdf_raw_image_rep : public concrete_struct
{
public:
  string data;
  int w,h;
  ObjectIDType id;
  
  pdf_raw_image_rep (string _data, int _w, int _h, ObjectIDType _id)
   : data(_data), w(_w), h(_h), id(_id) {}
  pdf_raw_image_rep () {}
  
  void flush(PDFWriter& pdfw)
  {
    //cout << "flushing :" << id << LF;
    create_pdf_image_raw (pdfw, data, w, h, id);
  }
}; // pdf_raw_image_ref

class pdf_raw_image {
  CONCRETE_NULL(pdf_raw_image);
  pdf_raw_image (string _data, int _w, int _h, ObjectIDType _id):
  rep (tm_new<pdf_raw_image_rep> (_data,_w,_h,_id)) {};
};

CONCRETE_NULL_CODE(pdf_raw_image);

void
pdf_hummus_renderer_rep::flush_glyphs ()
{
  // flush all images
  iterator<string> it = iterate (pdf_glyphs);
  while (it->busy()) {
    pdf_raw_image im = pdf_glyphs [it->next()];
    im->flush (pdfWriter);
  }
}

void
pdf_hummus_renderer_rep::draw_bitmap_glyph (int ch, font_glyphs fn, SI x, SI y)
{
  // use bitmap (to be improven)
  string fontname = fn->res_name;
  string char_name (fontname * "-" * as_string ((int) ch));
  if (!pdf_glyphs->contains(char_name)) {
    //cerr << "draw bitmap glyph " << (double)gl->width / 8 << " " << (double)gl->height / 8 << "\n";
    glyph gl= fn->get(ch);
    if (is_nil (gl)) return;
    string buf= load_virtual_glyph (gl);
    ObjectIDType imageXObjectID  = pdfWriter.GetObjectsContext().GetInDirectObjectsRegistry().AllocateNewObjectID();
    pdf_glyphs (char_name) = pdf_raw_image (buf, gl->width, gl->height, imageXObjectID);
  }
}


/******************************************************************************
 * Type 3 fonts
 ******************************************************************************/

class t3font_rep : public concrete_struct {
public:
  font_glyphs fn;
  ObjectIDType fontId;
  ObjectsContext &objectsContext;
  hashmap<int, int> used_chars;
  int firstchar;
  int lastchar;
  int b0,b1,b2,b3; // glyph bounding box
  bool first_glyph;
  
  t3font_rep (font_glyphs _fn, ObjectsContext &_objectsContext)
  : fn (_fn), objectsContext(_objectsContext), first_glyph(true)
  {
    fontId = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();
  }
  
  void update_bbox(int llx, int lly, int urx, int ury);
  void add_glyph (int ch) {  used_chars (ch) = 1; }
  void write_char (glyph gl, ObjectIDType inCharID);
  void write_definition ();
};

class t3font {
  CONCRETE_NULL(t3font);
  t3font (font_glyphs _fn, ObjectsContext &_objectsContext)
  : rep (tm_new<t3font_rep> (_fn,_objectsContext)) {};
};

CONCRETE_NULL_CODE(t3font);

void
t3font_rep::update_bbox(int llx, int lly, int urx, int ury)
{
  if (first_glyph) {
    b0 = llx; b1 = lly; b2 = urx; b3 = ury;
    first_glyph = false;
  } else {
    if (llx < b0) b0 = llx;
    if (lly < b1) b1 = lly;
    if (urx > b2) b2 = urx;
    if (ury > b3) b3 = ury;
  }
}

void
t3font_rep::write_char (glyph gl, ObjectIDType inCharID)
{
  int llx, lly, urx, ury, cwidth, cheight, lwidth;
  llx = -gl->xoff;
  lly = gl->yoff-gl->height+1;
  urx = gl->width-gl->xoff+1;
  ury = gl->yoff+1;
  cwidth = gl->width;
  cheight = gl->height;
  lwidth = gl->lwidth;
  
  objectsContext.StartNewIndirectObject(inCharID);
  // write char stream
  PDFStream *charStream = objectsContext.StartPDFStream(NULL, true);
  {
    string data;
    if (is_nil (gl)) {
      // write d0 command
      data  << "0 0 d0\r\n";
    } else {
      update_bbox(llx,lly,urx,ury);
      data  << as_string(lwidth) << " 0 ";
      data << as_string(llx) << " " << as_string(lly) << " "
           << as_string(urx) << " " << as_string(ury);
      data << " d1\r\n";
      data << " q\r\n";
      data  << as_string((double)(cwidth)) << " 0 0 "
      << as_string((double)(cheight)) << " " << as_string((double)(llx)) << " " << as_string(lly) << " cm\r\n";
      data << "BI\r\n/W " << as_string(cwidth) << "\r\n/H " << as_string(cheight) << "\r\n";
      data << "/CS /G /BPC 1 /F /AHx /D [0.0 1.0] /IM true\r\nID\r\n";
      {
        static const char* hex_string= "0123456789ABCDEF";
        string hex_code;
        int i, j, count=0, cur= 0;
        for (j=0; j < cheight; j++)
          for (i=0; i < ((cwidth+7) & (-8)); i++) {
            cur= cur << 1;
            if ((i<cwidth) && (gl->get_x(i,j)==0)) cur++;
            count++;
            if (count==4) {
              hex_code << hex_string[cur];
              cur  = 0;
              count= 0;
            }
          }
        data << hex_code;
      }
      data << ">\r\nEI\r\nQ\r\n"; // ">" is the EOD char for ASCIIHex
    }
    {
      c_string buf(data);
      charStream->GetWriteStream()->Write((unsigned char *)(char*)buf,N(data));
    }
  }
  objectsContext.EndPDFStream(charStream);
  delete charStream;
  objectsContext.EndIndirectObject();
  //return PDFHummus::eSuccess;
}

void
t3font_rep::write_definition ()
{
  array <int> glyph_list;
  array<ObjectIDType> charIds;
  // order used glyphs
  {
    firstchar = 255;
    lastchar = 0;
    iterator<int> it = iterate(used_chars);
    while (it->busy()) glyph_list << it->next();
    merge_sort(glyph_list);
    firstchar = glyph_list [0];
    lastchar = glyph_list [N(glyph_list)-1];
  }
  // write glyphs definitions
  for(int i = 0; i < N(glyph_list); ++i)
  {
    int ch = glyph_list[i];
    glyph gl = fn->get(ch);
    ObjectIDType temp  = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();
    charIds << temp;
    write_char (gl, temp);
  }
  // create font dictionary
  {
    string dict;
    dict << "<<\r\n";
    dict << "\t/Type /Font\r\n";
    dict << "\t/Subtype /Type3\r\n\t/FontBBox [ "
         << as_string(b0) << " " << as_string(b1) << " "
    << as_string(b2) << " " << as_string(b3) << "]\r\n";
    dict << "\t/FontMatrix [" << as_string(1/100.0) << " 0 0 " << as_string(1/100.0) << " 0 0 ]\r\n";
    dict << "\t/FirstChar " << as_string(firstchar) << "\r\n\t/LastChar " << as_string(lastchar) << "\r\n";
    dict << "\t/Widths [ ";
    for(int i = firstchar; i <= lastchar ; ++i)
      if (used_chars->contains (i)) {
        dict << as_string((double)(fn->get(i)->lwidth)) << " ";
      } else {
        dict << "0 ";
      }
    // Write CharProcs
    dict << "]\r\n\t/CharProcs <<\r\n";
    for(int i = 0; i < N(glyph_list); ++i)
    {
      int ch = glyph_list[i];
      ObjectIDType temp = charIds[i];
      dict << "\t\t/ch" << as_string(ch) << " " << as_string(temp) << " 0 R\r\n";
    }
    dict << "\t\t/.notdef " << as_string(charIds[0]) << " 0 R\r\n";
    dict << "\t>>\r\n";
    dict << "\t/Encoding <<\r\n";
    dict << "\t\t/Type /Encoding\r\n";
    dict << "\t\t/Differences [";
    for(int i = firstchar, previousEncoding = firstchar; i <= lastchar; ++i)
      if (used_chars->contains (i)) {
        if (previousEncoding + 1 != i) {
          dict << "\r\n\t\t\t" << as_string(i) << " ";
        }
        dict << "/ch" << as_string(i) << " ";
        previousEncoding = i;
      }
    dict << "\t\t]\r\n\t>>\r\n>>\r\n";
    
    write_indirect_obj(objectsContext, fontId, dict);
  }
}

void
pdf_hummus_renderer_rep::flush_fonts()
{
  // flush t3 fonts
  iterator<string> it = iterate(t3font_list);
  while (it->busy()) {
    string name = it->next();
    t3font f = t3font_list[name];
    f->write_definition();
  }
}

/******************************************************************************
 * Text handling
 ******************************************************************************/

void
pdf_hummus_renderer_rep::make_pdf_font (string fontname)
{
  int pos= search_forwards (":", fontname);
  string fname= (pos==-1? fontname: fontname (0, pos));
  url u = url_none ();
  {
    //cerr << " try freetype " << LF;
    u = tt_font_find (fname);
    //cerr << fname << " " << u << LF;
  }
  if (!is_none (u)) {
    int pos= search_forwards (".", fontname);
    string rname= (pos==-1? fontname: fontname (0, pos));
    //double fsize= font_size (fn->res_name);
    
    //char *_rname = as_charp(fname);
    PDFUsedFont* font;
    {
      //cout << "GetFontForFile "  << u  << LF;
      c_string _u (concretize (u));
      font = pdfWriter.GetFontForFile((char*)_u);
      //tm_delete_array(_rname);
    }
    
    if (font != 0) {
      pdf_fonts (fontname)= font;
    }  else {
      cout << "(pdf_hummus_renderer) Problems with font: " << fname << " file " << u << LF;
    }
  }
}

void
pdf_hummus_renderer_rep::draw_glyphs()
{
  SI x,y,w; // current pos

  if (N(drawn_glyphs) == 0) return;
  
  begin_text ();
  GlyphUnicodeMappingListOrDoubleList gbuf;
  GlyphUnicodeMappingList gbuf1;
  
  while (!is_nil(drawn_glyphs)) {
    SI xx,yy,ww;
    SI bx, by;
    x = drawn_glyphs->item.x1;
    y = drawn_glyphs->item.x2;
    w = drawn_glyphs->item.x4->lwidth*pixel;// - drawn_glyphs->item.x4->xoff;
    bx = x; by = y;
    while (1) {
      drawn_glyph dg = drawn_glyphs->item;
      //cout << "pushing " << dg.x4->index << " " << dg.x3 << LF;
      gbuf1.push_back(GlyphUnicodeMapping(dg.x4->index,dg.x3));
      drawn_glyphs = drawn_glyphs->next;
      if (is_nil(drawn_glyphs)) break;
      
      dg = drawn_glyphs->item;
      
      xx = dg.x1;
      yy = dg.x2;
      ww = dg.x4->lwidth*pixel;// - dg.x4->xoff;
      
      if (yy != y) break;
      
      SI dx = xx-x-w;
      if ((dx >= 4*pixel)||(dx <=-4*pixel)) {
        if (gbuf1.size()>0) {
          gbuf.push_back(gbuf1);
          gbuf1.clear();
        }
        gbuf.push_back((double)(-dx*(1000.0/pixel)/fsize));
      } else dx = 0;
      x = x+w+dx;
      w = ww;
    }
    if (gbuf1.size()>0) {
      gbuf.push_back(gbuf1);
      gbuf1.clear();
    }
    
    contentContext->Td(bx/pixel-prev_text_x, by/pixel-prev_text_y);
    prev_text_x = bx/pixel;
    prev_text_y = by/pixel;
    //contentContext->Tm(1,0,0,1, p*bx/PIXEL, p*by/PIXEL);
    contentContext->TJ(gbuf);
    gbuf.clear();
  }
}



static double font_size (string name) {
    int pos= search_forwards (".", name);
    int szpos= pos-1;
    while ((szpos>0) && is_numeric (name[szpos-1])) szpos--;
    double size= as_double (name (szpos, pos));
    if (size == 0) size= 10;
    double dpi= as_double (name (pos+1, N(name)-2));
    double mag= (size) * (dpi/72.0);
    return mag;
}


void
pdf_hummus_renderer_rep::draw (int ch, font_glyphs fn, SI x, SI y) {
  //cerr << "draw \"" << (char)ch << "\" " << ch << " " << fn->res_name << "\n";
  glyph gl= fn->get(ch);
  if (is_nil (gl)) return;
  string fontname = fn->res_name;
  string char_name (fontname * "-" * as_string ((int) ch));
  pdf_raw_image glyph;
  if (cfn != fontname) {
    if (!pdf_fonts [fontname]) {
      if (!t3font_list->contains(fontname)) {
        make_pdf_font (fontname);
        if (!pdf_fonts [fontname]) {
          t3font f(fn,  pdfWriter.GetObjectsContext());
          t3font_list(fontname) = f;
        }
      }
    }
    //cout << "CHANGE FONT" << LF;
    begin_text ();
    draw_glyphs();
    cfn = fontname;
    fsize = font_size (fontname);
    if (pdf_fonts->contains(fontname)) {
      cfid = pdf_fonts (cfn);
      contentContext->Tf(cfid, fsize);
    } else {
      cfid = NULL;
      std::string name = page->GetResourcesDictionary().AddFontMapping(t3font_list(cfn)->fontId);
      // pk fonts are encoded in t3 fonts as bitmaps. they cannot be scaled and
      // are encoded in such a way that they should be rendered at size 100 (conventional value)
      // to give the correct result (see the Font Matrix defined in t3font_rep::write_definition).
      contentContext->TfLow(name, 100);
    }
  }
  if (cfid != NULL) {
    begin_text ();
#if 0
    contentContext->Td(to_x(x)-prev_text_x, to_y(y)-prev_text_y);
    prev_text_x = to_x(x);
    prev_text_y = to_y(y);
    //cout << "char " << ch << "index " << gl->index <<" " << x << " " << y << " font " << cfn  << LF;
    GlyphUnicodeMappingList glyphs;
    glyphs.push_back(GlyphUnicodeMapping(gl->index, ch));
    contentContext->Tj(glyphs);
#else
    drawn_glyphs << drawn_glyph(ox+x,oy+y,ch,gl);
#endif
  } else {
    begin_text ();
    contentContext->Td(to_x(x)-prev_text_x, to_y(y)-prev_text_y);
    prev_text_x = to_x(x);
    prev_text_y = to_y(y);
    t3font_list(fontname)->add_glyph(ch);
    std::string buf;
    buf.push_back(ch);
    contentContext->TjLow(buf);
  }
}

/******************************************************************************
 * Graphics primitives
 ******************************************************************************/

void
pdf_hummus_renderer_rep::line (SI x1, SI y1, SI x2, SI y2) {
//  cerr << "line\n";
  end_text ();
  contentContext->m(to_x (x1), to_y (y1));
  contentContext->l(to_x (x2), to_y (y2));
  contentContext->S();
}

void
pdf_hummus_renderer_rep::lines (array<SI> x, array<SI> y) {
 // cerr << "lines\n";
  end_text ();
  int i, n= N(x);
  if ((N(y) != n) || (n<1)) return;
  contentContext->m(to_x (x[0]), to_y (y[0]));
  for (i=1; i<n; i++) {
    contentContext->l(to_x (x[i]), to_y (y[i]));
  }
  contentContext->S();
}

void
pdf_hummus_renderer_rep::clear (SI x1, SI y1, SI x2, SI y2) {
  end_text ();
  double xx1= to_x (min (x1, x2));
  double yy1= to_y (min (y1, y2));
  double xx2= to_x (max (x1, x2));
  double yy2= to_y (max (y1, y2));
  //cout << "clear" << xx1 << " " << yy1 << " " << xx2 << " " << yy2 << LF;
  contentContext->q();
  select_fill_color (bg);
  contentContext->re(xx1, yy1, xx2-xx1, yy2-yy1);
  contentContext->h();
  contentContext->f();
  select_fill_color (fg);
  contentContext->Q();
}

void
pdf_hummus_renderer_rep::fill (SI x1, SI y1, SI x2, SI y2) {
  if ((x1<x2) && (y1<y2)) {
    end_text ();
    double xx1= to_x (min (x1, x2));
    double yy1= to_y (min (y1, y2));
    double xx2= to_x (max (x1, x2));
    double yy2= to_y (max (y1, y2));
    contentContext->re(xx1, yy1, xx2-xx1, yy2-yy1);
    contentContext->h();
    contentContext->f();
  }
}


void
pdf_hummus_renderer_rep::bezier_arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta)
{
  // PDF can describe only cubic bezier paths, so we have to make up the arc with them
  
  // Control points for an arc of radius 1, centered on the x-axis and spanning theta degrees
  // are given by:
  // bx0 = cos(theta/2.0); by0 = sin(theta/2.0);
  // bx3 = bx0;            by3 = -by0;
  // bx1 = (4.0-bx0)/3.0;  by1 = (1.0-bx0)*(3.0-bx0)/(3.0*by0);
  // bx2 = bx1;            by2 = -by1;
  // from: http://www.tinaja.com/glib/bezcirc2.pdf
  
  // we compose the arc using the above basic structure and user space transformations
  

  contentContext->q(); // save graphics state

  {
    double xx1 = to_x(x1), yy1 = to_y(y1), xx2 = to_x(x2), yy2 = to_y(y2);
    double cx = (xx1+xx2)/2, cy = (yy1+yy2)/2;
    double rx = (xx2-xx1)/2, ry = (yy2-yy1)/2;
    contentContext->cm(rx, 0, 0, ry, cx, cy); // scaling and centering
  }
  
  if (alpha != 0) {
    double a = 2.0*M_PI*alpha/(360.0*64.0);
    contentContext->cm(cos(a), sin(a), -sin(a), cos(a), 0, 0); // rotation
  }
  
  if (delta == 360*64) {
    // closed circle
    contentContext->m(1.0,0.0);
  } else {
    // an open arc closed in the center
    //FIXME: is this really what we want ?
    contentContext->m(0.0,0.0);
    contentContext->l(1.0,0.0);
  }
  
  int prev_phi = 0;
  while (delta > 0) {
    int phi = min(delta,90*64); // draw arcs in portions of 90 degrees maximum
    delta -= phi;
    double sphi = sin(2*M_PI*(phi+prev_phi)/(2.0*360.0*64.0));
    double cphi = cos(2*M_PI*(phi+prev_phi)/(2.0*360.0*64.0));
    contentContext->cm(cphi, sphi, -sphi, cphi, 0, 0); // rotation of phi/2 degrees
    sphi = sin(2*M_PI*(phi)/(2.0*360.0*64.0));
    cphi = cos(2*M_PI*(phi)/(2.0*360.0*64.0));
    double bx0 = cphi, by0 = sphi; // , bx3 = bx0, by3 = -by0;
    double bx1 = (4.0-bx0)/3.0, by1 = (1.0-bx0)*(3.0-bx0)/(3.0*by0);
    double bx2 = bx1, by2 = -by1;
    contentContext->c(bx2, by2, bx1, by1, bx0, by0);
    prev_phi = phi;
  }
  
  contentContext->h(); // close the path
  contentContext->Q(); // restore the graphics state
}

void
pdf_hummus_renderer_rep::arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta) {
  //cerr << "arc\n";
  end_text ();
  bezier_arc(x1, y1, x2, y2, alpha, delta);
  contentContext->S();
}

void
pdf_hummus_renderer_rep::fill_arc (SI x1, SI y1, SI x2, SI y2, int alpha, int delta) {
  //cerr << "fill_arc\n";
  end_text ();
  bezier_arc(x1, y1, x2, y2, alpha, delta);
  contentContext->f();
}

void
pdf_hummus_renderer_rep::polygon (array<SI> x, array<SI> y, bool convex) {
  end_text ();
  //cerr << "polygon\n";
  int i, n= N(x);
  if ((N(y) != n) || (n<1)) return;
  contentContext->m(to_x (x[0]), to_y (y[0]));
  for (i=1; i<n; i++) {
    contentContext->l(to_x (x[i]), to_y (y[i]));
  }
  contentContext->h();
  contentContext->f();
}

class pdf_image_rep : public concrete_struct
{
public:
  url u;
  int bx1, by1, bx2, by2;
  ObjectIDType id;
  
  pdf_image_rep(url _u, ObjectIDType _id)
    : u(_u), id(_id)
  {
    ps_bounding_box (u, bx1, by1, bx2, by2);
  }
  ~pdf_image_rep() {}
  
  void flush(PDFWriter& pdfw)
  {
    url name= resolve (u);
    if (is_none (name))
      name= "$TEXMACS_PATH/misc/pixmaps/unknown.ps";

    // do not use "convert" to convert from eps to pdf since it rasterizes the picture
    // string filename = concretize (name);

    PDFRectangle cropBox (0,0,bx2-bx1,by2-by1);
    double tMat[6] = { 1, 0, 0, 1, 0, 0};

    url temp= url_temp (".pdf");
    string tempname= sys_concretize (temp);
    c_string fname (tempname);
    //cout << "flushing :" << fname << LF;
    
    string s= suffix (name);
    if (s == "pdf") {
      // FIXME: better avoid copying in this case
      string cmd= "cp";
      system (cmd, name, temp);
    } else if ( s != "ps" && s != "eps") {
      // generic image format : use convert from ImageMagik
      string cmd= "convert";
      system (cmd, name, temp);
    } else {
      // ps or eps : use ghostscript
#if 0
      // obsolete code : remove at some point
      string cmd = "ps2pdf14";
      system (cmd, u, temp);
      PDFRectangle cropBox (bx1,by1,bx2,by2);
      double tMat[6] = { 1, 0, 0, 1, -bx1, -by1};
#else
      // use gs to convert eps to pdf and take care of properly handling the bounding box
      // the resulting pdf image will always start at 0,0.
      string cmd= gs_prefix();
      cmd << " -dQUIET -dNOPAUSE -dBATCH -dSAFER -sDEVICE=pdfwrite ";
      cmd << " -sOutputFile=" << tempname << " ";
      cmd << " -c \" << /PageSize [ " << as_string(bx2-bx1) << " " << as_string(by2-by1)
          << " ] >> setpagedevice gsave  "
          << as_string(-bx1) << " " << as_string(-by1) << " translate \" ";
      cmd << " -f " << sys_concretize (name);
      cmd << " -c \" grestore \"  ";
      //cout << cmd << LF;
      system(cmd);
#endif
    }
    EStatusCode status = PDFHummus::eSuccess;
    DocumentContext& dc = pdfw.GetDocumentContext();

    PDFDocumentCopyingContext *copyingContext = pdfw.CreatePDFCopyingContext((char*)fname);
    do {
      if(!copyingContext) break;
      PDFFormXObject *form = dc.StartFormXObject(cropBox, id, tMat);
      status = copyingContext->MergePDFPageToFormXObject(form,0);
      if(status != eSuccess) break;
      pdfw.EndFormXObjectAndRelease(form);
    } while (false);
    if (copyingContext) {
      delete copyingContext;
      copyingContext = NULL;
    }
    remove (temp);

    if (status == PDFHummus::eFailure) {
      cerr <<  "pdf_hummus: failed to include image file:" << fname << LF;
    }
  }
}; // class pdf_image_ref

class pdf_image {
  CONCRETE_NULL(pdf_image);
  pdf_image (url _u, ObjectIDType _id):
    rep (tm_new<pdf_image_rep> (_u,_id)) {};
};

CONCRETE_NULL_CODE(pdf_image);


void
pdf_hummus_renderer_rep::flush_images ()
{
  // flush all images
  iterator<tree> it = iterate (image_pool);
  while (it->busy()) {
    pdf_image im = image_pool[it->next()];
    im->flush(pdfWriter);
  }
}


void
pdf_hummus_renderer_rep::image (
  url u, SI w, SI h, SI x, SI y,
  double cx1, double cy1, double cx2, double cy2,
  int alpha)
{
  //cerr << "image " << u << LF;
  (void) alpha; // FIXME

  tree lookup= tuple (u->t);
  pdf_image im = ( image_pool->contains(lookup) ? image_pool[lookup] : pdf_image() );
  
  if (is_nil(im)) {
    im = pdf_image(u, pdfWriter.GetObjectsContext().GetInDirectObjectsRegistry().AllocateNewObjectID());
    image_pool(lookup) = im;
  }

  if (is_nil(im)) return;
  
  double sc_x= ((double) (w/pixel)) / ((double) (cx2-cx1));
  double sc_y= ((double) (h/pixel)) / ((double) (cy2-cy1));
  
  end_text();
  
  contentContext->q();
  contentContext->cm(sc_x,0,0,sc_y,to_x(x), to_y(y));
  std::string pdfFormName = page->GetResourcesDictionary().AddFormXObjectMapping(im->id);
  contentContext->Do(pdfFormName);
  contentContext->Q();

}


void
pdf_hummus_renderer_rep::draw_picture (picture p, SI x, SI y, int alpha) {
  (void) alpha; // FIXME
  int w= p->get_width (), h= p->get_height ();
  int ox= p->get_origin_x (), oy= p->get_origin_y ();
  //int pixel= 5*PIXEL;
  string name= "picture";
  string eps= picture_as_eps (p, 600);
  int x1= -ox;
  int y1= -oy;
  int x2= w - ox;
  int y2= h - oy;
  x -= (int) 2.06 * ox * pixel; // FIXME: where does the magic 2.06 come from?
  y -= (int) 2.06 * oy * pixel;
  
  url temp= url_temp (".eps");
  save_string(temp, eps);
  image (temp, w * pixel, h * pixel, x, y, x1, y1, x2, y2,  255);
  remove(temp);
}

void
pdf_hummus_renderer_rep::draw_scalable (scalable im, SI x, SI y, int alpha) {
  if (im->get_type () != scalable_image)
    renderer_rep::draw_scalable (im, x, y, alpha);
  else {
    url u= im->get_name ();
    rectangle r= im->get_logical_extents ();
    SI w= r->x2, h= r->y2;
    //string ps_image= ps_load (u);
    //string imtext= is_ramdisc (u)? "inline image": as_string (u);
    int x1, y1, x2, y2;
    ps_bounding_box (u, x1, y1, x2, y2);
    image (u,w, h, x, y, x1, y1, x2, y2,  alpha);
  }
}


/******************************************************************************
 * hyperlinks
 ******************************************************************************/

static string
prepare_text (string s) {
  int i;
  string r;
  for (i=0; i<N(s); i++) {
    int c= ((unsigned char) s[i]);
    if ((s[i]=='(') || (s[i]==')') || (s[i]=='\\'))
      r << '\\' << s[i];
    else if ((c <= 32) || (c >= 128)) {
      r << '\\';
      r << ('0' + (c >> 6));
      r << ('0' + ((c >> 3) & 7));
      r << ('0' + (c & 7));
    }
    else r << s[i];
  }
  return r;
}

PDFHummus::EStatusCode
pdf_hummus_renderer_rep::on_catalog_write (CatalogInformation* inCatalogInformation,
                                         DictionaryContext* inCatalogDictionaryContext,
                                         ObjectsContext* inPDFWriterObjectContext,
                                         PDFHummus::DocumentContext* inDocumentContext)
{
  if (destId) {
    inCatalogDictionaryContext->WriteKey("Dests");
    inCatalogDictionaryContext->WriteNewObjectReferenceValue(destId);
  }
  if (outlineId) {
    inCatalogDictionaryContext->WriteKey("Outlines");
    inCatalogDictionaryContext->WriteNewObjectReferenceValue(outlineId);
  }
  return PDFHummus::eSuccess;
}

int pdf_hummus_renderer_rep::get_label_id(string label)
{
  if (!(label_id->contains(label))) {
    label_id(label) = label_count;
    label_count++;
  }
  return label_id(label);
}

void
pdf_hummus_renderer_rep::anchor (string label, SI x, SI y)
{
  string l = prepare_text (label);
  dests << dest_data(l, page_num, to_x(x), to_y(y));
}

void
pdf_hummus_renderer_rep::href (string label, SI x1, SI y1, SI x2, SI y2)
{
  bool preserve= (get_locus_rendering ("locus-on-paper") == "preserve");
  ObjectIDType annotId = pdfWriter.GetObjectsContext().GetInDirectObjectsRegistry().AllocateNewObjectID();
  pdfWriter.GetDocumentContext().RegisterAnnotationReferenceForNextPageWrite(annotId);
  string dict;
  dict << "<<\r\n\t/Type /Annot\r\n\t/Subtype /Link\r\n";
//  dict << "\t/Border [1.92 1.92 0.12[]]\r\n\t/Color [0.75 0.5 1.0]\r\n";
  if (preserve)
    dict << "\t/Border [16 16 1 [3 10]] /Color [0.75 0.5 1.0]\r\n";
  else
    dict << "\t/Border [16 16 0 [3 10]] /Color [0.75 0.5 1.0]\r\n";
  dict << "\t/Rect [" << as_string(((double)default_dpi / dpi)*to_x(x1 - 5*pixel)) << " ";
  dict << as_string(((double)default_dpi / dpi)*to_y(y1 - 10*pixel)) << " ";
  dict << as_string(((double)default_dpi / dpi)*to_x(x2 + 5*pixel)) << " ";
  dict << as_string(((double)default_dpi / dpi)*to_y(y2 + 10*pixel)) << "]\r\n";
  if (starts (label, "#")) {
    dict << "\t/Dest /label" << as_string(get_label_id(prepare_text (label))) << "\r\n";
  }
  else {
    dict << "/Action << /Subtype /URI /URI (" << prepare_text (label) << ") >>\r\n";
  }
  dict << ">>\r\n";
  annot_list (annotId) = dict;
}


void
pdf_hummus_renderer_rep::flush_dests()
{
  if (is_nil(dests)) return;

  // flush destinations
  
  string dict;
  dict << "<<\r\n";
  list<dest_data> it = dests;
  while (!is_nil(it)) {
    string label = it->item.x1;
    int dest_page = it->item.x2;
    int dest_x = it->item.x3;
    int dest_y = it->item.x4;
    {
      dict << "\t\t/label" << as_string(get_label_id(label)) << " [ " << as_string(page_id(dest_page)) << " 0 R /XYZ "
           << as_string(((double)default_dpi / dpi)*dest_x) << " " << as_string(((double)default_dpi / dpi)*dest_y) << " null ]\r\n";
    }
    it = it->next;
  }
  dict << ">>\r\n";
  {
    // flush the buffer
    ObjectsContext& objectsContext = pdfWriter.GetObjectsContext();
    destId = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();
    write_indirect_obj(objectsContext, destId, dict);
  }
}



void
pdf_hummus_renderer_rep::toc_entry (string kind, string title, SI x, SI y) {
  (void) kind; (void) title; (void) x; (void) y;
  //cout << kind << ", " << title << "\n";
  int ls= 1;
  if (kind == "toc-strong-1") ls= 1;
  if (kind == "toc-strong-2") ls= 2;
  if (kind == "toc-1") ls= 3;
  if (kind == "toc-2") ls= 4;
  if (kind == "toc-3") ls= 5;
  if (kind == "toc-4") ls= 6;
  if (kind == "toc-5") ls= 7;

  outlines << outline_data(title, page_num, to_x(x), to_y(y), ls);
}


void pdf_hummus_renderer_rep::recurse (ObjectsContext& objectsContext, list<outline_data>& it, ObjectIDType parentId,
              ObjectIDType& firstId, ObjectIDType& lastId, int &count)
{
  // weave the tangle of forward/backward references
  // are recurse over substructures
  
  ObjectIDType prevId = 0, curId = 0, nextId = 0;
  
  if (!is_nil(it))
    curId = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();

  firstId = curId;
  while (curId) {
    ObjectIDType subFirstId = 0, subLastId = 0;
    int subCount = 0;

    outline_data oitem = it->item;
    count++;
    it = it->next;
    
    if (!is_nil(it) && ((it->item).x5 > oitem.x5)) {
      // go down in level
      recurse(objectsContext, it, curId, subFirstId, subLastId, subCount);
    }
    
    // continue at this level or above
    if (!is_nil(it) && ((it->item).x5 == oitem.x5)) {
      nextId = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();
    } else {
      // finished this level, go up.
      nextId = 0;
    }
    
    {
      // write current outline item dictionary
      string dict;
      dict << "<<\r\n"
           << "\t/Title (" << prepare_text((oitem).x1) << ")\r\n"
           << "\t/Parent " << as_string(parentId) << " 0 R \r\n";
      if (prevId != 0) dict << "\t/Prev " << as_string(prevId) << " 0 R \r\n";
      if (nextId  != 0)  dict << "\t/Next " << as_string(nextId) << " 0 R \r\n";
      if (subCount > 0) {
        dict << "\t/First " << as_string(subFirstId) << " 0 R \r\n"
             << "\t/Last " << as_string(subLastId) << " 0 R \r\n"
             << "\t/Count " << as_string(-subCount) << "\r\n";
      }
      dict << "\t/Dest [ " << as_string(page_id((oitem).x2)) << " 0 R /XYZ "
           << as_string(((double)default_dpi / dpi)*((oitem).x3))
           << " " << as_string(((double)default_dpi / dpi)*((oitem).x4)) << " null ]\r\n"
           << ">>\r\n";
      write_indirect_obj(objectsContext, curId, dict);
    }
    prevId = curId; curId = nextId;
  }
  lastId = prevId;
}

void
pdf_hummus_renderer_rep::flush_outlines()
{
  if (is_nil(outlines)) return;
 
  ObjectsContext& objectsContext= pdfWriter.GetObjectsContext();
  ObjectIDType firstId= 0 , lastId= 0;
  int count = 0;
  list<outline_data> it = outlines;
  
  outlineId = objectsContext.GetInDirectObjectsRegistry().AllocateNewObjectID();
  recurse(objectsContext, it, outlineId, firstId, lastId, count);
  {
    // create top-level outline dictionary
    string dict;
    dict << "<<\r\n";
    dict << "\t/Type /Outlines\r\n";
    dict << "\t/First " << as_string(firstId) << " 0 R \r\n";
    dict << "\t/Last " << as_string(lastId) << " 0 R \r\n";
    dict << "\t/Count " << as_string(count) << "\r\n";
    dict << ">>\r\n";
    write_indirect_obj(objectsContext, outlineId, dict);
  }
}

/******************************************************************************
 * shadow rendering in trivial on pdf
 ******************************************************************************/

void
pdf_hummus_renderer_rep::fetch (SI x1, SI y1, SI x2, SI y2, renderer ren, SI x, SI y) {
//  cerr << "fetch\n";
/*  (void) x1; (void) y1; (void) x2; (void) y2;
  (void) ren; (void) x; (void) y;*/
}

void
pdf_hummus_renderer_rep::new_shadow (renderer& ren) {
//  cerr << "new_shadow\n";
//  (void) ren;
}

void
pdf_hummus_renderer_rep::delete_shadow (renderer& ren) {
//  cerr << "delete_shadow\n";
//  (void) ren;
}

void
pdf_hummus_renderer_rep::get_shadow (renderer ren, SI x1, SI y1, SI x2, SI y2) {
//  cerr << "get_shadow\n";
//  (void) ren; (void) x1; (void) y1; (void) x2; (void) y2;
}

void
pdf_hummus_renderer_rep::put_shadow (renderer ren, SI x1, SI y1, SI x2, SI y2) {
// cerr << "put_shadow\n";
//  (void) ren; (void) x1; (void) y1; (void) x2; (void) y2;
}

void
pdf_hummus_renderer_rep::apply_shadow (SI x1, SI y1, SI x2, SI y2) {
//  cerr << "apply_shadow\n";
//  (void) x1; (void) y1; (void) x2; (void) y2;
}

renderer
pdf_hummus_renderer_rep::shadow (picture& pic, SI x1, SI y1, SI x2, SI y2) {
  renderer ren= renderer_rep::shadow (pic, x1, y1, x2, y2);
  ren->set_zoom_factor (1.0);
  return ren;
}


/******************************************************************************
* user interface
******************************************************************************/

renderer
pdf_hummus_renderer (url pdf_file_name, int dpi, int nr_pages,
	 string page_type, 
              bool landscape, 
              double paper_w, 
              double paper_h)
{
  page_type= as_string (call ("standard-paper-size", object (page_type)));
  return tm_new<pdf_hummus_renderer_rep> (pdf_file_name, dpi, nr_pages,
			  page_type, landscape, paper_w, paper_h);
}


