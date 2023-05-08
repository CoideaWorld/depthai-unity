/**
* This file contains face detector pipeline and interface for Unity scene called "Face Detector"
* Main goal is to perform face detection + depth
*/

#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wdouble-promotion"

#if _MSC_VER // this is defined when compiling with Visual Studio
#define EXPORT_API __declspec(dllexport) // Visual Studio needs annotating exported functions with this
#else
#define EXPORT_API // XCode does not need annotating exported functions, so define is empty
#endif

// ------------------------------------------------------------------------
// Plugin itself

#include <iostream>
#include <cstdio>
#include <random>

#include "../utility.hpp"

// Common necessary includes for development using depthai library
#include "depthai/depthai.hpp"
#include "depthai/device/Device.hpp"

#include "depthai-unity/predefined/FaceDetector.hpp"

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#include "nlohmann/json.hpp"

/**
* Pipeline creation based on streams template
*
* @param config pipeline configuration 
* @returns pipeline 
*/
dai::Pipeline createFaceDetectorPipeline(PipelineConfig *config)
{
    dai::Pipeline pipeline;
    std::shared_ptr<dai::node::XLinkOut> xlinkOut;

    auto colorCam = pipeline.create<dai::node::ColorCamera>();

    // Color camera preview
    if (config->previewSizeWidth > 0 && config->previewSizeHeight > 0) 
    {
        xlinkOut = pipeline.create<dai::node::XLinkOut>();
        xlinkOut->setStreamName("preview");
        colorCam->setPreviewSize(config->previewSizeWidth, config->previewSizeHeight);
        colorCam->preview.link(xlinkOut->input);
    }

    // Color camera properties            
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    if (config->colorCameraResolution == 1) colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_4_K);
    if (config->colorCameraResolution == 2) colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_12_MP);
    if (config->colorCameraResolution == 3) colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_13_MP);
    colorCam->setInterleaved(config->colorCameraInterleaved);
    colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::BGR);
    if (config->colorCameraColorOrder == 1) colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::RGB);
    colorCam->setFps(config->colorCameraFPS);

    auto maxFrameSize = colorCam->getPreviewHeight() * colorCam->getPreviewHeight() * 3;
    
    auto manip1 = pipeline.create<dai::node::ImageManip>();
    manip1->initialConfig.setCropRect(0, 0, 0.5, 1);
    // Flip functionality
    //manip1->initialConfig.setHorizontalFlip(true);
    manip1->setMaxOutputFrameSize(maxFrameSize);
    colorCam->preview.link(manip1->inputImage);

    auto manip2 = pipeline.create<dai::node::ImageManip>();
    manip2->initialConfig.setCropRect(0.5, 0, 1, 1);
    // Flip functionality
    //manip2->initialConfig.setVerticalFlip(true);
    manip2->setMaxOutputFrameSize(maxFrameSize);
    colorCam->preview.link(manip2->inputImage);


    // neural network
    auto nn1 = pipeline.create<dai::node::NeuralNetwork>();
    nn1->setBlobPath(config->nnPath1);
    //colorCam->preview.link(nn1->input);
    manip1->out.link(nn1->input);
    
    //nn1->passthrough.link(xlinkOut->input);
    
    // neural network
    auto nn2 = pipeline.create<dai::node::NeuralNetwork>();
    nn2->setBlobPath(config->nnPath1);
    //colorCam->preview.link(nn1->input);
    manip2->out.link(nn2->input);

    //nn2->passthrough.link(xlinkOut->input);

    // output of neural network
    auto nnOut = pipeline.create<dai::node::XLinkOut>();
    nnOut->setStreamName("detections");    
    nn1->out.link(nnOut->input);

    // output of neural network
    auto nnOut2 = pipeline.create<dai::node::XLinkOut>();
    nnOut2->setStreamName("detections2");    
    nn2->out.link(nnOut2->input);


    // Depth
    if (config->confidenceThreshold > 0)
    {
        auto left = pipeline.create<dai::node::MonoCamera>();
        auto right = pipeline.create<dai::node::MonoCamera>();
        auto stereo = pipeline.create<dai::node::StereoDepth>();

        // For RGB-Depth align
        if (config->ispScaleF1 > 0 && config->ispScaleF2 > 0) colorCam->setIspScale(config->ispScaleF1, config->ispScaleF2);
        if (config->manualFocus > 0) colorCam->initialControl.setManualFocus(config->manualFocus);

        // Mono camera properties    
        left->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        if (config->monoLCameraResolution == 1) left->setResolution(dai::MonoCameraProperties::SensorResolution::THE_720_P);
        if (config->monoLCameraResolution == 2) left->setResolution(dai::MonoCameraProperties::SensorResolution::THE_800_P);
        if (config->monoLCameraResolution == 3) left->setResolution(dai::MonoCameraProperties::SensorResolution::THE_480_P);
        left->setBoardSocket(dai::CameraBoardSocket::LEFT);
        right->setResolution(dai::MonoCameraProperties::SensorResolution::THE_400_P);
        if (config->monoRCameraResolution == 1) right->setResolution(dai::MonoCameraProperties::SensorResolution::THE_720_P);
        if (config->monoRCameraResolution == 2) right->setResolution(dai::MonoCameraProperties::SensorResolution::THE_800_P);
        if (config->monoRCameraResolution == 3) right->setResolution(dai::MonoCameraProperties::SensorResolution::THE_480_P);
        right->setBoardSocket(dai::CameraBoardSocket::RIGHT);

        // Stereo properties
        stereo->setConfidenceThreshold(config->confidenceThreshold);
        // LR-check is required for depth alignment
        stereo->setLeftRightCheck(config->leftRightCheck);
        if (config->depthAlign > 0) stereo->setDepthAlign(dai::CameraBoardSocket::RGB);
        stereo->setSubpixel(config->subpixel);
        
        stereo->initialConfig.setMedianFilter(dai::MedianFilter::MEDIAN_OFF);
        if (config->medianFilter == 1) stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_3x3);
        if (config->medianFilter == 2) stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_5x5);
        if (config->medianFilter == 3) stereo->initialConfig.setMedianFilter(dai::MedianFilter::KERNEL_7x7);

        // Linking
        left->out.link(stereo->left);
        right->out.link(stereo->right);
        auto xoutDepth = pipeline.create<dai::node::XLinkOut>();            
        xoutDepth->setStreamName("depth");
        stereo->depth.link(xoutDepth->input);
    }

    // SYSTEM INFORMATION
    if (config->rate > 0.0f)
    {
        // Define source and output
        auto sysLog = pipeline.create<dai::node::SystemLogger>();
        auto xout = pipeline.create<dai::node::XLinkOut>();

        xout->setStreamName("sysinfo");

        // Properties
        sysLog->setRate(config->rate);  // 1 hz updates

        // Linking
        sysLog->out.link(xout->input);
    }

    // IMU
    if (config->freq > 0)
    {
        auto imu = pipeline.create<dai::node::IMU>();
        auto xlinkOutImu = pipeline.create<dai::node::XLinkOut>();

        xlinkOutImu->setStreamName("imu");

        // enable ROTATION_VECTOR at 400 hz rate
        imu->enableIMUSensor(dai::IMUSensor::ROTATION_VECTOR, config->freq);
        // above this threshold packets will be sent in batch of X, if the host is not blocked and USB bandwidth is available
        imu->setBatchReportThreshold(config->batchReportThreshold);
        // maximum number of IMU packets in a batch, if it's reached device will block sending until host can receive it
        // if lower or equal to batchReportThreshold then the sending is always blocking on device
        // useful to reduce device's CPU load  and number of lost packets, if CPU load is high on device side due to multiple nodes
        imu->setMaxBatchReports(config->maxBatchReports);

        // Link plugins IMU -> XLINK
        imu->out.link(xlinkOutImu->input);
    }

    return pipeline;
}

