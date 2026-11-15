// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "sdl_surface.hpp"
#include "geometry/triangle.hpp"

#include <cmath>

namespace blunted {

  SDL_Surface *CreateSDLSurface(int width, int height) {
    return SDL_CreateRGBSurface(0, width, height, 32, r_mask, g_mask, b_mask, a_mask);
  }

  void sdl_putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel) {

    int bpp = surface->format->BytesPerPixel;
    // Here p is the address to the pixel we want to set
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
  }

  Uint32 sdl_getpixel(const SDL_Surface *surface, int x, int y) {

    int bpp = surface->format->BytesPerPixel;
    // Here p is the address to the pixel we want to retrieve
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        return *p;

    case 2:
        return *(Uint16 *)p;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;

    case 4:
        return *(Uint32 *)p;

    default:
        return 0; // shouldn't happen, but avoids warnings
    }
  }

  static inline void sdl_blend_pixel32(SDL_Surface *surface, int x, int y, Uint8 sr, Uint8 sg, Uint8 sb, Uint8 sa, Uint32 solidPixel) {
    if (x < 0 || y < 0 || x >= surface->w || y >= surface->h) return;
    Uint8 *dst = (Uint8 *)surface->pixels + y * surface->pitch + x * 4;
    if (sa >= 254 || dst[3] == 0) {
      *(Uint32 *)dst = solidPixel;
      return;
    }
    unsigned int invA = 255u - (unsigned int)sa;
    dst[0] = (Uint8)(((unsigned int)sr * sa + (unsigned int)dst[0] * invA) / 255u);
    dst[1] = (Uint8)(((unsigned int)sg * sa + (unsigned int)dst[1] * invA) / 255u);
    dst[2] = (Uint8)(((unsigned int)sb * sa + (unsigned int)dst[2] * invA) / 255u);
    unsigned int outA = (unsigned int)sa + ((unsigned int)dst[3] * invA) / 255u;
    dst[3] = (Uint8)(outA > 255u ? 255u : outA);
  }

  static inline int cs_outcode(int x, int y, int xmax, int ymax) {
    int code = 0;
    if (x < 0) code |= 1;
    else if (x > xmax) code |= 2;
    if (y < 0) code |= 4;
    else if (y > ymax) code |= 8;
    return code;
  }

  void sdl_line(SDL_Surface *surface, int x1, int y1, int x2, int y2, Uint32 color) {
    if (!surface || surface->w <= 0 || surface->h <= 0) return;

    int xmax = surface->w - 1;
    int ymax = surface->h - 1;
    int code1 = cs_outcode(x1, y1, xmax, ymax);
    int code2 = cs_outcode(x2, y2, xmax, ymax);

    for (int clipIter = 0; clipIter < 8; clipIter++) {
      if ((code1 | code2) == 0) {
        break;
      }
      if ((code1 & code2) != 0) {
        return; // completely outside viewport
      }
      int codeOut = code1 ? code1 : code2;
      long long x = 0, y = 0;
      long long dx = (long long)x2 - (long long)x1;
      long long dy = (long long)y2 - (long long)y1;

      if (codeOut & 8) {
        x = x1 + (dy != 0 ? dx * (ymax - y1) / dy : 0);
        y = ymax;
      } else if (codeOut & 4) {
        x = x1 + (dy != 0 ? dx * (0 - y1) / dy : 0);
        y = 0;
      } else if (codeOut & 2) {
        y = y1 + (dx != 0 ? dy * (xmax - x1) / dx : 0);
        x = xmax;
      } else {
        y = y1 + (dx != 0 ? dy * (0 - x1) / dx : 0);
        x = 0;
      }

      if (codeOut == code1) {
        x1 = (int)x;
        y1 = (int)y;
        code1 = cs_outcode(x1, y1, xmax, ymax);
      } else {
        x2 = (int)x;
        y2 = (int)y;
        code2 = cs_outcode(x2, y2, xmax, ymax);
      }
    }
    if ((code1 | code2) != 0) return;

    Uint8 sr = 255, sg = 255, sb = 255, sa = 255;
    SDL_GetRGBA(color, surface->format, &sr, &sg, &sb, &sa);
    if (sa == 0) return;

    int dx = abs(x2 - x1);
    int sx = (x1 < x2) ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    bool useFast32 = (surface->format->BytesPerPixel == 4 && surface->format->Rmask == 0x000000FF && surface->pixels != NULL);

    int maxSteps = surface->w + surface->h + 4;
    for (int step = 0; step < maxSteps; step++) {
      if (useFast32) {
        sdl_blend_pixel32(surface, x1, y1, sr, sg, sb, sa, color);
      } else if (x1 >= 0 && y1 >= 0 && x1 <= xmax && y1 <= ymax) {
        sdl_putpixel(surface, x1, y1, color);
      }
      if (x1 == x2 && y1 == y2) break;
      int e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x1 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y1 += sy;
      }
    }
  }

  void sdl_triangle_filled_rgba(SDL_Surface *surface, const Triangle &triangle, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (!surface || !surface->pixels || surface->w <= 0 || surface->h <= 0 || a == 0) return;

    float x0 = triangle.GetVertex(0).coords[0];
    float y0 = triangle.GetVertex(0).coords[1];
    float x1 = triangle.GetVertex(1).coords[0];
    float y1 = triangle.GetVertex(1).coords[1];
    float x2 = triangle.GetVertex(2).coords[0];
    float y2 = triangle.GetVertex(2).coords[1];
    if (!std::isfinite(x0) || !std::isfinite(y0) ||
        !std::isfinite(x1) || !std::isfinite(y1) ||
        !std::isfinite(x2) || !std::isfinite(y2)) {
      return;
    }

    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (fabs(area) < 0.5f) return;
    if (area < 0.0f) {
      std::swap(x1, x2);
      std::swap(y1, y2);
    }

    int minX = std::max(0, (int)floor(std::min(x0, std::min(x1, x2))));
    int maxX = std::min(surface->w - 1, (int)ceil(std::max(x0, std::max(x1, x2))));
    int minY = std::max(0, (int)floor(std::min(y0, std::min(y1, y2))));
    int maxY = std::min(surface->h - 1, (int)ceil(std::max(y0, std::max(y1, y2))));
    if (minX > maxX || minY > maxY) return;

    Uint32 solidPixel = SDL_ISPIXELFORMAT_ALPHA(surface->format->format)
                            ? SDL_MapRGBA(surface->format, r, g, b, a)
                            : SDL_MapRGB(surface->format, r, g, b);
    bool useFast32 = (surface->format->BytesPerPixel == 4 && surface->format->Rmask == 0x000000FF);

    float dx01 = x1 - x0, dy01 = y1 - y0;
    float dx12 = x2 - x1, dy12 = y2 - y1;
    float dx20 = x0 - x2, dy20 = y0 - y2;

    unsigned int sa = (unsigned int)a;
    unsigned int invA = 256u - sa;
    unsigned int srPre = (unsigned int)r * sa;
    unsigned int sgPre = (unsigned int)g * sa;
    unsigned int sbPre = (unsigned int)b * sa;
    float startFx = (float)minX + 0.5f;

    for (int py = minY; py <= maxY; py++) {
      float fy = (float)py + 0.5f;
      float e0 = dx01 * (fy - y0) - dy01 * (startFx - x0);
      float e1 = dx12 * (fy - y1) - dy12 * (startFx - x1);
      float e2 = dx20 * (fy - y2) - dy20 * (startFx - x2);

      if (useFast32) {
        Uint32 *row32 = (Uint32 *)((Uint8 *)surface->pixels + py * surface->pitch);
        for (int px = minX; px <= maxX; px++) {
          if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
            Uint32 dstVal = row32[px];
            if ((dstVal >> 24) == 0 || sa >= 254u) {
              row32[px] = solidPixel;
            } else {
              Uint8 *dst = (Uint8 *)(&row32[px]);
              dst[0] = (Uint8)((srPre + (unsigned int)dst[0] * invA) >> 8);
              dst[1] = (Uint8)((sgPre + (unsigned int)dst[1] * invA) >> 8);
              dst[2] = (Uint8)((sbPre + (unsigned int)dst[2] * invA) >> 8);
              unsigned int outA = sa + (((unsigned int)dst[3] * invA) >> 8);
              dst[3] = (Uint8)(outA > 255u ? 255u : outA);
            }
          }
          e0 -= dy01;
          e1 -= dy12;
          e2 -= dy20;
        }
      } else {
        for (int px = minX; px <= maxX; px++) {
          if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
            sdl_putpixel(surface, px, py, solidPixel);
          }
          e0 -= dy01;
          e1 -= dy12;
          e2 -= dy20;
        }
      }
    }
  }

  void sdl_triangle_filled(SDL_Surface *surface, const Triangle &triangle, Uint8 r, Uint8 g, Uint8 b) {
    sdl_triangle_filled_rgba(surface, triangle, r, g, b, 255);
  }

  void sdl_rectangle_filled(SDL_Surface *surface, int x, int y, int width, int height, Uint32 color) {
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;
    SDL_FillRect(surface, &rect, color);
  }

  void sdl_alphablit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
    // http://listas.apesol.org/pipermail/sdl-libsdl.org/2002-August/029180.html
    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    int x, y;

    Uint8 *pdst;
    Uint8 *psrc;

    SDL_Rect srect;
    if (srcrect) {
      srect = SDL_Rect(*srcrect);
    } else {
      srect.x = 0;
      srect.y = 0;
      srect.w = src->w;
      srect.h = src->h;
    }

    SDL_Rect drect;
    if (dstrect) {
      drect = SDL_Rect(*dstrect);
    } else {
      drect.x = 0;
      drect.y = 0;
      drect.w = dst->w;
      drect.h = dst->h;
    }

    /* loop through all pixels */
    for (y = 0; y < (srect.h < drect.h ? srect.h : drect.h); y++) {
      for (x = 0; x < (srect.w < drect.w ? srect.w : drect.w); x++) {
        psrc = (Uint8 *)src->pixels
                    + (y + srect.y)*src->pitch
                          + (x + srect.x)*src->format->BytesPerPixel;

        /* copy value from source to destination */
        pdst = (Uint8 *)dst->pixels
                  + (y + drect.y)*dst->pitch
                        + (x + drect.x)*dst->format->BytesPerPixel;

        float srcOpacity = psrc[3] / 256.0f;
        float dstOpacity = pdst[3] / 256.0f;
        float srcBias = srcOpacity;
        pdst[0] = psrc[0] * srcBias + pdst[0] * (1.0f - srcBias);
        pdst[1] = psrc[1] * srcBias + pdst[1] * (1.0f - srcBias);
        pdst[2] = psrc[2] * srcBias + pdst[2] * (1.0f - srcBias);
        pdst[3] = psrc[3] * srcBias + pdst[3] * (1.0f - srcBias);

      }
    }

    SDL_UnlockSurface(src);
    SDL_UnlockSurface(dst);
  }

  void sdl_flipsurface(SDL_Surface *surface) {
    // tnx Sebastian Beschke!
    SDL_Surface *result = SDL_CreateRGBSurface(surface->flags, surface->w, surface->h,
      surface->format->BytesPerPixel * 8, surface->format->Rmask, surface->format->Gmask,
      surface->format->Bmask, surface->format->Amask);

    Uint8 *pixels = (Uint8*)surface->pixels;
    Uint8 *rpixels = (Uint8*)result->pixels;
    Uint32 pitch = surface->pitch;
    Uint32 pxlength = pitch * surface->h;

    for (int line = 0; line < surface->h; ++line) {
      int pos = line * pitch;
      memcpy(&rpixels[pos], &pixels[pxlength - pos - pitch], pitch);
    }

    memcpy(&pixels[0], &rpixels[0], pxlength);
    SDL_FreeSurface(result);
  }

  void sdl_setsurfacealpha(SDL_Surface *surface, int alpha) {
    // original from: Warren Schwader
    // sdl at libsdl.org

    // the surface width and height
    int width = surface->w;
    int height = surface->h;

    // the pitch of the surface. Used to determine where the next vertical line in pixels starts
    int pitch = surface->pitch;
    int bpp = surface->format->BytesPerPixel;

    // NOTE: since we are only modifying the alpha bytes we could simply read only that byte and then add 4 to get
    // to the next alpha byte. That approach depends on the endianess of the alpha byte and also the 32 bit pixel depth
    // so if those things change then this code will need changing too. but we already rely on 32 bpp

    SDL_LockSurface(surface);
    for (int h = 0; h < height; h++) {

      for (int w = 0; w < width; w++) {
        Uint8 *p = (Uint8 *)surface->pixels + h * pitch + w * bpp;
        // todo: endiannes!! *update: seems to work as is.. maybe alpha is always in index 3 and only the colors are inverted?
        p[3] = clamp((p[3] / 256.0f) * (alpha / 256.0f) * 256, 0, 255);
      }

    }
    SDL_UnlockSurface(surface);

  }

}
