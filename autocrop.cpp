#include "VapourSynth.h"
#include "VSHelper.h"
#include <iostream>
#define SET_COLOR_RESAMPLE 0x0000001
#define setBlack(color, format) {uint32_t b1[3] = {0, 123, 123}; setColor(color, format, b1, SET_COLOR_RESAMPLE);}
#define setBlack2(color, format) {uint32_t b2[3] = {21, 133, 133}; setColor(color, format, b2, SET_COLOR_RESAMPLE);}
#define setColorSimple(color, format, base) setColor(color, format, base, SET_COLOR_RESAMPLE);
#define OUT
#define IN

typedef struct {
    VSNodeRef *node;
    const VSVideoInfo *vi_finish;
    const VSVideoInfo *vi;
    int range;
    int top;
    int bottom;
    int left;
    int right;
    uint32_t color[3];
    uint32_t color_second[3];
} AutoCropData;

struct CropPlaneValues {
    int topArray[3];
    int bottomArray[3];
    int leftArray[3];
    int rightArray[3];
};

struct CropFrameValues {
    int top;
    int bottom;
    int left;
    int right;
    int width;
    int height;
};

inline void setColor(OUT uint32_t color[3], const IN VSFormat *format, IN uint32_t base[3], int flags=0) {
    if (flags & SET_COLOR_RESAMPLE && format->sampleType == stInteger) {
        for (int i = 0; i<3; i++)
            base[i] <<= (format->bitsPerSample - 8);
    }

    for (int i = 0; i < 3; i++)
        color[i] = base[i];
}

int checkSubSampling(int cropValueArray[3], int subSampling){
    int cropValue;

    cropValue = std::min(std::min(cropValueArray[0], cropValueArray[1] << subSampling), cropValueArray[2] << subSampling);
    cropValue = cropValue - (cropValue % (1 << subSampling));

    return cropValue;
}

template <typename Bit>
void getCropValues(CropPlaneValues *c, const Bit *srcp, int src_stride, int w, int h, int color, int color2,
                   int topRange, int bottomRange, int leftRange, int rightRange, int plane) {
    int topValue = 0;
    int bottomValue = bottomRange;
    int leftValue = leftRange;
    int rightValue = rightRange;
    for (int y = 0; y < h; y++) {
        //top
        if (y < topRange) {
            for (int x = 0; x < w; x += 10) {
                if (!(color <= srcp[x] && srcp[x] <= color2)) {
                    topRange = 0;
                    break;
                }
            }
            if (topRange) {
                topValue++;
            }
        }
        //left
        for (int x = 0; x < leftRange; x++) {
            if (!(color <= srcp[x] && srcp[x] <= color2)) {
                if (leftValue >= x) {
                    leftValue = x;
                }
            }
        }
        //right
        for (int x = w - rightRange; x < w; x++) {
            if (!(color <= srcp[x] && srcp[x] <= color2)) {
                if (rightValue >= w - x) {
                    rightValue = w - x - 1;
                }
            }
        }
        //bottom
        if (y >= h - bottomRange) {
            for (int x = 0; x < w; x += 10) {
                if (!(color <= srcp[x] && srcp[x] <= color2)) {
                    bottomValue = h - (y + 1);
                    break;
                }
            }
        }
        srcp += src_stride;
    }

    c->topArray[plane] = topValue;
    c->bottomArray[plane] = bottomValue;
    c->leftArray[plane] = leftValue;
    c->rightArray[plane] = rightValue;
}

