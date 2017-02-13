#pragma once
#ifndef DS_UI_MEDIA_VIEWER_PDF_PLAYER
#define DS_UI_MEDIA_VIEWER_PDF_PLAYER

#include <ds/ui/sprite/sprite.h>
#include <ds/data/resource.h>

namespace ds {
namespace ui {
class Pdf;
class MediaInterface;
class PDFInterface;

/**
* \class ds::ui::PDFPlayer
*			Shows a scrollable PDF and puts an interface on top of it.
*			Note: for PDF thumbnail viewer to show up, the PDF needs to be loaded via a Resource
*					that has a children vector of resources of the thumbnails set, and the children need to have the correct parentIndex (i.e. page number) set.
*/
class PDFPlayer : public ds::ui::Sprite  {
public:
	PDFPlayer(ds::ui::SpriteEngine& eng, bool embedInterface = true);

	void								setMedia(const std::string mediaPath);
	void								setResource(const ds::Resource mediaResource);

	ds::Resource&						getResource(){ return mSourceResource; }

	void								layout();

	void								showInterface();
	void								hideInterface();

	ds::ui::Pdf*						getPDF();

	void								nextPage();
	void								prevPage();

	void								setGoodStatusCallback(std::function<void()> func){ mGoodStatusCallback = func; }
	void								setErrorCallback(std::function<void(const std::string&)> func){ mErrorMsgCallback = func; }
	void								setShowInterfaceAtStart(bool showInterfaceAtStart);
	void								setSizeChangedCallback(std::function<void(const ci::Vec2f& size)> func){ mSizeChangedCallback = func; }

protected:

	virtual void								onSizeChanged();
	void										loadNextAndPrevPages();
	ds::ui::Pdf*								mPDF;
	ds::ui::Sprite*								mPDFThumbHolder;
	ds::Resource								mSourceResource;

	bool										mFirstPageLoaded;
	int											mCurrentPage; // for displaying the next/back thing
	ds::ui::Pdf*								mPDFNext;
	ds::ui::Pdf*								mPDFPrev;
	bool										mNextReady;
	bool										mPrevReady;

	PDFInterface*								mPdfInterface;
	bool										mEmbedInterface;
	bool										mShowInterfaceAtStart;
	std::function<void(void)>					mGoodStatusCallback;
	std::function<void(const ci::Vec2f&)>		mSizeChangedCallback;
	std::function<void(const std::string&)>		mErrorMsgCallback;

};

} // namespace ui
} // namespace ds

#endif
