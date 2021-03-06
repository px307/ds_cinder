#include "stdafx.h"

#include "text.h"

#include "cairo/cairo.h"
#include "fontconfig/fontconfig.h"
#include "pango/pangocairo.h"

#include <pango/pango-font.h>
#include <regex>

#if CAIRO_HAS_WIN32_SURFACE
#include <cairo-win32.h>
#endif

#include "ds/data/font_list.h"
#include "ds/app/blob_reader.h"
#include "ds/app/blob_registry.h"
#include "ds/data/data_buffer.h"
#include "ds/debug/logger.h"
#include "ds/ui/sprite/sprite_engine.h"
#include "ds/ui/service/pango_font_service.h"
#include "ds/util/string_util.h"

namespace {
// Pango/cairo output is premultiplied colors, so rendering it with opacity fades like you'd expect with other sprites
// requires a custom shader that multiplies in the rest of the opacity setting
const std::string opacityFrag =
"uniform sampler2D	tex0;\n"
"uniform bool		useTexture;\n"	// dummy, Engine always sends this anyway
"uniform bool       preMultiply;\n" // dummy, Engine always sends this anyway
"in vec4			Color;\n"
"in vec2			TexCoord0;\n"
"out vec4			oColor;\n"
"void main()\n"
"{\n"
"    oColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
"    if (useTexture) {\n"
"        oColor = texture2D( tex0, vec2(TexCoord0.x, 1.0-TexCoord0.y) );\n"
"    }\n"
"    // Undo the pango premultiplication\n"
"    oColor.rgb /= oColor.a;\n"
"    // Now do the normal colorize/optional premultiplication\n"
"    oColor *= Color;\n"
"    if (preMultiply)\n"
"        oColor.rgb *= oColor.a;\n"
"}\n";

const std::string vertShader =
"uniform mat4		ciModelMatrix;\n"
"uniform mat4		ciModelViewProjection;\n"
"uniform vec4		uClipPlane0;\n"
"uniform vec4		uClipPlane1;\n"
"uniform vec4		uClipPlane2;\n"
"uniform vec4		uClipPlane3;\n"
"in vec4			ciPosition;\n" 
"in vec4			ciColor;\n"
"in vec2			ciTexCoord0;\n"
"out vec2			TexCoord0;\n"
"out vec4			Color;\n"
"void main()\n"
"{\n"
"	gl_Position = ciModelViewProjection * ciPosition;\n"
"	TexCoord0 = ciTexCoord0;\n"
"	Color = ciColor;\n"
"	gl_ClipDistance[0] = dot(ciModelMatrix * ciPosition, uClipPlane0);\n"
"	gl_ClipDistance[1] = dot(ciModelMatrix * ciPosition, uClipPlane1);\n"
"	gl_ClipDistance[2] = dot(ciModelMatrix * ciPosition, uClipPlane2);\n"
"	gl_ClipDistance[3] = dot(ciModelMatrix * ciPosition, uClipPlane3);\n"
"}\n";

std::string shaderNameOpaccy = "pango_text_opacity";
}

namespace ds {
namespace ui {


namespace {
char				BLOB_TYPE = 0;

const DirtyState&	FONT_DIRTY = INTERNAL_A_DIRTY;
const DirtyState&	TEXT_DIRTY = INTERNAL_B_DIRTY;
const DirtyState&	LAYOUT_DIRTY = INTERNAL_C_DIRTY;

const char			FONTNAME_ATT = 80;
const char			TEXT_ATT = 81;
const char			LAYOUT_ATT = 82;
}

void Text::installAsServer(ds::BlobRegistry& registry)
{
	BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromClient(r); });
}

void Text::installAsClient(ds::BlobRegistry& registry)
{
	BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromServer<Text>(r); });
}