template <typename Bit>
void getFramePlane(const VSFrameRef *src, AutoCropData *data, const VSAPI *vsapi, CropFrameValues *cFrame) {
    CropPlaneValues cPlane{};
    const VSFormat *fi = data->vi->format;

    for (int plane = 0; plane < fi->numPlanes; plane++) {
        const auto *srcp = (const Bit *) vsapi->getReadPtr(src, plane);
        int src_stride = vsapi->getStride(src, plane)  / sizeof(Bit);
        int h = vsapi->getFrameHeight(src, plane);
        int w = vsapi->getFrameWidth(src, plane);

        if (plane == 0){
            getCropValues<Bit>(&cPlane, srcp, src_stride, w, h, data->color[0], data->color_second[0], data->top,
                               data->bottom, data->left, data->right, plane);
        }
        else if(plane == 1){
            if (data->vi->format->subSamplingH == 0 && data->vi->format->subSamplingW == 0) {
                getCropValues<Bit>(&cPlane, srcp, src_stride, w, h, data->color[1], data->color_second[1], data->top,
                                   data->bottom, data->left, data->right, plane);
            }
            else if (data->vi->format->subSamplingH == 1 && data->vi->format->subSamplingW == 1) {
                getCropValues<Bit>(&cPlane, srcp, src_stride, w, h, data->color[1], data->color_second[1], data->top/2,
                                   data->bottom/2, data->left/2, data->right/2, plane);
            }
        }
        else {
            if (data->vi->format->subSamplingH == 0 && data->vi->format->subSamplingW == 0) {
                getCropValues<Bit>(&cPlane, srcp, src_stride, w, h, data->color[2], data->color_second[2], data->top,
                                   data->bottom, data->left, data->right, plane);
            }
            else if (data->vi->format->subSamplingH == 1 && data->vi->format->subSamplingW == 1){
                getCropValues<Bit>(&cPlane, srcp, src_stride, w, h, data->color[2], data->color_second[2], data->top/2,
                                   data->bottom/2, data->left/2, data->right/2, plane);
            }
        }
    }

    cFrame->top    = checkSubSampling(cPlane.topArray, data->vi->format->subSamplingH);
    cFrame->bottom = checkSubSampling(cPlane.bottomArray, data->vi->format->subSamplingH);
    cFrame->left   = checkSubSampling(cPlane.leftArray, data->vi->format->subSamplingW);
    cFrame->right  = checkSubSampling(cPlane.rightArray, data->vi->format->subSamplingW);
    cFrame->height   = data->vi->height - cFrame->top - cFrame->bottom;
    cFrame->width    = data->vi->width - cFrame->left - cFrame->right;
}

/////////////////
// CropValues

static void VS_CC cropValuesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *) * instanceData;
    VSVideoInfo vi_finish = *data->vi_finish;
    vsapi->setVideoInfo(&vi_finish, 1, node);
}

static const VSFrameRef *VS_CC cropValuesGetFrame(int n, int activationReason, void **instanceData, void **frameData,
                                                  VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *) * instanceData;
    const char *top = "CropTopValue";
    const char *bottom = "CropBottomValue";
    const char *left = "CropLeftValue";
    const char *right = "CropRightValue";

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, data->node, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, data->node, frameCtx);
        const VSFormat *fi = data->vi->format;
        CropFrameValues cFrame{};

        if (data->vi->format->sampleType == stInteger && data->vi->format->bitsPerSample == 8) {
            getFramePlane<uint8_t>(src, data, vsapi, &cFrame);
        } else if (data->vi->format->sampleType == stInteger && data->vi->format->bitsPerSample <= 16){
            getFramePlane<uint16_t>(src, data, vsapi, &cFrame);
        }

        VSFrameRef *dst = vsapi->copyFrame(src, core);
        for (int plane = 0; plane < fi->numPlanes; plane++) {
            vsapi->getWritePtr(dst, plane);
        }

        VSMap *dstProps = vsapi->getFramePropsRW(dst);

        if (fi->sampleType == stInteger) {
            vsapi->propSetInt(dstProps, top, cFrame.top, paAppend);
            vsapi->propSetInt(dstProps, bottom, cFrame.bottom, paAppend);
            vsapi->propSetInt(dstProps, left, cFrame.left, paAppend);
            vsapi->propSetInt(dstProps, right, cFrame.right, paAppend);
        }
        vsapi->freeFrame(src);

        return dst;
    }

    return 0;
}

static void VS_CC cropValuesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *)instanceData;
    vsapi->freeNode(data->node);
    free(data);
}

