/* renderer.cpp — Direct2D + DirectWrite terminal cell renderer
 * C++ COM syntax: obj->Method() not obj->lpVtbl->Method()
 */
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "renderer.h"
#include "screen.h"
#include "../platform/config.h"
#include "font.h"
#include "../core/str_util.h"
#include "../core/log.h"

static D2D1_COLOR_F to_d2d(Color4F c) { D2D1_COLOR_F d={c.r,c.g,c.b,c.a}; return d; }

Color4F renderer_rgb_to_color(uint32_t rgb) {
    Color4F c; c.r=((rgb>>16)&0xFF)/255.0f; c.g=((rgb>>8)&0xFF)/255.0f;
    c.b=(rgb&0xFF)/255.0f; c.a=1.0f; return c;
}

static void build_palette(Renderer *r, const Config *cfg) {
    for (int i=0;i<16;i++) r->palette[i]=renderer_rgb_to_color(cfg->colors.ansi[i]);
    for (int i=16;i<232;i++) {
        int n=i-16, b=n%6; n/=6; int g=n%6; n/=6; int rd=n%6;
        r->palette[i].r=rd?(55+rd*40)/255.0f:0; r->palette[i].g=g?(55+g*40)/255.0f:0;
        r->palette[i].b=b?(55+b*40)/255.0f:0; r->palette[i].a=1.0f;
    }
    for (int i=232;i<256;i++) {
        float v=(8+(i-232)*10)/255.0f;
        r->palette[i].r=r->palette[i].g=r->palette[i].b=v; r->palette[i].a=1.0f;
    }
    r->bg_color=renderer_rgb_to_color(cfg->colors.background);
    r->fg_color=renderer_rgb_to_color(cfg->colors.foreground);
    r->cursor_color=renderer_rgb_to_color(cfg->colors.cursor);
    r->selection_color=renderer_rgb_to_color(cfg->colors.selection);
}

Color4F renderer_resolve_fg(const Renderer *r, const ScreenCell *cell) {
    if (cell->attr.fg_idx==0xFF) return renderer_rgb_to_color(cell->attr.fg_rgb);
    if (cell->attr.dim) { Color4F c=r->palette[cell->attr.fg_idx]; c.r*=.5f;c.g*=.5f;c.b*=.5f; return c; }
    return r->palette[cell->attr.fg_idx<256?cell->attr.fg_idx:7];
}
Color4F renderer_resolve_bg(const Renderer *r, const ScreenCell *cell) {
    if (cell->attr.bg_idx==0xFF) return renderer_rgb_to_color(cell->attr.bg_rgb);
    if (cell->attr.bg_idx==0) return r->bg_color;
    return r->palette[cell->attr.bg_idx<256?cell->attr.bg_idx:0];
}

static bool create_rt(Renderer *r) {
    if (r->render_target) { r->render_target->Release(); r->render_target=NULL; }
    RECT rc; GetClientRect(r->hwnd,&rc);
    D2D1_SIZE_U sz={(UINT32)(rc.right-rc.left),(UINT32)(rc.bottom-rc.top)};
    if (!sz.width) sz.width=1; if (!sz.height) sz.height=1;

    D2D1_RENDER_TARGET_PROPERTIES rp;
    rp.type=D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rp.pixelFormat.format=DXGI_FORMAT_UNKNOWN; rp.pixelFormat.alphaMode=D2D1_ALPHA_MODE_UNKNOWN;
    rp.dpiX=rp.dpiY=0; rp.usage=D2D1_RENDER_TARGET_USAGE_NONE; rp.minLevel=D2D1_FEATURE_LEVEL_DEFAULT;

    D2D1_HWND_RENDER_TARGET_PROPERTIES hp; hp.hwnd=r->hwnd; hp.pixelSize=sz; hp.presentOptions=D2D1_PRESENT_OPTIONS_NONE;

    HRESULT hr=r->d2d_factory->CreateHwndRenderTarget(rp,hp,&r->render_target);
    if (FAILED(hr)) { WSH_LOG_ERROR("CreateHwndRenderTarget: 0x%08X",hr); return false; }

    if (r->fg_brush)     { r->fg_brush->Release();     r->fg_brush=NULL; }
    if (r->bg_brush)     { r->bg_brush->Release();     r->bg_brush=NULL; }
    if (r->cursor_brush) { r->cursor_brush->Release(); r->cursor_brush=NULL; }

    D2D1_COLOR_F col=to_d2d(r->fg_color); r->render_target->CreateSolidColorBrush(col,&r->fg_brush);
    col=to_d2d(r->bg_color); r->render_target->CreateSolidColorBrush(col,&r->bg_brush);
    col=to_d2d(r->cursor_color); r->render_target->CreateSolidColorBrush(col,&r->cursor_brush);
    return true;
}