Text::Text(ds::ui::SpriteEngine& eng)
	: ds::ui::Sprite(eng)
	, mText(L"")
	, mProcessedText(L"")
	, mNeedsMarkupDetection(false)
	, mNeedsFontUpdate(false)
	, mNeedsMeasuring(false)
	, mNeedsTextRender(false)
	, mNeedsFontOptionUpdate(false)
	, mProbablyHasMarkup(false)
	, mTextFont("Sans")
	, mTextSize(120.0)
	, mTextColor(ci::Color::white())
	, mDefaultTextItalicsEnabled(false)
	, mDefaultTextSmallCapsEnabled(false)
	, mResizeLimitWidth(-1.0f)
	, mResizeLimitHeight(-1.0f)
	, mLeading(1.0f)
	, mTextAlignment(Alignment::kLeft)
	, mDefaultTextWeight(TextWeight::kNormal)
	, mEllipsizeMode(EllipsizeMode::kEllipsizeNone)
	, mWrapMode(WrapMode::kWrapModeWordChar)
	, mPixelWidth(-1)
	, mPixelHeight(-1)
	, mNumberOfLines(0)
	, mWrappedText(false)
	, mFontDescription(nullptr)
	, mPangoContext(nullptr)
	, mPangoLayout(nullptr)
	, mCairoSurface(nullptr)
	, mCairoContext(nullptr)
	, mCairoFontOptions(nullptr)

#ifdef CAIRO_HAS_WIN32_SURFACE
	, mCairoWinImageSurface(nullptr)
#endif
{
	mBlobType = BLOB_TYPE;

	setUseShaderTexture(true);
	mSpriteShader.setShaders(vertShader, opacityFrag, shaderNameOpaccy);
	mSpriteShader.loadShaders();

	if(!mEngine.getPangoFontService().getPangoFontMap()) {
		DS_LOG_WARNING("Cannot create the pango font map, nothing will render for this pango text sprite.");
		return;
	}

	// Create Pango Context for reuse
	mPangoContext = pango_font_map_create_context(mEngine.getPangoFontService().getPangoFontMap());
	if(nullptr == mPangoContext) {
		DS_LOG_WARNING("Cannot create the pango font context.");
		return;
	}

	// Create Pango Layout for reuse
	mPangoLayout = pango_layout_new(mPangoContext);
	if(mPangoLayout == nullptr) {
		DS_LOG_WARNING("Cannot create the pango layout.");
		return;
	}

	// Initialize Cairo surface and context, will be instantiated on demand
	mCairoFontOptions = cairo_font_options_create();
	if(mCairoFontOptions == nullptr) {
		DS_LOG_WARNING("Cannot create Cairo font options.");
		return;
	}

	// Generate the default font config
	mNeedsFontOptionUpdate = true;
	//mNeedsFontUpdate = true;

	setTransparent(false);
}

Text::~Text() {
	// This causes crash on windows
	if(mCairoContext) {
		cairo_destroy(mCairoContext);
		mCairoContext = nullptr;
	}

	if(mFontDescription) {
		pango_font_description_free(mFontDescription);
		mFontDescription = nullptr;
	}

	if(mCairoFontOptions) {
		cairo_font_options_destroy(mCairoFontOptions);
		mCairoFontOptions = nullptr;
	}

#ifdef CAIRO_HAS_WIN32_SURFACE
	if(mCairoWinImageSurface) {
		//cairo_surface_destroy(mCairoWinImageSurface);
		mCairoWinImageSurface = nullptr;
	}
	if(mCairoSurface != nullptr) {
		cairo_surface_destroy(mCairoSurface);
	}
#else
	// Crashes windows...
	if(mCairoSurface != nullptr) {
		cairo_surface_destroy(mCairoSurface);
	}
#endif

	g_object_unref(mPangoContext); // this one crashes Windows?
	g_object_unref(mPangoLayout);
}

std::string Text::getTextAsString() const{
	return ds::utf8_from_wstr(mText);
}

std::wstring Text::getText() const {
	return mText;
}

void Text::setText(std::string text) {
	setText(ds::wstr_from_utf8(text));
}

void Text::setText(std::wstring text) {	
	if(text != mText) {
		mText = text;
		mNeedsMarkupDetection = true;
		mNeedsMeasuring = true;
		mNeedsTextRender = true;

		markAsDirty(TEXT_DIRTY);
	}
}

const ci::gl::TextureRef Text::getTexture() {
	return mTexture;
}

