#include "gstreamer_wrapper.h"

#include "ds/debug/logger.h"
#include <iostream>
#include <algorithm>

#include "gst/net/gstnetclientclock.h"

namespace gstwrapper
{

GStreamerWrapper::GStreamerWrapper()
	: m_bFileIsOpen(false)
	, m_cVideoBuffer(NULL)
	, m_cAudioBuffer(NULL)
	, m_GstPipeline(NULL)
	, m_GstVideoSink(NULL)
	, m_GstAudioSink(NULL)
	, m_GstPanorama(NULL)
	, m_GstBus(NULL)
	, m_AudioBufferWanted(false)
	, m_StartPlaying(true)
	, m_StopOnLoopComplete(false)
	, m_CustomPipeline(false)
	, m_VideoLock(m_VideoMutex, std::defer_lock)
	//, m_VerboseLogging(true)
	, m_VerboseLogging(false)
	, m_cVideoBufferSize(0)
	, mClockProvider(NULL)
	, m_NetClock(NULL)
	, m_BaseTime(0)
	, m_RunningTime(0)
	, m_playFromPause(false)
	, m_SeekTime(0)
	, m_newLoop(false)
	, m_LivePipeline(false)
	, m_FullPipeline(false)
	, m_AutoRestartStream(true)
	, mServer(true)
	, m_ValidInstall(true)
	, mSyncedMode(false)
	, m_StreamNeedsRestart(false)
{

	m_CurrentPlayState = NOT_INITIALIZED;
}

GStreamerWrapper::~GStreamerWrapper()
{
	close();

	if (m_VideoLock.owns_lock()) {
		try {
			m_VideoLock.unlock();
		} catch (std::exception& ex) {
			std::cout	<< "A fatal deadlock occurred and I can't survive from this one :(" << std::endl
						<< "Probably your screen is stuck and this is the last log line you are reading." << std::endl
						<< "Exception: " << ex.what() << std::endl;
		}
	}
}

void GStreamerWrapper::resetProperties(){
	// init property variables
	m_iNumVideoStreams = 0;
	m_iNumAudioStreams = 0;
	m_iCurrentVideoStream = 0;
	m_iCurrentAudioStream = 0;
	m_iWidth = 0;
	m_iHeight = 0;
	m_iCurrentFrameNumber = 0;	// set to invalid, as it is not decoded yet
	m_dCurrentTimeInMs = 0;	// set to invalid, as it is not decoded yet
	m_bIsAudioSigned = false;
	m_bIsNewVideoFrame = false;
	m_iNumAudioChannels = 0;
	m_iAudioSampleRate = 0;
	m_iAudioBufferSize = 0;
	m_iAudioWidth = 0;
	m_fFps = 0;
	m_dDurationInMs = 0;
	m_iNumberOfFrames = 0;
	m_fVolume = 1.0f;
	m_fPan = 0.0f;
	m_fSpeed = 1.0f;
	m_PlayDirection = FORWARD;
	m_CurrentPlayState = NOT_INITIALIZED;
	m_CurrentGstState = STATE_NULL;
	m_LoopMode = LOOP;
	m_PendingSeek = false;
	m_cVideoBufferSize = 0;
	m_LivePipeline = false;
	m_FullPipeline = false;
	m_AutoRestartStream = true;
	m_iDurationInNs = -1;
	m_iCurrentTimeInNs = -1;
	mSyncedMode = false;
	m_StreamNeedsRestart = false;
	m_iStreamingLatency = 200000000;

}

void GStreamerWrapper::parseFilename(const std::string& theFile){
	std::string strFilename = theFile;
	std::replace(strFilename.begin(), strFilename.end(), '\\', '/');
	// Check and re-arrange filename string
	if(strFilename.find("file:/", 0) == std::string::npos &&
	   strFilename.find("file:///", 0) == std::string::npos &&
	   strFilename.find("http://", 0) == std::string::npos)
	{
		strFilename = "file:///" + strFilename;
	}
	m_strFilename = strFilename;
}

void GStreamerWrapper::enforceModFourWidth(const int vidWidth, const int vidHeight){
	int videoWidth = vidWidth;
	int videoHeight = vidHeight;

	if(videoWidth % 4 != 0){
		videoWidth += 4 - videoWidth % 4;
	}
	m_iWidth = videoWidth;
	m_iHeight = videoHeight;
}


void GStreamerWrapper::enforceModEightWidth(const int vidWidth, const int vidHeight){
	int videoWidth = vidWidth;
	int videoHeight = vidHeight;

	if(videoWidth % 8 != 0){
		videoWidth += 8 - videoWidth % 8;
	}

	if(videoHeight % 4 != 0){
		videoHeight += 4 - videoHeight % 4;
	}

	m_iWidth = videoWidth;
	m_iHeight = videoHeight;
}

guint64 GStreamerWrapper::getNetClockTime()
{
	if (m_NetClock) {
		return gst_clock_get_time(m_NetClock);
	}

	return GST_CLOCK_TIME_NONE;
}


bool GStreamerWrapper::isPlayFromPause()
{
	return m_playFromPause;
}

void GStreamerWrapper::clearPlayFromPause() {
	m_playFromPause = false;
}

bool					GStreamerWrapper::isNewLoop(){
	return m_newLoop;
}
void					GStreamerWrapper::clearNewLoop(){
	 m_newLoop = false;
}

void * GStreamerWrapper::getElementByName(const std::string& gst_element_name){
	if(m_GstPipeline){
		return gst_bin_get_by_name(GST_BIN(m_GstPipeline), gst_element_name.c_str());
	}

	return NULL;
}


static void deinterleave_new_pad(GstElement* element, GstPad* pad, gpointer data){
	gchar * padName = gst_pad_get_name(pad);
	std::cout << "New pad created! " << padName << std::endl;
	g_free(padName);
}

bool GStreamerWrapper::open(const std::string& strFilename, const bool bGenerateVideoBuffer, const bool bGenerateAudioBuffer, const int colorSpace,
							const int videoWidth, const int videoHeight, const bool hasAudioTrack, const double secondsDuration){
	if(!m_ValidInstall){
		return false;
	}

	resetProperties();

	if(secondsDuration > -1){
		m_iDurationInNs = gint64(secondsDuration * 1000000 * 1000);
	}

	if( m_bFileIsOpen )	{
		stop();
		close();
	}

	parseFilename(strFilename);

	if(colorSpace == kColorSpaceI420){
		enforceModEightWidth(videoWidth, videoHeight);
	} else {
		enforceModFourWidth(videoWidth, videoHeight);
	}

	// PIPELINE
	// Init main pipeline --> playbin
	m_GstPipeline = gst_element_factory_make( "playbin", "pipeline" );

	// Open Uri
	g_object_set(m_GstPipeline, "uri", m_strFilename.c_str(), NULL);

	// VIDEO SINK
	// Extract and Config Video Sink
	if ( bGenerateVideoBuffer ){
		// Create the video appsink and configure it
		m_GstVideoSink = gst_element_factory_make("appsink", "videosink");

		//gst_app_sink_set_max_buffers( GST_APP_SINK( m_GstVideoSink ), 2 );
		//gst_app_sink_set_drop( GST_APP_SINK( m_GstVideoSink ), true );
		gst_base_sink_set_qos_enabled(GST_BASE_SINK(m_GstVideoSink), true);
		gst_base_sink_set_max_lateness(GST_BASE_SINK(m_GstVideoSink), -1); // 1000000000 = 1 second, 40000000 = 40 ms, 20000000 = 20 ms

		// Set some fix caps for the video sink
		GstCaps* caps;

		if(colorSpace == kColorSpaceTransparent){
			m_cVideoBufferSize = 4 * m_iWidth * m_iHeight;
			caps = gst_caps_new_simple("video/x-raw",
									   "format", G_TYPE_STRING, "BGRA",
									   "width", G_TYPE_INT, m_iWidth,
									   "height", G_TYPE_INT, m_iHeight,
									   NULL);

		} else if(colorSpace == kColorSpaceSolid){
			m_cVideoBufferSize = 3 * m_iWidth * m_iHeight;

			caps = gst_caps_new_simple("video/x-raw",
									   "format", G_TYPE_STRING, "BGR",
									   "width", G_TYPE_INT, m_iWidth,
									   "height", G_TYPE_INT, m_iHeight,
									   NULL);

		} else if(colorSpace == kColorSpaceI420){
			// 1.5 * w * h, for I420 color space, which has a full-size luma channel, and 1/4 size U and V color channels
			m_cVideoBufferSize = (int)(1.5 * m_iWidth * m_iHeight);


			caps = gst_caps_new_simple("video/x-raw",
									   "format", G_TYPE_STRING, "I420",
									   "width", G_TYPE_INT, m_iWidth,
									   "height", G_TYPE_INT, m_iHeight,
									   NULL);
		}

		m_cVideoBuffer = new unsigned char[m_cVideoBufferSize];

		gst_app_sink_set_caps( GST_APP_SINK( m_GstVideoSink ), caps );
		gst_caps_unref( caps );

		// Set the configured video appsink to the main pipeline
		g_object_set(m_GstPipeline, "video-sink", m_GstVideoSink, (void*)NULL);

		// Tell the video appsink that it should not emit signals as the buffer retrieving is handled via callback methods
		g_object_set( m_GstVideoSink, "emit-signals", false, "sync", true, "async", true, (void*)NULL );

		// Set Video Sink callback methods
		m_GstVideoSinkCallbacks.eos = &GStreamerWrapper::onEosFromVideoSource;
		m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromVideoSource;
		m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromVideoSource;

		if(m_AudioBufferWanted){
			m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromAudioSource;
			m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromAudioSource;
		}

		gst_app_sink_set_callbacks( GST_APP_SINK( m_GstVideoSink ), &m_GstVideoSinkCallbacks, this, NULL );

	} else {

		if(m_iHeight > 0 && m_iWidth > 0){
			if(m_VerboseLogging){
				DS_LOG_INFO("Video size not detected or video buffer not set to be created. Ignoring video output.");
			}
			GstElement* videoSink = gst_element_factory_make("faksesink", NULL);
			g_object_set(m_GstPipeline, "video-sink", videoSink, NULL);
		}

	}

	// AUDIO SINK
	// Extract and config Audio Sink
#ifdef _WIN32
	if(!m_AudioDevices.empty()){
		GstElement* bin = gst_bin_new("converter_sink_bin");
		GstElement* mainConvert = gst_element_factory_make("audioconvert", NULL);
		GstElement* mainResample = gst_element_factory_make("audioresample", NULL);
		GstElement* mainVolume = gst_element_factory_make("volume", "mainvolume");
		GstElement* mainTee = gst_element_factory_make("tee", NULL);

		gst_bin_add_many(GST_BIN(bin), mainConvert, mainResample, mainVolume, mainTee, NULL);
		gboolean link_ok = gst_element_link_many(mainConvert, mainResample, mainVolume, mainTee, NULL);
	//	link_ok = gst_element_link_filtered(mainConvert, mainResample, caps);

		for(int i = 0; i < m_AudioDevices.size(); i++){

			// auto-detects guid's based on output name
			m_AudioDevices[i].initialize();

			if(m_AudioDevices[i].mDeviceGuid.empty()) continue;


			/* This is a start of how to implement 5.1, 7.1, or n-channel audio support.
				Basically, you have to deinterleave the channels from the source. 
				Deinterleave converts multichannel audio into single mono tracks, which can then be interleaved back into stereo outputs
				Since the deinterleave element only has dynamic pads, you'll need to add a signal callback and 
				connect the channels back up to directsoundsink outputs. 
				You'll need to know the number of channels for each video ahead of time and how to map that to output devices.
				Chances are that this will need to be implemented separately from this class. But the code is here for reference.
			GstElement* deinterleave = gst_element_factory_make("deinterleave", NULL);

			GstElement* queueLeft = gst_element_factory_make("queue", NULL);
			GstElement* queueRight = gst_element_factory_make("queue", NULL);

			GstElement* interleave = gst_element_factory_make("interleave", NULL);
			GstElement* thisQueue = gst_element_factory_make("queue", NULL);
			GstElement* thisConvert = gst_element_factory_make("audioconvert", NULL);
			GstElement* thisSink = gst_element_factory_make("directsoundsink", NULL);

			gst_bin_add_many(GST_BIN(bin), thisQueue, deinterleave, NULL); // , queueLeft, queueRight, interleave, thisQueue, thisConvert, thisSink, NULL);

			
			GValueArray* va = g_value_array_new(2);
			GValue v = { 0, };
			g_value_set_enum(&v, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT);
			g_value_array_append(va, &v);
			g_value_reset(&v);
			g_value_set_enum(&v, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT);
			g_value_array_append(va, &v);
			g_value_reset(&v);
			g_object_set(interleave, "channel-positions", va, NULL);
			g_value_array_free(va);

			g_object_set(thisSink, "device", m_AudioDevices[i].mDeviceGuid.c_str(), NULL);


			g_signal_connect(deinterleave, "pad-added", G_CALLBACK(deinterleave_new_pad), NULL);

			GstPadTemplate* tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mainTee), "src_%u");
			GstPad* teePad = gst_element_request_pad(mainTee, tee_src_pad_template, NULL, NULL);
			GstPad* queueSinkPad = gst_element_get_static_pad(thisQueue, "sink");
			link_ok = gst_pad_link(teePad, queueSinkPad);
			link_ok = gst_element_link_many(thisQueue, deinterleave, NULL);

			*/

			m_AudioDevices[i].mVolumeName = "volume" + std::to_string(i);
			m_AudioDevices[i].mPanoramaName = "panorama" + std::to_string(i);

			GstElement* thisQueue = gst_element_factory_make("queue", NULL);
			GstElement* thisConvert = gst_element_factory_make("audioconvert", NULL);
			GstElement* thisPanorama = gst_element_factory_make("audiopanorama", m_AudioDevices[i].mPanoramaName.c_str());
			GstElement* thisVolume = gst_element_factory_make("volume", m_AudioDevices[i].mVolumeName.c_str());
			GstElement* thisSink = gst_element_factory_make("directsoundsink", NULL);
			g_object_set(thisSink, "device", m_AudioDevices[i].mDeviceGuid.c_str(), NULL);// , "volume", m_AudioDevices[i].mVolume, NULL);
			gst_bin_add_many(GST_BIN(bin), thisQueue, thisConvert, thisPanorama, thisVolume, thisSink, NULL);


			link_ok = gst_element_link_many(thisQueue, thisConvert, thisPanorama, thisVolume, thisSink, NULL);
			GstPadTemplate* tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mainTee), "src_%u");
			GstPad* teePad = gst_element_request_pad(mainTee, tee_src_pad_template, NULL, NULL);
			GstPad* queue_audio_pad1 = gst_element_get_static_pad(thisQueue, "sink");
			link_ok = gst_pad_link(teePad, queue_audio_pad1);
		}
		