bool renderer_init(Renderer *r, HWND hwnd, const Config *cfg) {
    memset(r,0,sizeof(*r)); r->hwnd=hwnd; r->cfg=cfg; r->padding_x=4; r->padding_y=4;
    HDC hdc=GetDC(hwnd); r->dpi=hdc?(float)GetDeviceCaps(hdc,LOGPIXELSX):96.0f; if(hdc)ReleaseDC(hwnd,hdc);
    build_palette(r, cfg);
    D2D1_FACTORY_OPTIONS opts={D2D1_DEBUG_LEVEL_NONE};
    HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,opts,&r->d2d_factory);
    if (FAILED(hr)) { WSH_LOG_ERROR("D2D1CreateFactory: 0x%08X",hr); return false; }
    if (!create_rt(r)) return false;
    if (!font_init(&r->font,cfg->font.family,cfg->font.size,r->dpi)) { WSH_LOG_ERROR("font_init failed"); return false; }
    r->cell_w=r->font.cell_width; r->cell_h=r->font.cell_height;
    r->cursor_style=cfg->cursor.style; r->cursor_visible=true; r->cursor_blink_state=true;
    r->tab_bar_height=cfg->tabs.enabled?32:0;
    if (cfg->cursor.blink) r->blink_timer_id=SetTimer(hwnd,1,cfg->cursor.blink_rate_ms,NULL);
    RECT rc; GetClientRect(hwnd,&rc); renderer_resize(r,rc.right-rc.left,rc.bottom-rc.top);
    return true;
}

void renderer_resize(Renderer *r, int w, int h) {
    if (w<1)w=1; if(h <1)h=1;
    if (r->render_target) { D2D1_SIZE_U sz={(UINT32)w,(UINT32)h}; r->render_target->Resize(sz); }
    int uw=w-2*r->padding_x, uh=h-2*r->padding_y -r->tab_bar_height;
    if(uw<0)uw=0; if(uh<0)uh=0;
    r->cols=r->cell_w>0?(int)(uw/r->cell_w):80; r->rows=r->cell_h>0?(int)(uh/r->cell_h):24;
    if(r->cols<1)r->cols=1; if(r->rows<1)r->rows=1;
}

