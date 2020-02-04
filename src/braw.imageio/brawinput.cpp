/*
 Created by Linas Beresna.
 */

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

#define DEBUG 0

OIIO_PLUGIN_NAMESPACE_BEGIN

class CameraCodecCallback;

class BrawInput final : public ImageInput {
	public:
		BrawInput() { init(); }
		virtual ~BrawInput() { close(); }
		virtual const char* format_name(void) const override { return "braw"; }
		virtual bool open(const std::string& name, ImageSpec& newspec) override;
		virtual bool close() override;
    virtual int supports(string_view feature) const override
    {
        return (feature == "exif");
    }
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
		bool getMetadata(ImageSpec &spec);

	private:
		CameraCodecCallback* m_callback;
		IBlackmagicRawFactory* m_factory;
		IBlackmagicRaw* m_codec;
		IBlackmagicRawClip* m_clip;

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
/////// Callback class to get data from BlackmagicRAWAPI ///////////////
////////////////////////////////////////////////////////////////////////
class CameraCodecCallback : public IBlackmagicRawCallback
{
	public:
		explicit CameraCodecCallback(BrawInput* brawInput) : m_brawInput(brawInput) {}
		virtual ~CameraCodecCallback()
		{
			if(m_frame != nullptr)
				m_frame->Release();
		}

		IBlackmagicRawFrame* GetFrame() { return m_frame; }

		virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
		{
			IBlackmagicRawJob* decodeAndProcessJob = nullptr;

			if(result == S_OK) {
				m_frame = frame;
				m_frame->AddRef();
			}

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

			if (result == S_OK) {
				m_brawInput->storeImageData(imageData);
			}

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
		IBlackmagicRawFrame* m_frame = nullptr;
};

//////////////////////////////////////////////////////////////////////////
///////////////////// BrawInput Plugic Functions /////////////////////////
//////////////////////////////////////////////////////////////////////////