extern "C"
{
   /**
    * Pipeline creation based on streams template
    *
    * @param config pipeline configuration 
    * @returns pipeline 
    */
    EXPORT_API bool InitFaceDetector(PipelineConfig *config)
    {
        dai::Pipeline pipeline = createFaceDetectorPipeline(config);

        // If deviceId is empty .. just pick first available device
        bool res = false;

        if (strcmp(config->deviceId,"NONE")==0 || strcmp(config->deviceId,"")==0) res = DAIStartPipeline(pipeline,config->deviceNum,NULL);        
        else res = DAIStartPipeline(pipeline,config->deviceNum,config->deviceId);
        
        return res;
    }

    /**
    * Pipeline results
    *
    * @param frameInfo camera images pointers
    * @param getPreview True if color preview image is requested, False otherwise. Requires previewSize in pipeline creation.
    * @param useDepth True if depth information is requested, False otherwise. Requires confidenceThreshold in pipeline creation.
    * @param retrieveInformation True if system information is requested, False otherwise. Requires rate in pipeline creation.
    * @param useIMU True if IMU information is requested, False otherwise. Requires freq in pipeline creation.
    * @param deviceNum Device selection on unity dropdown
    * @returns Json with results or information about device availability. 
    */    

    /**
    * Example of json returned
    * { "faces": [ {"label":0,"score":0.0,"xmin":0.0,"ymin":0.0,"xmax":0.0,"ymax":0.0,"xcenter":0.0,"ycenter":0.0},{"label":1,"score":1.0,"xmin":0.0,"ymin":0.0,"xmax":0.0,* "ymax":0.0,"xcenter":0.0,"ycenter":0.0}],"best":{"label":1,"score":1.0,"xmin":0.0,"ymin":0.0,"xmax":0.0,"ymax":0.0,"xcenter":0.0,"ycenter":0.0},"fps":0.0}
    */

    EXPORT_API const char* FaceDetectorResults(FrameInfo *frameInfo, bool getPreview, bool drawBestFaceInPreview, bool drawAllFacesInPreview, float faceScoreThreshold, bool useDepth, bool retrieveInformation, bool useIMU, int deviceNum)
    {
        using namespace std;
        using namespace std::chrono;

        // Get device deviceNum
        std::shared_ptr<dai::Device> device = GetDevice(deviceNum);
        // Device no available
        if (device == NULL) 
        {
            char* ret = (char*)::malloc(strlen("{\"error\":\"NO_DEVICE\"}"));
            ::memcpy(ret, "{\"error\":\"NO_DEVICE\"}",strlen("{\"error\":\"NO_DEVICE\"}"));
            ret[strlen("{\"error\":\"NO_DEVICE\"}")] = 0;
            return ret;
        }

        // If device deviceNum is running pipeline
        if (IsDeviceRunning(deviceNum))
        {
            // preview image
            cv::Mat frame;
            std::shared_ptr<dai::ImgFrame> imgFrame;

            // other images
            cv::Mat depthFrame, depthFrameOrig, dispFrameOrig, dispFrame, monoRFrameOrig, monoRFrame, monoLFrameOrig, monoLFrame;
            
            // face info
            nlohmann::json faceDetectorJson = {};

            std::shared_ptr<dai::DataOutputQueue> preview;
            std::shared_ptr<dai::DataOutputQueue> depthQueue;
            
            // face detector results
            auto detections = device->getOutputQueue("detections",1,false);
            auto detections2 = device->getOutputQueue("detections2",1,false);
            
            // if preview image is requested. True in this case.
            if (getPreview) preview = device->getOutputQueue("preview",1,false);
            
            // if depth images are requested. All images.
            if (useDepth) depthQueue = device->getOutputQueue("depth", 1, false);
            
            int countd;

            if (getPreview)
            {
                auto imgFrames = preview->tryGetAll<dai::ImgFrame>();
                countd = imgFrames.size();
                if (countd > 0) {
                    auto imgFrame = imgFrames[countd-1];
                    if(imgFrame){
                        frame = toMat(imgFrame->getData(), imgFrame->getWidth(), imgFrame->getHeight(), 3, 1);
                    }
                }
            }
        
            vector<std::shared_ptr<dai::ImgFrame>> imgDepthFrames,imgDispFrames,imgMonoRFrames,imgMonoLFrames;
            std::shared_ptr<dai::ImgFrame> imgDepthFrame,imgDispFrame,imgMonoRFrame,imgMonoLFrame;
            
            int count;
            // In this case we allocate before Texture2D (ARGB32) and memcpy pointer data 
            if (useDepth)
            {   
                // Depth         
                imgDepthFrames = depthQueue->tryGetAll<dai::ImgFrame>();
                count = imgDepthFrames.size();
                if (count > 0)
                {
                    imgDepthFrame = imgDepthFrames[count-1];
                    depthFrameOrig = imgDepthFrame->getFrame();
                    cv::normalize(depthFrameOrig, depthFrame, 255, 0, cv::NORM_INF, CV_8UC1);
                    cv::equalizeHist(depthFrame, depthFrame);
                    cv::cvtColor(depthFrame, depthFrame, cv::COLOR_GRAY2BGR);
                }
            }

            // Face detection results
            struct Detection {
                unsigned int label;
                float score;
                float x_min;
                float y_min;
                float x_max;
                float y_max;
            };

            vector<Detection> dets;

            auto det = detections->get<dai::NNData>();
            std::vector<float> detData = det->getFirstLayerFp16();
            float maxScore = 0.0;
            int maxPos = 0;

            nlohmann::json facesArr = {};
            nlohmann::json bestFace = {};

            if(detData.size() > 0){
                int i = 0;
                while (detData[i*7] != -1.0f && i*7 < (int)detData.size()) {
                    
                    Detection d;
                    d.label = detData[i*7 + 1];
                    d.score = detData[i*7 + 2];
                    if (d.score > maxScore) 
                    {
                        maxScore = d.score;
                        maxPos = i;
                    }
                    d.x_min = detData[i*7 + 3];
                    d.y_min = detData[i*7 + 4];
                    d.x_max = detData[i*7 + 5];
                    d.y_max = detData[i*7 + 6];
                    i++;
                    dets.push_back(d);

                    nlohmann::json face;
                    face["label"] = d.label;
                    face["score"] = d.score;
                    face["xmin"] = d.x_min;
                    face["ymin"] = d.y_min;
                    face["xmax"] = d.x_max;
                    face["ymax"] = d.y_max;
                    int x1 = d.x_min * frame.cols;
                    int y1 = d.y_min * frame.rows;
                    int x2 = d.x_max * frame.cols;
                    int y2 = d.y_max * frame.rows;
                    int mx = x1 + ((x2 - x1) / 2);
                    int my = y1 + ((y2 - y1) / 2);
                    face["xcenter"] = mx;
                    face["ycenter"] = my;
                    
                    if (faceScoreThreshold <= d.score) 
                    {
                        if (getPreview && countd > 0 && drawAllFacesInPreview) cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(255,255,255));
                        if (useDepth && count>0)
                        {
                            auto spatialData = computeDepth(mx,my,frame.rows,depthFrameOrig); 

                            for(auto depthData : spatialData) {
                                auto roi = depthData.config.roi;
                                roi = roi.denormalize(depthFrame.cols, depthFrame.rows);

                                face["X"] = (int)depthData.spatialCoordinates.x;
                                face["Y"] = (int)depthData.spatialCoordinates.y;
                                face["Z"] = (int)depthData.spatialCoordinates.z;
                            }
                        }
                        facesArr.push_back(face);
                    }
                }
            }
            int i = 0;
            for(const auto& d : dets){
                if (i == maxPos)
                {                    
                    int x1 = d.x_min * 300;//frame.cols;
                    int y1 = d.y_min * 300;//frame.rows;
                    int x2 = d.x_max * 300;//frame.cols;
                    int y2 = d.y_max * 300;//frame.rows;
                    int mx = x1 + ((x2 - x1) / 2);
                    int my = y1 + ((y2 - y1) / 2);

                    // m_mx = mx;
                    // m_my = my;

                    if (faceScoreThreshold <= d.score)
                    {
                        bestFace["label"] = d.label;
                        bestFace["score"] = d.score;
                        bestFace["xmin"] = d.x_min;
                        bestFace["ymin"] = d.y_min;
                        bestFace["xmax"] = d.x_max;
                        bestFace["ymax"] = d.y_max;
                        bestFace["xcenter"] = mx;
                        bestFace["ycenter"] = my;

                        if (useDepth && count>0)
                        {
                            auto spatialData = computeDepth(mx,my,frame.rows,depthFrameOrig); 

                            for(auto depthData : spatialData) {
                                auto roi = depthData.config.roi;
                                roi = roi.denormalize(depthFrame.cols, depthFrame.rows);

                                bestFace["X"] = (int)depthData.spatialCoordinates.x;
                                bestFace["Y"] = (int)depthData.spatialCoordinates.y;
                                bestFace["Z"] = (int)depthData.spatialCoordinates.z;
                            }
                        }

                        if (getPreview && countd > 0 && drawBestFaceInPreview) cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(255,255,255));
                    }
                }
                i++;
            }


            vector<Detection> dets2;

            auto det2 = detections2->get<dai::NNData>();
            std::vector<float> detData2 = det2->getFirstLayerFp16();
            float maxScore2 = 0.0;
            int maxPos2 = 0;

            nlohmann::json facesArr2 = {};
            nlohmann::json bestFace2 = {};

            if(detData2.size() > 0){
                int i = 0;
                while (detData2[i*7] != -1.0f && i*7 < (int)detData2.size()) {
                    
                    Detection d;
                    d.label = detData2[i*7 + 1];
                    d.score = detData2[i*7 + 2];
                    if (d.score > maxScore2) 
                    {
                        maxScore2 = d.score;
                        maxPos2 = i;
                    }
                    d.x_min = detData2[i*7 + 3];
                    d.y_min = detData2[i*7 + 4];
                    d.x_max = detData2[i*7 + 5];
                    d.y_max = detData2[i*7 + 6];
                    i++;
                    dets2.push_back(d);

                    nlohmann::json face;
                    face["label"] = d.label;
                    face["score"] = d.score;
                    face["xmin"] = d.x_min;
                    face["ymin"] = d.y_min;
                    face["xmax"] = d.x_max;
                    face["ymax"] = d.y_max;
                    int x1 = d.x_min * frame.cols;
                    int y1 = d.y_min * frame.rows;
                    int x2 = d.x_max * frame.cols;
                    int y2 = d.y_max * frame.rows;
                    int mx = x1 + ((x2 - x1) / 2);
                    int my = y1 + ((y2 - y1) / 2);
                    face["xcenter"] = mx;
                    face["ycenter"] = my;
                    
                    if (faceScoreThreshold <= d.score) 
                    {
                        if (getPreview && countd > 0 && drawAllFacesInPreview) cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(255,255,255));
                        if (useDepth && count>0)
                        {
                            auto spatialData = computeDepth(mx,my,frame.rows,depthFrameOrig); 

                            for(auto depthData : spatialData) {
                                auto roi = depthData.config.roi;
                                roi = roi.denormalize(depthFrame.cols, depthFrame.rows);

                                face["X"] = (int)depthData.spatialCoordinates.x;
                                face["Y"] = (int)depthData.spatialCoordinates.y;
                                face["Z"] = (int)depthData.spatialCoordinates.z;
                            }
                        }
                        facesArr2.push_back(face);
                    }
                }
            }
            int i2 = 0;
            for(const auto& d : dets2){
                if (i2 == maxPos2)
                {                    
                    int x1 = d.x_min * 300;//frame.cols;
                    int y1 = d.y_min * 300;//frame.rows;
                    int x2 = d.x_max * 300;//frame.cols;
                    int y2 = d.y_max * 300;//frame.rows;
                    int mx = x1 + ((x2 - x1) / 2);
                    int my = y1 + ((y2 - y1) / 2);

                    // m_mx = mx;
                    // m_my = my;

                    if (faceScoreThreshold <= d.score)
                    {
                        bestFace2["label"] = d.label;
                        bestFace2["score"] = d.score;
                        bestFace2["xmin"] = d.x_min;
                        bestFace2["ymin"] = d.y_min;
                        bestFace2["xmax"] = d.x_max;
                        bestFace2["ymax"] = d.y_max;
                        bestFace2["xcenter"] = mx;
                        bestFace2["ycenter"] = my;

                        if (useDepth && count>0)
                        {
                            auto spatialData = computeDepth(mx,my,frame.rows,depthFrameOrig); 

                            for(auto depthData : spatialData) {
                                auto roi = depthData.config.roi;
                                roi = roi.denormalize(depthFrame.cols, depthFrame.rows);

                                bestFace2["X"] = (int)depthData.spatialCoordinates.x;
                                bestFace2["Y"] = (int)depthData.spatialCoordinates.y;
                                bestFace2["Z"] = (int)depthData.spatialCoordinates.z;
                            }
                        }

                        if (getPreview && countd > 0 && drawBestFaceInPreview) cv::rectangle(frame, cv::Rect(cv::Point(300+x1, y1), cv::Point(300+x2, y2)), cv::Scalar(255,255,255));
                    }
                }
                i2++;
            }

            if (getPreview && countd>0) toARGB(frame,frameInfo->colorPreviewData);

            faceDetectorJson["faces"] = facesArr;
            faceDetectorJson["best"] = bestFace;
            faceDetectorJson["faces2"] = facesArr2;
            faceDetectorJson["best2"] = bestFace2;

            // SYSTEM INFORMATION
            if (retrieveInformation) faceDetectorJson["sysinfo"] = GetDeviceInfo(device);        
            // IMU
            if (useIMU) faceDetectorJson["imu"] = GetIMU(device);

            char* ret = (char*)::malloc(strlen(faceDetectorJson.dump().c_str())+1);
            ::memcpy(ret, faceDetectorJson.dump().c_str(),strlen(faceDetectorJson.dump().c_str()));
            ret[strlen(faceDetectorJson.dump().c_str())] = 0;

            return ret;
        }

        char* ret = (char*)::malloc(strlen("{\"error\":\"DEVICE_NOT_RUNNING\"}"));
        ::memcpy(ret, "{\"error\":\"DEVICE_NOT_RUNNING\"}",strlen("{\"error\":\"DEVICE_NOT_RUNNING\"}"));
        ret[strlen("{\"error\":\"DEVICE_NOT_RUNNING\"}")] = 0;
        return ret;
    }


}