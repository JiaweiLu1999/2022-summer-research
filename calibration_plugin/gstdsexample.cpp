#include <string.h>
#include <string>
#include <sstream>
#include <iostream>
#include <ostream>
#include <fstream>
#include "gstdsexample.h"
#include <sys/time.h>
GST_DEBUG_CATEGORY_STATIC (gst_dsexample_debug);
#define GST_CAT_DEFAULT gst_dsexample_debug
#define USE_EGLIMAGE 1
/* enable to write transformed cvmat to files */
/* #define DSEXAMPLE_DEBUG */
static GQuark _dsmeta_quark = 0;

/* Enum to identify properties */
enum
{
  PROP_0,
  PROP_UNIQUE_ID,
  PROP_PROCESSING_WIDTH,
  PROP_PROCESSING_HEIGHT,
  PROP_PROCESS_VIEW,
  PROP_GPU_DEVICE_ID
};

#define CHECK_NVDS_MEMORY_AND_GPUID(object, surface)  \
  ({ int _errtype=0;\
   do {  \
    if ((surface->memType == NVBUF_MEM_DEFAULT || surface->memType == NVBUF_MEM_CUDA_DEVICE) && \
        (surface->gpuId != object->gpu_id))  { \
    GST_ELEMENT_ERROR (object, RESOURCE, FAILED, \
        ("Input surface gpu-id doesnt match with configured gpu-id for element," \
         " please allocate input using unified memory, or use same gpu-ids"),\
        ("surface-gpu-id=%d,%s-gpu-id=%d",surface->gpuId,GST_ELEMENT_NAME(object),\
         object->gpu_id)); \
    _errtype = 1;\
    } \
    } while(0); \
    _errtype; \
  })


/* Default values for properties */
#define DEFAULT_UNIQUE_ID 15
#define DEFAULT_PROCESSING_WIDTH 1920
#define DEFAULT_PROCESSING_HEIGHT 1080
#define DEFAULT_PROCESS_VIEW 1
#define DEFAULT_BLUR_OBJECTS FALSE
#define DEFAULT_GPU_ID 0

#define RGB_BYTES_PER_PIXEL 3
#define RGBA_BYTES_PER_PIXEL 4
#define Y_BYTES_PER_PIXEL 1
#define UV_BYTES_PER_PIXEL 2

#define MIN_INPUT_OBJECT_WIDTH 16
#define MIN_INPUT_OBJECT_HEIGHT 16

#define CHECK_NPP_STATUS(npp_status,error_str) do { \
  if ((npp_status) != NPP_SUCCESS) { \
    g_print ("Error: %s in %s at line %d: NPP Error %d\n", \
        error_str, __FILE__, __LINE__, npp_status); \
    goto error; \
  } \
} while (0)

#define CHECK_CUDA_STATUS(cuda_status,error_str) do { \
  if ((cuda_status) != cudaSuccess) { \
    g_print ("Error: %s in %s at line %d (%s)\n", \
        error_str, __FILE__, __LINE__, cudaGetErrorName(cuda_status)); \
    goto error; \
  } \
} while (0)

/* By default NVIDIA Hardware allocated memory flows through the pipeline. We
 * will be processing on this type of memory only. */
#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"
static GstStaticPadTemplate gst_dsexample_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ NV12, RGBA, I420 }")));

static GstStaticPadTemplate gst_dsexample_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ NV12, RGBA, I420 }")));

/* Define our element type. Standard GObject/GStreamer boilerplate stuff */
#define gst_dsexample_parent_class parent_class
G_DEFINE_TYPE (GstDsExample, gst_dsexample, GST_TYPE_BASE_TRANSFORM);

static void gst_dsexample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dsexample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_dsexample_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_dsexample_start (GstBaseTransform * btrans);
static gboolean gst_dsexample_stop (GstBaseTransform * btrans);

static GstFlowReturn gst_dsexample_transform_ip (GstBaseTransform *
    btrans, GstBuffer * inbuf);

static void
attach_metadata_full_frame (GstDsExample * dsexample, NvDsFrameMeta *frame_meta,
    gdouble scale_ratio, DsExampleOutput * output, guint batch_id);
static void attach_metadata_object (GstDsExample * dsexample,
    NvDsObjectMeta * obj_meta, DsExampleOutput * output);

