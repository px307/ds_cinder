#include "image.h"

#include <map>
#include <cinder/ImageIo.h>
#include "ds/ui/sprite/sprite_engine.h"
#include "ds/util/file_name_parser.h"

namespace {

std::map<std::string, std::shared_ptr<ci::gl::Texture>> mTextureCache;

}

namespace ds {
namespace ui {

Image::Image( SpriteEngine& engine, const std::string &filename )
    : inherited(engine)
    , mImageService(engine.getLoadImageService())
    , mImageToken(mImageService)
    , mFlags(0)
    , mResourceFn(filename)
{
  try {
    Vec2f size = parseFileMetaDataSize(filename);
    mWidth = size.x;
    mHeight = size.y;
  } catch (ParseFileMetaException &e) {
    std::cout << e.what() << std::endl;
    std::cout << "Going to load image synchronously; this will affect performance." << std::endl;
  }
  setTransparent(false);
}

Image::~Image()
{
}

void Image::drawLocal()
{
  if (!mTexture) {
    // XXX Do bounds check here
    if (mImageToken.canAcquire()) { // && intersectsLocalScreen){
      requestImage();
    }
    float         fade;
    mTexture = mImageToken.getImage(fade);
    // Keep up the bounds
    if (mTexture) {
      const float         prevRealW = getWidth(), prevRealH = getHeight();
      if (prevRealW <= 0 || prevRealH <= 0) {
        Sprite::setSize(static_cast<float>(mTexture.getWidth()), static_cast<float>(mTexture.getHeight()));
      } else {
        float             prevWidth = prevRealW * getScale().x;
        float             prevHeight = prevRealH * getScale().y;
        Sprite::setSize(static_cast<float>(mTexture.getWidth()), static_cast<float>(mTexture.getHeight()));
        setSize(prevWidth, prevHeight);
      }
    }
    return;
  }

  ci::gl::draw(mTexture);
}

void Image::setSize( float width, float height )
{
    setScale( width / getWidth(), height / getHeight() );
}

void Image::loadImage( const std::string &filename )
{
  mTexture.reset();
  mResourceFn = filename;
  requestImage();

//  std::cout << "Image::loadImage() Still makin' this compatibile with async loading..." << std::endl;
#if 0
    mTexture = getImage( filename );
    float prevWidth = getWidth() * getScale().x;
    float prevHeight = getHeight() * getScale().y;

    if ( !mTexture )
        return;

    Sprite::setSize(static_cast<float>(mTexture->getWidth()), static_cast<float>(mTexture->getHeight()));
    setSize(prevWidth, prevHeight);
#endif
}

void Image::requestImage()
{
  // XXX Check to see if I have a resource ID, and use that instead.
  if (mResourceFn.empty()) return;

  mImageToken.acquire(mResourceFn, mFlags);
}

bool Image::isLoaded() const
{
  return mTexture;
}

} // namespace ui
} // namespace ds
