#include "web.h"

#include <algorithm>
#include <regex>
#include <cinder/ImageIo.h>
#include <boost/filesystem.hpp>
#include <ds/app/app.h>
#include <ds/app/engine/engine.h>
#include <ds/app/blob_reader.h>
#include <ds/app/environment.h>
#include <ds/data/data_buffer.h>
#include <ds/debug/logger.h>
#include <ds/math/math_func.h>
#include <ds/ui/sprite/sprite_engine.h>
#include <ds/ui/tween/tweenline.h>
#include <ds/util/string_util.h>
#include "private/web_service.h"
#include "private/web_callbacks.h"


#include "include/cef_app.h"

namespace {
// Statically initialize the world class. Done here because the Body is
// guaranteed to be referenced by the final application.
class Init {
public:
	Init() {
		ds::App::AddStartup([](ds::Engine& e) {
			ds::web::WebCefService*		w = new ds::web::WebCefService(e);
			if (!w) throw std::runtime_error("Can't create ds::web::Service");
			e.addService("cef_web", *w);

			e.installSprite([](ds::BlobRegistry& r){ds::ui::Web::installAsServer(r);},
							[](ds::BlobRegistry& r){ds::ui::Web::installAsClient(r);});
		});
	}
	void					doNothing() { }
};
Init						INIT;

char						BLOB_TYPE			= 0;
const ds::ui::DirtyState&	URL_DIRTY			= ds::ui::INTERNAL_A_DIRTY;
const ds::ui::DirtyState&	TOUCHES_DIRTY		= ds::ui::INTERNAL_B_DIRTY;
const ds::ui::DirtyState&	KEYBOARD_DIRTY		= ds::ui::INTERNAL_C_DIRTY;
const ds::ui::DirtyState&	HISTORY_DIRTY		= ds::ui::INTERNAL_D_DIRTY;
const char					URL_ATT				= 80;
const char					TOUCH_ATT			= 81;
const char					KEYBOARD_ATT		= 82;
const char					HISTORY_ATT			= 83;
}

namespace ds {
namespace ui {

/**
 * \class ds::ui::sprite::Web static
 */
void Web::installAsServer(ds::BlobRegistry& registry) {
	BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromClient(r);});
}

void Web::installAsClient(ds::BlobRegistry& registry) {
	BLOB_TYPE = registry.add([](BlobReader& r) {Sprite::handleBlobFromServer<Web>(r);});
}

/**
 * \class ds::ui::sprite::Web
 */
Web::Web( ds::ui::SpriteEngine &engine, float width, float height )
	: Sprite(engine, width, height)
	, mService(engine.getService<ds::web::WebCefService>("cef_web"))
	, mDragScrolling(false)
	, mDragScrollMinFingers(2)
	, mClickDown(false)
	, mPageScrollCount(0)
	, mDocumentReadyFn(nullptr)
	, mHasError(false)
	, mAllowClicks(true)
	, mBrowserId(-1)
	, mBuffer(nullptr)
	, mHasBuffer(false)
	, mBrowserSize(0, 0)
	, mUrl("")
	, mIsLoading(false)
	, mCanBack(false)
	, mCanForward(false)
	, mZoom(1.0)
	, mTransparentBackground(false)
	, mNeedsZoomCheck(false)
	, mHasDocCallback(false)
	, mHasErrorCallback(false)
	, mHasAddressCallback(false)
	, mHasTitleCallback(false)
	, mHasFullCallback(false)
	, mHasLoadingCallback(false)
	, mHasCallbacks(false)
	, mHasAuthCallback(false)
	, mIsFullscreen(false)
	, mNeedsInitialized(false)
	, mCallbacksCue(nullptr)
{
	// Should be unnecessary, but really want to make sure that static gets initialized
	INIT.doNothing();

	mBlobType = BLOB_TYPE;
	mLayoutFixedAspect = true;

	setTransparent(false);
	setUseShaderTexture(true);
	setSize(width, height);

	enable(true);
	enableMultiTouch(ds::ui::MULTITOUCH_INFO_ONLY);

	setProcessTouchCallback([this](ds::ui::Sprite *, const ds::ui::TouchInfo &info) {
		handleTouch(info);
	});

	createBrowser();
}

