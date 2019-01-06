#include <libavfilter/avfilter.h>
#include <libavutil/version.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include "Myfilter.h"


/*
 * The first field AVClass is needed if this AVFilter accepts options.
 *
 * If filter accepts outside parameters, then the options is needed 
 * for user to send parameters through api like av_opt_set which would
 * find parameter position from ctx->priv. Options need to be maitained
 * by AVClass, and the AVClass is a field of AVFilter with the field 
 * named priv_calss. When initializing AVFilterContext, the execution
 * below would be taken
 *      ff_filter_alloc()
 *          ret->priv = av_mallocz(priv_size);     //allcate for private context(TestContext)
 *          if (filter->priv_class){
 *              *(const AVClass**)ret->priv = filter->priv_class;  //first filed of private context must be AVClass
 *              av_opt_set_defaults(ret->priv);       //the first filed of private context would be used for options tracing
 *          }
 *
 * While calling api with flag AV_OPT_SEARCH_CHILDREN, the finding route is 
 *      +-----------+   +-->+-----------+  +-->+-----------+
 *      |  AVClass  |   |   |  AVClass  |  |   |  AVClass  |
 *      |           |   |   |           |  |   |           |
 *      +-----------+   |   +-----------+  |   +-----------+
 *      |   priv    +---+   |   priv    +--+   |   priv    |
 *      +-----------+       +-----------+      +-----------+
 *      |           |       |           |      |           |
 *      |           |       |           |      |           |
 *      |           |       |           |      |           |
 *      +-----------+       +-----------+      +-----------+
 * see av_opt_find2 --> av_opt_child_next -->filter_child_next
 * For each node in this link, search the memory position of wanted 
 * field through options inside AVClass
 * */
typedef struct TestContext {
    AVClass class;
    int w;
    int h;
    int Reserved;
} TestContext;

#define OFFSET(x) offsetof(TestContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption TestContext_options[] = {
    { "size",      "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0, FLAGS },
    { NULL }
};

static const AVClass TestContext_class = {
    .class_name = "TestContext",
    .item_name  = av_default_item_name,
    .option     = TestContext_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

//set TestContext Parameters, some initialization
static int init(AVFilterContext *ctx, AVDictionary **opt)
{
    TestContext *test = (TestContext *)ctx->priv;
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

//request frame from inlink of the filter and (should) put it over the outlink
static int request_frame(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    
    //av_log(NULL, AV_LOG_INFO, "[%s before request]inlink->frame_wanted_out = %d, outlink->frame_wanted_out = %d\n", ctx->name, inlink->frame_wanted_out, outlink->frame_wanted_out);

    ret = ff_request_frame(inlink);
    //av_log(NULL, AV_LOG_INFO, "[%s after request] inlink->frame_wanted_out = %d, outlink->frame_wanted_out = %d\n", ctx->name, inlink->frame_wanted_out, outlink->frame_wanted_out);
    return ret;
}

//get frame from inlink then filter and output frame to the outlink
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    TestContext *testcontext = inlink->dst->priv;
    int ret;
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    //av_log(NULL, AV_LOG_INFO, "width = %d, height = %d\n", testcontext->w, testcontext->h);
    //av_log(NULL, AV_LOG_INFO, "[%s before filter] inlink->frame_wanted_out = %d, outlink->frame_wanted_out = %d\n", ctx->name, inlink->frame_wanted_out, outlink->frame_wanted_out);
    ret = ff_filter_frame(outlink, in);
    //av_log(NULL, AV_LOG_INFO, "[%s after filter]  inlink->frame_wanted_out = %d, outlink->frame_wanted_out = %d\n", ctx->name, inlink->frame_wanted_out, outlink->frame_wanted_out);
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
    .priv_class    = &TestContext_class,
    .init_dict     = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = avfilter_f_test_inputs,
    .outputs       = avfilter_f_test_outputs,
};
