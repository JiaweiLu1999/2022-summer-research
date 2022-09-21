# Real-time Object Detection and Tracking Based on Nvidia DeepStream

- Author: Jiawei Lu (jl5999)
- Email: jl5999 [at] columbia.edu
- Report: [[LINK](https://docs.google.com/document/d/1zvpGrAKTPrgvHFKUqXHDK5PIb31Xp3hgopMYRiEG56Q/edit)]
- Slides: [[LINK](https://docs.google.com/presentation/d/1HCiPz6JyAjWUuo5c4ipyERsox3h_iyqcIt_HmRGra3A/)]

## Introduction
This project can be summarized as follows:
- Set up a GNU/Linux server located at Columbia University.
- Deploy the YOLOv4 model on DeepStream.
- Build a Python application to realize the real-time detection and tracking pipeline.
- Implemented a C++ Gstreamer plugin to perform perspective transformation and mask out the background.

The experiment shows that our model achieves 0.82 AP for vehicle detection, 0.51 AP for pedestrian detection, and 0.67 mAP. 

The application is able to run 5 frames-per-second on our Linux server with a single Nivida Titan X GPU.

## Set up the Linux server
After updating the operating system from Ubuntu 16.04 LTS to Ubuntu 20.04 LTS, setting up the SSH connection, and installing the software required for our experiment, we successfully built a GNU/Linux server with the following features:
- Hardware Information:
    - CPU: 8 × Intel(R) Xeon(R) CPU E5-2603 v2 @ 1.80GHz
    - GPU: 1 × Nvidia GeForce Titan X @ 12GB
- Software Information:
    - Operating System: Ubuntu 20.04 LTS
    - DeepStream Version: 6.1
    - Nvidia Driver Version:   510.47.03
    - CUDA Version: 11.6

## Deploy YOLOv4 model with DeepStream
Details are in the README.md of the folder [deepstream_app](/deepstream_app/).

## Build a Python application to realize the real-time detection and tracking pipeline
Details are in the README.md of the folder [python_app](/python_app/)

## Implemented a C++ Gstreamer plugin to perform perspective transformation and background subtraction
Details are in the README.md of the folder [calibration_plugin](/calibration_plugin/)

## Acknowledgement
Thanks to Prof. Zoran Kostic and Prof. Gil Zussman’s instruction, Mahshid Ghasemi Dehkordi, Zhengye Yang, and Mehmet Kerem Turkcan’s help.

## References
[1] Shiyun Yang, Emily Bailey, Zhengye Yang, Jonatan Ostrometzky, Gil Zussman, Ivan Seskar, Zoran Kostic, “COSMOS Smart Intersection: Edge Compute and Communications for Bird’s Eye Object Tracking,” IEEE Percom – SmartEdge 2020, 4th International Workshop on Smart Edge Computing and Networking, Mar. 2020.

[2] A. Angus, Z. Duan, G. Zussman, and Z. Kostic, “Real-Time Video Anonymization in Smart City Intersections,” in Proc. IEEE MASS’22 (invited), Oct. 2022.

[3] Zhuoxu Duan, Zhengye Yang, Richard Samoilenko, Dwiref Snehal Oza, Ashvin Jagadeesan, Mingfei Sun, Hongzhe Ye, Zihao Xiong, Gil Zussman, Zoran Kostic,” Smart City Traffic Intersection: Impact of Video Quality and Scene Complexity on Precision and Inference,” in Proc. 19th IEEE International Conference on Smart City, Dec. 2021.

[4] D. Raychaudhuri, I. Seskar, G. Zussman, T. Korakis, D. Kilper, T. Chen, J. Kolodziejski, M. Sherman, Z. Kostic, X. Gu, H. Krishnaswamy, S. Maheshwari, P. Skrimponis, and C. Gutterman, “Challenge: COSMOS:
A city-scale programmable testbed for experimentation with advanced wireless,” in Proc. ACM MobiCom’20, 2020.

[5] [NVIDIA DeepStream SDK](https://docs.nvidia.com/metropolis/deepstream/dev-guide/)


[6] [DeepStream Graph Architecture](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_Overview.html#deepstream-graph-architecture)

[7] Bochkovskiy, Alexey, Chien-Yao Wang, and Hong Yuan Mark Liao. "Yolov4: Optimal speed and accuracy of object detection." arXiv preprint arXiv:2004.10934 (2020).

[8] [GStreamer Tutorials](https://gstreamer.freedesktop.org/documentation/tutorials/index.html?gi-language=c)


