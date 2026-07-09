#include "GUI/GUI_Font.h"

static const uint8_t *myFont_GetGlyphBitmap(const lv_font_t *font, uint32_t unicode_letter)
{
	const lv_font_t *installed_font;

	(void)font;

	installed_font = GUI_Font_GetXbfFont();
	if (installed_font == NULL || installed_font->get_glyph_bitmap == NULL)
	{
		return NULL;
	}

	return installed_font->get_glyph_bitmap(installed_font, unicode_letter);
}

static bool myFont_GetGlyphDsc(const lv_font_t *font,
                               lv_font_glyph_dsc_t *dsc_out,
                               uint32_t unicode_letter,
                               uint32_t unicode_letter_next)
{
	const lv_font_t *installed_font;

	(void)font;

	installed_font = GUI_Font_GetXbfFont();
	if (installed_font == NULL || installed_font->get_glyph_dsc == NULL)
	{
		return false;
	}

	return installed_font->get_glyph_dsc(installed_font, dsc_out, unicode_letter, unicode_letter_next);
}

lv_font_t myFont =
{
	.get_glyph_bitmap = myFont_GetGlyphBitmap,
	.get_glyph_dsc = myFont_GetGlyphDsc,
	.line_height = 21,
	.base_line = 0,
	.subpx = LV_FONT_SUBPX_NONE,
	.underline_position = 0,
	.underline_thickness = 0,
	.dsc = NULL,
	.fallback = LV_FONT_DEFAULT,
};
