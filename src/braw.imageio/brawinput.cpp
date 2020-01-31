#include "BlackmagicRawAPI.h"

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/typedesc.h>

#include <boost/filesystem.hpp>
namespace filesystem = boost::filesystem;
// FIXME: use std::filesystem when available

#include <vector>

int COUNTER = 0; // WHY THE FUCK IS IT THREADED, WHY THE FUCK?

OIIO_PLUGIN_NAMESPACE_BEGIN

class CameraCodecCallback;

class BrawInput final : public ImageInput {
	public:
		BrawInput() { init(); }
		virtual ~BrawInput() { close(); }
		virtual const char* format_name(void) const override { return "braw"; }
		virtual bool open(const std::string& name, ImageSpec& newspec) override;
		virtual bool close() override;
		virtual int current_subimage(void) const override
		{
			lock_guard lock(m_mutex);
			return m_subimage;
		}
		virtual bool seek_subimage(int subimage, int miplevel) override;
		virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
				void* data) override;
		void storeImageData(void* imageData);
		bool readFrame(uint64_t frameIndex);

	private:
		CameraCodecCallback* m_callback;
		IBlackmagicRawFactory* m_factory;
		IBlackmagicRaw* m_codec;
		IBlackmagicRawClip* m_clip;
		IBlackmagicRawJob* m_readJob;
		IBlackmagicRawClipEx* m_clipEx;

		long int m_number_channels;
		uint64_t m_frame_count;
		float m_frame_rate;
		uint32_t m_width;
		uint32_t m_height;
		std::vector<unsigned char> m_imageData;
		int m_subimage;
		bool m_read_frame;
		int64_t m_nsubimages;
		uint64_t m_frame_index;

		/// Reset everything to initial state
		void init()
		{
			m_callback        = nullptr;
			m_factory	        = nullptr;
			m_codec           = nullptr;
			m_clip            = nullptr;
			m_readJob         = nullptr;
			m_clipEx          = nullptr;
			m_number_channels = 0;
			m_width           = 0;
			m_height          = 0;
			m_frame_count	    = 0;
			m_frame_rate      = 0;
			m_subimage        = 0;
			m_frame_index     = 0;
			m_read_frame      = false;
			m_imageData.clear();
		}

};

// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

	OIIO_EXPORT ImageInput*
braw_input_imageio_create()
{
	return new BrawInput;
}

OIIO_EXPORT int braw_imageio_version = OIIO_PLUGIN_VERSION;

	OIIO_EXPORT const char*
braw_imageio_library_version()
{
	return nullptr;
}

OIIO_EXPORT const char* braw_input_extensions[] = { "braw", nullptr };

OIIO_PLUGIN_EXPORTS_END

////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS AND CLASSES FOR BLACK MAGIC RAW API
////////////////////////////////////////////////////////////////////////
class CameraCodecCallback : public IBlackmagicRawCallback
{
	public:
		explicit CameraCodecCallback(BrawInput* brawInput) : m_brawInput(brawInput) {}
		virtual ~CameraCodecCallback() {}

		virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
		{
			IBlackmagicRawJob* decodeAndProcessJob = nullptr;

			// if (result == S_OK)
			// VERIFY(frame->SetResourceFormat(s_resourceFormat));

			if (result == S_OK)
				result = frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob);

			if (result == S_OK)
				result = decodeAndProcessJob->Submit();

			if (result != S_OK) {
				if (decodeAndProcessJob)
					decodeAndProcessJob->Release();
			}

			readJob->Release();
		}

		virtual void ProcessComplete(IBlackmagicRawJob* job, HRESULT result, IBlackmagicRawProcessedImage* processedImage)
		{
			void* imageData = nullptr;

			if (result == S_OK)
				result = processedImage->GetResource(&imageData);

			if (result == S_OK)
				m_brawInput->storeImageData(imageData);

			job->Release();
		}

		virtual void DecodeComplete(IBlackmagicRawJob*, HRESULT) {}
		virtual void TrimProgress(IBlackmagicRawJob*, float) {}
		virtual void TrimComplete(IBlackmagicRawJob*, HRESULT) {}
		virtual void SidecarMetadataParseWarning(IBlackmagicRawClip*, const char*, uint32_t, const char*) {}
		virtual void SidecarMetadataParseError(IBlackmagicRawClip*, const char*, uint32_t, const char*) {}
		virtual void PreparePipelineComplete(void*, HRESULT) {}

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*)
		{
			return E_NOTIMPL;
		}

		virtual ULONG STDMETHODCALLTYPE AddRef(void)
		{
			return 0;
		}

		virtual ULONG STDMETHODCALLTYPE Release(void)
		{
			return 0;
		}
	private:
		BrawInput* m_brawInput;
};

//////////////////////////////////////////////////////////////////////////

	bool