		GstPad* pad = gst_element_get_static_pad(mainConvert, "sink");
		GstPad* ghost_pad = gst_ghost_pad_new("sink", pad);
		GstCaps* caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, (int)m_AudioDevices.size() * 2, "format", G_TYPE_STRING, "S16LE");
		gst_pad_set_caps(pad, caps);
		gst_caps_unref(caps);
		gst_pad_set_active(ghost_pad, TRUE);
		gst_element_add_pad(bin, ghost_pad);

		g_object_set(m_GstPipeline, "audio-sink", bin, NULL);
		gst_object_unref(pad);

	} else 
#endif

	if (bGenerateAudioBuffer){
		if (m_CustomPipeline){
			setCustomFunction();
		}
		else if(hasAudioTrack){
			//Add components for sub-pipeline

			m_GstConverter = gst_element_factory_make("audioconvert", "convert");
			m_GstPanorama = gst_element_factory_make("audiopanorama", "pan");
			m_GstAudioSink	= gst_element_factory_make("autoaudiosink", NULL);
			g_object_set(m_GstAudioSink, "sync", true, (void*)NULL);

			GstElement* bin = gst_bin_new("converter_sink_bin");

			//Add and Link sub-pipeline components: 'Audio Converter' ---> 'Panorama' ---> 'Audio Sink'
			gst_bin_add_many(GST_BIN(bin), m_GstConverter, m_GstPanorama, m_GstAudioSink, NULL);
			gboolean link_ok = gst_element_link_many(m_GstConverter, m_GstPanorama, m_GstAudioSink, NULL);

			//Set pan value
			g_object_set(m_GstPanorama, "panorama", m_fPan, NULL);

			//Setup pads to connect main 'playbin' pipeline:   'playbin' ---> 'Audio Converter' ---> 'panorama' ---> 'Audio sink'
			GstPad *pad = gst_element_get_static_pad(m_GstConverter, "sink");
			GstPad *ghost_pad = gst_ghost_pad_new("sink", pad);
			gst_pad_set_active(ghost_pad, TRUE);
			gst_element_add_pad(bin, ghost_pad);

			//Set 'bin' pipeline as audio sink
			g_object_set(m_GstPipeline, "audio-sink", bin, (void*)NULL);

			gst_object_unref(pad);
		}
	} 

	// BUS
	// Set GstBus
	m_GstBus = gst_pipeline_get_bus( GST_PIPELINE( m_GstPipeline ) );

	if ( m_GstPipeline ){
		gst_element_set_state( m_GstPipeline, GST_STATE_READY );
		gst_element_set_state( m_GstPipeline, GST_STATE_PAUSED );

		setTimePositionInMs(0);

		m_CurrentPlayState = OPENED;

		if(m_StartPlaying){
			GstStateChangeReturn retrun = gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);
			if(retrun != GST_STATE_CHANGE_FAILURE){
				m_CurrentPlayState = PLAYING;
			} else {
				std::string errorMessage = "Gstreamer Wrapper Failed to play when loading video! ";
				if(m_ErrorMessageCallback) m_ErrorMessageCallback(errorMessage);
				DS_LOG_WARNING(errorMessage);
			}
		}
	}


	// TODO: Check if everything was initialized correctly
	// May need conditional checks when creating the buffers.

	// A file has been opened
	m_bFileIsOpen = true;

	return true;
}

