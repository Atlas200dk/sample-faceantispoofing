/*******
*
* Copyright(c)<2018>, <Huawei Technologies Co.,Ltd>
*
* @version 1.0
*
* @date 2018-5-19
*/
#include "biopsy_inference.h"
#include "hiaiengine/log.h"
#include "hiaiengine/data_type_reg.h"
#include "ascenddk/ascend_ezdvpp/dvpp_process.h"
#include <memory>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>

using namespace ascend::utils;
using hiai::Engine;
using namespace std;
using namespace hiai;
using namespace cv;

namespace {
// The image's width need to be resized
const int32_t kResizedImgWidth = 224;

// The image's height need to be resized
const int32_t kResizedImgHeight = 224;

const int32_t kEachResult1Size = 68;//特征点结果数
const int32_t kEachResult2Size = 3;//姿态结果数
// inference output result index
const int32_t kResult1Index = 0;
const int32_t kResult2Index = 1;

// The rgb image's channel number
const int32_t kRgbChannel = 3;

// For each input, the result should be one tensor
const int32_t kEachResultTensorNum = 10;

// The center's size for the inference result
const float kNormalizedCenterData = 0.5;

const int32_t kSendDataIntervalMiss = 20;
}
shared_ptr<FaceRecognitionInfo> face_recognition_info2;

/**
* @ingroup hiaiengine
* @brief HIAI_DEFINE_PROCESS : implementaion of the engine
* @[in]: engine name and the number of input
*/
HIAI_StatusT biopsy_inference::Init(const hiai::AIConfig &config,
    const std::vector<hiai::AIModelDescription> &model_desc)
{
    if (!InitAiModel(config)) {
        return HIAI_ERROR;
    }

    return HIAI_OK;
}
bool biopsy_inference::InitAiModel(const AIConfig &config) {
    AIStatus ret = SUCCESS;

    // Define the initialization value for the ai_model_manager_
    if (ai_model_manager_ == nullptr) {
        ai_model_manager_ = make_shared<AIModelManager>();
    }

    vector<AIModelDescription> model_desc_vec;
    AIModelDescription model_desc;

    // Get the model information from the file graph.config
    for (int index = 0; index < config.items_size(); ++index) {
        const AIConfigItem &item = config.items(index);
        if (item.name() == kModelPathParamKey) {
        const char *model_path = item.value().data();
        model_desc.set_path(model_path);
        } else if (item.name() == kBatchSizeParamKey) {
        stringstream ss(item.value());
        ss >> batch_size_;
        } else {
        continue;
        }
    }

    //Invoke the framework's interface to init the information
    model_desc_vec.push_back(model_desc);
    ret = ai_model_manager_->Init(config, model_desc_vec);

    if (ret != SUCCESS) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "AI model init failed!");
        return false;
    }
    return true;
}