void Text::setTextStyle(std::string font, float size, ci::ColorA color, TextWeight weight,	Alignment::Enum alignment) {
	setFont(font);
	setFontSize(size);
	setColor(color);
	setDefaultTextWeight(weight);
	setAlignment(alignment);
}

TextWeight Text::getDefaultTextWeight() {
	return mDefaultTextWeight;
}

void Text::setDefaultTextWeight(TextWeight weight) {
	if(mDefaultTextWeight != weight) {
		mDefaultTextWeight = weight;
		mNeedsFontUpdate = true;
		mNeedsMeasuring = true;
		mNeedsTextRender = true;

		markAsDirty(FONT_DIRTY);
	}
}

Alignment::Enum Text::getAlignment() {
	return mTextAlignment;
}

void Text::setAlignment(Alignment::Enum alignment) {
	if(mTextAlignment != alignment) {
		mTextAlignment = alignment;
		mNeedsMeasuring = true;
		mNeedsTextRender = true;
		
		markAsDirty(FONT_DIRTY);
	}
}

float Text::getLeading() const {
	return mLeading;
}

Text& Text::setLeading(const float leading) {
	if(mLeading != leading) {
		mLeading = leading;
		mNeedsMeasuring = true;
		mNeedsTextRender = true;

		markAsDirty(FONT_DIRTY);
	}
	return *this;
}

float Text::getResizeLimitWidth() const {
	return mResizeLimitWidth;
}

float Text::getResizeLimitHeight() const {
	return mResizeLimitHeight;
}

Text& Text::setResizeLimit(const float maxWidth, const float maxHeight) {
	if(mResizeLimitWidth != maxWidth || mResizeLimitHeight != maxHeight){
		mResizeLimitWidth = maxWidth;
		mResizeLimitHeight = maxHeight;

		if(mResizeLimitWidth < 1){
			mResizeLimitWidth = -1.0f; // negative one turns off text wrapping
		}

		if(mResizeLimitHeight < 1){
			mResizeLimitHeight = -1.0f;
		}
		mNeedsMeasuring = true;

		markAsDirty(LAYOUT_DIRTY);
	}

	return *this;
}

void Text::setTextColor(const ci::Color& color) {
	if(mTextColor != color) {
		mTextColor = color;
		mNeedsTextRender = true;

		markAsDirty(FONT_DIRTY);
	}
}

bool Text::getDefaultTextSmallCapsEnabled() {
	return mDefaultTextSmallCapsEnabled;
}

void Text::setDefaultTextSmallCapsEnabled(bool value) {
	if(mDefaultTextSmallCapsEnabled != value) {
		mDefaultTextSmallCapsEnabled = value;
		mNeedsFontUpdate = true;
		mNeedsMeasuring = true;

		markAsDirty(FONT_DIRTY);
	}
}

bool Text::getDefaultTextItalicsEnabled() {
	return mDefaultTextItalicsEnabled;
}

void Text::setDefaultTextItalicsEnabled(bool value) {
	if(mDefaultTextItalicsEnabled != value) {
		mDefaultTextItalicsEnabled = value;
		mNeedsFontUpdate = true;
		mNeedsMeasuring = true;

		markAsDirty(FONT_DIRTY);
	}
}

void Text::setFontSize(float size) {
	if(mTextSize != size) {
		mTextSize = size;
		mNeedsFontUpdate = true;
		mNeedsMeasuring = true;

		markAsDirty(FONT_DIRTY);
	}
}

void Text::setColor(const ci::Color& c){
	setTextColor(c);
}

void Text::setColor(float r, float g, float b){
	setTextColor(ci::Color(r, g, b));
}

void Text::setColorA(const ci::ColorA& c){
	setTextColor(ci::Color(c));
	setOpacity(c.a);
}