// This is only for streaming setups that use playbin
static void sourceSetupHandler(void* playbin, GstElement* source, gpointer user_data){
	// this sets the latency of the rtsp source (or other detected source). 
	// It may be required to have this value be configurable in the future, but for now, we're hard-coding
	g_object_set(source, "latency", 100);
}

bool GStreamerWrapper::openStream(const std::string& streamingPipeline, const int videoWidth, const int videoHeight, const uint64_t latencyInNs){
	if(!m_ValidInstall){
		return false;
	}

	resetProperties();

	if( m_bFileIsOpen )	{
		stop();
		close();
	}

	if(streamingPipeline.empty()){
		DS_LOG_WARNING("Streaming pipeline is empty, aborting stream open.");
		return false;
	}
	enforceModFourWidth(videoWidth, videoHeight);
	m_StreamPipeline = streamingPipeline;
	m_iStreamingLatency = latencyInNs;
	m_FullPipeline = true;
	m_LivePipeline = true;
	m_ContentType = VIDEO_AND_AUDIO;

	// If you've constructed a streaming pipeline yourself, there will be '!' characters separating the elements
	// If not, there won't be any !'s. So auto-detect when the URI has been set, and auto-create the pipeline with a playbin element
	if(m_StreamPipeline.find("!") == std::string::npos){

		m_GstPipeline = gst_element_factory_make("playbin", "pipeline");

		// This is obsolete
		//g_signal_connect(m_GstPipeline, "source-setup", G_CALLBACK(sourceSetupHandler), NULL);

		// Open Uri
		g_object_set(m_GstPipeline, "uri", m_StreamPipeline.c_str(), "latency", latencyInNs, NULL);

		// VIDEO SINK
		// Extract and Config Video Sink
		// Create the video appsink and configure it
		m_GstVideoSink = gst_element_factory_make("appsink", "videosink");

		gst_base_sink_set_qos_enabled(GST_BASE_SINK(m_GstVideoSink), true);
		gst_base_sink_set_max_lateness(GST_BASE_SINK(m_GstVideoSink), -1); // 1000000000 = 1 second, 40000000 = 40 ms, 20000000 = 20 ms

		// Set some fix caps for the video sink

		GstCaps* caps = gst_caps_new_simple("video/x-raw",
											"format", G_TYPE_STRING, "I420",
											"width", G_TYPE_INT, m_iWidth,
											"height", G_TYPE_INT, m_iHeight,
											NULL);



		gst_app_sink_set_caps(GST_APP_SINK(m_GstVideoSink), caps);
		gst_caps_unref(caps);

		// Set the configured video appsink to the main pipeline
		g_object_set(m_GstPipeline, "video-sink", m_GstVideoSink, (void*)NULL);

		GstElement* audioSink = gst_element_factory_make("autoaudiosink", NULL);
		g_object_set(audioSink, "sync", true, (void*)NULL);
		g_object_set(m_GstPipeline, "audio-sink", audioSink, NULL);

	} else {

		// VIDEO SINK

		GError* error = nullptr;
		// PIPELINE
		// Init main pipeline --> playbin
		m_GstPipeline = gst_parse_launch(streamingPipeline.c_str(), &error);

		m_GstVideoSink = gst_bin_get_by_name(GST_BIN(m_GstPipeline), "appsink0");
		m_GstVolumeElement = gst_bin_get_by_name(GST_BIN(m_GstPipeline), "volume0");

		if(error){
			DS_LOG_WARNING("Streaming pipeline error: " << error->message);
		}
	}


	if(!m_GstPipeline){
		DS_LOG_WARNING("Streaming pipeline failed to be created. ");
		return false;
	}

	// Set some fix caps for the video sink
	// 1.5 * w * h, for I420 color space, which has a full-size luma channel, and 1/4 size U and V color channels
	m_cVideoBufferSize = (int)(1.5 * m_iWidth * m_iHeight);
	m_cVideoBuffer = new unsigned char[m_cVideoBufferSize];


	// Tell the video appsink that it should not emit signals as the buffer retrieving is handled via callback methods
	g_object_set(m_GstVideoSink, "emit-signals", false, (void*)NULL);

	// Set Video Sink callback methods
	m_GstVideoSinkCallbacks.eos = &GStreamerWrapper::onEosFromVideoSource;
	m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromVideoSource;
	m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromVideoSource;
	gst_app_sink_set_callbacks(GST_APP_SINK(m_GstVideoSink), &m_GstVideoSinkCallbacks, this, NULL);



	// BUS
	// Set GstBus
	m_GstBus = gst_pipeline_get_bus(GST_PIPELINE(m_GstPipeline));

	if(m_GstPipeline){
		// We need to stream the file a little bit in order to be able to retrieve information from it
		gst_element_set_state(m_GstPipeline, GST_STATE_READY);
		gst_element_set_state(m_GstPipeline, GST_STATE_PAUSED);

		// For some reason this is needed in order to gather video information such as size, framerate etc ...
		//GstState state;
		//gst_element_get_state( m_GstPipeline, &state, NULL, 20 * GST_SECOND );
		m_CurrentPlayState = OPENED;

		if(m_StartPlaying){
			gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);
			m_CurrentPlayState = PLAYING;
		}
	}


	// TODO: Check if everything was initialized correctly
	// May need conditional checks when creating the buffers.

	// A file has been opened
	m_bFileIsOpen = true;

	return true;
}

