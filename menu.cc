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

#define RESIZE_BUFFERS 3

unsigned int palette[] =
{
  /* ANSI colors */
  0xff000000, 0xff1818c2, 0xff18c218, 0xff18c2c2,
  0xffc21818, 0xffc218c2, 0xffc2c218, 0xffc2c2c2,
  0xff686868, 0xff7474ff, 0xff54ff54, 0xff54ffff,
  0xffff5454, 0xffff54ff, 0xffffff54, 0xffffffff,
};
static XRenderColor xrpalette[16];
static Picture resize_buffers[RESIZE_BUFFERS];

using namespace cantera_wm;
#define SMALL 0
static const int yskips[] = { 10, 15 };

void
menu_draw_desktops (const struct screen& scr);

void
menu_init (void)
{
  size_t i;
  size_t max_width = 0, max_height = 0;

  for (auto &screen : current_session.screens)
    {
      if (screen.geometry.width > max_width)
        max_width = screen.geometry.width;

      if (screen.geometry.height > max_height)
        max_height = screen.geometry.height;
    }

  for (i = 0; i < RESIZE_BUFFERS; ++i)
    {
      Pixmap temp_pixmap;

      temp_pixmap = XCreatePixmap(x_display, x_root_window, max_width >> (i + 1), max_height >> (i + 1), 32);

      resize_buffers[i] = XRenderCreatePicture(x_display, temp_pixmap, XRenderFindStandardFormat(x_display, PictStandardARGB32), 0, 0);

      XRenderSetPictureFilter(x_display, resize_buffers[i], FilterBilinear, 0, 0);

      XFreePixmap(x_display, temp_pixmap);
    }

  for(i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i)
    {
      xrpalette[i].alpha = ((palette[i] & 0xff000000) >> 24) * 0x0101;
      xrpalette[i].red = ((palette[i] & 0xff0000) >> 16) * 0x0101;
      xrpalette[i].green = ((palette[i] & 0x00ff00) >> 8) * 0x0101;
      xrpalette[i].blue = (palette[i] & 0x0000ff) * 0x0101;
    }
}

void
menu_thumbnail_dimensions (const screen& scr, int* width, int* height, int* margin)
{
  int tmp_margin = 10;
  int tmp_width;

  tmp_width = (scr.geometry.width - tmp_margin * 17) / 12;

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
  int thumb_width, thumb_height, thumb_margin;

  menu_thumbnail_dimensions(scr, &thumb_width, &thumb_height, &thumb_margin);

  menu_draw_desktops(scr);
}

void
menu_draw_desktops (const struct screen& scr)
{
  int thumb_width, thumb_height, thumb_margin;
  unsigned int i;
  int x = 0, y;
  time_t ttnow;
  struct tm* tmnow;
  wchar_t wbuf[256];
  char buf[256];

  XTransform xform_scaled;
  XTransform xform_identity =
    {
        {
            { 0x10000, 0, 0 },
            { 0, 0x10000, 0 },
            { 0, 0, 0x10000 }
        }
    };

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

      xform_scaled = xform_identity;

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

          scaled_x = w->position.x * thumb_width / scr.geometry.width;
          scaled_width = w->position.width * thumb_width / scr.geometry.width;

          scaled_y = w->position.y * thumb_width / scr.geometry.width;
          scaled_height = w->position.height * thumb_width / scr.geometry.width;

          xform_scaled.matrix[2][2] = XDoubleToFixed((double) thumb_width / scr.geometry.width);
          XRenderSetPictureTransform (x_display, w->x_picture, &xform_scaled);

          XRenderComposite (x_display,
                            PictOpSrc,
                            w->x_picture,
                            None,
                            scr.x_buffer,
                            0, 0,
                            0, 0,
                            x + scaled_x, y + scaled_y,
                            scaled_width, scaled_height);

          XRenderSetPictureTransform (x_display, w->x_picture, &xform_identity);
        }
    }
}
