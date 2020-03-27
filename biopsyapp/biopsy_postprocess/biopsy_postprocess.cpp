/*******
*
* Copyright(c)<2018>, <Huawei Technologies Co.,Ltd>
*
* @version 1.0
*
* @date 2018-5-19
*/
#include "biopsy_postprocess.h"
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>
#include <regex>
#include <cmath>

#include "hiaiengine/log.h"
#include "hiaiengine/data_type_reg.h"
#include "ascenddk/ascend_ezdvpp/dvpp_process.h"

using hiai::Engine;
using namespace std::__cxx11;
using namespace ascend::presenter;

// register custom data type
HIAI_REGISTER_DATA_TYPE("FaceRecognitionInfo", FaceRecognitionInfo);
HIAI_REGISTER_DATA_TYPE("FaceRectangle", FaceRectangle);
HIAI_REGISTER_DATA_TYPE("FaceImage", FaceImage);

// constants
namespace {
//// parameters for drawing box and label begin////
// face box color
const uint8_t kFaceBoxColorR = 255;
const uint8_t kFaceBoxColorG = 190;
const uint8_t kFaceBoxColorB = 0;

// face box border width
const int kFaceBoxBorderWidth = 2;

// face label color
const uint8_t kFaceLabelColorR = 255;
const uint8_t kFaceLabelColorG = 255;
const uint8_t kFaceLabelColorB = 0;

// face label font
const double kFaceLabelFontSize = 0.7;
const int kFaceLabelFontWidth = 2;

// face label text prefix
const std::string kFaceLabelTextPrefix = "Face:";
const std::string kFaceLabelTextSuffix = "%";
//// parameters for drawing box and label end////

// port number range
const int32_t kPortMinNumber = 0;
const int32_t kPortMaxNumber = 65535;

// confidence range
const float kConfidenceMin = 0.0;
const float kConfidenceMax = 1.0;

// face detection function return value
const int32_t kFdFunSuccess = 0;
const int32_t kFdFunFailed = -1;

// need to deal results when index is 2
const int32_t kDealResultIndex = 2;

// each results size
const int32_t kEachResultSize = 7;

// attribute index
const int32_t kAttributeIndex = 1;

// score index
const int32_t kScoreIndex = 2;

// anchor_lt.x index
const int32_t kAnchorLeftTopAxisIndexX = 3;

// anchor_lt.y index
const int32_t kAnchorLeftTopAxisIndexY = 4;

// anchor_rb.x index
const int32_t kAnchorRightBottomAxisIndexX = 5;

// anchor_rb.y index
const int32_t kAnchorRightBottomAxisIndexY = 6;

// face attribute
const float kAttributeFaceLabelValue = 1.0;
const float kAttributeFaceDeviation = 0.00001;

// percent
const int32_t kScorePercent = 100;

const int32_t kScreenW = 1280;
const int32_t kScreenH = 720;

// IP regular expression
const std::string kIpRegularExpression =
    "^((25[0-5]|2[0-4]\\d|[1]{1}\\d{1}\\d{1}|[1-9]{1}\\d{1}|\\d{1})($|(?!\\.$)\\.)){4}$";

// channel name regular expression
const std::string kChannelNameRegularExpression = "[a-zA-Z0-9/]+";
}

biopsy_postprocess::biopsy_postprocess() {
  fd_post_process_config_ = nullptr;
  presenter_channel_ = nullptr;
}

