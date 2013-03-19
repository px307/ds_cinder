#pragma once
#ifndef DS_APP_ENGINECLIENT_H_
#define DS_APP_ENGINECLIENT_H_

#include "ds/app/blob_reader.h"
#include "ds/app/engine.h"
#include "ds/app/engine_io.h"
//#include "ds/data/data_buffer.h"
#include "ds/thread/gl_thread.h"
#include "ds/thread/work_manager.h"
#include "ds/ui/service/load_image_service.h"
//#include "ds/data/raw_data_buffer.h"
//#include "ds/network/zmq_connection.h"

namespace ds {

/**
 * \class ds::EngineClient
 * The Server engine contains all app-side behaviour, but no rendering.
 */
class EngineClient : public Engine {
  public:
    EngineClient(ds::App&, const ds::cfg::Settings&);
    ~EngineClient();

    virtual ds::WorkManager       &getWorkManager()         { return mWorkManager; }
    virtual ui::LoadImageService  &getLoadImageService()    { return mLoadImageService; }
    virtual ds::sprite_id_t        nextSpriteId();

    virtual void                  installSprite(const std::function<void(ds::BlobRegistry&)>& asServer,
                                                const std::function<void(ds::BlobRegistry&)>& asClient);

    virtual void				          setup(ds::App&);
    virtual void                  setupTuio(ds::App&);
    virtual void	                update();
    virtual void                  draw();

    virtual void                  stopServices();
    virtual int                   getMode() const { return CLIENT_MODE; }

private:
    void                          receiveHeader(ds::DataBuffer&);
    void                          receiveCommand(ds::DataBuffer&);

    typedef Engine inherited;
    WorkManager                   mWorkManager;
    GlThread                      mLoadImageThread;
    ui::LoadImageService          mLoadImageService;

    ds::ZmqConnection             mConnection;
    EngineSender                  mSender;
    EngineReceiver                mReceiver;
    ds::BlobReader                mBlobReader;

    // STATES

    class State {
    public:
      State();
      virtual void              update(EngineClient&) = 0;
    };

    // I have no data, and am waiting for a complete refresh
    class BlankState : public State {
    public:
      BlankState();
      virtual void              update(EngineClient&);
    };

    State*                      mState;
    BlankState                  mBlankState;
};

} // namespace ds

#endif // DS_APP_ENGINESERVER_H_