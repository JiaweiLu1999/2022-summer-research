# Build a Python application to realize the real-time detection and tracking pipeline

## Application Workflow
rtsp-source -> srcpad -> sinkpad -> streammux -> nvvidconv -> capsfilter -> nvinfer -> nvtracker -> nvosd ->
queue -> encoder -> parser -> container -> sink -> output-file

## Usage
Run with `$ sudo python3 realtime_main.py`

## Results
1. Detection and tracking results of the YOLOv4 model pretrained on the COCO dataset.

<p>
  <img alt="Amsterdam" src="./results/app_ams_coco.png" width="300">
  <img alt="120St" src="./results/app_120_coco.png" width="300">
</p>

2. Detection and tracking results of Zhengye’s YOLOv4 model.

<p>
  <img alt="Amsterdam" src="./results/app_ams_zy.png" width="300">
  <img alt="120St" src="./results/app_120_zy.png" width="300">
</p>

3. Detection and tracking results of Zhengye’s YOLOv4 model after addding GStreamer Plugin.

<p>
  <img alt="Amsterdam" src="./results/app_ams_cal_1.png" width="300">
  <img alt="Amsterdam" src="./results/app_ams_cal_2.png" width="300">
</p>

<p>
  <img alt="120St" src="./results/app_120_cal_1.png" width="300">
  <img alt="120St" src="./results/app_120_cal_2.png" width="300">
</p>