/**
* @ingroup hiaiengine
* @brief HIAI_DEFINE_PROCESS : implementaion of the engine
* @[in]: engine name and the number of input
*/
HIAI_StatusT biopsy_postprocess::Init(
    const hiai::AIConfig &config,
    const std::vector<hiai::AIModelDescription> &model_desc) {
    // get configurations
    if (fd_post_process_config_ == nullptr) {
      fd_post_process_config_ = std::make_shared<FaceDetectionPostConfig>();
    }
    // get parameters from graph.config
    for (int index = 0; index < config.items_size(); index++) {
      const ::hiai::AIConfigItem& item = config.items(index);
      const std::string& name = item.name();
      const std::string& value = item.value();
      std::stringstream ss;
      ss << value;
      if (name == "Confidence") {
        ss >> (*fd_post_process_config_).confidence;
        // validate confidence
        if (IsInvalidConfidence(fd_post_process_config_->confidence)) {
          HIAI_ENGINE_LOG(HIAI_GRAPH_INVALID_VALUE,
                          "Confidence=%s which configured is invalid.",
                          value.c_str());
          return HIAI_ERROR;
        }
      } else if (name == "PresenterIp") {
        // validate presenter server IP
        if (IsInValidIp(value)) {
          HIAI_ENGINE_LOG(HIAI_GRAPH_INVALID_VALUE,
                          "PresenterIp=%s which configured is invalid.",
                          value.c_str());
          return HIAI_ERROR;
        }
        ss >> (*fd_post_process_config_).presenter_ip;
      } else if (name == "PresenterPort") {
        ss >> (*fd_post_process_config_).presenter_port;
        // validate presenter server port
        if (IsInValidPort(fd_post_process_config_->presenter_port)) {
          HIAI_ENGINE_LOG(HIAI_GRAPH_INVALID_VALUE,
                          "PresenterPort=%s which configured is invalid.",
                          value.c_str());
          return HIAI_ERROR;
        }
      } else if (name == "ChannelName") {
        // validate channel name
        if (IsInValidChannelName(value)) {
          HIAI_ENGINE_LOG(HIAI_GRAPH_INVALID_VALUE,
                          "ChannelName=%s which configured is invalid.",
                          value.c_str());
          return HIAI_ERROR;
        }
        ss >> (*fd_post_process_config_).channel_name;
      }
      // else : nothing need to do
    }
    // call presenter agent, create connection to presenter server
    uint16_t u_port = static_cast<uint16_t>(fd_post_process_config_
        ->presenter_port);
    OpenChannelParam channel_param = { fd_post_process_config_->presenter_ip,
        u_port, fd_post_process_config_->channel_name, ContentType::kVideo };
    Channel *chan = nullptr;
    PresenterErrorCode err_code = OpenChannel(chan, channel_param);
    // open channel failed
    if (err_code != PresenterErrorCode::kNone) {
      HIAI_ENGINE_LOG(HIAI_GRAPH_INIT_FAILED,
                      "Open presenter channel failed, error code=%d", err_code);
      return HIAI_ERROR;
    }

    presenter_channel_.reset(chan);
    HIAI_ENGINE_LOG(HIAI_DEBUG_INFO, "End initialize!");
    return HIAI_OK;
}

bool biopsy_postprocess::IsInValidIp(const std::string &ip) {
  regex re(kIpRegularExpression);
  smatch sm;
  return !regex_match(ip, sm, re);
}

bool biopsy_postprocess::IsInValidPort(int32_t port) {
  return (port <= kPortMinNumber) || (port > kPortMaxNumber);
}

bool biopsy_postprocess::IsInValidChannelName(
    const std::string &channel_name) {
  regex re(kChannelNameRegularExpression);
  smatch sm;
  return !regex_match(channel_name, sm, re);
}

bool biopsy_postprocess::IsInvalidConfidence(float confidence) {
  return (confidence <= kConfidenceMin) || (confidence > kConfidenceMax);
}

bool biopsy_postprocess::IsInvalidResults(float attr, float score,
                                                const Point &point_lt,
                                                const Point &point_rb) {
  // attribute is not face (background)
  if (std::abs(attr - kAttributeFaceLabelValue) > kAttributeFaceDeviation) {
    return true;
  }

  // confidence check
  if ((score < fd_post_process_config_->confidence)
      || IsInvalidConfidence(score)) {
    return true;
  }

  // rectangle position is a point or not: lt == rb
  if ((point_lt.x == point_rb.x) && (point_lt.y == point_rb.y)) {
    return true;
  }
  return false;
}



bool biopsy_postprocess::IsSupportFormat(hiai::IMAGEFORMAT format) {
  return format == hiai::YUV420SP;
}