void Web::createBrowser(){
	clearBrowser();

	mService.createBrowser("", this, [this](int browserId){
		{
			// This callback comes back from the CEF UI thread
			std::lock_guard<std::mutex> lock(mMutex);
			mBrowserId = browserId;
		}

		mNeedsInitialized = true; 

		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			mCallbacksCue = t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			
			mHasCallbacks = true;
		}

	}, mTransparentBackground);
}

void Web::clearBrowser(){
	if(mBrowserId < 0){
		mService.cancelCreation(this);
	} else {
		// This clears the callbacks too
		mService.closeBrowser(mBrowserId);
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mBrowserId = -1;
	}
}

Web::~Web() {

	clearBrowser();

	if(mCallbacksCue){
		mCallbacksCue->removeSelf();
	}

	{
		// I don't think we'll need to lock this anymore, as the previous call to clear will prevent any callbacks
	//	std::lock_guard<std::mutex> lock(mMutex);
		if(mBuffer){
			delete mBuffer;
			mBuffer = nullptr;
		}
	}
}

void Web::setWebTransparent(const bool isTransparent){
	if(isTransparent == mTransparentBackground) return;
	mTransparentBackground = isTransparent;

	createBrowser();
}

void Web::initializeBrowser(){
	if(mBrowserId < 0){
		return;
	}

	DS_LOG_INFO("Initialize browser: " << mUrl <<" " << mBrowserId);

	mNeedsInitialized = false;

	// Now that we know about the browser, set it to the correct size
	if(!mBuffer){
		onSizeChanged();
	} else {
		mService.requestBrowserResize(mBrowserId, mBrowserSize);
	}

	loadUrl(mUrl);
	if(mZoom != 1.0){
		setZoom(mZoom);
	}

	ds::web::WebCefCallbacks wcc;
	wcc.mTitleChangeCallback = [this](const std::wstring& newTitle){
		// This callback comes back from the CEF UI thread
		std::lock_guard<std::mutex> lock(mMutex);

		mTitle = newTitle;

		mHasTitleCallback = true;
		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			mHasCallbacks = true;
		}
	};

	wcc.mLoadChangeCallback = [this](const bool isLoading, const bool canBack, const bool canForwards, const std::string& newUrl){
		// This callback comes back from the CEF UI thread
		std::lock_guard<std::mutex> lock(mMutex);

		mIsLoading = isLoading;
		mCanBack = canBack;
		mCanForward = canForwards;
		mCurrentUrl = newUrl;

		// zoom seems to need to be set for every page
		// This callback is locked in CEF, so zoom checking needs to happen later
		if(mZoom != 1.0){
			mNeedsZoomCheck = true;
		}

		mHasAddressCallback = true;
		mHasLoadingCallback = true;
		mHasDocCallback = true;

		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			mCallbacksCue = t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			mHasCallbacks = true;
		}
	};

	wcc.mPaintCallback = [this](const void * buffer, const int bufferWidth, const int bufferHeight){

		// This callback comes back from the CEF UI thread
		std::lock_guard<std::mutex> lock(mMutex);

		// verify the buffer exists and is the correct size
		// TODO: Add ability to redraw only the changed rectangles (which is what comes from CEF)
		// Would be much more performant, especially for large browsers with small ui changes (like blinking cursors)
		if(mBuffer && bufferWidth == mBrowserSize.x && bufferHeight == mBrowserSize.y){
			mHasBuffer = true;
			memcpy(mBuffer, buffer, bufferWidth * bufferHeight * 4);
		}
	};

	wcc.mErrorCallback = [this](const std::string& theError){
		// This callback comes back from the CEF UI thread
		std::lock_guard<std::mutex> lock(mMutex);
		mHasError = true;
		mErrorMessage = theError;

		mHasErrorCallback = true;

		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			mCallbacksCue = t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			mHasCallbacks = true;
		}
	};

	wcc.mFullscreenCallback = [this](const bool isFullscreen){
		// This callback comes back from the CEF UI thread
		std::lock_guard<std::mutex> lock(mMutex);
		mIsFullscreen = isFullscreen;

		mHasFullCallback = true;

		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			mCallbacksCue = t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			mHasCallbacks = true;
		}
	};

	wcc.mAuthCallback = [this](const bool isProxy, const std::string& host, const int port, const std::string& realm, const std::string& scheme){
		// This callback comes back from the CEF IO thread
		std::lock_guard<std::mutex> lock(mMutex);

		// If the client ui has a callback for authorization, do that
		// Otherwise, just cancel the request
		if(mAuthRequestCallback){
			mAuthCallback.mIsProxy = isProxy;
			mAuthCallback.mHost = host;
			mAuthCallback.mPort = port;
			mAuthCallback.mRealm = realm;
			mAuthCallback.mScheme = scheme;

		} // if there's no authRequestCallback, we handle this in a callback to avoid recursive lock

	
		mHasAuthCallback = true;

		if(!mHasCallbacks){
			auto& t = mEngine.getTweenline().getTimeline();
			mCallbacksCue = t.add([this]{ dispatchCallbacks(); }, t.getCurrentTime() + 0.001f);
			mHasCallbacks = true;
		}
	};

	mService.addWebCallbacks(mBrowserId, wcc);
}


