/** 
 * @file llfontgl.h
 * @author Doug Soo
 * @brief Wrapper around FreeType
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 * 
 * Copyright (c) 2001-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTGL_H
#define LL_LLFONTGL_H

#include "llfont.h"
#include "lltexture.h"
#include "v2math.h"
#include "llcoord.h"
#include "llrect.h"

#include "llfontregistry.h"

class LLColor4;

// Key used to request a font.
class LLFontDescriptor;

// Structure used to store previously requested fonts.
class LLFontRegistry;

class LLFontGL : public LLFont
{
public:
	enum HAlign
	{
		// Horizontal location of x,y coord to render.
		LEFT = 0,		// Left align
		RIGHT = 1,		// Right align
		HCENTER = 2,	// Center
	};

	enum VAlign
	{
		// Vertical location of x,y coord to render.
		TOP = 3,		// Top align
		VCENTER = 4,	// Center
		BASELINE = 5,	// Baseline
		BOTTOM = 6		// Bottom
	};

	enum StyleFlags
	{
		// text style to render.  May be combined (these are bit flags)
		// text style to render.  May be combined (these are bit flags)
		NORMAL    = 0x00,	
		BOLD      = 0x01,
		ITALIC    = 0x02,
		UNDERLINE = 0x04,
	};

	enum ShadowType
	{
		NO_SHADOW,
		DROP_SHADOW,
		DROP_SHADOW_SOFT
	};

	LLFontGL();
	~LLFontGL();


	void reset(); // Reset a font after GL cleanup.  ONLY works on an already loaded font.

	void destroyGL();

	/* virtual*/ BOOL loadFace(const std::string& filename, const F32 point_size, const F32 vert_dpi, const F32 horz_dpi, const S32 components, BOOL is_fallback);

	S32 render(const LLWString &text, S32 begin_offset, 
				const LLRect& rect, 
				const LLColor4 &color, 
				HAlign halign = LEFT,  VAlign valign = BASELINE, 
				U8 style = NORMAL, ShadowType shadow = NO_SHADOW, 
				S32 max_chars = S32_MAX,
				F32* right_x=NULL,
				BOOL use_embedded = FALSE,
				BOOL use_ellipses = FALSE) const;

	S32 render(const LLWString &text, S32 begin_offset, 
				F32 x, F32 y, 
				const LLColor4 &color, 
				HAlign halign = LEFT,  VAlign valign = BASELINE, 
				U8 style = NORMAL, ShadowType shadow = NO_SHADOW, 
				S32 max_chars = S32_MAX, S32 max_pixels = S32_MAX, 
				F32* right_x=NULL,
				BOOL use_embedded = FALSE,
				BOOL use_ellipses = FALSE) const;

	S32 render(const LLWString &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color) const;

	// renderUTF8 does a conversion, so is slower!
	S32 renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign,  VAlign valign, U8 style, ShadowType shadow, S32 max_chars, S32 max_pixels,  F32* right_x, BOOL use_ellipses) const;
	S32 renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color) const;
	S32 renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style = NORMAL, ShadowType shadow = NO_SHADOW) const;

	// font metrics - override for LLFont that returns units of virtual pixels
	/*virtual*/ F32 getLineHeight() const		{ return (F32)llround(mLineHeight / sScaleY); }
	/*virtual*/ F32 getAscenderHeight() const	{ return (F32)llround(mAscender / sScaleY); }
	/*virtual*/ F32 getDescenderHeight() const	{ return (F32)llround(mDescender / sScaleY); }
	
	S32 getWidth(const std::string& utf8text) const;
	S32 getWidth(const llwchar* wchars) const;
	S32 getWidth(const std::string& utf8text, const S32 offset, const S32 max_chars ) const;
	S32 getWidth(const llwchar* wchars, const S32 offset, const S32 max_chars, BOOL use_embedded = FALSE) const;

	F32 getWidthF32(const std::string& utf8text) const;
	F32 getWidthF32(const llwchar* wchars) const;
	F32 getWidthF32(const std::string& text, const S32 offset, const S32 max_chars ) const;
	F32 getWidthF32(const llwchar* wchars, const S32 offset, const S32 max_chars, BOOL use_embedded = FALSE ) const;

	// The following are called often, frequently with large buffers, so do not use a string interface
	
	// Returns the max number of complete characters from text (up to max_chars) that can be drawn in max_pixels
	typedef enum e_word_wrap_style
	{
		ONLY_WORD_BOUNDARIES,
		WORD_BOUNDARY_IF_POSSIBLE,
		ANYWHERE
	} EWordWrapStyle ;
	S32	maxDrawableChars(const llwchar* wchars, F32 max_pixels, S32 max_chars = S32_MAX, EWordWrapStyle end_on_word_boundary = ANYWHERE,
	 const BOOL use_embedded = FALSE, F32* drawn_pixels = NULL) const;

	// Returns the index of the first complete characters from text that can be drawn in max_pixels
	// given that the character at start_pos should be the last character (or as close to last as possible).
	S32	firstDrawableChar(const llwchar* wchars, F32 max_pixels, S32 text_len, S32 start_pos=S32_MAX, S32 max_chars = S32_MAX) const;

	// Returns the index of the character closest to pixel position x (ignoring text to the right of max_pixels and max_chars)
	S32 charFromPixelOffset(const llwchar* wchars, const S32 char_offset, F32 x, F32 max_pixels=F32_MAX, S32 max_chars = S32_MAX, BOOL round = TRUE, BOOL use_embedded = FALSE) const;

	const LLFontDescriptor &getFontDesc() const { return mFontDesc; }
	void setFontDesc(const LLFontDescriptor& font_desc) { mFontDesc = font_desc; }

	LLTexture *getTexture() const;

	void	   addEmbeddedChar( llwchar wc, LLTexture* image, const std::string& label) const;
	void	   addEmbeddedChar( llwchar wc, LLTexture* image, const LLWString& label) const;
	void	   removeEmbeddedChar( llwchar wc ) const;

	static void initClass(F32 screen_dpi, F32 x_scale, F32 y_scale,	 const std::string& app_dir, const std::vector<std::string>& xui_paths,	 bool create_gl_textures = true);

	// Load sans-serif, sans-serif-small, etc.
	// Slow, requires multiple seconds to load fonts.
	static bool loadDefaultFonts();
	static void	destroyDefaultFonts();
	static void destroyAllGL();

	// Takes a string with potentially several flags, i.e. "NORMAL|BOLD|ITALIC"
	static U8 getStyleFromString(const std::string &style);
	static std::string getStringFromStyle(U8 style);
	static std::string nameFromFont(const LLFontGL* fontp);

	static std::string nameFromHAlign(LLFontGL::HAlign align);
	static LLFontGL::HAlign hAlignFromName(const std::string& name);

	static std::string nameFromVAlign(LLFontGL::VAlign align);
	static LLFontGL::VAlign vAlignFromName(const std::string& name);

	static void setFontDisplay(BOOL flag) { sDisplayFont = flag ; }