HIAI_StatusT biopsy_postprocess::ConvertImage(hiai::ImageData<u_int8_t>& org_img) {
  hiai::IMAGEFORMAT format = org_img.format;
  if (!IsSupportFormat(format)){
    HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                    "Format %d is not supported!", format);
    return HIAI_ERROR;
  }

  uint32_t width = org_img.width;
  uint32_t height = org_img.height;
  uint32_t img_size = org_img.size;

  // parameter
  ascend::utils::DvppToJpgPara dvpp_to_jpeg_para;
  dvpp_to_jpeg_para.format = JPGENC_FORMAT_NV12;
  dvpp_to_jpeg_para.level = 100;//控制质量
  dvpp_to_jpeg_para.resolution.height = height;
  dvpp_to_jpeg_para.resolution.width = width;
  ascend::utils::DvppProcess dvpp_to_jpeg(dvpp_to_jpeg_para);
  

  // call DVPP
  ascend::utils::DvppOutput dvpp_output;
  int32_t ret = dvpp_to_jpeg.DvppOperationProc(reinterpret_cast<char*>(org_img.data.get()),
                                                img_size, &dvpp_output);

  // failed, no need to send to presenter
  if (ret != 0) {
    HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                    "Failed to convert YUV420SP to JPEG, skip it.");
    return HIAI_ERROR;
  }

  // reset the data in img_vec
  org_img.data.reset(dvpp_output.buffer, default_delete<uint8_t[]>());
  org_img.size = dvpp_output.size;

  return HIAI_OK;
}

HIAI_StatusT biopsy_postprocess::HandleResults(
    const std::shared_ptr<FaceRecognitionInfo> &inference_res) {
    HIAI_StatusT status = HIAI_OK;
    std::vector<FaceImage> face_img_vec = inference_res->face_imgs;
    ConvertImage(inference_res->org_img);//转换为jpeg格式
    // check send result
    ascend::presenter::proto::PresentImageRequest data;
    data.set_format(ascend::presenter::proto::ImageFormat::kImageFormatJpeg); 
    data.set_width(kScreenW);
    data.set_height(kScreenH);
    unique_ptr<google::protobuf::Message> resp;
    data.set_data(string((char *)inference_res->org_img.data.get(),
		inference_res->org_img.size));
    if(face_img_vec.size() == 1){
		ascend::presenter::proto::Rectangle_Attr *lxre=nullptr;
		ascend::presenter::proto::Coordinate *po=nullptr;
		lxre = data.add_rectangle_list();
		lxre->mutable_left_top()->set_x(face_img_vec[0].rectangle.lt.x);
		lxre->mutable_left_top()->set_y(face_img_vec[0].rectangle.lt.y);
		lxre->mutable_right_bottom()->set_x(face_img_vec[0].rectangle.rb.x);
		lxre->mutable_right_bottom()->set_y(face_img_vec[0].rectangle.rb.y);   
		string s="";
		s = s +  "pitch:" + to_string(inference_res->face_imgs[0].infe_res.head_pose[0]);
		s = s + ",yaw:" + to_string(inference_res->face_imgs[0].infe_res.head_pose[1]);
		s = s + ",roll:" + to_string(inference_res->face_imgs[0].infe_res.head_pose[2]);
		lxre->set_label_text(s);
		for(int i=0;i<68;i++){
			po = data.add_point_list();
			if (face_img_vec[0].infe_res.face_points[i].x<0)
				face_img_vec[0].infe_res.face_points[i].x = 0;
			if (face_img_vec[0].infe_res.face_points[i].y<0)
				face_img_vec[0].infe_res.face_points[i].y = 0;
			po->set_x(face_img_vec[0].infe_res.face_points[i].x);
			po->set_y(face_img_vec[0].infe_res.face_points[i].y);
    	}
    }    
    PresenterErrorCode error_code = presenter_channel_->SendMessage(data, resp);
    return status;
}

HIAI_IMPL_ENGINE_PROCESS("biopsy_postprocess", biopsy_postprocess,INPUT_SIZE)
{
    if (arg0 == nullptr) {
    HIAI_ENGINE_LOG(HIAI_ENGINE_RUN_ARGS_NOT_RIGHT,
                    "Failed to process invalid message.");
    return HIAI_ERROR;
  }
  std::shared_ptr<FaceRecognitionInfo> inference_res = std::static_pointer_cast<
      FaceRecognitionInfo>(arg0);
  return HandleResults(inference_res);
  //return HIAI_OK;
}