bool biopsy_inference::Crop(const shared_ptr<FaceRecognitionInfo> &face_recognition_info, const ImageData<u_int8_t> &org_img,
                                  vector<FaceImage> &face_imgs) {
  HIAI_ENGINE_LOG("Begin to crop the face, face number is %d",
                  face_imgs.size());
  int32_t img_size = org_img.size;
  for (vector<FaceImage>::iterator face_img_iter = face_imgs.begin();
       face_img_iter != face_imgs.end(); ++face_img_iter) {
    // call ez_dvpp to crop image
    DvppBasicVpcPara crop_para;
    crop_para.input_image_type = face_recognition_info->frame.org_img_format;

    // Change the left top coordinate to even numver 将左上角坐标改变为偶数
    u_int32_t lt_horz = ((face_img_iter->rectangle.lt.x) >> 1) << 1;
    u_int32_t lt_vert = ((face_img_iter->rectangle.lt.y) >> 1) << 1;
    // Change the left top coordinate to odd numver 将右下角坐标改变为奇数
    u_int32_t rb_horz = (((face_img_iter->rectangle.rb.x) >> 1) << 1) - 1;
    u_int32_t rb_vert = (((face_img_iter->rectangle.rb.y) >> 1) << 1) - 1;
    HIAI_ENGINE_LOG("The crop is from left-top(%d,%d) to right-bottom(%d,%d)",
                    lt_horz, lt_vert, rb_horz, rb_vert);
    // 偶数减去奇数再加1，还是偶数
    u_int32_t cropped_width = rb_horz - lt_horz + 1;
    u_int32_t cropped_height = rb_vert - lt_vert + 1;

    crop_para.src_resolution.width = org_img.width;
    crop_para.src_resolution.height = org_img.height;
    crop_para.dest_resolution.width = cropped_width;
    crop_para.dest_resolution.height = cropped_height;
    crop_para.crop_left = lt_horz;
    crop_para.crop_right = rb_horz;
    crop_para.crop_up = lt_vert;
    crop_para.crop_down = rb_vert;

    // The align flag for input and output data,output data should be aligned 对齐标志。裁剪后输出的数据需要是对齐的
    crop_para.is_input_align = face_recognition_info->frame.img_aligned;
    crop_para.is_output_align = false;
    DvppProcess dvpp_crop_img(crop_para);
    DvppVpcOutput dvpp_output;
    int ret = dvpp_crop_img.DvppBasicVpcProc(
                org_img.data.get(), img_size, &dvpp_output);
    if (ret != kDvppOperationOk) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "Call ez_dvpp failed, failed to crop image.");
      return false;
    }

    // 最终给faceimgs结构填充图像内容
    face_img_iter->image.data.reset(dvpp_output.buffer, default_delete<u_int8_t[]>());
    face_img_iter->image.size = dvpp_output.size;
    face_img_iter->image.width = cropped_width;
    face_img_iter->image.height = cropped_height;
    //std::cout<<"size:"<<face_img_iter->image.size<<" width"<<face_img_iter->image.width<<" height"<<face_img_iter->image.height<<std::endl;
  }
  return true;
}

bool biopsy_inference::Resize(const vector<FaceImage> &face_imgs,
                                    vector<ImageData<u_int8_t>> &resized_imgs) {
  // Begin to resize all the resize image
  for (vector<FaceImage>::const_iterator face_img_iter = face_imgs.begin();
       face_img_iter != face_imgs.end(); ++face_img_iter) {

    int32_t img_size = face_img_iter->image.size;
    if (img_size <= 0) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "image size less than or equal zero, size=%d", img_size);
      return false;
    }

    // 人脸图像的真实宽、高
    int32_t origin_width = face_img_iter->image.width;
    int32_t origin_height = face_img_iter->image.height;
    ImageData<u_int8_t> resized_image;
    DvppBasicVpcPara resize_para;

    resize_para.input_image_type = face_recognition_info2->frame.org_img_format;//INPUT_YUV420_SEMI_PLANNER_UV;
    resize_para.crop_left = 0;
    resize_para.crop_up = 0;
    resize_para.crop_right = origin_width - 1;
    resize_para.crop_down = origin_height - 1;
    resize_para.src_resolution.width = origin_width;
    resize_para.src_resolution.height = origin_height;

    resize_para.dest_resolution.width = kResizedImgWidth;
    resize_para.dest_resolution.height = kResizedImgHeight;
    //resize_para.is_input_align = true;
    resize_para.is_output_align = false;
    resize_para.is_input_align = face_recognition_info2->frame.img_aligned;
    DvppProcess dvpp_resize_img(resize_para);

    // Invoke EZ_DVPP interface to resize image
    DvppVpcOutput dvpp_output;
    int ret = dvpp_resize_img.DvppBasicVpcProc(
                face_img_iter->image.data.get(), img_size, &dvpp_output);
    if (ret != kDvppOperationOk) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "Call ez_dvpp failed, failed to resize image.");
      return false;
    }

    // call success, set data and size
    resized_image.data.reset(dvpp_output.buffer, default_delete<u_int8_t[]>());
    resized_image.size = dvpp_output.size;
    resized_image.width = kResizedImgWidth;
    resized_image.height = kResizedImgHeight;
    resized_imgs.push_back(resized_image);
  }
  return true;
}