Text& Text::setFont(const std::string& font, const float fontSize) {
	if(mTextFont != font || mTextSize != fontSize) {
		mTextFont = mEngine.getFonts().getFontNameForShortName(font);

		mTextSize = fontSize;
		mNeedsFontUpdate = true;
		mNeedsMeasuring = true;

		markAsDirty(FONT_DIRTY);

		/*
		if(!mEngine.getPangoFontService().getFamilyExists(mTextFont) && !mEngine.getPangoFontService().getFaceExists(mTextFont)){
			DS_LOG_WARNING("Text: Family or face not found: " << mTextFont);
		}
		*/
	}
	return *this;
}

Text& Text::setFont(const std::string& name){
	return setFont(name, mTextSize);
}

float Text::getWidth() const {
	if(mNeedsMeasuring) {
		(const_cast<Text*>(this))->measurePangoText();
	}
	return mWidth;
}

float Text::getHeight() const {
	if(mNeedsMeasuring) {
		(const_cast<Text*>(this))->measurePangoText();
	}
	return mHeight;
}

void Text::setEllipsizeMode(EllipsizeMode theMode){
	if(theMode == mEllipsizeMode) return;

	mEllipsizeMode = theMode;
	mNeedsMeasuring = true;
	markAsDirty(LAYOUT_DIRTY);
}

EllipsizeMode Text::getEllipsizeMode(){
	return mEllipsizeMode;
}

void Text::setWrapMode(WrapMode theMode){
	if(theMode == mWrapMode) return;

	mWrapMode = theMode;
	mNeedsMeasuring = true;
	markAsDirty(LAYOUT_DIRTY);
}

WrapMode Text::getWrapMode(){
	return mWrapMode;
}

void Text::onBuildRenderBatch(){


	float preWidth = 0.0f;
	float preHeight = 0.0f;
	if(mTexture){
		preWidth = mTexture->getWidth();
		preHeight = mTexture->getHeight();
	}

	renderPangoText();

	if(!mTexture){
		mRenderBatch = nullptr;
		return;
	}

	// if we already have a batch of this size, don't rebuild it
	if(mRenderBatch && preHeight == mTexture->getHeight() && preWidth == mTexture->getWidth()){
		mNeedsBatchUpdate = false;
		return;
	}

	auto drawRect = ci::Rectf(0.0f, 0.0f, static_cast<float>(mTexture->getWidth()), static_cast<float>(mTexture->getHeight()));
	if(getPerspective()){
		drawRect = ci::Rectf(0.0f, static_cast<float>(mTexture->getHeight()), static_cast<float>(mTexture->getWidth()), 0.0f);
	}
	auto theGeom = ci::geom::Rect(drawRect);
	if(mRenderBatch){
		mRenderBatch->replaceVboMesh(ci::gl::VboMesh::create(theGeom));
	} else {
		mRenderBatch = ci::gl::Batch::create(theGeom, mSpriteShader.getShader());
	}
	
}

void Text::drawLocalClient(){
	if(mTexture && !mText.empty()){

		ci::gl::color(mColor.r, mColor.g, mColor.b, mDrawOpacity);
		ci::gl::ScopedTextureBind scopedTexture(mTexture);

		if(mRenderBatch){
			mRenderBatch->draw();
		} else {
			if(getPerspective()) {
				ci::gl::drawSolidRect(ci::Rectf(0.0f, static_cast<float>(mTexture->getHeight()), static_cast<float>(mTexture->getWidth()), 0.0f));
			} else {
				ci::gl::drawSolidRect(ci::Rectf(0.0f, 0.0f, static_cast<float>(mTexture->getWidth()), static_cast<float>(mTexture->getHeight())));
			}
		}
	}
}