void Web::dispatchCallbacks(){
	if(mNeedsInitialized){
		initializeBrowser();
	}

	if(mHasDocCallback){
		if(mDocumentReadyFn) mDocumentReadyFn();
		mHasDocCallback = false;
	}

	if(mHasErrorCallback){
		if(mErrorCallback) mErrorCallback(mErrorMessage);
		mHasErrorCallback = false;
	}

	if(mHasAddressCallback){
		if(mAddressChangedCallback) mAddressChangedCallback(mUrl);
		mHasAddressCallback = false;
	}

	if(mHasTitleCallback){
		if(mTitleChangedCallback) mTitleChangedCallback(mTitle);
		mHasTitleCallback = false;
	}

	if(mHasFullCallback){
		if(mFullscreenCallback) mFullscreenCallback(mIsFullscreen);
		mHasFullCallback = false;
	}

	if(mHasLoadingCallback){
		if(mLoadingUpdatedCallback) mLoadingUpdatedCallback(mIsLoading);
		mHasLoadingCallback = false;
	}

	if(mHasAuthCallback){
		if(mAuthRequestCallback){
			mAuthRequestCallback(mAuthCallback);
		} else {
			mService.authCallbackCancel(mBrowserId);
		}
		mHasAuthCallback = false;
	}

	mHasCallbacks = false;
	mCallbacksCue = nullptr;
}

void Web::onUpdateClient(const ds::UpdateParams &p) {
	update(p);
}

void Web::onUpdateServer(const ds::UpdateParams &p) {
	mPageScrollCount = 0;

	update(p);
}

void Web::update(const ds::UpdateParams &p) {

	// Get zoom locks CEF, so 
	if(mNeedsZoomCheck && getZoom() != mZoom){
		mNeedsZoomCheck = false;
		setZoom(mZoom);
	}

	// Anything that modifies mBuffer needs to be locked
	std::lock_guard<std::mutex> lock(mMutex);

	if(mBuffer && mHasBuffer){
		ci::gl::Texture::Format fmt;
		fmt.setMinFilter(GL_LINEAR);
		fmt.setMagFilter(GL_LINEAR);
		mWebTexture = ci::gl::Texture::create(mBuffer, GL_BGRA, mBrowserSize.x, mBrowserSize.y, fmt);
		mHasBuffer = false;
	}
}

void Web::onSizeChanged() {
	{
		// Anything that modifies mBuffer needs to be locked
		std::lock_guard<std::mutex> lock(mMutex);

		const int theWid = static_cast<int>(getWidth());
		const int theHid = static_cast<int>(getHeight());
		const ci::ivec2 newBrowserSize(theWid, theHid);
		if(newBrowserSize == mBrowserSize && mBuffer){
			return;
		}

		mBrowserSize = newBrowserSize;

		if(mBuffer){
			delete mBuffer;
			mBuffer = nullptr;
		}
		const int bufferSize = theWid * theHid * 4;
		mBuffer = new unsigned char[bufferSize];

		mHasBuffer = false;
	}

	if(mBrowserId > -1){
		mService.requestBrowserResize(mBrowserId, mBrowserSize);
	}
}

void Web::drawLocalClient() {
	if (mWebTexture) {
		if(mRenderBatch){
			// web texture is top down, and render batches work bottom up
			// so flippy flip flip
			ci::gl::scale(1.0f, -1.0f);
			ci::gl::translate(0.0f, -getHeight());
			mWebTexture->bind();
			mRenderBatch->draw();
			mWebTexture->unbind();
		} else {
			ci::gl::draw(mWebTexture, ci::Rectf(0.0f, static_cast<float>(mWebTexture->getHeight()), static_cast<float>(mWebTexture->getWidth()), 0.0f));
		}
	}
}