bool biopsy_inference::ImageYUV2BGR (
  const vector<ImageData<u_int8_t>> &resized_image,
  vector<Mat> &bgr_image) {
  for (vector<ImageData<u_int8_t>>::const_iterator resized_img_iter = resized_image.begin();
       resized_img_iter != resized_image.end(); ++resized_img_iter) {

    int img_height = resized_img_iter->height;
    int img_width = resized_img_iter->width;
    Mat src(img_height * kNv12SizeMolecule / kNv12SizeDenominator,
            img_width, CV_8UC1);

    int copy_size = img_width * img_height * kNv12SizeMolecule
                    / kNv12SizeDenominator;
    int destination_size = src.cols * src.rows * src.elemSize();
    int ret = memcpy_s(src.data, destination_size, resized_img_iter->data.get(),
                       copy_size);
    CHECK_MEM_OPERATOR_RESULTS(ret);
    Mat dst_temp;
    cvtColor(src, dst_temp, CV_YUV2BGR_NV12);
    Mat dst;
    dst_temp.convertTo(dst, CV_32FC3);
    bgr_image.push_back(dst);
  }
  return true;
}

bool biopsy_inference::Inference(vector<Mat> &normalized_image,
                                       vector<FaceImage> &face_imgs) {
  // Define the ai model's data
  AIContext ai_context;

  int normalized_image_size = normalized_image.size();
  int normalized_image_mod = normalized_image_size % batch_size_;

  // calcuate the iter number
  // calcuate the value by batch
  int iter_num = normalized_image_mod == 0 ?
                 (normalized_image_size / batch_size_) : (normalized_image_size / batch_size_ + 1);

  // Invoke interface to do the inference
  for (int i = 0; i < iter_num; i++) {
    HIAI_ENGINE_LOG("Batch data's number is %d!", i);
    int start_index = batch_size_ * i;
    int end_index = start_index + batch_size_;

    // Last group data, need to fulfill the extra data
    // fulfill with last Mat in the vector
    if (i == iter_num - 1 && normalized_image_mod != 0) {
      int fulfill_number = batch_size_ - normalized_image_mod;
      EnrichDataByLastMat(normalized_image, fulfill_number);
      end_index = i * batch_size_ + normalized_image_mod;
    }

    float *tensor_buffer = new(std::nothrow) float[batch_size_ * kResizedImgWidth * kResizedImgHeight * kRgbChannel];
    if (tensor_buffer == nullptr) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "New the tensor buffer error.");
      return false;
    }
    int last_size = CopyDataToBuffer(normalized_image, start_index, tensor_buffer);

    if (last_size == -1) {
      return false;
    }

    vector<shared_ptr<IAITensor>> input_data_vec;
    shared_ptr<AINeuralNetworkBuffer> neural_buffer = shared_ptr <
        AINeuralNetworkBuffer > (new(std::nothrow) AINeuralNetworkBuffer());
    shared_ptr<IAITensor> input_data = static_pointer_cast <
                                       IAITensor > (neural_buffer);
    neural_buffer->SetBuffer((void *)tensor_buffer,  last_size * sizeof(float));
    input_data_vec.push_back(input_data);

    vector<shared_ptr<IAITensor>> output_data_vec;
    AIStatus ret = ai_model_manager_->CreateOutputTensor(input_data_vec, output_data_vec);
    if (ret != SUCCESS) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "Fail to create output tensor");
      delete [] tensor_buffer;
      return false;
    }

    ret = ai_model_manager_->Process(ai_context, input_data_vec, output_data_vec, 0);

    if (ret != SUCCESS) {
      HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                      "Fail to process the data in FWK");
      delete [] tensor_buffer;
      return false;
    }
    std::cout<<"output data vec size:"<<output_data_vec.size();

    HIAI_ENGINE_LOG("Inference successed!");

    // Get the inference result.
    shared_ptr<AISimpleTensor> result_tensor = static_pointer_cast <
        AISimpleTensor > (output_data_vec[kResult1Index]);//第0路输出 136个float
    shared_ptr<hiai::AISimpleTensor> result_tensor1 = static_pointer_cast <
        AISimpleTensor > (output_data_vec[kResult2Index]);//第1路输出 3个float

    // copy data to float array
    int32_t size = result_tensor->GetSize() / sizeof(float);
    int32_t size1 = result_tensor1->GetSize() / sizeof(float);
        
    float result[size];
    errno_t mem_ret = memcpy_s(result, sizeof(result), result_tensor->GetBuffer(),
                                result_tensor->GetSize());
    if (mem_ret != EOK) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "post process call memcpy_s() error=%d", mem_ret);
        return false;
    }
    int32_t k_points = 0;
    int x=face_imgs[i].rectangle.lt.x,y=face_imgs[i].rectangle.lt.y,z=face_imgs[i].rectangle.rb.x,w=face_imgs[i].rectangle.rb.y; 
	while(k_points < kEachResult1Size)
    {
      face_imgs[i].infe_res.face_points[k_points].x = result[k_points * 2]*(z - x)*0.5 + 0.5*(x + z);
      face_imgs[i].infe_res.face_points[k_points].y = result[(k_points * 2) + 1]*(w - y)*0.5 + 0.5*(y + w);
      k_points += 1;
    }

    float result1[size1];
    errno_t mem_ret1 = memcpy_s(result1, sizeof(result1), result_tensor1->GetBuffer(),
                                result_tensor1->GetSize());
    if (mem_ret1 != EOK) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "post process call memcpy_s() error=%d", mem_ret1);
        return false;
    }
    int32_t k_pose = 0;
    while(k_pose < kEachResult2Size)
    {
      face_imgs[i].infe_res.head_pose[k_pose] = result1[k_pose] * 50;
      k_pose += 1;
    }
    input_data_vec.clear();
    output_data_vec.clear();
    delete [] tensor_buffer;
  }
  return true;
}
//-- end region


