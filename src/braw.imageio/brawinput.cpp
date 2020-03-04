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
#include <memory>

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

    private:
        void init();
        bool read_frame(uint64_t frameIndex);
        bool fill_metadata(ImageSpec &spec);

    private:
        std::unique_ptr<CameraCodecCallback> m_callback;
        IBlackmagicRawFactory* m_factory;
        IBlackmagicRaw* m_codec;
        IBlackmagicRawClip* m_clip;

        uint64_t m_frame_count;
        uint32_t m_width;
        uint32_t m_height;
        int m_subimage;
        bool m_read_frame;
        int64_t m_nsubimages;
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
////////// Settings for debayering and post processing /////////////////
////////////////////////////////////////////////////////////////////////

// Manual settings for output pixel format
static const BlackmagicRawResourceFormat s_resourceFormat = blackmagicRawResourceFormatRGBF32;
static const uint8_t s_resourceChannels = 3;
typedef float PixelType;

////////////////////////////////////////////////////////////////////////
/////// Callback class to get data from BlackmagicRAWAPI ///////////////
////////////////////////////////////////////////////////////////////////

class CameraCodecCallback : public IBlackmagicRawCallback
{
    public:
        explicit CameraCodecCallback() {}
        virtual ~CameraCodecCallback()
        {
            if(m_frame != nullptr)
                m_frame->Release();
        }

        IBlackmagicRawFrame* GetFrame() { return m_frame; }

        uint8_t* GetData() { return m_imageData.data(); }

        virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
        {
            IBlackmagicRawJob* decodeAndProcessJob = nullptr;

            if(result == S_OK)
                result = frame->SetResourceFormat(s_resourceFormat);

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
            uint32_t width = 0;
            uint32_t height = 0;
            uint8_t channels = s_resourceChannels;
            void* imageData = nullptr;

            if (result == S_OK)
                result = processedImage->GetWidth(&width);

            if (result == S_OK)
                result = processedImage->GetHeight(&height);

            if (result == S_OK)
                result = processedImage->GetResource(&imageData);

            if (result == S_OK) {
                m_imageData.resize(width * height * channels * sizeof(PixelType));

                PixelType* src = (PixelType*) imageData;
                PixelType* dst = (PixelType*) m_imageData.data();

                for (uint32_t y = 0; y < height; y++)
                    for (uint32_t x = 0; x < width; x++)
                        for (uint8_t c = 0; c < channels; ++c)
                            *dst++ = *src++;
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
        std::vector<uint8_t> m_imageData;
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
    errorf("BRAW_LIBRARIES are not defined, therefore cannot construct BlackmagicRawFactory\n");
    return false;
#endif

    filesystem::path lib_path(STRINGIFY(BRAW_LIBRARIES));

    HRESULT result = S_OK;

    m_factory = CreateBlackmagicRawFactoryInstanceFromPath(lib_path.parent_path().c_str());
    if (m_factory == nullptr) {
        errorf("Failed to create IBlackmagicRawFactory!");
        return false;
    }

    result = m_factory->CreateCodec(&m_codec);
    if (result != S_OK) {
        errorf(" Failed to create IBlackmagicRaw!");
        return false;
    }

    result = m_codec->OpenClip(name.c_str(), &m_clip);
    if (result != S_OK) {
        errorf("Failed to open IBlackmagicRawClip!");
        return false;
    }

    // Get basic data from the clip
    m_clip->GetWidth(&m_width);
    m_clip->GetHeight(&m_height);
    m_clip->GetFrameCount(&m_frame_count);

    // Set callback class
    m_callback = std::unique_ptr<CameraCodecCallback>(new CameraCodecCallback());
    result = m_codec->SetCallback(m_callback.get());
    if (result != S_OK) {
        errorf("Failed to set IBlackmagicRawCallback!");
        return false;
    }

    newspec.attribute("oiio:ColorSpace", "linear");
    m_nsubimages = m_frame_count;
    m_spec = ImageSpec((int)m_width, (int)m_height, s_resourceChannels, BaseTypeFromC<PixelType>::value);
    m_spec.attribute("oiio:Movie", true);
    fill_metadata(m_spec);

    newspec = m_spec;
    return true;
}

bool
BrawInput::fill_metadata(ImageSpec &spec)
{
    // Metadata
    IBlackmagicRawMetadataIterator* clipMetadataIterator = nullptr;
    const char *key = nullptr;
    Variant value;

    spec.attribute("Exif:Make", "Blackmagic Design");

    HRESULT result = m_clip->GetMetadataIterator(&clipMetadataIterator);
    if (result != S_OK) {
        errorf("Failed to get metdata iterator\n");
        return false;
    }

    while (SUCCEEDED(clipMetadataIterator->GetKey(&key))) {
        std::string str_value = "";
        VariantInit(&value);

        result = clipMetadataIterator->GetData(&value);
        if (result != S_OK)
        {
            errorf("Failed to get data from IBlackmagicRawMetadataIterator!");
            break;
        }

        BlackmagicRawVariantType variantType = value.vt;
        switch (variantType)
        {
            case blackmagicRawVariantTypeS16:
                {
                    short s16 = value.iVal;
                    str_value = std::to_string(s16);
                }
                break;
            case blackmagicRawVariantTypeU16:
                {
                    unsigned short u16 = value.uiVal;
                    str_value = std::to_string(u16);
                }
                break;
            case blackmagicRawVariantTypeS32:
                {
                    int i32 = value.intVal;
                    str_value = std::to_string(i32);
                }
                break;
            case blackmagicRawVariantTypeU32:
                {
                    unsigned int u32 = value.uintVal;
                    str_value = std::to_string(u32);
                }
                break;
            case blackmagicRawVariantTypeFloat32:
                {
                    float f32 = value.fltVal;
                    str_value = std::to_string(f32);
                }
                break;
            case blackmagicRawVariantTypeString:
                {
                    str_value = value.bstrVal;
                }
                break;
            default:
                break;

        }

        spec.attribute(key, str_value);
        VariantClear(&value);

        clipMetadataIterator->Next();
    }

    if(clipMetadataIterator != nullptr)
        clipMetadataIterator->Release();
    return true;
}

void
BrawInput::init()
{
    // Reset everything to initial state
    m_callback        = nullptr;
    m_factory         = nullptr;
    m_codec           = nullptr;
    m_clip            = nullptr;
    m_width           = 0;
    m_height          = 0;
    m_frame_count     = 0;
    m_subimage        = 0;
    m_read_frame      = false;
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
        if(!read_frame(m_subimage))
            return false;
    }

    if (y < 0 || y >= m_spec.height)  // out of range scanline
        return false;

    size_t count = m_spec.width * m_spec.nchannels * sizeof(PixelType);
    uint8_t* src = &m_callback->GetData()[y * count];
    memcpy(data, (void*) src, count);

    return true;
}

bool
BrawInput::read_frame(uint64_t frameIndex)
{
    HRESULT result = S_OK;
    IBlackmagicRawJob* readJob = nullptr;

    result = m_clip->CreateJobReadFrame(frameIndex, &readJob);
    if (result != S_OK) {
        errorf("Failed to create IBlackmagicRawJob!");
        return false;
    }

    result = readJob->Submit();
    if (result != S_OK) {
        readJob->Release();
        errorf("Failed to submit IBlackmagicRawJob!");
        return false;
    }
    m_codec->FlushJobs();

    m_read_frame = true;

    return true;
}

OIIO_PLUGIN_NAMESPACE_END