static void VS_CC cropValuesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    AutoCropData d;
    AutoCropData *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi_finish = vsapi->getVideoInfo(d.node);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample < 8 || d.vi->format->bitsPerSample > 16) {
        vsapi->setError(out, "CropValues: only constant format 8...16Bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (!(d.vi->format->colorFamily == cmYCoCg || d.vi->format->colorFamily == cmYUV)){
        vsapi->setError(out, "CropValues: only YUV or YCoCg  input supported");
        vsapi->freeNode(d.node);
        return;
    }

    d.range = int64ToIntS(vsapi->propGetInt(in, "range", 0, &err));
    if (err)
        d.range = 4;

    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d.top = d.range;

    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    if (err)
        d.bottom = d.range;

    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    if (err)
        d.right = d.range;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d.left = d.range;

    if (d.vi->format->subSamplingH == 1 && d.vi->format->subSamplingW == 1){
        if (d.range&1 || d.left&1 || d.right&1 || d.top&1 || d.bottom&1) {
            vsapi->setError(out, "CropValues: Odd numbers for Crop not allowed");
            vsapi->freeNode(d.node);
            return;
        }
    }

    if (d.range < 0 || d.left < 0 || d.right < 0 || d.top < 0 || d.bottom < 0) {
        vsapi->setError(out, "AutoCrop: Negative numbers for crop not allowed");
        vsapi->freeNode(d.node);
        return;
    }

    uint32_t color_base[3];
    uint32_t color_base1[3];
    int numcomponents = (d.vi->format->colorFamily == cmCompat) ? 3 : d.vi->format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");
    int ncolors1 = vsapi->propNumElements(in, "color_second");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            color_base[i] = static_cast<uint32_t>(vsapi->propGetInt(in, "color", i, 0));
        }
        setColorSimple(d.color, d.vi->format, color_base);
    }
    else{
        setBlack(d.color, d.vi->format);
    }

    if (ncolors1 == numcomponents) {
        for (int i = 0; i < ncolors1; i++) {
            color_base1[i] = static_cast<uint32_t>(vsapi->propGetInt(in, "color_second", i, 0));
        }
        setColorSimple(d.color_second, d.vi->format, color_base1);
    }
    else{
        setBlack2(d.color_second, d.vi->format);
    }

    data = static_cast<AutoCropData*>(malloc(sizeof(d)));
    *data = d;

    vsapi->createFilter(in, out, "CropValues", cropValuesInit, cropValuesGetFrame, cropValuesFree, fmParallel, 0, data, core);
}

/////////////////
// AutoCrop

static void VS_CC autocropInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *) * instanceData;
    VSVideoInfo vi_finish = *data->vi_finish;
    vi_finish.height = 0;
    vi_finish.width = 0;
    vsapi->setVideoInfo(&vi_finish, 1, node);
}

static const VSFrameRef *VS_CC autocropGetFrame(int n, int activationReason, void **instanceData, void **frameData,
                                                VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *) * instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, data->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *src = vsapi->getFrameFilter(n, data->node, frameCtx);
        const VSFormat *fi = data->vi->format;
        CropFrameValues cFrame{};

        if (data->vi->format->sampleType == stInteger && data->vi->format->bitsPerSample == 8) {
            getFramePlane<uint8_t>(src, data, vsapi, &cFrame);
        } else if (data->vi->format->sampleType == stInteger && data->vi->format->bitsPerSample <= 16){
            getFramePlane<uint16_t>(src, data, vsapi, &cFrame);
        }

        //from https://github.com/vapoursynth/vapoursynth/blob/738f2be63b8d2b19c73b5e20116058f12f9b278d/src/core/simplefilters.c#L132
        VSFrameRef *dst_finish = vsapi->newVideoFrame(fi, cFrame.width, cFrame.height, src, core);
        for (int plane_new = 0; plane_new < fi->numPlanes; plane_new++) {
            int srcstride = vsapi->getStride(src, plane_new);
            int dststride = vsapi->getStride(dst_finish, plane_new);
            const uint8_t *srcdata = vsapi->getReadPtr(src, plane_new);
            uint8_t *dstdata = vsapi->getWritePtr(dst_finish, plane_new);
            srcdata += srcstride * (cFrame.top >> (plane_new ? fi->subSamplingH : 0));
            srcdata += (cFrame.left >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample;
            vs_bitblt(dstdata, dststride, srcdata, srcstride,
                      (cFrame.width >> (plane_new ? fi->subSamplingW : 0)) * fi->bytesPerSample,
                      vsapi->getFrameHeight(dst_finish, plane_new));
        }
        vsapi->freeFrame(src);

        return dst_finish;
        }

    return 0;
}