int Text::getCharacterIndexForPosition(const ci::vec2& lp){
	measurePangoText();

	int outputIndex = 0;
	if(mPangoLayout){
		int trailing = 0;
		auto success = pango_layout_xy_to_index(mPangoLayout, (int)lp.x * PANGO_SCALE, (int)lp.y * PANGO_SCALE, &outputIndex, &trailing);
		// the "trailing" is if the xy is more than halfway to the next character. this is required to be added for the cursor to be able to be placed after the last character
		outputIndex += trailing;

		//std::cout << "Selected index: " << outputIndex << " " << ds::utf8_from_wstr(mText) << " " << mText.size() << std::endl;

	}	
	return outputIndex;
}
ci::vec2 Text::getPositionForCharacterIndex(const int characterIndex){
	measurePangoText();

	ci::vec2 outputPos = ci::vec2();
	if(mPangoLayout && !mText.empty()){
		PangoRectangle outputRectangle;
		pango_layout_index_to_pos(mPangoLayout, characterIndex, &outputRectangle);

		outputPos.x = (float)outputRectangle.x / (float)PANGO_SCALE;
		// Note: the rectangle returned is to the very top of the very tallest possible character (I think), which makes it a good distance above the top of most characters
		// So I fudged the output of this for a reasonable position for the 'start' of each character from the top-left
		// The exact output for the rectangle of each character can be got from getRectForCharacterIndex() 
		outputPos.y = (float)outputRectangle.y / (float)PANGO_SCALE +(float)outputRectangle.height / (float)PANGO_SCALE / 4.0f; 
	}
	return outputPos;
	
}

ci::Rectf Text::getRectForCharacterIndex(const int characterIndex){
	measurePangoText();

	ci::Rectf outputRect = ci::Rectf();
	if(mPangoLayout && !mText.empty()){
		PangoRectangle outputRectangle;
		pango_layout_index_to_pos(mPangoLayout, characterIndex, &outputRectangle);

		float xx = (float)outputRectangle.x / (float)PANGO_SCALE;
		float yy = (float)outputRectangle.y / (float)PANGO_SCALE;
		outputRect.set(xx, yy, xx + (float)outputRectangle.width / (float)PANGO_SCALE, yy + (float)outputRectangle.height / (float)PANGO_SCALE);
	}
	return outputRect;
}

bool Text::getTextWrapped(){
	// calculate current state if needed
	measurePangoText();
	return mWrappedText;
}

int Text::getNumberOfLines(){
	// calculate current state if needed
	measurePangoText();
	return mNumberOfLines;
}


void Text::onUpdateClient(const UpdateParams&){
	measurePangoText();
}

void Text::onUpdateServer(const UpdateParams&){
	measurePangoText();
}