protected:
	struct embedded_data_t
	{
		embedded_data_t(LLImageGL* image, const LLWString& label) : mImage(image), mLabel(label) {}
		LLPointer<LLImageGL> mImage;
		LLWString			 mLabel;
	};
	const embedded_data_t* getEmbeddedCharData(const llwchar wch) const;
	F32 getEmbeddedCharAdvance(const embedded_data_t* ext_data) const;
	void clearEmbeddedChars();
public:
		
	static LLFontGL* getFontMonospace();
	static LLFontGL* getFontSansSerifSmall();
	static LLFontGL* getFontSansSerif();
	static LLFontGL* getFontSansSerifBig();
	static LLFontGL* getFontSansSerifHuge();
	static LLFontGL* getFontSansSerifBold();
	static LLFontGL* getFontExtChar();
	static LLFontGL* getFont(const LLFontDescriptor& desc);

	static std::string getFontPathLocal();
	static std::string getFontPathSystem();

	static LLCoordGL sCurOrigin;
	static F32			sCurDepth;
	static std::vector<std::pair<LLCoordGL, F32> > sOriginStack;

	static LLColor4 sShadowColor;

	static F32 sVertDPI;
	static F32 sHorizDPI;
	static F32 sScaleX;
	static F32 sScaleY;
	static BOOL     sDisplayFont ;
	static std::string sAppDir;			// For loading fonts
	friend class LLTextBillboard;
	friend class LLHUDText;

	LLFontGL(const LLFontGL &source);
	LLFontGL &operator=(const LLFontGL &source);
protected:
	/*virtual*/ BOOL addChar(const llwchar wch) const;

protected:
	typedef std::map<llwchar,embedded_data_t*> embedded_map_t;
	mutable embedded_map_t mEmbeddedChars;
	
	LLFontDescriptor mFontDesc;

	void renderQuad(const LLRectf& screen_rect, const LLRectf& uv_rect, F32 slant_amt) const;
	void drawGlyph(const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4& color, U8 style, ShadowType shadow, F32 drop_shadow_fade) const;

	// Registry holds all instantiated fonts.
	static LLFontRegistry* sFontRegistry;

};

#endif