std::string Web::getUrl() {
	return mUrl;
}

std::string Web::getCurrentUrl(){
	std::lock_guard<std::mutex> lock(mMutex);
	return mCurrentUrl;
}

void Web::loadUrl(const std::wstring &url) {
	loadUrl(ds::utf8_from_wstr(url));
}

void Web::loadUrl(const std::string &url) {
	mCurrentUrl = url;
	mUrl = url;
	markAsDirty(URL_DIRTY);
	if(mBrowserId > -1 && !mUrl.empty()){
		mService.loadUrl(mBrowserId, mUrl);
	}
}

void Web::setUrl(const std::string& url) {
	loadUrl(url);
}

void Web::setUrlOrThrow(const std::string& url) {
	loadUrl(url);
}

void Web::sendKeyDownEvent(const ci::app::KeyEvent &event) {
	mService.sendKeyEvent(mBrowserId, 0, event.getNativeKeyCode(), event.getChar(), event.isShiftDown(), event.isControlDown(), event.isAltDown());

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		mKeyPresses.push_back(WebKeyboardInput(0, event.getNativeKeyCode(), event.getChar(), event.isShiftDown(), event.isControlDown(), event.isAltDown()));
		markAsDirty(KEYBOARD_DIRTY);
	}
}

void Web::sendKeyUpEvent(const ci::app::KeyEvent &event){
	mService.sendKeyEvent(mBrowserId, 2, event.getNativeKeyCode(), event.getChar(), event.isShiftDown(), event.isControlDown(), event.isAltDown());

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		mKeyPresses.push_back(WebKeyboardInput(2, event.getNativeKeyCode(), event.getChar(), event.isShiftDown(), event.isControlDown(), event.isAltDown()));
		markAsDirty(KEYBOARD_DIRTY);
	}
}

void Web::sendMouseDownEvent(const ci::app::MouseEvent& e) {
	if(!mAllowClicks) return;

	sendTouchToService(e.getX(), e.getY(), 0, 0, 1);
}

void Web::sendMouseDragEvent(const ci::app::MouseEvent& e) {
	if(!mAllowClicks) return;

	sendTouchToService(e.getX(), e.getY(), 0, 1, 1);
}

void Web::sendMouseUpEvent(const ci::app::MouseEvent& e) {
	if(!mAllowClicks) return;

	sendTouchToService(e.getX(), e.getY(), 0, 2, 1);
}

void Web::sendMouseClick(const ci::vec3& globalClickPoint){
	if(!mAllowClicks) return;

	ci::vec2 pos = ci::vec2(globalToLocal(globalClickPoint));
	int xPos = (int)roundf(pos.x);
	int yPos = (int)roundf(pos.y);

	sendTouchToService(xPos, yPos, 0, 0, 1);
	sendTouchToService(xPos, yPos, 0, 1, 1);
	sendTouchToService(xPos, yPos, 0, 2, 1);
}

void Web::sendTouchToService(const int xp, const int yp, const int btn, const int state, const int clickCnt, 
							 const bool isWheel, const int xDelta, const int yDelta) {
	if(mBrowserId < 0) return;

	if(isWheel){
		mService.sendMouseWheelEvent(mBrowserId, xp, yp, xDelta, yDelta);
	} else {
		mService.sendMouseClick(mBrowserId, xp, yp, btn, state, clickCnt);
	}

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		WebTouch wt = WebTouch(xp, yp, btn, state, clickCnt);
		if(isWheel){
			wt.mIsWheel = true;
			wt.mXDelta = xDelta;
			wt.mYDelta = yDelta;
		}
		mTouches.push_back(wt);

		markAsDirty(TOUCHES_DIRTY);
	}
}