bool Text::measurePangoText() {
	if(mNeedsFontUpdate || mNeedsMeasuring || mNeedsTextRender || mNeedsMarkupDetection) {

		if(mText.empty()){
			if(mWidth > 0.0f || mWidth > 0.0f){
				setSize(0.0f, 0.0f);
			}
			mNeedsMarkupDetection = false;
			mNeedsMeasuring = false;
			mNeedsBatchUpdate = true;
			return false;
		}

		mNeedsTextRender = true;
		bool hadMarkup = mProbablyHasMarkup;

		if(mNeedsMarkupDetection) {

			// Pango doesn't support HTML-esque line-break tags, so
			// find break marks and replace with newlines, e.g. <br>, <BR>, <br />, <BR />
			// TODO
			std::regex e("<br\\s?/?>", std::regex_constants::icase);
			mProcessedText = ds::wstr_from_utf8(std::regex_replace(ds::utf8_from_wstr(mText), e, "\n")) + mEngine.getPangoFontService().getTextSuffix();
			//mProcessedText = mText + mEngine.getPangoFontService().getTextSuffix();

			// Let's also decide and flag if there's markup in this string
			// Faster to use pango_layout_set_text than pango_layout_set_markup later on if
			// there's no markup to bother with.
			// Be pretty liberal, there's more harm in false-postives than false-negatives
			mProbablyHasMarkup = ((mProcessedText.find(L"<") != std::wstring::npos) && (mProcessedText.find(L">") != std::wstring::npos));

			mNeedsMarkupDetection = false;
		}

		// First run, and then if the fonts change
		if(mNeedsFontOptionUpdate) {
			// TODO, expose these?

			cairo_font_options_set_antialias(mCairoFontOptions, CAIRO_ANTIALIAS_SUBPIXEL);
			cairo_font_options_set_hint_style(mCairoFontOptions, CAIRO_HINT_STYLE_DEFAULT);
			cairo_font_options_set_hint_metrics(mCairoFontOptions, CAIRO_HINT_METRICS_ON);
			cairo_font_options_set_subpixel_order(mCairoFontOptions, CAIRO_SUBPIXEL_ORDER_RGB);

			pango_cairo_context_set_font_options(mPangoContext, mCairoFontOptions);

			mNeedsFontOptionUpdate = false;
		}

		if(mNeedsFontUpdate) {
			if(mFontDescription != nullptr) {
				pango_font_description_free(mFontDescription);
			}

			mFontDescription = pango_font_description_from_string(mTextFont.c_str());// +" " + std::to_string(mTextSize)).c_str());
			pango_font_description_set_absolute_size(mFontDescription, (double)(mTextSize * 1.333333333f) * PANGO_SCALE);
			//	pango_font_description_set_weight(fontDescription, static_cast<PangoWeight>(mDefaultTextWeight));
			//	pango_font_description_set_style(mFontDescription, PANGO_STYLE_ITALIC);// mDefaultTextItalicsEnabled ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
			//	pango_font_description_set_variant(fontDescription, mDefaultTextSmallCapsEnabled ? PANGO_VARIANT_SMALL_CAPS : PANGO_VARIANT_NORMAL);
			pango_layout_set_font_description(mPangoLayout, mFontDescription);
			pango_font_map_load_font(mEngine.getPangoFontService().getPangoFontMap(), mPangoContext, mFontDescription);

			//	std::cout << pango_font_description_to_string(mFontDescription) << std::endl;

			mNeedsFontUpdate = false;
		}


		// If the text or the bounds change
		if(mNeedsMeasuring) {

			const int lastPixelWidth = mPixelWidth;
			const int lastPixelHeight = mPixelHeight;

			pango_layout_set_width(mPangoLayout, (int)mResizeLimitWidth * PANGO_SCALE);
			pango_layout_set_height(mPangoLayout, (int)mResizeLimitHeight * PANGO_SCALE);

			// Pango separates alignment and justification... I prefer a simpler API here to handling certain edge cases.
			if(mTextAlignment == Alignment::kJustify) {
				pango_layout_set_justify(mPangoLayout, true);
				pango_layout_set_alignment(mPangoLayout, PANGO_ALIGN_LEFT);
			} else {
				PangoAlignment aligny = PANGO_ALIGN_LEFT;
				if(mTextAlignment == Alignment::kCenter){
					aligny = PANGO_ALIGN_CENTER;
				} else if(mTextAlignment == Alignment::kRight){
					aligny = PANGO_ALIGN_RIGHT;
				} else if(mTextAlignment == Alignment::kJustify){ // handled above, but just to be safe
					aligny = PANGO_ALIGN_LEFT;
				}

				pango_layout_set_justify(mPangoLayout, false);
				pango_layout_set_alignment(mPangoLayout, aligny);
			}

			if(mWrapMode == WrapMode::kWrapModeChar){
				pango_layout_set_wrap(mPangoLayout, PANGO_WRAP_CHAR);
			} else if(mWrapMode == WrapMode::kWrapModeWord){
				pango_layout_set_wrap(mPangoLayout, PANGO_WRAP_WORD);
			} else {
				pango_layout_set_wrap(mPangoLayout, PANGO_WRAP_WORD_CHAR);
			}

			PangoEllipsizeMode elipsizeMode = PANGO_ELLIPSIZE_NONE;
			if(mEllipsizeMode == EllipsizeMode::kEllipsizeEnd){
				elipsizeMode = PANGO_ELLIPSIZE_END;
			} else if(mEllipsizeMode == EllipsizeMode::kEllipsizeMiddle){
				elipsizeMode = PANGO_ELLIPSIZE_MIDDLE;
			} else if(mEllipsizeMode == EllipsizeMode::kEllipsizeStart){
				elipsizeMode = PANGO_ELLIPSIZE_START;
			}

			pango_layout_set_ellipsize(mPangoLayout, elipsizeMode);
			pango_layout_set_spacing(mPangoLayout, (int)(mTextSize * (mLeading - 1.0f)) * PANGO_SCALE);

			// Set text, use the fastest method depending on what we found in the text
			int newPixelWidth = 0;
			int newPixelHeight = 0;
			if(mProbablyHasMarkup){// || true) {
				pango_layout_set_markup(mPangoLayout, ds::utf8_from_wstr(mProcessedText).c_str(), -1);
				// check the pixel size, if it's empty, then we can try again without markup
				pango_layout_get_pixel_size(mPangoLayout, &newPixelWidth, &newPixelHeight);
			}

			if(!mProbablyHasMarkup || newPixelWidth < 1) {
				if(hadMarkup){
					pango_layout_set_markup(mPangoLayout, ds::utf8_from_wstr(mProcessedText).c_str(), -1);
				}
				pango_layout_set_text(mPangoLayout, ds::utf8_from_wstr(mProcessedText).c_str(), -1);
			}

			mWrappedText = pango_layout_is_wrapped(mPangoLayout) != FALSE;
			mNumberOfLines = pango_layout_get_line_count(mPangoLayout);

			// use this instead: pango_layout_get_pixel_extents
			PangoRectangle inkRect;
			PangoRectangle extentRect;
			pango_layout_get_pixel_extents(mPangoLayout, &inkRect, &extentRect);

			// TODO: output a warning, and / or do a better job detecting and fixing issues or something
			if((extentRect.width == 0 || extentRect.height == 0) && !mText.empty()){
				DS_LOG_WARNING("No size detected for pango text size. Font not detected or invalid markup are likely causes. Text: " << getTextAsString());
			}

			/*
			std::cout << getTextAsString() << std::endl;
			std::cout << "Ink rect: " << inkRect.x << " " << inkRect.y << " " << inkRect.width << " " << inkRect.height << std::endl;
			std::cout << "Ext rect: " << extentRect.x << " " << extentRect.y << " " << extentRect.width << " " << extentRect.height << std::endl;
			std::cout << "Pixel size: " << newPixelWidth << " " << newPixelHeight << std::endl;
			*/

			mPixelWidth = extentRect.width+(extentRect.x*2.0f);
			mPixelHeight = extentRect.height+(extentRect.y*2.0f);

			setSize((float)mPixelWidth, (float)mPixelHeight);

			mNeedsMeasuring = false;
		}

		mNeedsBatchUpdate = true;
		return true;
	} else {
		return false;
	}
}

