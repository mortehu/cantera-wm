#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include <X11/extensions/Xrender.h>

#include "cantera-wm.h"
#include "menu.h"

using namespace cantera_wm;
#define SMALL 0
static const int yskips[] = { 10, 15 };

static const XTransform xform_identity =
{
    {
        { 0x10000, 0, 0 },
        { 0, 0x10000, 0 },
        { 0, 0, 0x10000 }
    }
};

void
menu_thumbnail_dimensions (const screen& scr, unsigned int* width, unsigned int* height, unsigned int* margin);

void
menu_draw_desktops (const struct screen& scr);

void
menu_init (void)
{
  for (auto &screen : current_session.screens)
    {
      unsigned int previous_width, previous_height;
      unsigned int current_width, current_height;
      unsigned int thumb_width, thumb_height;
      XTransform xform_scaled;
      Picture temp_picture;

      menu_thumbnail_dimensions (screen, &thumb_width, &thumb_height, NULL);

      previous_width = screen.geometry.width;
      previous_height = screen.geometry.height;

      for (;;)
        {
          Pixmap temp_pixmap;

          current_width = previous_width >> 1;
          current_height = previous_height >> 1;

          if (current_width <= thumb_width)
            current_width = thumb_width;

          if (current_height <= thumb_height)
            current_height = thumb_height;

          xform_scaled = xform_identity;
          xform_scaled.matrix[2][2] = XDoubleToFixed((double) current_width / previous_width);

          if (screen.resize_buffers.empty ())
            screen.initial_transform = xform_scaled;
          else
            XRenderSetPictureTransform (x_display, temp_picture, &xform_scaled);

          if (current_width == thumb_width)
            break;

          temp_pixmap = XCreatePixmap (x_display, x_root_window, current_width, current_height, 32);

          temp_picture = XRenderCreatePicture (x_display, temp_pixmap, XRenderFindStandardFormat (x_display, PictStandardARGB32), 0, 0);

          XRenderSetPictureFilter (x_display, temp_picture, FilterBilinear, 0, 0);

          XFreePixmap (x_display, temp_pixmap);

          screen.resize_buffers.push_back (temp_picture);

          previous_width = current_width;
          previous_height = current_height;
        }
    }
}

void
menu_thumbnail_dimensions (const screen& scr, unsigned int* width, unsigned int* height, unsigned int* margin)
{
  unsigned int tmp_margin, tmp_width;

  tmp_width = scr.geometry.width / 14;
  tmp_margin = (scr.geometry.width - tmp_width * 12) / 17;

  if (width)
    *width = tmp_width;

  if (height)
    *height = scr.geometry.height * tmp_width / scr.geometry.width;

  if(margin)
    *margin = tmp_margin;
}

void
menu_draw (const struct screen& scr)
{
  unsigned int thumb_width, thumb_height, thumb_margin;

  menu_thumbnail_dimensions(scr, &thumb_width, &thumb_height, &thumb_margin);

  menu_draw_desktops(scr);
}

void
menu_draw_desktops (const struct screen& scr)
{
  unsigned int thumb_width, thumb_height, thumb_margin;
  unsigned int i;
  int x = 0, y;
  time_t ttnow;
  struct tm* tmnow;
  wchar_t wbuf[256];
  char buf[256];

  menu_thumbnail_dimensions(scr, &thumb_width, &thumb_height, &thumb_margin);

  ttnow = time(0);
  tmnow = localtime(&ttnow);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmnow);
  swprintf(wbuf, sizeof(wbuf), L"%s", buf);

  swprintf (wcschr (wbuf, 0), sizeof (wbuf),
            L"  %s", PACKAGE_STRING);

  x = thumb_margin;

  for(i = 0; i < 24; ++i)
    {
      XRenderColor border_color;
      unsigned int buffer_width, buffer_height;

      x = thumb_margin + (i % 12) * (thumb_width + thumb_margin);

      if((i % 12) > 7)
        x += 4 * thumb_margin;
      else if((i % 12) > 3)
        x += 2 * thumb_margin;

      if(i < 12)
        y = scr.geometry.height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL];
      else
        y = scr.geometry.height - thumb_height - thumb_margin - yskips[SMALL];

      border_color.alpha = 0xffff;

      if(i == scr.active_workspace) /* XXX: && scr == focused screen */
        {
          border_color.red = 0xffff;
          border_color.green = 0x5050;
          border_color.blue = 0x5050;
        }
      else if(scr.workspaces[i].empty ())
        {
          border_color.red = 0x5050;
          border_color.green = 0x5050;
          border_color.blue = 0x5050;
        }
      else
        {
          border_color.red = 0x7070;
          border_color.green = 0x7070;
          border_color.blue = 0x7070;
        }

      XRenderFillRectangle(x_display, PictOpSrc, scr.x_buffer, &border_color, x - 1, y - 1, 1, thumb_height + 2);
      XRenderFillRectangle(x_display, PictOpSrc, scr.x_buffer, &border_color, x + thumb_width, y - 1, 1, thumb_height + 2);
      XRenderFillRectangle(x_display, PictOpSrc, scr.x_buffer, &border_color, x - 1, y - 1, thumb_width + 2, 1);
      XRenderFillRectangle(x_display, PictOpSrc, scr.x_buffer, &border_color, x - 1, y + thumb_height, thumb_width + 2, 1);

      if (scr.workspaces[i].empty ())
        {
          XRenderColor fill_color;

          fill_color.red = 0x0000;
          fill_color.green = 0x0000;
          fill_color.blue = 0x0000;
          fill_color.alpha = 0x7f7f;

          XRenderFillRectangle (x_display,
                                PictOpOver,
                                scr.x_buffer,
                                &fill_color,
                                x, y,
                                thumb_width, thumb_height);

          continue;
        }

      for (auto &w : scr.workspaces[i])
        {
          int scaled_x, scaled_y, scaled_width, scaled_height;

          scaled_x = w->position.x >> 1;
          scaled_width = w->position.width >> 1;

          scaled_y = w->position.y >> 1;
          scaled_height = w->position.height >> 1;

          XRenderSetPictureTransform (x_display, w->x_picture, (XTransform *) &scr.initial_transform);
          XRenderSetPictureFilter (x_display, w->x_picture, FilterBilinear, 0, 0);

          XRenderComposite (x_display,
                            PictOpSrc,
                            w->x_picture,
                            None,
                            scr.resize_buffers.front (),
                            0, 0,
                            0, 0,
                            scaled_x, scaled_y,
                            scaled_width, scaled_height);

          XRenderSetPictureTransform (x_display, w->x_picture, (XTransform *) &xform_identity);
          XRenderSetPictureFilter (x_display, w->x_picture, FilterNearest, 0, 0);
        }

      buffer_width = scr.geometry.width;
      buffer_height = scr.geometry.height;

      for (size_t i = 1; i < scr.resize_buffers.size (); ++i)
        {
          buffer_width >>= 1;
          buffer_height >>= 1;

          XRenderComposite (x_display,
                            PictOpSrc,
                            scr.resize_buffers[i - 1],
                            None,
                            scr.resize_buffers[i],
                            0, 0,
                            0, 0,
                            0, 0,
                            buffer_width, buffer_height);
        }

      XRenderComposite (x_display,
                        PictOpSrc,
                        scr.resize_buffers.back (),
                        None,
                        scr.x_buffer,
                        0, 0,
                        0, 0,
                        x, y,
                        thumb_width, thumb_height);
    }
}