void Web::handleTouch(const ds::ui::TouchInfo& touchInfo) {
	if(touchInfo.mFingerIndex != 0)
		return;

	ci::vec2 pos = ci::vec2(globalToLocal(touchInfo.mCurrentGlobalPoint));
	int xPos = (int)roundf(pos.x);
	int yPos = (int)roundf(pos.y);

	if(ds::ui::TouchInfo::Added == touchInfo.mPhase) {
		if(mAllowClicks){
			sendTouchToService(xPos, yPos, 0, 0, 1);
		}
		if(mDragScrolling){
			mClickDown = true;
		}
		
	} else if(ds::ui::TouchInfo::Moved == touchInfo.mPhase) {

		if(mDragScrolling && touchInfo.mNumberFingers >= mDragScrollMinFingers){
			
			if(mClickDown){
				if(mAllowClicks){
					sendTouchToService(xPos, yPos, 0, 1, 0);
					sendTouchToService(xPos, yPos, 0, 2, 0);
				}
				mClickDown = false;
			}

			float yDelta = touchInfo.mCurrentGlobalPoint.y - mPreviousTouchPos.y;
			sendTouchToService(xPos, yPos, 0, 0, 0, true, 0, static_cast<int>(roundf(yDelta)));		
			
		} else {
			if(mAllowClicks){
				sendTouchToService(xPos, yPos, 0, 1, 1);
			}
		}
	} else if(ds::ui::TouchInfo::Removed == touchInfo.mPhase) {
		if(mAllowClicks){
			sendTouchToService(xPos, yPos, 0, 2, 1);
		}
	}

	mPreviousTouchPos = touchInfo.mCurrentGlobalPoint;
}

void Web::setZoom(const double percent) {
	mZoom = percent;
	mService.setZoomLevel(mBrowserId, (percent - 1.0) / .25);
}

double Web::getZoom() const {
	if(mBrowserId < 0) return mZoom;
	return (mService.getZoomLevel(mBrowserId)*.25 + 1.0);
}

void Web::goBack() {
	mService.goBackwards(mBrowserId);

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		mHistoryRequests.push_back(WebControl(WebControl::GO_FORW));
		markAsDirty(HISTORY_DIRTY);
	}
}

void Web::goForward() {
	mService.goForwards(mBrowserId);

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		mHistoryRequests.push_back(WebControl(WebControl::GO_BACK));
		markAsDirty(HISTORY_DIRTY);
	}
}

void Web::reload(const bool ignoreCache) {
	mService.reload(mBrowserId, ignoreCache);

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		if(ignoreCache){
			mHistoryRequests.push_back(WebControl(WebControl::RELOAD_HARD));
		} else {
			mHistoryRequests.push_back(WebControl(WebControl::RELOAD_SOFT));
		}
		markAsDirty(HISTORY_DIRTY);
	}
}

void Web::stop() {
	mService.stopLoading(mBrowserId);

	if(mEngine.getMode() == ds::ui::SpriteEngine::SERVER_MODE || mEngine.getMode() == ds::ui::SpriteEngine::CLIENTSERVER_MODE){
		mHistoryRequests.push_back(WebControl(WebControl::STOP_LOAD));
		markAsDirty(HISTORY_DIRTY);
	}
}

bool Web::canGoBack() {
	return mCanBack;
}

bool Web::canGoForward() {
	return mCanForward;
}

bool Web::isLoading() {
	return mIsLoading;
}

void Web::setTitleChangedFn(const std::function<void(const std::wstring& newTitle)>& func){
	mTitleChangedCallback = func;
}

void Web::setAddressChangedFn(const std::function<void(const std::string& new_address)>& fn) {
	mAddressChangedCallback = fn;
}

void Web::setDocumentReadyFn(const std::function<void(void)>& fn) {
	mDocumentReadyFn = fn;
}

void Web::setErrorCallback(std::function<void(const std::string&)> func){
	mErrorCallback = func;
}

void Web::setFullscreenChangedCallback(std::function<void(const bool)> func){
	mFullscreenCallback = func;
}

void Web::setAuthCallback(std::function<void(AuthCallback)> func){
	mAuthRequestCallback = func;
}

void Web::authCallbackCancel(){
	mService.authCallbackCancel(mBrowserId);
}

void Web::authCallbackContinue(const std::string& username, const std::string& password){
	mService.authCallbackContinue(mBrowserId, username, password);
}

void Web::setErrorMessage(const std::string &message){
	mHasError = true;
	mErrorMessage = message;

	if(mErrorCallback){
		mErrorCallback(mErrorMessage);
	}
}

void Web::clearError(){
	mHasError = false;
}

ci::vec2 Web::getDocumentSize() {
	// TODO?
	return ci::vec2(getWidth(), getHeight());
}