bool GStreamerWrapper::parseLaunch(const std::string& fullPipeline, const int videoWidth, const int videoHeight, const int colorSpace,
								   const std::string& videoSinkName, const std::string& volumeElementName,
								   const double secondsDuration){
	if(!m_ValidInstall){
		return false;
	}

	m_FullPipeline = true;
	m_LivePipeline = false;
	m_StreamPipeline = fullPipeline;

	resetProperties();

	if(secondsDuration > -1){
		m_iDurationInNs = gint64(secondsDuration * 1000000 * 1000);
	}

	if(m_bFileIsOpen)	{
		stop();
		close();
	}

	if(colorSpace == kColorSpaceI420){
		enforceModEightWidth(videoWidth, videoHeight);
	} else {
		enforceModFourWidth(videoWidth, videoHeight);
	}

	// PIPELINE
	m_GstPipeline = gst_parse_launch(fullPipeline.c_str(), NULL);

	if(!m_GstPipeline){
		DS_LOG_WARNING("GStreamer pipeline could not be created! Aborting video playback. Check gstreamer install.");
		return false;
	}

	if(colorSpace == kColorSpaceTransparent){
		m_cVideoBufferSize = 4 * m_iWidth * m_iHeight;
	} else if(colorSpace == kColorSpaceSolid){
		m_cVideoBufferSize = 3 * m_iWidth * m_iHeight;
	} else if(colorSpace == kColorSpaceI420){
		// 1.5 * w * h, for I420 color space, which has a full-size luma channel, and 1/4 size U and V color channels
		m_cVideoBufferSize = (int)(1.5 * m_iWidth * m_iHeight);
	}

	m_cVideoBuffer = new unsigned char[m_cVideoBufferSize];

	m_GstVideoSink = gst_bin_get_by_name(GST_BIN(m_GstPipeline), videoSinkName.c_str());
	m_GstVolumeElement = gst_bin_get_by_name(GST_BIN(m_GstPipeline), volumeElementName.c_str());
	m_GstPanorama = gst_bin_get_by_name(GST_BIN(m_GstPipeline), "panorama0");

	// Set Video Sink callback methods
	m_GstVideoSinkCallbacks.eos = &GStreamerWrapper::onEosFromVideoSource;
	m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromVideoSource;
	m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromVideoSource;

	if(m_AudioBufferWanted){
		m_GstVideoSinkCallbacks.new_preroll = &GStreamerWrapper::onNewPrerollFromAudioSource;
		m_GstVideoSinkCallbacks.new_sample = &GStreamerWrapper::onNewBufferFromAudioSource;
	}

	g_object_set(m_GstVideoSink, "emit-signals", false, "sync", true, "async", true, (void*)NULL);
	gst_app_sink_set_callbacks(GST_APP_SINK(m_GstVideoSink), &m_GstVideoSinkCallbacks, this, NULL);

	m_GstBus = gst_pipeline_get_bus(GST_PIPELINE(m_GstPipeline));
	gst_element_set_state(m_GstPipeline, GST_STATE_READY);
	gst_element_set_state(m_GstPipeline, GST_STATE_PAUSED);

	setTimePositionInMs(0);

	m_bFileIsOpen = true;
	m_CurrentPlayState = OPENED;
	m_ContentType = VIDEO_AND_AUDIO;

	if(m_StartPlaying){
		GstStateChangeReturn retrun = gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);
		if(retrun != GST_STATE_CHANGE_FAILURE){
			m_CurrentPlayState = PLAYING;

		}
	}
	return true;
}

void GStreamerWrapper::setStreamingLatency(uint64_t latency_ns){
	m_iStreamingLatency = latency_ns;
	if(!m_LivePipeline || !m_GstPipeline){
		return;
	}

	g_object_set(m_GstPipeline, "latency", m_iStreamingLatency, NULL);

}

void GStreamerWrapper::setServerNetClock(const bool isServer, const std::string& addr, const int port, std::uint64_t& netClock, std::uint64_t& clockBaseTime){
	mSyncedMode = true;
	mServer = true;
	DS_LOG_INFO("Setting IP Address to: " << addr.c_str() << " Port: " << port);
	if (mClockProvider){
		gst_object_unref(mClockProvider);
		mClockProvider = nullptr;
	}

	// apply pipeline clock to itself, to make sure we're on charge
	auto clock = gst_system_clock_obtain();
	m_NetClock = clock;
	gst_pipeline_use_clock(GST_PIPELINE(m_GstPipeline), clock);
	mClockProvider = gst_net_time_provider_new(clock, addr.c_str(), port);
	gst_clock_set_timeout(m_NetClock, 10);

	if (!mClockProvider)		{
		DS_LOG_WARNING("Could not instantiate the GST server network clock.");
	}

	// get the time for clients to start based on...

	std::uint64_t newTime = getNetClockTime();
	clockBaseTime = newTime;


	//When setting up the server clock, we initialize the base clock to it.
	m_BaseTime = clockBaseTime;
	netClock = clockBaseTime;
	m_CurrentTime = clockBaseTime;
	// reset my clock so it won't advance detached from net
	gst_element_set_start_time(m_GstPipeline, GST_CLOCK_TIME_NONE);

	// set the net clock to start ticking from our base time
	setPipelineBaseTime(netClock);
}

void GStreamerWrapper::setClientNetClock(const bool isServer, const std::string& addr, const int port, std::uint64_t& netClock, std::uint64_t& baseTime){
	mSyncedMode = true;
	mServer = false;
	DS_LOG_INFO("Setting IP Address to: " << addr.c_str() << " Port: " << port);

	// reset my clock so it won't advance detached from net
	gst_element_set_start_time(m_GstPipeline, GST_CLOCK_TIME_NONE);

	//Create client clock synchronized with net clock.  We want it synchronized exactly, so we provide an initial time of '0'.
	m_NetClock = gst_net_client_clock_new("net_clock", addr.c_str(), port, 0);
	gst_clock_set_timeout(m_NetClock, 10);


	// apply the net clock
	gst_pipeline_use_clock(GST_PIPELINE(m_GstPipeline), m_NetClock);
	
	setPipelineBaseTime(baseTime);
	if (!m_NetClock)
	{
		DS_LOG_WARNING("Could not instantiate the GST client network clock.");
	}
}