void biopsy_inference::EnrichDataByLastMat(vector<Mat> &normalized_image,
    int number) {
  Mat last_mat = normalized_image[normalized_image.size() - 1];
  for (int i = 0; i < number; i++) {
    normalized_image.push_back(last_mat);
  }
}

int biopsy_inference::CopyDataToBuffer(vector<Mat> &normalized_image,
    int start_index, float *tensor_buffer) {

  int last_size = 0;
  for (int i = start_index; i < start_index + batch_size_; i++) {

    // Split the normalized data by opencv
    vector<Mat> temp_splited_image;
    split(normalized_image[i], temp_splited_image);

    // Copy data to buffer
    for (vector<Mat>::iterator
         splited_data_iter = temp_splited_image.begin();
         splited_data_iter != temp_splited_image.end(); ++splited_data_iter) {

      // Change data type to float
      (*splited_data_iter).convertTo(*splited_data_iter, CV_32FC3);
      //(*splited_data_iter).convertTo(*splited_data_iter, CV_8U);
      int splited_data_length = (*splited_data_iter).rows * (*splited_data_iter).cols;
      int each_size = splited_data_length * sizeof(float);
      int ret = memcpy_s(tensor_buffer + last_size, each_size, (*splited_data_iter).ptr<float>(0), each_size);
      if (ret != EOK) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "memory operation failed in the feature mask's CopyDataToBuffer, error=%d", ret);
        return 0;
      }

      // Calcuate the memory's end position
      last_size += splited_data_length;
    }
  }
  return last_size;
}

bool biopsy_inference::IsDataHandleWrong(shared_ptr<FaceRecognitionInfo> &face_detail_info) {
  ErrorInfo err_info = face_detail_info->err_info;
  if (err_info.err_code != AppErrorCode::kNone) {
    return false;
  }
  return true;
}

