#!/usr/bin/env python3

import sys
import numpy as np
from common.bus_call import bus_call
from common.FPS import PERF_DATA
import pyds
import platform
import math
import time
import gi
import ctypes
gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst, GObject

# Defining the Class Labels
PGIE_CLASS_ID_PERSON = 0
PGIE_CLASS_ID_VEHICLE = 1
PGIE_CLASS_ID_CLASS3 = 2
past_tracking_meta=[0]

# Defining the input output source
INPUT_URI_AMS  = "rtsp://admin:mudd12a2@10.60.144.185:554/Streaming/Channels/1"
OUTPUT_PATH_AMS = "/home/jl5999/Dev/project/out_0817/out_ams_0817_1080_4.mp4"
INPUT_URI_120  = "rtsp://admin:mudd12a1@10.60.61.6:554/Streaming/Channels/1"
OUTPUT_PATH_120 = "/home/jl5999/Dev/project/out_0817/out_120_0817_1080_7.mp4"

## Make Element or Print Error and any other detail
def make_elm_or_print_err(factoryname, name, printedname, detail=""):
    print("Creating", printedname)
    elm = Gst.ElementFactory.make(factoryname, name)
    if not elm:
        sys.stderr.write("Unable to create " + printedname + " \n")
    if detail:
        sys.stderr.write(detail)
    return elm

def cb_newpad(decodebin, decoder_src_pad, data):
    print("In cb_newpad\n")
    caps = decoder_src_pad.get_current_caps()
    gststruct = caps.get_structure(0)
    gstname = gststruct.get_name()
    source_bin = data
    features = caps.get_features(0)

    print(caps.get_structure(0).get_value('format'))
    print(caps.get_structure(0).get_value('height'))
    print(caps.get_structure(0).get_value('width'))
    print(caps.get_structure(0).get_value('framerate'))

    # Need to check if the pad created by the decodebin is for video and not
    # audio.
    print("gstname=", gstname)
    if gstname.find("video") != -1:
        # Link the decodebin pad only if decodebin has picked nvidia
        # decoder plugin nvdec_*. We do this by checking if the pad caps contain
        # NVMM memory features.
        print("features=", features)
        if features.contains("memory:NVMM"):
            # Get the source bin ghost pad
            bin_ghost_pad = source_bin.get_static_pad("src")
            if not bin_ghost_pad.set_target(decoder_src_pad):
                sys.stderr.write(
                    "Failed to link decoder src pad to source bin ghost pad\n"
                )
        else:
            sys.stderr.write(
                " Error: Decodebin did not pick nvidia decoder plugin.\n")

def decodebin_child_added(child_proxy, Object, name, user_data):
    print("Decodebin child added:", name, "\n")
    if name.find("source") != -1:
        # Object.set_property("do-rtsp-keep-alive", True)
        Object.set_property("protocols", "tcp")
    if name.find("decodebin") != -1:
        Object.connect("child-added", decodebin_child_added, user_data)

############## Working with the Metadata ################