void GStreamerWrapper::close(){
	// Collect information under locked mutex
	bool hasVideoSink = false;
	bool hasPipeline = false;
	bool hasGstBus = false;
	bool hasClockProvider = false;
	{
		std::lock_guard<std::mutex> lock(m_VideoMutex);
		if (m_GstVideoSink != nullptr)
			hasVideoSink = true;
		if (m_GstPipeline != nullptr)
			hasPipeline = true;
		if (m_GstBus != nullptr)
			hasGstBus = true;
		if (mClockProvider != nullptr)
			hasClockProvider = true;
	}

	// Clear callbacks before closing
	if(hasVideoSink) {
		GstAppSinkCallbacks emptyCallbacks = {NULL, NULL, NULL};
		gst_app_sink_set_callbacks(GST_APP_SINK(m_GstVideoSink), &emptyCallbacks, NULL, NULL);
	}

	//GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(m_GstPipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");

	// Lock mutex while modifying member variables
	{
		std::lock_guard<std::mutex> lock(m_VideoMutex);
		m_bFileIsOpen = false;
		m_CurrentPlayState = NOT_INITIALIZED;
		m_ContentType = NONE;
	}

	// Don't hold mutex while stopping, because a
	// callback could still be running...
	stop();

	if ( hasPipeline ) {
		//gst_element_set_state( m_GstPipeline, GST_STATE_NULL );
		gst_object_unref(m_GstPipeline);
	}

	if ( hasGstBus )
		gst_object_unref(m_GstBus);

	if( hasClockProvider )
		gst_object_unref(mClockProvider);

	// Cleanup member variables under mutex
	{
		std::lock_guard<std::mutex> lock(m_VideoMutex);

		m_GstPipeline = NULL;
		m_GstVideoSink = NULL;
		m_GstAudioSink = NULL;
		m_GstPanorama = NULL;
		m_GstBus = NULL;

		delete [] m_cVideoBuffer;
		m_cVideoBuffer = NULL;

		delete [] m_cAudioBuffer;
		m_cAudioBuffer = NULL;
	}
}

void GStreamerWrapper::update(){
	handleGStMessage();

	if(m_StreamNeedsRestart){
		m_StreamRestartCount++;
		// 2 seconds at 60fps, should prolly move to a timed situation
		if(m_StreamRestartCount > 120){
			m_StreamNeedsRestart = false;
			openStream(m_StreamPipeline, m_iWidth, m_iHeight, m_iStreamingLatency);
		}
	}
}

uint64_t GStreamerWrapper::getPipelineTime(){
	GstClock* clock = gst_pipeline_get_clock(GST_PIPELINE(m_GstPipeline));
	uint64_t now = gst_clock_get_time(clock);

	return now;
}

void GStreamerWrapper::setPipelineBaseTime(uint64_t base_time){
	gst_element_set_base_time(m_GstPipeline, base_time);
	m_BaseTime = base_time;
}


void GStreamerWrapper::play(){
	if (m_GstPipeline){

		// Only seek on play in net mode
		if(mSyncedMode){
			GstStateChangeReturn gscr = gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);

			if(gscr == GST_STATE_CHANGE_FAILURE){
				DS_LOG_WARNING("Gst State change failure");
			}

			if(mServer){
				if(getState() == PAUSED){
					std::cout << "Playing from pause" << std::endl;
					m_playFromPause = true;

					uint64_t baseTime = getPipelineTime();
					setPipelineBaseTime(baseTime);
					uint64_t latency = 200000000;
					setSeekTime(m_SeekTime + latency);

				}

			}

			setTimePositionInNs(m_SeekTime);
		} else {
			if(m_CurrentPlayState != PLAYING){
				GstStateChangeReturn gscr = gst_element_set_state(m_GstPipeline, GST_STATE_PLAYING);

				if(gscr == GST_STATE_CHANGE_FAILURE){
					DS_LOG_WARNING("Gst State change failure");
				}
			}
		}

		m_CurrentPlayState = PLAYING;
	}
}

void GStreamerWrapper::stop(){
	bool hasPipeline = false;
	{
		std::lock_guard<std::mutex> lock(m_VideoMutex);
		hasPipeline = (m_GstPipeline != nullptr);
	}

	if ( hasPipeline ){
		// Stop in this context now means a full clearing of the buffers in gstreamer
		gst_element_set_state( m_GstPipeline, GST_STATE_NULL );

		std::lock_guard<std::mutex> lock(m_VideoMutex);
		m_CurrentPlayState = STOPPED;
	}
}

void GStreamerWrapper::pause(){

	if ( m_GstPipeline ){
		GstStateChangeReturn gscr = gst_element_set_state(m_GstPipeline, GST_STATE_PAUSED);

		if(mSyncedMode){
			if(mServer) {
				m_SeekTime = getCurrentTimeInNs();
			}

			setTimePositionInNs(m_SeekTime);
		}

		if(gscr == GST_STATE_CHANGE_FAILURE){
			DS_LOG_WARNING("GStreamerWrapper: State change failure trying to pause");
		} else {
			m_CurrentPlayState = PAUSED;
		}

	} else {
		DS_LOG_WARNING("GStreamerWrapper: Pipeline doesn't exist when trying to pause video.");
	}
}

void GStreamerWrapper::setCurrentVideoStream( int iCurrentVideoStream ){
	if(m_LivePipeline) return;

	if ( m_iCurrentVideoStream != iCurrentVideoStream )	{
		if ( iCurrentVideoStream >= 0 && iCurrentVideoStream < m_iNumVideoStreams )		{
			m_iCurrentVideoStream = iCurrentVideoStream;

			g_object_set( m_GstPipeline, "current-video", m_iCurrentVideoStream, NULL );
		}
	}
}

void GStreamerWrapper::setCurrentAudioStream(int iCurrentAudioStream){
	if(m_LivePipeline) return;

	if ( m_iCurrentAudioStream != iCurrentAudioStream )	{
		if ( iCurrentAudioStream >= 0 && iCurrentAudioStream < m_iNumAudioStreams )		{
			m_iCurrentAudioStream = iCurrentAudioStream;

			g_object_set( m_GstPipeline, "current-audio", m_iCurrentAudioStream, NULL );
		}
	}
}

void GStreamerWrapper::setSpeed(float fSpeed){
	if(m_LivePipeline) return;

	if( fSpeed != m_fSpeed )
	{
		m_fSpeed = fSpeed;
		if ( m_fSpeed < 0.0f )
			m_fSpeed = 0.0f;

		changeSpeedAndDirection( m_fSpeed, m_PlayDirection );
	}
}

void GStreamerWrapper::setDirection(PlayDirection direction){
	if(m_LivePipeline) return;

	if ( m_PlayDirection != direction )	{
		m_PlayDirection = direction;
		changeSpeedAndDirection( m_fSpeed, m_PlayDirection );
	}
}

void GStreamerWrapper::setLoopMode(LoopMode loopMode){
	if(m_LivePipeline) return;

	m_LoopMode = loopMode;
}

void GStreamerWrapper::setFramePosition(gint64 iTargetFrameNumber){
	m_iCurrentFrameNumber = iTargetFrameNumber;

	setPosition( (float)m_iCurrentFrameNumber / (float)m_iNumberOfFrames );
}

void GStreamerWrapper::setTimePositionInMs( double dTargetTimeInMs ){
	m_dCurrentTimeInMs = dTargetTimeInMs;
	seekFrame( (gint64)(m_dCurrentTimeInMs * 1000000) );
}

void GStreamerWrapper::setTimePositionInNs( gint64 iTargetTimeInNs ){
	m_iCurrentTimeInNs = iTargetTimeInNs;
	seekFrame( m_iCurrentTimeInNs );
}


void GStreamerWrapper::setPosition(double fPos){
	if( fPos < 0.0 )
		fPos = 0.0;
	else if( fPos > 1.0 )
		fPos = 1.0;


		m_dCurrentTimeInMs = fPos * m_dCurrentTimeInMs;
		m_iCurrentFrameNumber = (gint64)(fPos * m_iNumberOfFrames);
		m_iCurrentTimeInNs = (gint64)(fPos * m_iDurationInNs);

		seekFrame(m_iCurrentTimeInNs);

}