ci::vec2 Web::getDocumentScroll() {
	/* TODO
	if (!mWebViewPtr) return ci::vec2(0.0f, 0.0f);
	return get_document_scroll(*mWebViewPtr);
	*/
	return ci::vec2(0.0f, 0.0f);
}

void Web::executeJavascript(const std::string& theScript){
	/* TODO
	Awesomium::WebString		object_ws(Awesomium::WebString::CreateFromUTF8(theScript.c_str(), theScript.size()));
	Awesomium::JSValue			object = mWebViewPtr->ExecuteJavascriptWithResult(object_ws, Awesomium::WebString());
	std::cout << "Object return: " << ds::web::str_from_webstr(object.ToString()) << std::endl;
	*/
}

void Web::writeAttributesTo(ds::DataBuffer &buf) {
	ds::ui::Sprite::writeAttributesTo(buf);

	if (mDirty.has(URL_DIRTY)) {
		buf.add(URL_ATT);
		buf.add(mUrl);
	}

	if(mDirty.has(TOUCHES_DIRTY) && !mTouches.empty()){
		buf.add(TOUCH_ATT);
		buf.add(static_cast<int>(mTouches.size()));
		for (auto it : mTouches){
			buf.add(it.mX);
			buf.add(it.mY);
			buf.add(it.mBttn);
			buf.add(it.mState);
			buf.add(it.mClickCount);
			buf.add(it.mIsWheel);
			buf.add(it.mXDelta);
			buf.add(it.mYDelta);
		}

		mTouches.clear();
	}

	if(mDirty.has(KEYBOARD_DIRTY) && !mKeyPresses.empty()){
		buf.add(KEYBOARD_ATT);
		buf.add(static_cast<int>(mKeyPresses.size()));
		for(auto it : mKeyPresses){
			buf.add(it.mState);
			buf.add(it.mNativeKeyCode);
			buf.add(it.mCharacter);
			buf.add(it.mShiftDown);
			buf.add(it.mCntrlDown);
			buf.add(it.mAltDown);
		}

		mKeyPresses.clear();
	}

	if(mDirty.has(HISTORY_DIRTY) && !mHistoryRequests.empty()){
		buf.add(HISTORY_ATT);
		buf.add(static_cast<int>(mHistoryRequests.size()));
		for (auto it : mHistoryRequests){
			buf.add(it.mCommand);
		}

		mHistoryRequests.clear();
	}
}

void Web::readAttributeFrom(const char attributeId, ds::DataBuffer &buf) {
	if(attributeId == URL_ATT) {
		setUrl(buf.read<std::string>());
	} else if(attributeId == TOUCH_ATT){
		auto sizey = buf.read<int>();
		for(int i = 0; i < sizey; i++){
			int xxx = buf.read<int>();
			int yyy = buf.read<int>();
			int btn = buf.read<int>();
			int sta = buf.read<int>();
			int clk = buf.read<int>();
			bool iw = buf.read<bool>();
			int xd = buf.read<int>();
			int yd = buf.read<int>();
			sendTouchToService(xxx, yyy, btn, sta, clk, iw, xd, yd);
		}

	} else if(attributeId == KEYBOARD_ATT){
		auto sizey = buf.read<int>();
		for(int i = 0; i < sizey; i++){
			int state = buf.read<int>();
			int nativ = buf.read<int>();
			char chary = buf.read<char>();
			bool isShif = buf.read<bool>();
			bool isCntrl = buf.read<bool>();
			bool isAlt = buf.read<bool>();

			if(mBrowserId > -1){
				mService.sendKeyEvent(mBrowserId, state, nativ, chary, isShif, isCntrl, isAlt);
			}
		}

	} else if(attributeId == HISTORY_ATT){
		auto sizey = buf.read<int>();
		for(auto i = 0; i < sizey; i++){
			int commandy = buf.read<int>();
			if(commandy == WebControl::GO_BACK){
				goBack();
			} else if(commandy == WebControl::GO_FORW){
				goForward();
			} else if(commandy == WebControl::RELOAD_SOFT){
				reload();
			} else if(commandy == WebControl::RELOAD_HARD){
				reload(true);
			} else if(commandy == WebControl::STOP_LOAD){
				stop();
			} 
		}
	} else {
		ds::ui::Sprite::readAttributeFrom(attributeId, buf);
	}
}

void Web::setAllowClicks(const bool doAllowClicks){
	mAllowClicks = doAllowClicks;
}

} // namespace ui
} // namespace ds