BrawInput::open(const std::string& name, ImageSpec& newspec)
{
#ifdef BRAW_LIBRARIES
#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#else
	std::cerr << "BRAW_LIBRARIES are not defined, therefore cannot construct BlackmagicRawFactory\n";
	return false;
#endif

	filesystem::path lib_path(STRINGIFY(BRAW_LIBRARIES));
	// std::cerr << "lib_dir: " << lib_path.parent_path() << "\n";

	// FUCK OFF THREADS
	// COUNTER++;
	// if(COUNTER > 1)
		// return true;

	m_number_channels = 4;  // Black magic has 4 channels?

	HRESULT result = S_OK;

	// Create factory job to read the files
	m_factory = CreateBlackmagicRawFactoryInstanceFromPath(lib_path.parent_path().c_str());
	if (m_factory == nullptr) {
		std::cerr << "Failed to create IBlackmagicRawFactory!" << std::endl;
		return false;
	}

	result = m_factory->CreateCodec(&m_codec);
	if (result != S_OK) {
		std::cerr << "Failed to create IBlackmagicRaw!" << std::endl;
		return false;
	}

	result = m_codec->OpenClip(name.c_str(), &m_clip);
	if (result != S_OK) {
		std::cerr << "Failed to open IBlackmagicRawClip!" << std::endl;
		return false;
	}

	// Get basic data from the clip
	m_clip->GetWidth(&m_width);
	m_clip->GetHeight(&m_height);
	m_clip->GetFrameCount(&m_frame_count);
	m_clip->GetFrameRate(&m_frame_rate);

	// Set callback class
	m_callback = new CameraCodecCallback(this);
	result = m_codec->SetCallback(m_callback);
	if (result != S_OK) {
		std::cerr << "Failed to set IBlackmagicRawCallback!" << std::endl;
		return false;
	}

	m_spec = ImageSpec((int)m_width, (int)m_height, m_number_channels, TypeDesc::UINT8);
	newspec = m_spec;
	newspec.alpha_channel = 4;
	newspec.attribute("oiio:ColorSpace", "sRGB");
	newspec.attribute("oiio:Movie", true);

	m_nsubimages = m_frame_count;

	// std::cerr << "Width: " << m_width
		// << "\nHeight: " << m_height
		// << "\nFrame Count: " << m_frame_count
		// << "\nFrame Rate: " << m_frame_rate << "\n";

	return true;
}

	bool
BrawInput::close()
{
	if(m_clipEx != nullptr)
		m_clipEx->Release();
	if(m_clip != nullptr)
		m_clip->Release();
	if(m_codec != nullptr)
		m_codec->Release();
	if(m_factory != nullptr)
		m_factory->Release();
	if(m_callback != nullptr)
		delete m_callback;

	init();

	return true;
}

	bool
BrawInput::seek_subimage(int subimage, int miplevel)
{
	if (subimage < 0 || subimage >= m_nsubimages || miplevel > 0) {
		return false;
	}
	if (subimage == m_subimage) {
		return true;
	}
	m_subimage   = subimage;
	m_read_frame = false;
	return true;
}

	bool
BrawInput::read_native_scanline(int subimage, int miplevel, int y, int z,
		void* data)
{
	// std::cerr << "About to start read_native_scanline\n";

	lock_guard lock(m_mutex);
	std::cerr << "subimage: " << subimage << " miplevel: " << miplevel << "\n";

	if (!seek_subimage(subimage, miplevel))
		return false;
	if(!m_read_frame) {
		if(!readFrame(m_subimage))
			return false;
	}

	if (y < 0 || y >= m_spec.height)  // out of range scanline
		return false;

	memcpy(data, &m_imageData[y * m_spec.width * m_spec.nchannels],
			m_spec.width * m_spec.nchannels);

	return true;
}

	bool
BrawInput::readFrame(uint64_t frameIndex)
{
	HRESULT result = S_OK;
	IBlackmagicRawJob* readJob = nullptr;

	result = m_clip->CreateJobReadFrame(frameIndex, &readJob);
	if (result != S_OK) {
		std::cerr << "Failed to create IBlackmagicRawJob!" << std::endl;
		return false;
	}

	result = readJob->Submit();
	if (result != S_OK) {
		std::cerr << "Failed to submit IBlackmagicRawJob!" << std::endl;
		return false;
	}
	m_codec->FlushJobs();

	m_read_frame = true;

	return true;
}

	void
BrawInput::storeImageData(void* imageData)
{
	m_imageData.resize(m_width * m_height * m_number_channels);
	std::fill(m_imageData.begin(), m_imageData.end(), 0x00);

	unsigned char* rgba = (unsigned char*)imageData;
	for (int y = 0; y <= (int)m_height-1; y++)
	{
		for (int x = 0; x < (int)m_width; x++)
		{
			unsigned char red   = rgba[0];
			unsigned char green = rgba[1];
			unsigned char blue  = rgba[2];
			unsigned char alpha = rgba[3];

			// int x   = window_left + wx;
			int idx = m_number_channels * (y * m_width + x);
			// std::cerr << "IDX: " << idx << "\n";
			// // if (0 <= x && x < m_spec.width
			// // && fscanline[wx] != m_transparent_color) {
			m_imageData[idx]     = red;
			m_imageData[idx + 1] = green;
			m_imageData[idx + 2] = blue;
			m_imageData[idx + 3] = alpha;

			rgba += 4; // next 4

		}
	}
}

	OIIO_PLUGIN_NAMESPACE_END
