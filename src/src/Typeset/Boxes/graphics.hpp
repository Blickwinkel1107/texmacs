
/******************************************************************************
* MODULE     : graphics.hpp
* DESCRIPTION: construction routines for graphical boxes
* COPYRIGHT  : (C) 2003  Joris van der Hoeven
*******************************************************************************
* This software falls under the GNU general public license and comes WITHOUT
* ANY WARRANTY WHATSOEVER. See the file $TEXMACS_PATH/LICENSE for more details.
* If you don't have this file, write to the Free Software Foundation, Inc.,
* 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
******************************************************************************/

#ifndef GRAPHICS_H
#define GRAPHICS_H
#include "boxes.hpp"
#include "Graphics/frame.hpp"
#include "Graphics/curve.hpp"
#include "Graphics/grid.hpp"
#include "display.hpp"

box graphics_box (
  path ip, array<box> bs, frame f, grid g, point lim1, point lim2);
box graphics_group_box (path ip, array<box> bs);
box textat_box (path ip, box b, SI x, SI y);
box point_box (
  path ip, point p, SI r, color col, int fill, color fill_col, string style);
box curve_box (path ip, curve c, SI width, color col,
  array<bool> style, SI style_unit,
  int fill, color fill_col,
  array<box> arrows);
box grid_box (path ip, grid g, frame f, SI un, point lim1, point lim2);

#endif // defined GRAPHICS_H