bool GStreamerWrapper::hasVideo(){
	return m_ContentType == VIDEO_AND_AUDIO || m_ContentType == VIDEO;
}

bool GStreamerWrapper::hasAudio(){
	return m_ContentType == VIDEO_AND_AUDIO || m_ContentType == AUDIO;
}

std::string GStreamerWrapper::getFileName(){
	return m_strFilename;
}

unsigned char* GStreamerWrapper::getVideo(){
	std::lock_guard<std::mutex> lock(m_VideoMutex);
	m_bIsNewVideoFrame = false;
	return m_cVideoBuffer;
}

int GStreamerWrapper::getCurrentVideoStream(){
	return m_iCurrentVideoStream;
}

int GStreamerWrapper::getCurrentAudioStream(){
	return m_iCurrentAudioStream;
}

int GStreamerWrapper::getNumberOfVideoStreams(){
	return m_iNumVideoStreams;
}

int GStreamerWrapper::getNumberOfAudioStreams(){
	return m_iNumAudioStreams;
}

unsigned int GStreamerWrapper::getWidth(){
	return m_iWidth;
}

unsigned int GStreamerWrapper::getHeight(){
	return m_iHeight;
}

bool GStreamerWrapper::isNewVideoFrame(){
	return m_bIsNewVideoFrame;
}

float GStreamerWrapper::getFps(){
	return m_fFps;
}

float GStreamerWrapper::getSpeed(){
	return m_fSpeed;
}

double GStreamerWrapper::getPosition() const {
	return static_cast<double>(getCurrentTimeInNs()) / static_cast<double>(m_iDurationInNs);
}

gint64 GStreamerWrapper::getCurrentFrameNumber(){
	m_iCurrentFrameNumber = (gint64)(floor( (double)getCurrentTimeInMs() / 1000.0 * m_fFps ));
	return m_iCurrentFrameNumber;
}

gint64 GStreamerWrapper::getNumberOfFrames(){
	return m_iNumberOfFrames;
}

double GStreamerWrapper::getCurrentTimeInMs(){
	return (double)(getCurrentTimeInNs() / 1000000);
}

double GStreamerWrapper::getDurationInMs() const {
	return m_dDurationInMs;
}

gint64 GStreamerWrapper::getCurrentTimeInNs() const {
	GstFormat gstFormat = GST_FORMAT_TIME;
	gst_element_query_position( GST_ELEMENT( m_GstPipeline ), gstFormat, &m_iCurrentTimeInNs );
	return m_iCurrentTimeInNs;
}

gint64 GStreamerWrapper::getDurationInNs() {
	return m_iDurationInNs;
}

PlayState GStreamerWrapper::getState() const {
	return m_CurrentPlayState;
}

PlayDirection GStreamerWrapper::getDirection(){
	return m_PlayDirection;
}

LoopMode GStreamerWrapper::getLoopMode(){
	return m_LoopMode;
}

ContentType GStreamerWrapper::getContentType(){
	return m_ContentType;
}

void GStreamerWrapper::setVolume(float fVolume){
	if(m_fVolume != fVolume)
	{
		m_fVolume = fVolume;
		if(m_fVolume < 0.0f)
			m_fVolume = 0.0f;
		else if(m_fVolume > 1.0f)
			m_fVolume = 1.0f;

		if(m_GstPipeline) g_object_set(m_GstPipeline, "volume", m_fVolume, NULL);

		if(!m_AudioDevices.empty()){
			auto mainVolumeElement = gst_bin_get_by_name(GST_BIN(m_GstPipeline), "mainvolume");
			if(mainVolumeElement){
				g_object_set(mainVolumeElement, "volume", m_fVolume, NULL);
			}
		}
		
	}
}

void GStreamerWrapper::setPan(float fPan){
	if(m_fPan != fPan && m_GstPanorama)
	{
		m_fPan = fPan;
		if(m_fPan < -1.0f)
			m_fPan = -1.0f;
		else if(m_fPan > 1.0f)
			m_fPan = 1.0f;

		g_object_set(m_GstPanorama, "panorama", m_fPan, NULL);
	}
}

unsigned char* GStreamerWrapper::getAudio(){
	std::lock_guard<decltype(m_VideoLock)> lock(m_VideoLock);
	return m_cAudioBuffer;
}

bool GStreamerWrapper::getIsAudioSigned(){
	return m_bIsAudioSigned;
}

int	GStreamerWrapper::getNumOfAudioChannels(){
	return m_iNumAudioChannels;
}

int GStreamerWrapper::getAudioSampleRate(){
	return m_iAudioSampleRate;
}

size_t GStreamerWrapper::getAudioBufferSize(){
	return m_iAudioBufferSize;
}

int GStreamerWrapper::getAudioDecodeBufferSize(){
	return m_iAudioDecodeBufferSize;
}

int GStreamerWrapper::getAudioWidth(){
	return m_iAudioWidth;
}

float GStreamerWrapper::getCurrentVolume(){
	return m_fVolume;
}

gint64 GStreamerWrapper::getBaseTime(){
	return m_BaseTime;
}


void GStreamerWrapper::setSeekTime(uint64_t seek_time){
	m_SeekTime = seek_time;
}


gint64 GStreamerWrapper::getSeekTime(){
	return m_SeekTime;
}



gint64 GStreamerWrapper::getStartTime(){
	return m_StartTime;
}

void GStreamerWrapper::setStartTime(uint64_t start_time){
	m_StartTime = start_time;
}

bool GStreamerWrapper::seekFrame( gint64 iTargetTimeInNs ){
	if(m_iDurationInNs < 0 || m_CurrentGstState == STATE_NULL) {
		m_PendingSeekTime = iTargetTimeInNs;
		m_PendingSeek = true;
		return false;
	}

	GstFormat gstFormat = GST_FORMAT_TIME;

	// The flags determine how the seek behaves, in this case we simply want to jump to certain part in stream
	// while keeping the pre-set speed and play direction
	GstSeekFlags gstSeekFlags = (GstSeekFlags)(GST_SEEK_FLAG_FLUSH);


	gboolean bIsSeekSuccessful = false;

	if (mSyncedMode && mServer) {
		uint64_t baseTime = getPipelineTime();
		setPipelineBaseTime(baseTime);
		m_SeekTime = iTargetTimeInNs;
	}

	if (m_PlayDirection == FORWARD){
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			m_fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			iTargetTimeInNs,
			GST_SEEK_TYPE_NONE,
			GST_CLOCK_TIME_NONE );
	} else if ( m_PlayDirection == BACKWARD )	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			-m_fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			0,
			GST_SEEK_TYPE_SET,
			iTargetTimeInNs );
	}

	if(!(bIsSeekSuccessful == 0)){
		m_PendingSeek = false;
	}
	
	return bIsSeekSuccessful != 0;
}

bool GStreamerWrapper::changeSpeedAndDirection( float fSpeed, PlayDirection direction ){
	GstFormat gstFormat = GST_FORMAT_TIME;
	// The flags determine how the seek behaves, in this case we stay at the current position in the stream but simply
	// want to change either speed, play direction or both
	GstSeekFlags gstSeekFlags = (GstSeekFlags)( GST_SEEK_FLAG_SKIP | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH );

	gboolean bIsSeekSuccessful = false;

	if ( direction == FORWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			getCurrentTimeInNs(),
			GST_SEEK_TYPE_SET,
			m_iDurationInNs );
	}
	else if ( direction == BACKWARD )
	{
		bIsSeekSuccessful = gst_element_seek( GST_ELEMENT( m_GstPipeline ),
			-fSpeed,
			gstFormat,
			gstSeekFlags,
			GST_SEEK_TYPE_SET,
			0,
			GST_SEEK_TYPE_SET,
			getCurrentTimeInNs() );
	}

	return bIsSeekSuccessful != 0;
}