def osd_sink_pad_buffer_probe(pad,info,u_data):
    
    #Intiallizing object counter with 0.
    obj_counter = {
        PGIE_CLASS_ID_VEHICLE:0,
        PGIE_CLASS_ID_PERSON:0,
        PGIE_CLASS_ID_CLASS3:0
    }
    # Set frame_number & rectangles to draw as 0 
    frame_number=0
    num_rects=0
    
    gst_buffer = info.get_buffer()

    if not gst_buffer:
        print("Unable to get GstBuffer ")
        return

    # Retrieve batch metadata from the gst_buffer
    # Note that pyds.gst_buffer_get_nvds_batch_meta() expects the
    # C address of gst_buffer as input, which is obtained with hash(gst_buffer)
    batch_meta = pyds.gst_buffer_get_nvds_batch_meta(hash(gst_buffer))
    l_frame = batch_meta.frame_meta_list
    while l_frame is not None:
        try:
            # Note that l_frame.data needs a cast to pyds.NvDsFrameMeta
            frame_meta = pyds.NvDsFrameMeta.cast(l_frame.data)
        except StopIteration:
            break
        
        # Get frame number , number of rectables to draw and object metadata
        frame_number=frame_meta.frame_num
        num_rects = frame_meta.num_obj_meta
        l_obj=frame_meta.obj_meta_list
        
        while l_obj is not None:
            try:
                # Casting l_obj.data to pyds.NvDsObjectMeta
                obj_meta=pyds.NvDsObjectMeta.cast(l_obj.data)
            except StopIteration:
                break
            # Increment Object class by 1 and Set Box border to Red color     
            obj_counter[obj_meta.class_id] += 1
            obj_meta.rect_params.border_color.set(0.0, 0.0, 1.0, 0.0)
            try: 
                l_obj=l_obj.next
            except StopIteration:
                break
        ################## Setting Metadata Display configruation ############### 
        # Acquiring a display meta object.
        display_meta=pyds.nvds_acquire_display_meta_from_pool(batch_meta)
        display_meta.num_labels = 1
        py_nvosd_text_params = display_meta.text_params[0]
        # Setting display text to be shown on screen
        py_nvosd_text_params.display_text = "Frame Number={} Number of Objects={} Vehicle_count={} Person_count={}".format(frame_number, num_rects, obj_counter[PGIE_CLASS_ID_VEHICLE], obj_counter[PGIE_CLASS_ID_PERSON])
        # Now set the offsets where the string should appear
        py_nvosd_text_params.x_offset = 10
        py_nvosd_text_params.y_offset = 12
        # Font , font-color and font-size
        py_nvosd_text_params.font_params.font_name = "Serif"
        py_nvosd_text_params.font_params.font_size = 10
        # Set(red, green, blue, alpha); Set to White
        py_nvosd_text_params.font_params.font_color.set(1.0, 1.0, 1.0, 1.0)
        # Text background color
        py_nvosd_text_params.set_bg_clr = 1
        # Set(red, green, blue, alpha); set to Black
        py_nvosd_text_params.text_bg_clr.set(0.0, 0.0, 0.0, 1.0)
        # Using pyds.get_string() to get display_text as string to print in notebook
        print(pyds.get_string(py_nvosd_text_params.display_text))
        pyds.nvds_add_display_meta_to_frame(frame_meta, display_meta)

        ############################################################################
        
        global perf_data
        perf_data.update_fps("stream0")

        try:
            l_frame=l_frame.next
        except StopIteration:
            break
    return Gst.PadProbeReturn.OK

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr("Please input: 1 - Amsterdam, 2 - 120St")
    
    if sys.argv[1] == '1':
        uri = INPUT_URI_AMS
        output_path = OUTPUT_PATH_AMS
    elif sys.argv[1] == '2':
        uri = INPUT_URI_120
        output_path = OUTPUT_PATH_120
    else:
        sys.stderr("Please input: 1 - Amsterdam, 2 - 120St")
    
    global perf_data
    perf_data = PERF_DATA(1)

    # Standard GStreamer initialization
    Gst.init(None)

    # Create Pipeline element that will form a connection of other elements
    print("Creating Pipeline \n ")
    pipeline = Gst.Pipeline()
    if not pipeline:
        sys.stderr.write(" Unable to create Pipeline \n")

    # Create nvstreammux instance to form batches from one or more sources.
    streammux = make_elm_or_print_err("nvstreammux", "Stream-muxer",'NvStreamMux')

    # Create a source GstBin to abstract this bin's content from the rest of the
    # pipeline
    print("Creating source bin")
    bin_name = "source-bin-%02d" % 0
    nbin = Gst.Bin.new(bin_name)
    if not nbin:
        sys.stderr.write(" Unable to create source bin \n")
    # Source element for reading from the uri.
    # We will use decodebin and let it figure out the container format of the
    # stream and the codec and plug the appropriate demux and decode plugins.
    uri_decode_bin = make_elm_or_print_err("uridecodebin", "uri-decode-bin", "uridecodebin")
    uri_decode_bin.set_property("uri", uri)

    # Connect to the "pad-added" signal of the decodebin which generates a
    # callback once a new pad for raw data has beed created by the decodebin
    uri_decode_bin.connect("pad-added", cb_newpad, nbin)
    uri_decode_bin.connect("child-added", decodebin_child_added, nbin)
    # We need to create a ghost pad for the source bin which will act as a proxy
    # for the video decoder src pad. The ghost pad will not have a target right
    # now. Once the decode bin creates the video decoder and generates the
    # cb_newpad callback, we will set the ghost pad target to the video decoder
    # src pad.
    Gst.Bin.add(nbin, uri_decode_bin)
    bin_pad = nbin.add_pad(Gst.GhostPad.new_no_target("src", Gst.PadDirection.SRC))
    if not bin_pad:
        sys.stderr.write(" Failed to add ghost pad in source bin \n")
    source_bin = nbin
    if not source_bin:
        sys.stderr.write("Unable to create source bin \n")
    padname = "sink_%u" % 0
    sinkpad = streammux.get_request_pad(padname)
    if not sinkpad:
        sys.stderr.write("Unable to create sink pad bin \n")
    srcpad = source_bin.get_static_pad("src")

    if not srcpad:
        sys.stderr.write("Unable to create src pad bin \n")

    # Add nvvideoconverter and capsfilter to transform N12 to RGBA
    nvvidconv = make_elm_or_print_err("nvvideoconvert", "convertor","nvvidconv")
    nvvidconv2 = make_elm_or_print_err("nvvideoconvert", "convertor2","nvvidconv2")
    caps = make_elm_or_print_err("capsfilter", "capsfilter", "capsfilter")
    # Use dsexample pluginin to perform perspective transformation
    dsexample = make_elm_or_print_err("dsexample", "ds-example", "dsexample")
    # Use nvinfer to run inferencing on decoder's output,behaviour of inferencing is set through config file
    pgie = make_elm_or_print_err("nvinfer", "primary-inference" ,"pgie")
    # Add tracker
    tracker = make_elm_or_print_err("nvtracker", "tracker", "tracker")
    # Create OSD to draw on the converted RGBA buffer
    nvosd = make_elm_or_print_err("nvdsosd", "onscreendisplay","nvosd")
    # Finally encode and save the osd output
    queue = make_elm_or_print_err("queue", "queue", "Queue")
    # Place an encoder instead of OSD to save as video file
    encoder = make_elm_or_print_err("avenc_mpeg4", "encoder", "Encoder")
    # Parse output from Encoder 
    codeparser = make_elm_or_print_err("mpeg4videoparse", "mpeg4-parser", 'Code Parser')
    # Create a container
    container = make_elm_or_print_err("matroskamux", "qtmux", "Container")
    # Create Sink for storing the output 
    sink = make_elm_or_print_err("filesink", "filesink", "Sink")

    

    ############ Set properties for the Elements ############
    print("Playing file ",uri)
    # Set Input Width , Height and Batch Size 
    streammux.set_property('width', 1920)
    streammux.set_property('height', 1080)
    streammux.set_property('batch-size', 1)
    # Timeout in microseconds to wait after the first buffer is available 
    # to push the batch even if a complete batch is not formed.
    streammux.set_property('batched-push-timeout', 4000000)
    # Set the mem-type of the RGBA output buffer, required by dsexample plugin
    nvvidconv.set_property("nvbuf-memory-type", 1)
    # Set the capsfilter format
    caps.set_property("caps", Gst.Caps.from_string("video/x-raw(memory:NVMM), format=RGBA"))
    # Align the perspective view
    dsexample.set_property('view', int(sys.argv[1]))
    # Set Congifuration file for nvinfer 
    pgie.set_property('config-file-path', "/home/jl5999/Dev/project/configs/tensorrt_engine_config_yolov4_zhengye.txt")
    # Set the tracker properties
    tracker.set_property('tracker-width', 640)
    tracker.set_property('tracker-height', 384)
    tracker.set_property('ll-lib-file', "/opt/nvidia/deepstream/deepstream-6.1/lib/libnvds_nvmultiobjecttracker.so")
    tracker.set_property('enable_batch_process', 1)
    # Set Encoder bitrate for output video
    encoder.set_property("bitrate", 4000000)
    # Set Output file name and disable sync and async
    sink.set_property("location", output_path)
    sink.set_property("sync", 0)
    sink.set_property("async", 0)


    ########## Add and Link ELements in the Pipeline ########## 

    print("Adding elements to Pipeline \n")
    pipeline.add(source_bin)
    pipeline.add(streammux)
    pipeline.add(nvvidconv)
    pipeline.add(nvvidconv2)
    pipeline.add(caps)
    pipeline.add(dsexample)
    pipeline.add(pgie)
    pipeline.add(tracker)
    pipeline.add(nvosd)
    pipeline.add(queue)
    pipeline.add(encoder)
    pipeline.add(codeparser)
    pipeline.add(container)
    pipeline.add(sink)



    # We now  link the elements together 
    # rtsp-source -> srcpad -> sinkpad -> streammux -> nvvidconv -> capsfilter -> nvinfer -> nvtracker -> nvosd ->
    # queue -> encoder -> parser -> container -> sink -> output-file
    print("Linking elements in the Pipeline \n")
    srcpad.link(sinkpad)
    streammux.link(nvvidconv) # pgie -> nvinfer
    nvvidconv.link(caps)
    caps.link(dsexample)
    dsexample.link(pgie)
    pgie.link(tracker)
    tracker.link(nvosd)
    nvosd.link(queue)
    queue.link(nvvidconv2)
    nvvidconv2.link(encoder)
    encoder.link(codeparser)
    codeparser.link(container)
    container.link(sink)



    # create an event loop and feed gstreamer bus mesages to it
    loop = GLib.MainLoop()
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect ("message", bus_call, loop)

    # Lets add probe to get informed of the meta data generated, we add probe to the sink pad  
    # of the osd element, since by that time, the buffer would have had got all the metadata.
    osdsinkpad = nvosd.get_static_pad("sink")
    if not osdsinkpad:
        sys.stderr.write(" Unable to get sink pad of nvosd \n")
    osdsinkpad.add_probe(Gst.PadProbeType.BUFFER, osd_sink_pad_buffer_probe, 0)
    GLib.timeout_add(5000, perf_data.perf_print_callback)

    # start play back and listen to events
    print("Starting pipeline \n")
    pipeline.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    except BaseException:
        pipeline.set_state(Gst.State.NULL)
    # cleanup
    pipeline.set_state(Gst.State.NULL)
    
    