	bool
BrawInput::open(const std::string& name, ImageSpec& newspec)
{
#ifdef BRAW_LIBRARIES
#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#else
#if DEBUG
	std::cerr << "BRAW_LIBRARIES are not defined, therefore cannot construct BlackmagicRawFactory\n";
#endif
	return false;
#endif

	filesystem::path lib_path(STRINGIFY(BRAW_LIBRARIES));

	m_number_channels = 4;  // Black magic has 4 channels?

	HRESULT result = S_OK;

	m_factory = CreateBlackmagicRawFactoryInstanceFromPath(lib_path.parent_path().c_str());
	if (m_factory == nullptr) {
#if DEBUG
		std::cerr << "Failed to create IBlackmagicRawFactory!" << std::endl;
#endif
		return false;
	}

	result = m_factory->CreateCodec(&m_codec);
	if (result != S_OK) {
#if DEBUG
		std::cerr << "Failed to create IBlackmagicRaw!" << std::endl;
#endif
		return false;
	}

	result = m_codec->OpenClip(name.c_str(), &m_clip);
	if (result != S_OK) {
#if DEBUG
		std::cerr << "Failed to open IBlackmagicRawClip!" << std::endl;
#endif
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
#if DEBUG
		std::cerr << "Failed to set IBlackmagicRawCallback!" << std::endl;
#endif
		return false;
	}

	m_spec = ImageSpec((int)m_width, (int)m_height, m_number_channels, TypeDesc::UINT8);
	newspec = m_spec;
	newspec.alpha_channel = 4;
	newspec.attribute("oiio:ColorSpace", "linear");
	newspec.attribute("oiio:Movie", true);

	m_nsubimages = m_frame_count;

	IBlackmagicRawClipProcessingAttributes* clipProcessingAttributes = nullptr;

	// std::cerr << "\n";
	// getMetadata(newspec);
	// std::cerr << "\n";

#if DEBUG
	std::cerr << "Width: " << m_width
		<< "\nHeight: " << m_height
		<< "\nFrame Count: " << m_frame_count
		<< "\nFrame Rate: " << m_frame_rate << "\n";
#endif

	return true;
}

bool
BrawInput::getMetadata(ImageSpec &spec)
{
	// Metadata
	IBlackmagicRawMetadataIterator* clipMetadataIterator = nullptr;
	const char *key = nullptr;
	Variant value;

	HRESULT result = m_clip->GetMetadataIterator(&clipMetadataIterator);
	if (result != S_OK) {
#if DEBUG
		std::cerr << "Failed to get metdata iterator\n";
#endif
		return false;
	}

	while (SUCCEEDED(clipMetadataIterator->GetKey(&key))) {
		std::cerr << "Key - " << key << ": value - ";
		VariantInit(&value);

		result = clipMetadataIterator->GetData(&value);
		if (result != S_OK)
		{
#if DEBUG
			std::cerr << "Failed to get data from IBlackmagicRawMetadataIterator!" << std::endl;}
#endif
			break;
		}

		BlackmagicRawVariantType variantType = value.vt;
		switch (variantType)
		{
			case blackmagicRawVariantTypeS16:
			{
				short s16 = value.iVal;
				std::cout << s16;
			}
			break;
			case blackmagicRawVariantTypeU16:
			{
				unsigned short u16 = value.uiVal;
				std::cout << u16;
			}
			break;
			case blackmagicRawVariantTypeS32:
			{
				int i32 = value.intVal;
				std::cout << i32;
			}
			break;
			case blackmagicRawVariantTypeU32:
			{
				unsigned int u32 = value.uintVal;
				std::cout << u32;
			}
			break;
			case blackmagicRawVariantTypeFloat32:
			{
				float f32 = value.fltVal;
				std::cout << f32;
			}
			break;
			case blackmagicRawVariantTypeString:
			{
				std::cout << value.bstrVal;
			}
			break;
			case blackmagicRawVariantTypeSafeArray:
			{
				SafeArray* safeArray = value.parray;

				void* safeArrayData = nullptr;
				result = SafeArrayAccessData(safeArray, &safeArrayData);
				if (result != S_OK)
				{
#if DEBUG
					std::cerr << "Failed to access safeArray data!" << std::endl;
#endif
					break;
				}

				BlackmagicRawVariantType arrayVarType;
				result = SafeArrayGetVartype(safeArray, &arrayVarType);
				if (result != S_OK)
				{
#if DEBUG
					std::cerr << "Failed to get BlackmagicRawVariantType from safeArray!" << std::endl;
#endif
					break;
				}

				long lBound;
				result = SafeArrayGetLBound(safeArray, 1, &lBound);
				if (result != S_OK)
				{
#if DEBUG
					std::cerr << "Failed to get LBound from safeArray!" << std::endl;
#endif
					break;
				}

				long uBound;
				result = SafeArrayGetUBound(safeArray, 1, &uBound);
				if (result != S_OK)
				{
#if DEBUG
					std::cerr << "Failed to get UBound from safeArray!" << std::endl;
#endif
					break;
				}

				long safeArrayLength = (uBound - lBound) + 1;
				long arrayLength = safeArrayLength > 32 ? 32 : safeArrayLength;

				for (int i = 0; i < arrayLength; ++i)
				{
					switch (arrayVarType)
					{
						case blackmagicRawVariantTypeU8:
						{
							int u8 = static_cast<int>(static_cast<unsigned char*>(safeArrayData)[i]);
						if (i > 0)
							std::cout << ",";
						std::cout << u8;
						}
						break;
						case blackmagicRawVariantTypeS16:
						{
							short s16 = static_cast<short*>(safeArrayData)[i];
							std::cout << s16 << " ";
						}
						break;
						case blackmagicRawVariantTypeU16:
						{
							unsigned short u16 = static_cast<unsigned short*>(safeArrayData)[i];
							std::cout << u16 << " ";
						}
						break;
						case blackmagicRawVariantTypeS32:
						{
							int i32 = static_cast<int*>(safeArrayData)[i];
							std::cout << i32 << " ";
						}
						break;
						case blackmagicRawVariantTypeU32:
						{
							unsigned int u32 = static_cast<unsigned int*>(safeArrayData)[i];
							std::cout << u32 << " ";
						}
						break;
						case blackmagicRawVariantTypeFloat32:
						{
							float f32 = static_cast<float*>(safeArrayData)[i];
							std::cout << f32 << " ";
						}
						break;
						default:
							break;
					}
				}
			}
			default:
				break;
		}

		VariantClear(&value);

		std::cout << std::endl;

		clipMetadataIterator->Next();
	}

	if(clipMetadataIterator != nullptr)
		clipMetadataIterator->Release();
	return true;
}

	bool
BrawInput::close()
{
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
	lock_guard lock(m_mutex);

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
#if DEBUG
		std::cerr << "Failed to create IBlackmagicRawJob!" << std::endl;
#endif
		return false;
	}

	result = readJob->Submit();
	if (result != S_OK) {
		readJob->Release();
#if DEBUG
		std::cerr << "Failed to submit IBlackmagicRawJob!" << std::endl;
#endif
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

			int idx = m_number_channels * (y * m_width + x);
			m_imageData[idx]     = red;
			m_imageData[idx + 1] = green;
			m_imageData[idx + 2] = blue;
			m_imageData[idx + 3] = alpha;

			rgba += 4; // next 4

		}
	}
}

	OIIO_PLUGIN_NAMESPACE_END