void GStreamerWrapper::retrieveVideoInfo(){
	if(m_LivePipeline || m_FullPipeline){
		return; // streaming sets it's open values
	}
	////////////////////////////////////////////////////////////////////////// Media Duration
	// Nanoseconds
	GstFormat gstFormat = GST_FORMAT_TIME;
	gst_element_query_duration( GST_ELEMENT( m_GstPipeline ), gstFormat, &m_iDurationInNs );

	// Milliseconds
	m_dDurationInMs = (double)(GST_TIME_AS_MSECONDS( m_iDurationInNs ));

	////////////////////////////////////////////////////////////////////////// Stream Info
	// Number of Video Streams
	g_object_get( m_GstPipeline, "n-video", &m_iNumVideoStreams, NULL );

	// Number of Audio Streams
	g_object_get( m_GstPipeline, "n-audio", &m_iNumAudioStreams, NULL );

	// Set Content Type according to the number of available Video and Audio streams
	if ( m_iNumVideoStreams > 0 && m_iNumAudioStreams > 0 ){
		m_ContentType = VIDEO_AND_AUDIO;
	} else if ( m_iNumVideoStreams > 0 ){
		m_ContentType = VIDEO;
	} else if ( m_iNumAudioStreams > 0 ){
		m_ContentType = AUDIO;
	}

	if(m_VerboseLogging){
		DS_LOG_INFO("Got video info, duration=" << m_iDurationInNs << " Number of video streams: " << m_iNumVideoStreams << " audio: " << m_iNumAudioStreams);
	}
}

static void print_one_tag(const GstTagList * list, const gchar * tag, gpointer user_data){
	int i, num;

	num = gst_tag_list_get_tag_size(list, tag);

	for(i = 0; i < num; ++i) {
		const GValue *val;

		/* Note: when looking for specific tags, use the gst_tag_list_get_xyz() API,
		* we only use the GValue approach here because it is more generic */
		val = gst_tag_list_get_value_index(list, tag, i);

		if(G_VALUE_HOLDS_STRING(val)) {
			std::cout << tag << " " << g_value_get_string(val) << std::endl;
		} 
		else if(G_VALUE_HOLDS_UINT(val)) {
			std::cout << tag << " " << g_value_get_uint(val) << std::endl;
		} else if(G_VALUE_HOLDS_DOUBLE(val)) {
			std::cout << tag << " " << g_value_get_double(val) << std::endl;
		} else if(G_VALUE_HOLDS_BOOLEAN(val)) {
			std::cout << tag << " " << g_value_get_boolean(val) << std::endl;
		} 
	}
}

void GStreamerWrapper::setAudioDeviceVolume(ds::GstAudioDevice& theDevice){
	for (auto it : m_AudioDevices){
		if(it.mDeviceName == theDevice.mDeviceName){
			auto volumeElement = gst_bin_get_by_name(GST_BIN(m_GstPipeline), it.mVolumeName.c_str());
			if(volumeElement){
				g_object_set(volumeElement, "volume", theDevice.mVolume, (void*)NULL);
			}
			break;
		}
	}
}

void GStreamerWrapper::setAudioDevicePan(ds::GstAudioDevice& theDevice){
	for(auto it : m_AudioDevices){
		if(it.mDeviceName == theDevice.mDeviceName){
			auto panElement = gst_bin_get_by_name(GST_BIN(m_GstPipeline), it.mPanoramaName.c_str());
			if(panElement){
				g_object_set(panElement, "panorama", theDevice.mPan, (void*)NULL);
			}
			break;
		}
	}
}

void GStreamerWrapper::handleGStMessage(){
	if ( m_GstBus )	{
		while ( gst_bus_have_pending( m_GstBus ) ){
			m_GstMessage = gst_bus_pop( m_GstBus );

			GstTagList *tags = NULL;
			if ( m_GstMessage )	{

				switch ( GST_MESSAGE_TYPE( m_GstMessage ) )
				{

				case  GST_MESSAGE_QOS:
				{
					guint64 processed;
					guint64 dropped;
					GstFormat format = GST_FORMAT_TIME;
					gst_message_parse_qos_stats(m_GstMessage, &format, &processed, &dropped);
					if(m_VerboseLogging){
						DS_LOG_INFO("Gst QoS message, seconds processed: " << processed << " frames dropped:" << dropped);
					}
				}
				break;

				case GST_MESSAGE_WARNING:
				{
					GError* err;
					gchar* debug;
					gst_message_parse_warning(m_GstMessage, &err, &debug);
					DS_LOG_WARNING("Gst warning: " << err->message << " " << debug);
				}
				break;
				case GST_MESSAGE_INFO:
					{
						GError* err;
						gchar* debug;
						gst_message_parse_info(m_GstMessage, &err, &debug);

						if(m_VerboseLogging){
							DS_LOG_INFO("Gst info: " << err->message << " " << debug);
						}
					}
					break;

				case GST_MESSAGE_ERROR: 
					{
						GError* err;
						gchar* debug;
						gst_message_parse_error(m_GstMessage, &err, &debug);

						std::stringstream errorMessage;
						errorMessage << "Gst error: Embedded video playback halted: module " << gst_element_get_name(GST_MESSAGE_SRC(m_GstMessage)) << " reported " << err->message;

						std::string errorMessageStr = errorMessage.str();
						DS_LOG_ERROR(errorMessageStr);
						if(m_ErrorMessageCallback){
							m_ErrorMessageCallback(errorMessageStr);
						}

						close();

						if(m_FullPipeline && m_AutoRestartStream){
							m_StreamNeedsRestart = true;
							m_StreamRestartCount = 0;
						}

						g_error_free(err);
						g_free(debug);
					}
					break;

				case GST_MESSAGE_STATE_CHANGED:
					{
						//retrieveVideoInfo();
						GstState oldState;
						GstState newState;
						GstState pendingState;
						gst_message_parse_state_changed(m_GstMessage, &oldState, &newState, &pendingState);


						if (newState == GST_STATE_PLAYING) {
							m_CurrentGstState = STATE_PLAYING;
						} else if(newState == GST_STATE_NULL){
							m_CurrentGstState = STATE_NULL;
						} else if(newState == GST_STATE_PAUSED){
							m_CurrentGstState = STATE_PAUSED;
						} else if(newState == GST_STATE_READY){
							m_CurrentGstState = STATE_READY;
						}


					  }

				break;

				case GST_MESSAGE_ASYNC_DONE :{
					// In certain cases the volume is set before the pipeline is
					// constructed, so doesn't get applied.
					g_object_set( m_GstPipeline, "volume", m_fVolume, NULL );
					retrieveVideoInfo();
				
					if ((m_CurrentGstState == STATE_PLAYING || m_CurrentGstState == STATE_PAUSED) && m_PendingSeek){
						seekFrame(m_PendingSeekTime);
					}
				}
				break;

				case GST_MESSAGE_NEW_CLOCK:{
					if(m_VerboseLogging){
						DS_LOG_INFO("Gst New clock");
					}
					// For example on net sync: http://noraisin.net/diary/?p=954
					// also: #include "gst/net/gstnettimeprovider.h"

				}
				break;

				case GST_MESSAGE_SEGMENT_DONE : 
					{
						if(m_StopOnLoopComplete){
							stop();
							m_StopOnLoopComplete = false;
						} else {
							gst_element_seek(GST_ELEMENT(m_GstPipeline),
								m_fSpeed,
								GST_FORMAT_TIME,
								(GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT),
								GST_SEEK_TYPE_SET,
								0,
								GST_SEEK_TYPE_SET,
								m_iDurationInNs);
						}
					}
												break;
				case GST_MESSAGE_EOS:

					switch ( m_LoopMode )
					{

						case NO_LOOP:
							pause();
							if(mVideoCompleteCallback) mVideoCompleteCallback(this);
							break;

						case LOOP:
							if (mServer){
								setSeekTime(0);

								//Update the base time with the value of the pipeline/net clock.
								setPipelineBaseTime(getNetClockTime());

								if (gst_element_seek(GST_ELEMENT(m_GstPipeline),
									m_fSpeed,
									GST_FORMAT_TIME,
									(GstSeekFlags)(GST_SEEK_FLAG_FLUSH),// | GST_SEEK_FLAG_SEGMENT),
									GST_SEEK_TYPE_SET,
									0,
									GST_SEEK_TYPE_SET,
									m_iDurationInNs)){
									play();
									m_newLoop = true;
								}
								else
								{
									DS_LOG_WARNING("Looping: Could not Seek to requested location.");
								}
							}
							break;


						case BIDIRECTIONAL_LOOP:
							DS_LOG_WARNING("Gst bi-directional looping not implemented!");
							//m_PlayDirection = (PlayDirection)-m_PlayDirection;
							//stop();
							//play();
							break;

						default:
							break;
					}
					break;

				case GST_MESSAGE_TAG :
					break;

				default:
					if(m_VerboseLogging){
						DS_LOG_INFO("Gst Message, Type: " << GST_MESSAGE_TYPE_NAME(m_GstMessage));
					}
					break;
				}
			}

			gst_message_unref( m_GstMessage );
		}
	}
}