void Text::renderPangoText(){

	/// HACK
	/// Some fonts clip some descenders and characters at the end of the text
	/// So we make the surface and texture somewhat bigger than the size of the sprite
	/// Yes, this means that it could fuck up some shaders.
	/// I dunno what else to do. I couldn't seem to find any relevant docs or issues on stackoverflow
	/// The official APIs from Pango are simply reporting less pixel size than they draw into. (shrug)
	int extraTextureSize = (int)mTextSize;

	if(mNeedsTextRender) {
		// Create appropriately sized cairo surface
		const bool grayscale = false; // Not really supported
		_cairo_format cairoFormat = grayscale ? CAIRO_FORMAT_A8 : CAIRO_FORMAT_ARGB32;


		// clean up any existing surfaces
		if(mCairoSurface) {
			cairo_surface_destroy(mCairoSurface);
			mCairoSurface = nullptr;
#if CAIRO_HAS_WIN32_SURFACE
			mCairoWinImageSurface = nullptr;
#endif
		}

#if CAIRO_HAS_WIN32_SURFACE
		mCairoSurface = cairo_win32_surface_create_with_dib(cairoFormat, mPixelWidth + extraTextureSize, mPixelHeight + extraTextureSize);
#else
		mCairoSurface = cairo_image_surface_create(cairoFormat, mPixelWidth + extraTextureSize, mPixelHeight + extraTextureSize);
#endif
		auto cairoSurfaceStatus = cairo_surface_status(mCairoSurface);
		if(CAIRO_STATUS_SUCCESS != cairoSurfaceStatus) {
			DS_LOG_WARNING("Error creating Cairo surface. Status:" << cairoSurfaceStatus << " w:" << mPixelWidth + extraTextureSize << " h:" << mPixelHeight + extraTextureSize << " text:" << ds::utf8_from_wstr(mText));
			// make sure we don't render garbage
			if(mTexture){
				mTexture = nullptr;
			}
			return;
		}


		if(mCairoSurface){
			// Create context
			/* create our cairo context object that tracks state. */
			if(mCairoContext) {
				cairo_destroy(mCairoContext);
				mCairoContext = nullptr;
			}
			mCairoContext = cairo_create(mCairoSurface);

			auto cairoStatus = cairo_status(mCairoContext);

			if(CAIRO_STATUS_NO_MEMORY == cairoStatus) {
				DS_LOG_WARNING("Out of memory, error creating Cairo context");

				return;
			}

			if(CAIRO_STATUS_SUCCESS != cairoStatus){
				DS_LOG_WARNING("Error creating Cairo context " << cairoStatus);
				return;
			}
		}

		if(mCairoContext) {

			// Draw the text into the buffer
			cairo_set_source_rgb(mCairoContext, mTextColor.r, mTextColor.g, mTextColor.b);//, getDrawOpacity());
			pango_cairo_update_layout(mCairoContext, mPangoLayout);
			pango_cairo_show_layout(mCairoContext, mPangoLayout);

			//	cairo_surface_write_to_png(cairoSurface, "test_font.png");

			// Copy it out to a texture
#ifdef CAIRO_HAS_WIN32_SURFACE
			mCairoWinImageSurface = cairo_win32_surface_get_image(mCairoSurface);
			unsigned char *pixels = cairo_image_surface_get_data(mCairoWinImageSurface);
#else
			unsigned char *pixels = cairo_image_surface_get_data(mCairoSurface);
#endif

			ci::gl::Texture::Format format;
			format.setMagFilter(GL_LINEAR);
			format.setMinFilter(GL_LINEAR);
			mTexture = ci::gl::Texture::create(pixels, GL_BGRA, mPixelWidth + extraTextureSize, mPixelHeight + extraTextureSize, format);
			mTexture->setTopDown(true);
			mNeedsTextRender = false;

			/* */
			if(mCairoContext) {
				cairo_destroy(mCairoContext);
				mCairoContext = nullptr;
			}

			if(mCairoSurface) {
				cairo_surface_destroy(mCairoSurface);
				mCairoSurface = nullptr;
#ifdef CAIRO_HAS_WIN32_SURFACE
				mCairoWinImageSurface = nullptr;
#endif
			}
		}

	} 
}