HIAI_StatusT biopsy_inference::SendFailed(const string error_log,
    shared_ptr<FaceRecognitionInfo> &face_recognition_info) {

  HIAI_ENGINE_LOG("VCNN network run error");
  ErrorInfo err_info = face_recognition_info->err_info;
  err_info.err_code = AppErrorCode::kFeatureMask;
  err_info.err_msg = error_log;
  HIAI_StatusT ret = HIAI_OK;
  do {
    ret = SendData(DEFAULT_DATA_PORT, "FaceRecognitionInfo",
                   static_pointer_cast<void>(face_recognition_info));
    if (ret == HIAI_QUEUE_FULL) {
      HIAI_ENGINE_LOG("Queue is full, sleep %d ms", HIAI_QUEUE_FULL);
      usleep(kSendDataIntervalMiss);
    }
  } while (ret == HIAI_QUEUE_FULL);

  return ret;
}

HIAI_StatusT biopsy_inference::SendSuccess(
  shared_ptr<FaceRecognitionInfo> &face_recognition_info) {

  HIAI_ENGINE_LOG("VCNN network run success, the total face is %d .",
                  face_recognition_info->face_imgs.size());
  HIAI_StatusT ret = HIAI_OK;
  do {
    ret = SendData(DEFAULT_DATA_PORT, "FaceRecognitionInfo",
                   static_pointer_cast<void>(face_recognition_info));
    if (ret == HIAI_QUEUE_FULL) {
      HIAI_ENGINE_LOG("Queue is full, sleep 200ms");
      usleep(kSendDataIntervalMiss);
    }
  } while (ret == HIAI_QUEUE_FULL);

  return ret;
}

HIAI_IMPL_ENGINE_PROCESS("biopsy_inference", biopsy_inference, INPUT_SIZE)
{
    // args is null, arg0 is image info, arg1 is model info
    if (nullptr == arg0) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "Fail to process invalid message, is null.");
        return HIAI_ERROR;
    }

    // Get the data from last Engine
    // If not correct, Send the message to next node directly
    shared_ptr<FaceRecognitionInfo> face_recognition_info = static_pointer_cast <
        FaceRecognitionInfo > (arg0);
    face_recognition_info2 = face_recognition_info;
    if (!IsDataHandleWrong(face_recognition_info)) {
        HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                        "The message status is not normal");
        SendData(DEFAULT_DATA_PORT, "FaceRecognitionInfo",
                static_pointer_cast<void>(face_recognition_info));
        return HIAI_ERROR;
    }
    // 没有检测到合格的人脸，所以不需要推理，直接发送结果给post process
    if (face_recognition_info->face_imgs.size() == 0) {
        HIAI_ENGINE_LOG("No face image need to be handled.");
        return SendSuccess(face_recognition_info);
    }
    
    // 根据face_imgs中存储的的人脸坐标，将人脸图像数据从原图像中扣出来放到face_imgs中的image中
    if (!Crop(face_recognition_info, face_recognition_info->org_img, face_recognition_info->face_imgs)) {
        return SendFailed("Crop all the data failed, all the data failed",
                        face_recognition_info);
    }
    
    // 将face_imgs的image图像resize到模型需要的大小，存放在resized_imgs中
    vector<ImageData<u_int8_t>> resized_imgs;
    if (!Resize(face_recognition_info->face_imgs, resized_imgs)) {
        return SendFailed("Resize all the data failed, all the data failed",
                        face_recognition_info);
    }

    
    vector<Mat> bgr_imgs;
    if (!ImageYUV2BGR(resized_imgs, bgr_imgs)) {
        return SendFailed("Convert all the data failed, all the data failed",
                        face_recognition_info);
    }

    // Inference the data
    bool inference_flag = Inference(bgr_imgs, face_recognition_info->face_imgs);
    if (!inference_flag) {
        return SendFailed("Inference the data failed",
                        face_recognition_info);
    }

    return SendSuccess(face_recognition_info);
    return HIAI_OK;
}