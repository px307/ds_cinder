#include "media_viewer.h"


#include <ds/app/environment.h>
#include <ds/ui/sprite/sprite_engine.h>
#include <ds/ui/sprite/image.h>
#include <ds/debug/logger.h>
#include <ds/util/string_util.h>
#include <ds/ui/util/ui_utils.h>

#include <ds/data/resource.h>

#include "ds/ui/media/media_player.h"
#include "ds/ui/media/media_interface.h"

namespace ds {
namespace ui {

MediaViewer::MediaViewer(ds::ui::SpriteEngine& eng, const bool embedInterface)
	: BasePanel(eng)
	, mMediaPlayer(nullptr)
{
	mMediaPlayer = new MediaPlayer(mEngine, embedInterface);
	addChildPtr(mMediaPlayer);
	setDefaultProperties();
}

MediaViewer::MediaViewer(ds::ui::SpriteEngine& eng, const std::string& mediaPath, const bool embedInterface)
	: BasePanel(eng)
	, mMediaPlayer(nullptr)
{
	mMediaPlayer = new MediaPlayer(mEngine, mediaPath, embedInterface);
	addChildPtr(mMediaPlayer);
	setDefaultProperties();
}

MediaViewer::MediaViewer(ds::ui::SpriteEngine& eng, const ds::Resource& resource, const bool embedInterface)
	: BasePanel(eng)
	, mMediaPlayer(nullptr)
{
	mMediaPlayer = new MediaPlayer(mEngine, resource, embedInterface);
	addChildPtr(mMediaPlayer);
	setDefaultProperties();
}


void MediaViewer::setSettings(const MediaViewerSettings& newSettings){
	mMediaViewerSettings = newSettings;
}

void MediaViewer::setDefaultProperties(){
	if(mMediaPlayer){
		mMediaPlayer->setInitializedCallback([this]{ initialize(); });
	}

	setDefaultBounds(mEngine.getWorldWidth(), mEngine.getWorldHeight());
	setWebViewSize(ci::Vec2f::zero());
	setCacheImages(false);

}

void MediaViewer::loadMedia(const std::string& mediaPath, const bool initializeImmediately) {
	if(mMediaPlayer){
		mMediaPlayer->loadMedia(mediaPath, initializeImmediately);
	}
}

void MediaViewer::loadMedia(const ds::Resource& reccy, const bool initializeImmediately) {
	if(mMediaPlayer){
		mMediaPlayer->loadMedia(reccy, initializeImmediately);
	}
}


void MediaViewer::setDefaultBounds(const float defaultWidth, const float defaultHeight){
	mMediaViewerSettings.mDefaultBounds.x = defaultWidth;
	mMediaViewerSettings.mDefaultBounds.y = defaultHeight;
}

void MediaViewer::setWebViewSize(const ci::Vec2f webSize){
	mMediaViewerSettings.mWebDefaultSize = webSize;
}	

void MediaViewer::initialize(){
	if(!mMediaPlayer) return;

	if(!mMediaPlayer->getInitialized()){
		// the initialize function on media calls a callback to call this initialize function, so we return right after initializing the player
		mMediaPlayer->initialize();
		return;
	}

	mMediaPlayer->sendToFront();

	mContentAspectRatio = mMediaPlayer->getContentAspectRatio();

	const float contentWidth = mMediaPlayer->getWidth();
	const float contentHeight = mMediaPlayer->getHeight();

	// calculate a default size that maximizes size
	float settingsAspect = 1.0f;
	const float settingsWidth = mMediaViewerSettings.mDefaultBounds.x;
	const float settingsHeight = mMediaViewerSettings.mDefaultBounds.y;
	if(settingsHeight > 0.0f){
		settingsAspect = settingsWidth / settingsHeight;
	}
	
	// calculate a width to make the player fit maximally
	float scaleFactor = 1.0f;
	float idealWidth = settingsWidth;
	float idealHeight = settingsHeight;
	if(mContentAspectRatio < settingsAspect){
		scaleFactor = settingsHeight / contentHeight;
		idealWidth = contentWidth * scaleFactor;
	} else if(mContentAspectRatio > settingsAspect){
		scaleFactor = settingsWidth / contentWidth;
		idealHeight = contentHeight * scaleFactor;
	}

	mDefaultSize = ci::Vec2f(idealWidth, idealHeight);
	// setting size is necessary to get size limits to work
	setSize(idealWidth, idealHeight);
	setSizeLimits();
	setViewerSize(mDefaultSize.x, mDefaultSize.y);
}

void MediaViewer::uninitialize() {
	if(mMediaPlayer){
		mMediaPlayer->uninitialize();
	}
}

void MediaViewer::setCacheImages(bool cacheImages){
	mMediaViewerSettings.mCacheImages = cacheImages;
}

void MediaViewer::onLayout(){
	if(mMediaPlayer){
		mMediaPlayer->setSize(getWidth(), getHeight());
	}
}

void MediaViewer::enter(){
	if(mMediaPlayer){
		mMediaPlayer->enter();
	}
}

void MediaViewer::exit(){
	if(mMediaPlayer){
		mMediaPlayer->exit();
	}
}

void MediaViewer::userInputReceived(){
	BasePanel::userInputReceived();

	showInterface();
}

void MediaViewer::showInterface(){
	if(mMediaPlayer){
		mMediaPlayer->showInterface();
	}
}

void MediaViewer::stopContent(){
	if(mMediaPlayer){
		mMediaPlayer->stopContent();
	}
}

ds::ui::Sprite* MediaViewer::getPlayer(){
	if(mMediaPlayer){
		return mMediaPlayer->getPlayer();
	}

	return nullptr;
}

void MediaViewer::setErrorCallback(std::function<void(const std::string& msg)> func){
	if(mMediaPlayer){
		mMediaPlayer->setErrorCallback(func);
	}
}

void MediaViewer::setStatusCallback(std::function<void(const bool isGood)> func){
	if(mMediaPlayer){
		mMediaPlayer->setStatusCallback(func);
	}
}

void MediaViewer::handleStandardClick(const ci::Vec3f& globalPos){
	if(mMediaPlayer){
		mMediaPlayer->handleStandardClick(globalPos);
	}
}

void MediaViewer::enableStandardClick(){
	setTapCallback([this](ds::ui::Sprite* bs, const ci::Vec3f& pos){
		handleStandardClick(pos);
	});
}

} // namespace ui
} // namespace ds