void Text::writeAttributesTo(ds::DataBuffer& buf){
	ds::ui::Sprite::writeAttributesTo(buf);

	if(mDirty.has(TEXT_DIRTY)){
		buf.add(TEXT_ATT);
		buf.add(mText);
	}

	if(mDirty.has(FONT_DIRTY)) {
		buf.add(FONTNAME_ATT);
		buf.add(mTextFont);
		buf.add(mTextSize);
		buf.add(mLeading);
		buf.add(mTextColor);
		buf.add((int)mTextAlignment);
	}
	if(mDirty.has(LAYOUT_DIRTY)) {
		buf.add(LAYOUT_ATT);
		buf.add(mResizeLimitWidth);
		buf.add(mResizeLimitHeight);
	}
}

void Text::readAttributeFrom(const char attributeId, ds::DataBuffer& buf){
	if(attributeId == TEXT_ATT) {
		std::wstring theText = buf.read<std::wstring>();
		setText(theText);
	} else if(attributeId == FONTNAME_ATT) {

		std::string fontName = buf.read<std::string>();
		float fontSize = buf.read<float>();
		float leading = buf.read<float>();
		ci::Color fontColor = buf.read<ci::Color>();
		auto alignment = (ds::ui::Alignment::Enum)(buf.read<int>());

		setFont(fontName, fontSize);
		setLeading(leading);
		setTextColor(fontColor);
		setAlignment(alignment);

	} else if(attributeId == LAYOUT_ATT) {
		float rsw = buf.read<float>();
		float rsh = buf.read<float>();
		setResizeLimit(rsw, rsh);

	} else {
		ds::ui::Sprite::readAttributeFrom(attributeId, buf);
	}
}

} // namespace ui
} // namespace ds