/* Install properties, set sink and src pad capabilities, override the required
 * functions of the base class, These are common to all instances of the
 * element.
 */
static void
gst_dsexample_class_init (GstDsExampleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  /* Indicates we want to use DS buf api */
  g_setenv ("DS_NEW_BUFAPI", "1", TRUE);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  /* Overide base class functions */
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_dsexample_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_dsexample_get_property);

  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_dsexample_set_caps);
  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_dsexample_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_dsexample_stop);

  gstbasetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_dsexample_transform_ip);

  /* Install properties */
  g_object_class_install_property (gobject_class, PROP_UNIQUE_ID,
      g_param_spec_uint ("unique-id",
          "Unique ID",
          "Unique ID for the element. Can be used to identify output of the"
          " element", 0, G_MAXUINT, DEFAULT_UNIQUE_ID, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PROCESSING_WIDTH,
      g_param_spec_int ("processing-width",
          "Processing Width",
          "Width of the input buffer to algorithm",
          1, G_MAXINT, DEFAULT_PROCESSING_WIDTH, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PROCESSING_HEIGHT,
      g_param_spec_int ("processing-height",
          "Processing Height",
          "Height of the input buffer to algorithm",
          1, G_MAXINT, DEFAULT_PROCESSING_HEIGHT, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PROCESS_VIEW,
      g_param_spec_int ("view",
          "Perspective of the camera",
          "Change the value to choose perspective transformation matrix."
          "by primary detector", 1, G_MAXINT, DEFAULT_PROCESS_VIEW, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id",
          "Set GPU Device ID",
          "Set GPU Device ID", 0,
          G_MAXUINT, 0,
          GParamFlags
          (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
  /* Set sink and src pad capabilities */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dsexample_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dsexample_sink_template));

  /* Set metadata describing the element */
  gst_element_class_set_details_simple (gstelement_class,
      "DsExample plugin",
      "DsExample Plugin",
      "Process a 3rdparty example algorithm on objects / full frame",
      "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
      "@ https://devtalk.nvidia.com/default/board/209/");
}

static void
gst_dsexample_init (GstDsExample * dsexample)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (dsexample);

  /* We will not be generating a new buffer. Just adding / updating
   * metadata. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  /* We do not want to change the input caps. Set to passthrough. transform_ip
   * is still called. */
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);

  /* Initialize all property variables to default values */
  dsexample->unique_id = DEFAULT_UNIQUE_ID;
  dsexample->processing_width = DEFAULT_PROCESSING_WIDTH;
  dsexample->processing_height = DEFAULT_PROCESSING_HEIGHT;
  dsexample->process_view = DEFAULT_PROCESS_VIEW;
  dsexample->gpu_id = DEFAULT_GPU_ID;

  /* This quark is required to identify NvDsMeta when iterating through
   * the buffer metadatas */
  if (!_dsmeta_quark)
    _dsmeta_quark = g_quark_from_static_string (NVDS_META_STRING);
}

/* Function called when a property of the element is set. Standard boilerplate.
 */
static void
gst_dsexample_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (object);
  switch (prop_id) {
    case PROP_UNIQUE_ID:
      dsexample->unique_id = g_value_get_uint (value);
      break;
    case PROP_PROCESSING_WIDTH:
      dsexample->processing_width = g_value_get_int (value);
      break;
    case PROP_PROCESSING_HEIGHT:
      dsexample->processing_height = g_value_get_int (value);
      break;
    case PROP_PROCESS_VIEW:
      dsexample->process_view = g_value_get_int (value);
      break;
    case PROP_GPU_DEVICE_ID:
      dsexample->gpu_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Function called when a property of the element is requested. Standard
 * boilerplate.
 */
static void
gst_dsexample_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (object);

  switch (prop_id) {
    case PROP_UNIQUE_ID:
      g_value_set_uint (value, dsexample->unique_id);
      break;
    case PROP_PROCESSING_WIDTH:
      g_value_set_int (value, dsexample->processing_width);
      break;
    case PROP_PROCESSING_HEIGHT:
      g_value_set_int (value, dsexample->processing_height);
      break;
    case PROP_PROCESS_VIEW:
      g_value_set_int (value, dsexample->process_view);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, dsexample->gpu_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * Initialize all resources and start the output thread
 */
static gboolean
gst_dsexample_start (GstBaseTransform * btrans)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (btrans);
  NvBufSurfaceCreateParams create_params;
  DsExampleInitParams init_params =
      { dsexample->processing_width, dsexample->processing_height,
    dsexample->process_view
  };

  GstQuery *queryparams = NULL;
  guint batch_size = 1;
  int val = -1;

#ifdef WITH_OPENCV
  std::vector<cv::Point2f> calibration_dst(4);
  calibration_dst[0] = cv::Point2f(600,300);
  calibration_dst[1] = cv::Point2f(600,650);
  calibration_dst[2] = cv::Point2f(950,300);
  calibration_dst[3] = cv::Point2f(950,650);

  std::vector<cv::Point2f> calibration_src(4);
  if (dsexample->process_view == 1) {
    calibration_src[0] = cv::Point2f(998,323);
    calibration_src[1] = cv::Point2f(1098,690);
    calibration_src[2] = cv::Point2f(1334,276);
    calibration_src[3] = cv::Point2f(1498,607);
  } else {
    calibration_src[0] = cv::Point2f(751,644);
    calibration_src[1] = cv::Point2f(1120,762);
    calibration_src[2] = cv::Point2f(934,460);
    calibration_src[3] = cv::Point2f(1249,541);
  }
  dsexample->M = cv::getPerspectiveTransform(calibration_src, calibration_dst);
#endif

  /* Algorithm specific initializations and resource allocation. */
  dsexample->dsexamplelib_ctx = DsExampleCtxInit (&init_params);

  GST_DEBUG_OBJECT (dsexample, "ctx lib %p \n", dsexample->dsexamplelib_ctx);

  CHECK_CUDA_STATUS (cudaSetDevice (dsexample->gpu_id),
      "Unable to set cuda device");

  cudaDeviceGetAttribute (&val, cudaDevAttrIntegrated, dsexample->gpu_id);
  dsexample->is_integrated = val;

  dsexample->batch_size = 1;
  queryparams = gst_nvquery_batch_size_new ();
  if (gst_pad_peer_query (GST_BASE_TRANSFORM_SINK_PAD (btrans), queryparams)
      || gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (btrans), queryparams)) {
    if (gst_nvquery_batch_size_parse (queryparams, &batch_size)) {
      dsexample->batch_size = batch_size;
    }
  }
  GST_DEBUG_OBJECT (dsexample, "Setting batch-size %d \n",
      dsexample->batch_size);
  gst_query_unref (queryparams);

#ifndef WITH_OPENCV
  if (dsexample->process_view) {
    GST_ELEMENT_ERROR (dsexample, STREAM, FAILED,
          ("OpenCV has been deprecated, hence perspective transformation will not work."
          "Enable OpenCV compilation in gst-dsexample Makefile by setting 'WITH_OPENCV:=1"), (NULL));
    goto error;
  }
#endif

  CHECK_CUDA_STATUS (cudaStreamCreate (&dsexample->cuda_stream),
      "Could not create cuda stream");

  if (dsexample->inter_buf)
    NvBufSurfaceDestroy (dsexample->inter_buf);
  dsexample->inter_buf = NULL;

  /* An intermediate buffer for NV12/RGBA to BGR conversion  will be
   * required. Can be skipped if custom algorithm can work directly on NV12/RGBA. */
  create_params.gpuId  = dsexample->gpu_id;
  create_params.width  = dsexample->processing_width;
  create_params.height = dsexample->processing_height;
  create_params.size = 0;
  create_params.colorFormat = NVBUF_COLOR_FORMAT_RGBA;
  create_params.layout = NVBUF_LAYOUT_PITCH;

  if(dsexample->is_integrated) {
    create_params.memType = NVBUF_MEM_DEFAULT;
  }
  else {
    create_params.memType = NVBUF_MEM_CUDA_PINNED;
  }

  if (NvBufSurfaceCreate (&dsexample->inter_buf, 1,
          &create_params) != 0) {
    GST_ERROR ("Error: Could not allocate internal buffer for dsexample");
    goto error;
  }

  /* Create host memory for storing converted/scaled interleaved RGB data */
  CHECK_CUDA_STATUS (cudaMallocHost (&dsexample->host_rgb_buf,
          dsexample->processing_width * dsexample->processing_height *
          RGB_BYTES_PER_PIXEL), "Could not allocate cuda host buffer");

  GST_DEBUG_OBJECT (dsexample, "allocated cuda buffer %p \n",
      dsexample->host_rgb_buf);


#ifdef WITH_OPENCV

  /* CV Mat containing interleaved RGB data. This call does not allocate memory.
   * It uses host_rgb_buf as data. */
  dsexample->cvmat =
      new cv::Mat (dsexample->processing_height, dsexample->processing_width,
      CV_8UC3, dsexample->host_rgb_buf,
      dsexample->processing_width * RGB_BYTES_PER_PIXEL);

  if (!dsexample->cvmat)
    goto error;

  GST_DEBUG_OBJECT (dsexample, "created CV Mat\n");
#endif

  return TRUE;

error:
  if (dsexample->host_rgb_buf) {
    cudaFreeHost (dsexample->host_rgb_buf);
    dsexample->host_rgb_buf = NULL;
  }

  if (dsexample->cuda_stream) {
    cudaStreamDestroy (dsexample->cuda_stream);
    dsexample->cuda_stream = NULL;
  }
  if (dsexample->dsexamplelib_ctx)
    DsExampleCtxDeinit (dsexample->dsexamplelib_ctx);
  return FALSE;
}

/**
 * Stop the output thread and free up all the resources
 */
static gboolean
gst_dsexample_stop (GstBaseTransform * btrans)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (btrans);

  if (dsexample->inter_buf)
    NvBufSurfaceDestroy(dsexample->inter_buf);
  dsexample->inter_buf = NULL;

  if (dsexample->cuda_stream)
    cudaStreamDestroy (dsexample->cuda_stream);
  dsexample->cuda_stream = NULL;

#ifdef WITH_OPENCV
  delete dsexample->cvmat;
  dsexample->cvmat = NULL;
#endif

  if (dsexample->host_rgb_buf) {
    cudaFreeHost (dsexample->host_rgb_buf);
    dsexample->host_rgb_buf = NULL;
  }

  GST_DEBUG_OBJECT (dsexample, "deleted CV Mat \n");

  /* Deinit the algorithm library */
  DsExampleCtxDeinit (dsexample->dsexamplelib_ctx);
  dsexample->dsexamplelib_ctx = NULL;

  GST_DEBUG_OBJECT (dsexample, "ctx lib released \n");

  return TRUE;
}

