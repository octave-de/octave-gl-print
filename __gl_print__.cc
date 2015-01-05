// Copyright (C) 2015 Andreas Weber <andy.weber.aw@gmail.com>
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, see <http://www.gnu.org/licenses/>.
//
// This code is based on Brian Pauls' src/osdemos/osdemo.c
// from git://anongit.freedesktop.org/mesa/demos

#include <octave/oct.h>
#include <sys/types.h>
#include "sysdep.h"
#include <string>
#include "GL/osmesa.h"

#include "gl-render.h"
#include "gl2ps-renderer.h"
#include "graphics.h"

DEFUN_DLD(__gl_print__, args, nargout,
          "-*- texinfo -*-\n\
@deftypefn {Loadable Function}  __gl_print__ (@var{h}, @var{filename}, @var{term})\n\
@deftypefnx {Loadable Function} {@var{img}  =} __gl_print__ (@var{h})\n\
Print figure @var{h} using OSMesa and gl2ps for vector formats.\n\
\n\
The first method calls gl2ps with the appropriate @var{term} and writes\n\
the output of gl2ps to @var{filename}.\n\
\n\
Valid options for @var{term}, which can be concatenated in one string, are:\n\
@table @asis\n\
@item @qcode{eps}, @qcode{pdf}, @qcode{ps}, @qcode{svg}, @qcode{pgf}, @qcode{tex}\n\
Select output format.\n\
@item @qcode{is2D}\n\
Use GL2PS_SIMPLE_SORT instead of GL2PS_BSP_SORT as Z-depth sorting algorithm.\n\
@item @qcode{notext}\n\
Don't render text.\n\
@end table\n\
\n\
The second method doesn't use gl2ps and returns a RGB image in @var{img} instead.\n\
\n\
@end deftypefn")
{
  octave_value_list retval;

  int nargin = args.length ();

  if (! (nargin == 1 || nargin == 3))
    {
      print_usage ();
      return retval;
    }

  if ((nargin == 3))
    {
      if(! (args(1).is_string () && args(2).is_string ()))
        {
          error ("__gl_print__: FILENAME and TERM has to be strings");
          return retval;
        }

      #ifndef HAVE_GL2PS_H
        error ("Octave has been compiled without gl2ps");
        return retval;
      #endif
    }

  int h = args(0).double_value ();
  graphics_object fobj = gh_manager::get_object (h);
  if (! (fobj &&  fobj.isa ("figure")))
    {
      error ("H has to be a valid figure handle");
      return retval;
    }

  figure::properties& fp =
    dynamic_cast<figure::properties&> (fobj.get_properties ());

  bool internal = true;
  Matrix bb = fp.get_boundingbox (internal);

  int Width = bb(2);
  int Height = bb(3);

  OSMesaContext ctx;
  void *buffer;

  // Create an RGBA-mode context
  #if OSMESA_MAJOR_VERSION * 100 + OSMESA_MINOR_VERSION >= 305
  // specify Z, stencil, accum sizes
  ctx = OSMesaCreateContextExt( OSMESA_RGBA, 16, 0, 0, NULL );
  #else
  ctx = OSMesaCreateContext( OSMESA_RGBA, NULL );
  #endif
  if (! ctx)
    {
      printf("OSMesaCreateContext failed!\n");
      return retval;
    }

  // Allocate the image buffer
  buffer = malloc (Width * Height * 4 * sizeof (GLubyte));
  if (! buffer)
    {
      printf ("Alloc image buffer failed!\n");
      return retval;
    }

  // Bind the buffer to the context and make it current
  if (! OSMesaMakeCurrent (ctx, buffer, GL_UNSIGNED_BYTE, Width, Height))
    {
      printf ("OSMesaMakeCurrent failed!\n");
      return retval;
    }

  int z, s, a;
  glGetIntegerv (GL_DEPTH_BITS, &z);
  glGetIntegerv (GL_STENCIL_BITS, &s);
  glGetIntegerv (GL_ACCUM_RED_BITS, &a);
  printf ("Depth=%d Stencil=%d Accum=%d\n", z, s, a);

  // check if the figure is visible
  bool v = fp.is_visible ();

  if (nargin == 3)
    {
      std::string filename  = args(1).string_value ();
      std::string term      = args(2).string_value ();

      // debug output
      octave_stdout << "Using gl2ps Width=" << Width
                    << " Height=" << Height
                    << " Filename=" << filename
                    << " Term=" << term << std::endl;

      FILE *filep;
      filep = fopen (filename.c_str (), "w");
      if (filep)
        {
          glps_renderer rend (filep, term);
          rend.draw (fobj, "");

          fclose (filep);
        }
      else
        error ("Couldn't create file \"%s\"", filename.c_str ());

    }
  else
    {
      // return RGB image
      opengl_renderer rend;
      rend.draw (fobj);

      dim_vector dv (4, Width, Height);

      //We expect that GLubyte is 1byte long
      //adapt code if this isn't always true
      assert (sizeof (GLubyte) == 1);
      uint8NDArray img (dv);
      unsigned char *p = reinterpret_cast<unsigned char*>(img.fortran_vec());
      memcpy(p, buffer, (4 * Width * Height));

      Array<octave_idx_type> perm (dim_vector (3, 1));
      perm(0) = 2;
      perm(1) = 1;
      perm(2) = 0;

      Array<idx_vector> idx (dim_vector (3, 1));
      // Flip Y
      idx(0) = idx_vector::make_range (Height - 1, -1, Height);
      //std::cout << idx(0) << std::endl;

      idx(1) = idx_vector::colon;

      // Remove alpha channel
      idx(2) = idx_vector (0, 3); //remo

      retval = octave_value (img.permute (perm). index(idx));
    }

  // HACK for graphics_toolkit FLTK:
  // toggle figure visibility
  // If the figure previously was shown with FLTK, this causes
  // an update with ID = base_properties::ID_VISIBLE
  // which calls
  // -> do_toggle_window_visibility
  // -> show_canvas
  // -> canvas.make_current ()
  // which selects the FLTK OpenGL context again.
  // This is important because OSMesaMakeCurrent above changed the current context.
  if (v)
    {
      fp.set_visible ("off");
      fp.set_visible ("on");
      //rend.set_viewport (Width, Height);
    }

  // free the image buffer
  free (buffer);

  // destroy the context
  OSMesaDestroyContext ( ctx );

  return retval;
}