void renderer_paint(Renderer *r, const ScreenBuffer *sb, bool cursor_shown, int cursor_x, int cursor_y) {
    if (!r->render_target||!r->font.fmt_normal) return;
    r->render_target->BeginDraw();
    D2D1_COLOR_F bgc=to_d2d(r->bg_color); r->render_target->Clear(&bgc);

    int rows=sb->rows<r->rows?sb->rows:r->rows, cols=sb->cols<r->cols?sb->cols:r->cols;
    float ox=(float)r->padding_x, oy=(float)(r->padding_y+r->tab_bar_height);
    wchar_t wch[4];

    for (int row=0;row<rows;row++) for (int col=0;col<cols;col++) {
        const ScreenCell *cell=NULL;
        if (sb->viewport_offset>0) {
            int sl=sb->viewport_offset-(rows-row);
            if (sl>=0&&sl<sb->scrollback_count) cell=screen_scrollback_line((ScreenBuffer*)sb,sl,col);
        }
        if (!cell) cell=&sb->cells[row*sb->cols+col];

        float cx=ox+col*r->cell_w, cy=oy+row*r->cell_h;
        D2D1_RECT_F cr={cx,cy,cx+r->cell_w,cy+r->cell_h};
        Color4F fg=renderer_resolve_fg(r,cell), bgcc=renderer_resolve_bg(r,cell);
        if (cell->attr.reverse) { Color4F t=fg; fg=bgcc; bgcc=t; }

        if (bgcc.r!=r->bg_color.r||bgcc.g!=r->bg_color.g||bgcc.b!=r->bg_color.b) {
            D2D1_COLOR_F d=to_d2d(bgcc); r->bg_brush->SetColor(d);
            r->render_target->FillRectangle(cr,r->bg_brush);
        }

        if (!cell->wide_cont&&cell->ch&&cell->ch!=' ') {
            if (cell->ch<0x10000) { wch[0]=(wchar_t)cell->ch; wch[1]=0; }
            else { uint32_t cp=cell->ch-0x10000; wch[0]=(wchar_t)(0xD800|(cp>>10)); wch[1]=(wchar_t)(0xDC00|(cp&0x3FF)); wch[2]=0; }

            IDWriteTextFormat *fmt=r->font.fmt_normal;
            if (cell->attr.bold&&cell->attr.italic) fmt=r->font.fmt_bold_italic;
            else if (cell->attr.bold)               fmt=r->font.fmt_bold;
            else if (cell->attr.italic)             fmt=r->font.fmt_italic;

            IDWriteTextLayout *layout=NULL;
            if (r->font.factory) r->font.factory->CreateTextLayout(wch,(UINT32)(cell->ch>=0x10000?2:1),fmt,r->cell_w*(cell->wide?2.0f:1.0f),r->cell_h,&layout);
            if (layout) {
                D2D1_POINT_2F orig={cx,cy}; D2D1_COLOR_F d=to_d2d(fg); r->fg_brush->SetColor(d);
                r->render_target->DrawTextLayout(orig,layout,r->fg_brush,D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                layout->Release();
            }
            if (cell->attr.underline) {
                float uy=cy+r->cell_h-2.0f;
                D2D1_POINT_2F p1={cx,uy},p2={cx+r->cell_w,uy}; D2D1_COLOR_F d=to_d2d(fg); r->fg_brush->SetColor(d);
                r->render_target->DrawLine(p1,p2,r->fg_brush,1.0f,NULL);
            }
        }

        if (cursor_shown&&r->cursor_blink_state&&col==cursor_x&&row==cursor_y&&sb->cursor_visible) {
            D2D1_COLOR_F cc=to_d2d(r->cursor_color); r->cursor_brush->SetColor(cc);
            switch(r->cursor_style) {
                case CURSOR_BLOCK: r->render_target->FillRectangle(cr,r->cursor_brush); break;
                case CURSOR_BAR: { D2D1_RECT_F b={cx,cy,cx+2.0f,cy+r->cell_h}; r->render_target->FillRectangle(b,r->cursor_brush); break; }
                case CURSOR_UNDERLINE: { D2D1_RECT_F u={cx,cy+r->cell_h-2.0f,cx+r->cell_w,cy+r->cell_h}; r->render_target->FillRectangle(u,r->cursor_brush); break; }
            }
        }
    }

    HRESULT hr=r->render_target->EndDraw();
    if (hr==D2DERR_RECREATE_TARGET) create_rt(r);
}

void renderer_set_font(Renderer *r, const wchar_t *family, float pt) {
    font_free(&r->font); font_init(&r->font,family,pt,r->dpi);
    r->cell_w=r->font.cell_width; r->cell_h=r->font.cell_height;
}
void renderer_update_dpi(Renderer *r, float dpi) {
    r->dpi=dpi; float pt=r->font.pt_size>0?r->font.pt_size:(r->cfg?r->cfg->font.size:13.0f);
    font_resize(&r->font,pt,dpi); r->cell_w=r->font.cell_width; r->cell_h=r->font.cell_height; create_rt(r);
}
void renderer_toggle_cursor_blink(Renderer *r) { r->cursor_blink_state=!r->cursor_blink_state; }
void renderer_destroy(Renderer *r) {
    if (r->blink_timer_id) { KillTimer(r->hwnd,r->blink_timer_id); r->blink_timer_id=0; }
    font_free(&r->font);
    if (r->cursor_brush) { r->cursor_brush->Release(); r->cursor_brush=NULL; }
    if (r->bg_brush)     { r->bg_brush->Release();     r->bg_brush=NULL; }
    if (r->fg_brush)     { r->fg_brush->Release();     r->fg_brush=NULL; }
    if (r->render_target){ r->render_target->Release();r->render_target=NULL; }
    if (r->d2d_factory)  { r->d2d_factory->Release();  r->d2d_factory=NULL; }
}
