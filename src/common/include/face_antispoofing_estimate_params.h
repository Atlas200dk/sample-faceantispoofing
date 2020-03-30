#ifndef face_antispoofingParam_H_
#define face_antispoofingParam_H_

#include "hiaiengine/data_type.h"
#include "ascenddk/ascend_ezdvpp/dvpp_data_type.h"
// #include "hiaiengine/data_type_reg.h"
// #include "hiaiengine/status.h"
// #include <unistd.h>
// #include <sys/stat.h>

#define CHECK_MEM_OPERATOR_RESULTS(ret) \
if (ret != EOK) { \
  HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT, \
                  "memory operation failed, error=%d", ret); \
  return false; \
}

// NV12 image's transformation param
// The memory size of the NV12 image is 1.5 times that of width*height.
const int32_t kNv12SizeMolecule = 3;
const int32_t kNv12SizeDenominator = 2;

// model path parameter key in graph.config
const string kModelPathParamKey = "model_path";

// batch size parameter key in graph.config
const string kBatchSizeParamKey = "batch_size";

/**
 * @brief: face recognition APP error code definition
 */
enum class AppErrorCode {
  // Success, no error
  kNone = 0,

  // register engine failed
  kRegister,

  // detection engine failed
  kDetection,

  // feature mask engine failed
  kFeatureMask,

  // recognition engine failed 
  kRecognition
};

/**
 * @brief: frame information
 */
struct FrameInfo {
  uint32_t frame_id = 0;  // frame id
  uint32_t channel_id = 0;  // channel id for current frame
  uint32_t timestamp = 0;  // timestamp for current frame
  uint32_t image_source = 0;  // 0:Camera 1:Register
  std::string face_id = "";  // registered face id
  // original image format and rank using for org_img addition
  // IMAGEFORMAT defined by HIAI engine does not satisfy the dvpp condition
  VpcInputFormat org_img_format = INPUT_YUV420_SEMI_PLANNER_UV;
  bool img_aligned = false; // original image already aligned or not
  unsigned char *original_jpeg_pic_buffer; // ouput buffer
  unsigned int original_jpeg_pic_size; // size of output buffer
};

/**
 * @brief: serialize for FrameInfo
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, FrameInfo& data) {
  ar(data.frame_id, data.channel_id, data.timestamp, data.image_source,
     data.face_id, data.org_img_format, data.img_aligned);
}

/**
 * @brief: Error information
 */
struct ErrorInfo {
  AppErrorCode err_code = AppErrorCode::kNone;
  std::string err_msg = "";
};

/**
 * @brief: serialize for ErrorInfo
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, ErrorInfo& data) {
  ar(data.err_code, data.err_msg);
}

/**
 * @brief: face rectangle
 */
struct FaceRectangle {
  hiai::Point2D lt;  // left top
  hiai::Point2D rb;  // right bottom
};

/**
 * @brief: serialize for FacePoint
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, FaceRectangle& data) {
  ar(data.lt, data.rb);
}

/**
 * @brief: face feature
 */
struct FaceFeature {
  bool flag;
  hiai::Point2D left_eye;  // left eye
  hiai::Point2D right_eye;  // right eye
  hiai::Point2D nose;  // nose
  hiai::Point2D left_mouth;  // left mouth
  hiai::Point2D right_mouth;  // right mouth
};

/**
 * @brief: serialize for FaceFeature
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, FaceFeature& data) {
  ar(data.left_eye, data.right_eye, data.nose, data.left_mouth,
     data.right_mouth);
}

/**
 * @brief: face image inference result
 */
 struct InferenceResult {
   hiai::Point2D face_points[68];
   float head_pose[3];
};
/**
 * @brief: face image
 */
struct FaceImage {
  hiai::ImageData<u_int8_t> image;  // cropped image from original image
  FaceRectangle rectangle;  // face rectangle
  FaceFeature feature_mask;  // face feature mask
  std::vector<float> feature_vector;  // face feature vector
  float score = 0;
  InferenceResult infe_res;
};

/**
 * @brief: serialize for FaceImage
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, FaceImage& data) {
  ar(data.image, data.rectangle, data.feature_mask, data.feature_vector);
}

/**
 * @brief: information for face recognition
 */
struct FaceRecognitionInfo {
  FrameInfo frame;  // frame information
  ErrorInfo err_info;  // error information
  hiai::ImageData<u_int8_t> org_img;  // original image
  std::vector<FaceImage> face_imgs;  // cropped image
  int faces_size = -1;
  int presize = -1;
};

/**
 * @brief: serialize for FaceRecognitionInfo
 *         engine uses it to transfer data between host and device
 */
template<class Archive>
void serialize(Archive& ar, FaceRecognitionInfo& data) {
  ar(data.frame, data.err_info, data.org_img, data.face_imgs);
}

#endif /* FACE_RECOGNITION_PARAMS_H_ */
