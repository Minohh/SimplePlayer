#include <libavfilter/avfilter.h>
#include <libavutil/version.h>
#include "Myfilter.h"

typedef struct TestContext {
    int Reserved;
} TestContext;

//set TestContext Parameters, some initialization
static int init(AVFilterContext *ctx, AVDictionary **opt)
{
    return 0;
}

//uninitialization
static void uninit(AVFilterContext *ctx)
{
}

/* This callback must set AVFilterLink.out_format on every input link and 
 * AVFilterlink.in_formats on every output link to a list of pixel/sample
 * formats that the filter supports on that link. For audio links , this 
 * filter must also set 
 * @ref AVFilterLink.in_samplerates "in_samplerates"
 * @ref AVFilterLink.out_samplerates "out_samplerates"
 * @ref AVFilterLink.in_channel_layouts "in_channel_layouts"
 * @ref AVFilterLink.out_channel_layouts "out_channel_layouts"
 *
 * This callback may be NULL for filters with one input, in which case 
 * libavfilter assumes that it supports all input formats and preserves 
 * them on output 
 * */
static int query_formats(AVFilterContext *ctx)
{
    int ret;

    ret = ff_default_query_formats(ctx);
    return ret;
}

/* For output pads, this should set the link properties such as 
 * width/height. This should NOT set the format property -that is 
 * negotiated between filters by the filter system using the
 * query_formats() callback before this funtion is called.
 *
 * For input pads, this should check the properties of the link ,and 
 * undate the filter's internal state as necessary.
 * */
static int config_output(AVFilterLink *outlink)
{
    return 0;
}

//request frame from prev filter
static int request_frame(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    
    ret = ff_request_frame(inlink);
    return ret;
}

//filter and output frame to next filter
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int ret;
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];

    ret = ff_filter_frame(outlink, in);
    return ret;
}

static const AVClass test_class = {
    .class_name = "test",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad avfilter_f_test_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_f_test_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_f_test = {
    .name          = "test",
    .priv_size     = sizeof(TestContext),
    .priv_class    = &test_class,
    .init_dict     = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_f_test_inputs,
    .outputs       = avfilter_f_test_outputs,
};