static void VS_CC autocropFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    auto *data = (AutoCropData *)instanceData;
    vsapi->freeNode(data->node);
    free(data);
}

static void VS_CC autocropCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    AutoCropData d;
    AutoCropData *data;
    uint32_t color_base[3];
    uint32_t color_base1[3];
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi_finish = vsapi->getVideoInfo(d.node);
    d.vi = vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(d.vi) || d.vi->format->sampleType != stInteger || d.vi->format->bitsPerSample < 8 || d.vi->format->bitsPerSample > 16) {
        vsapi->setError(out, "AutoCrop: only constant format 8...16Bit integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (!(d.vi->format->colorFamily == cmYCoCg || d.vi->format->colorFamily == cmYUV)){
        vsapi->setError(out, "AutoCrop: only YUV or YCoCg  input supported");
        vsapi->freeNode(d.node);
        return;
    }

    d.range = int64ToIntS(vsapi->propGetInt(in, "range", 0, &err));
    if (err)
        d.range = 4;

    d.top = int64ToIntS(vsapi->propGetInt(in, "top", 0, &err));
    if (err)
        d.top = d.range;

    d.bottom = int64ToIntS(vsapi->propGetInt(in, "bottom", 0, &err));
    if (err)
        d.bottom = d.range;

    d.right = int64ToIntS(vsapi->propGetInt(in, "right", 0, &err));
    if (err)
        d.right = d.range;

    d.left = int64ToIntS(vsapi->propGetInt(in, "left", 0, &err));
    if (err)
        d.left = d.range;

    if (d.vi->format->subSamplingH == 1 && d.vi->format->subSamplingW == 1){
        if (d.range&1 || d.left&1 || d.right&1 || d.top&1 || d.bottom&1) {
            vsapi->setError(out, "AutoCrop: Odd numbers for crop not allowed");
            vsapi->freeNode(d.node);
            return;
        }
    }

    if (d.range < 0 || d.left < 0 || d.right < 0 || d.top < 0 || d.bottom < 0) {
        vsapi->setError(out, "AutoCrop: Negative numbers for crop not allowed");
        vsapi->freeNode(d.node);
        return;
    }

    int numcomponents = (d.vi->format->colorFamily == cmCompat) ? 3 : d.vi->format->numPlanes;
    int ncolors = vsapi->propNumElements(in, "color");
    int ncolors1 = vsapi->propNumElements(in, "color_second");

    if (ncolors == numcomponents) {
        for (int i = 0; i < ncolors; i++) {
            color_base[i] = static_cast<uint32_t>(vsapi->propGetInt(in, "color", i, 0));
        }
        setColorSimple(d.color, d.vi->format, color_base);
    }
    else{
        setBlack(d.color, d.vi->format);
    }

    if (ncolors1 == numcomponents) {
        for (int i = 0; i < ncolors1; i++) {
            color_base1[i] = static_cast<uint32_t>(vsapi->propGetInt(in, "color_second", i, 0));
        }
        setColorSimple(d.color_second, d.vi->format, color_base1);
    }
    else{
        setBlack2(d.color_second, d.vi->format);
    }
    data = static_cast<AutoCropData*>(malloc(sizeof(d)));
    *data = d;

    vsapi->createFilter(in, out, "AutoCrop", autocropInit, autocropGetFrame, autocropFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("moe.infi.autocrop", "acrop", "VapourSynth AutoCrop", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("AutoCrop", "clip:clip;range:int:opt;top:int:opt;bottom:int:opt;left:int:opt;right:int:opt;color:int[]:opt;color_second:int[]:opt", autocropCreate, 0, plugin);
    registerFunc("CropValues", "clip:clip;range:int:opt;top:int:opt;bottom:int:opt;left:int:opt;right:int:opt;color:int[]:opt;color_second:int[]:opt", cropValuesCreate, 0, plugin);
}