/**
 * Called when source / sink pad capabilities have been negotiated.
 */
static gboolean
gst_dsexample_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (btrans);
  /* Save the input video information, since this will be required later. */
  gst_video_info_from_caps (&dsexample->video_info, incaps);

  if (dsexample->process_view) {
    /* requires RGBA format for transform the view in opencv */
     if (dsexample->video_info.finfo->format != GST_VIDEO_FORMAT_RGBA) {
      GST_ELEMENT_ERROR (dsexample, STREAM, FAILED,
          ("input format should be RGBA when performing perspective transformation!"), (NULL));
      goto error;
      }
  }

  return TRUE;

error:
  return FALSE;
}

/**
 * Called when element recieves an input buffer from upstream element.
 */
static GstFlowReturn
gst_dsexample_transform_ip (GstBaseTransform * btrans, GstBuffer * inbuf)
{
  GstDsExample *dsexample = GST_DSEXAMPLE (btrans);
  GstMapInfo in_map_info;
  GstFlowReturn flow_ret = GST_FLOW_ERROR;
  gdouble scale_ratio = 1.0;
  DsExampleOutput *output;

  NvBufSurface *surface = NULL;
  NvDsBatchMeta *batch_meta = NULL;
  NvDsFrameMeta *frame_meta = NULL;
  NvDsMetaList * l_frame = NULL;
  guint i = 0;

  dsexample->frame_num++;
  CHECK_CUDA_STATUS (cudaSetDevice (dsexample->gpu_id),
      "Unable to set cuda device");

  memset (&in_map_info, 0, sizeof (in_map_info));
  if (!gst_buffer_map (inbuf, &in_map_info, GST_MAP_READ)) {
    g_print ("Error: Failed to map gst buffer\n");
    goto error;
  }

  nvds_set_input_system_timestamp (inbuf, GST_ELEMENT_NAME (dsexample));
  surface = (NvBufSurface *) in_map_info.data;
  GST_DEBUG_OBJECT (dsexample,
      "Processing Frame %" G_GUINT64_FORMAT " Surface %p\n",
      dsexample->frame_num, surface);

  if (CHECK_NVDS_MEMORY_AND_GPUID (dsexample, surface))
    goto error;

  batch_meta = gst_buffer_get_nvds_batch_meta (inbuf);
  if (batch_meta == nullptr) {
    GST_ELEMENT_ERROR (dsexample, STREAM, FAILED,
        ("NvDsBatchMeta not found for input buffer."), (NULL));
    return GST_FLOW_ERROR;
  }

  for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
    l_frame = l_frame->next)
  {
    frame_meta = (NvDsFrameMeta *) (l_frame->data);

    #ifdef WITH_OPENCV
      cv::Mat src;
      /* Map the buffer so that it can be accessed by CPU */
      if (surface->surfaceList[frame_meta->batch_id].mappedAddr.addr[0] == NULL){
        if (NvBufSurfaceMap (surface, frame_meta->batch_id, 0, NVBUF_MAP_READ_WRITE) != 0){
          GST_ELEMENT_ERROR (dsexample, STREAM, FAILED,
              ("%s:buffer map to be accessed by CPU failed", __func__), (NULL));
          return GST_FLOW_ERROR;
        }
      }

      src =
          cv::Mat (surface->surfaceList[frame_meta->batch_id].planeParams.height[0],
          surface->surfaceList[frame_meta->batch_id].planeParams.width[0], CV_8UC4,
          surface->surfaceList[frame_meta->batch_id].mappedAddr.addr[0],
          surface->surfaceList[frame_meta->batch_id].planeParams.pitch[0]);
      /* Uncomment for DEBUG */
      // cv::imwrite("/home/jl5999/Dev/project/out/screenshots/frame_raw_" + std::to_string(dsexample->frame_num) + std::string(".jpg"), src);

      cv::Mat dst = cv::Mat::zeros(cv::Size(1920, 1080), CV_8UC4);

      // cv::Mat M = cv::getPerspectiveTransform(calibration_src, calibration_dst);
      cv::Size warpped_size = cv::Size(1920, 1080);
      cv::warpPerspective(src, dst, dsexample->M, warpped_size);

      /* Uncomment for DEBUG */
      // cv::imwrite("/home/jl5999/Dev/project/out/screenshots/frame_trans_" + std::to_string(dsexample->frame_num) + std::string(".jpg"), dst);

      cv::rectangle(dst, cv::Point(0, 0), cv::Point(500, 300), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(500, 0), cv::Point(600, 200), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(0, 650), cv::Point(500, 1080), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(500, 750), cv::Point(600, 1080), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(950, 0), cv::Point(1920, 200), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(1050, 200), cv::Point(1920, 300), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(950, 750), cv::Point(1920, 1080), cv::Scalar(0, 0, 0, 1), -1);
      cv::rectangle(dst, cv::Point(1050, 650), cv::Point(1920, 750), cv::Scalar(0, 0, 0, 1), -1);

      
      if (dsexample->process_view == 1) {
        cv::rectangle(dst, cv::Point(0, 0), cv::Point(397, 1080), cv::Scalar(0, 0, 0, 1), -1);
      } else {
        cv::rectangle(dst, cv::Point(0, 830), cv::Point(1920, 1080), cv::Scalar(0, 0, 0, 1), -1);
      }

      dst.copyTo(src);

      /* Uncomment for DEBUG */
      // cv::imwrite("/home/jl5999/Dev/project/out/screenshots/frame_final_" + std::to_string(dsexample->frame_num) + std::string(".jpg"), dst);

    #endif 
  }
  flow_ret = GST_FLOW_OK;

error:
  nvds_set_output_system_timestamp (inbuf, GST_ELEMENT_NAME (dsexample));
  gst_buffer_unmap (inbuf, &in_map_info);
  return flow_ret;
}


/**
 * Boiler plate for registering a plugin and an element.
 */
static gboolean
dsexample_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dsexample_debug, "dsexample", 0,
      "dsexample plugin");

  return gst_element_register (plugin, "dsexample", GST_RANK_PRIMARY,
      GST_TYPE_DSEXAMPLE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdsgst_dsexample,
    DESCRIPTION, dsexample_plugin_init, "6.1", LICENSE, BINARY_PACKAGE, URL)