void GStreamerWrapper::onEosFromVideoSource(GstAppSink* appsink, void* listener){
	// ignore
	// Not handling EOS callbacks creates a crash, but we handle EOS on the bus messages
}

void GStreamerWrapper::setVerboseLogging(const bool verboseOn){
	m_VerboseLogging = verboseOn;
}

GstFlowReturn GStreamerWrapper::onNewPrerollFromVideoSource(GstAppSink* appsink, void* listener){
	GstSample* gstVideoSinkBuffer = gst_app_sink_pull_preroll( GST_APP_SINK( appsink ) );
	( ( GStreamerWrapper *)listener )->newVideoSinkPrerollCallback( gstVideoSinkBuffer );
	gst_sample_unref( gstVideoSinkBuffer );

	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewPrerollFromAudioSource(GstAppSink* appsink, void* listener){
	GstSample* gstAudioSinkBuffer = gst_app_sink_pull_preroll(GST_APP_SINK(appsink));
	((GStreamerWrapper *)listener)->newAudioSinkPrerollCallback(gstAudioSinkBuffer);
	gst_sample_unref(gstAudioSinkBuffer);

	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewBufferFromVideoSource( GstAppSink* appsink, void* listener ){	
	GstSample* gstVideoSinkBuffer = gst_app_sink_pull_sample( GST_APP_SINK( appsink ) );	
	( ( GStreamerWrapper * )listener )->newVideoSinkBufferCallback( gstVideoSinkBuffer );
	gst_sample_unref( gstVideoSinkBuffer );
	return GST_FLOW_OK;
}

GstFlowReturn GStreamerWrapper::onNewBufferFromAudioSource(GstAppSink* appsink, void* listener){

	GstSample* gstAudioSinkBuffer = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
	((GStreamerWrapper *)listener)->newAudioSinkBufferCallback(gstAudioSinkBuffer);
	gst_sample_unref(gstAudioSinkBuffer);

	return GST_FLOW_OK;
}

void GStreamerWrapper::newVideoSinkPrerollCallback(GstSample* videoSinkSample){
	std::lock_guard<std::mutex> lock(m_VideoMutex);

	if(!m_cVideoBuffer) return;

	GstBuffer* buff = gst_sample_get_buffer(videoSinkSample);	


	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);

	size_t videoBufferSize = map.size; 

	// sanity check on buffer size, in case something weird happened.
	// In practice, this can fuck up the look of the video, but it plays and doesn't crash
	if(m_cVideoBufferSize != videoBufferSize){
		delete[] m_cVideoBuffer;
		m_cVideoBufferSize = videoBufferSize;
		m_cVideoBuffer = new unsigned char[m_cVideoBufferSize];
	} 

	memcpy((unsigned char *)m_cVideoBuffer, map.data, videoBufferSize);
	if (!m_PendingSeek) m_bIsNewVideoFrame = true;
	

	gst_buffer_unmap(buff, &map);
}

void GStreamerWrapper::newVideoSinkBufferCallback( GstSample* videoSinkSample ){
	std::lock_guard<std::mutex> lock(m_VideoMutex);

	if(!m_cVideoBuffer) return;

	GstBuffer* buff = gst_sample_get_buffer(videoSinkSample);	
	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);


	size_t videoBufferSize = map.size;

	if(m_cVideoBufferSize != videoBufferSize){
		delete[] m_cVideoBuffer;
		m_cVideoBufferSize = videoBufferSize;
		m_cVideoBuffer = new unsigned char[m_cVideoBufferSize];
	}

	memcpy((unsigned char *)m_cVideoBuffer, map.data, videoBufferSize);
	if(!m_PendingSeek) m_bIsNewVideoFrame = true;
	m_bIsNewVideoFrame = true;
	gst_buffer_unmap(buff, &map);
}

void GStreamerWrapper::newAudioSinkPrerollCallback( GstSample* audioSinkBuffer ){

	std::lock_guard<decltype(m_VideoLock)> lock(m_VideoLock);

	GstBuffer* buff = gst_sample_get_buffer(audioSinkBuffer);
	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);


	size_t audioBufferSize = map.size;

	if(m_iAudioBufferSize != audioBufferSize){
		if(m_cAudioBuffer) delete[] m_cAudioBuffer;
		m_iAudioBufferSize = audioBufferSize;
		m_cAudioBuffer = new unsigned char[m_iAudioBufferSize];
	}

	memcpy((unsigned char *)m_cAudioBuffer, map.data, m_iAudioBufferSize);


	gst_buffer_unmap(buff, &map);
}

void GStreamerWrapper::newAudioSinkBufferCallback( GstSample* audioSinkBuffer ){

	std::lock_guard<decltype(m_VideoLock)> lock(m_VideoLock);

	GstBuffer* buff = gst_sample_get_buffer(audioSinkBuffer);
	GstMapInfo map;
	GstMapFlags flags = GST_MAP_READ;
	gst_buffer_map(buff, &map, flags);
	
	size_t audioBufferSize = map.size;
	
	if(m_iAudioBufferSize != audioBufferSize){
		if(m_cAudioBuffer) delete[] m_cAudioBuffer;
		m_iAudioBufferSize = audioBufferSize;
		m_cAudioBuffer = new unsigned char[m_iAudioBufferSize];
	}
	
	memcpy((unsigned char *)m_cAudioBuffer, map.data, m_iAudioBufferSize);

	gst_buffer_unmap(buff, &map);
}

void GStreamerWrapper::setVideoCompleteCallback( const std::function<void(GStreamerWrapper* video)> &func ){
	mVideoCompleteCallback = func;
}

void GStreamerWrapper::setErrorMessageCallback(const std::function<void(const std::string& errMessage)>& func){
	m_ErrorMessageCallback = func;
}

};
