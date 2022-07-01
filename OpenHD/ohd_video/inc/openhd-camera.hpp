#ifndef OPENHD_CAMERA_H
#define OPENHD_CAMERA_H

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "openhd-camera-enums.hpp"
#include "json.hpp"
#include "openhd-log.hpp"
#include "openhd-util.hpp"
#include "openhd-util-filesystem.hpp"
#include "openhd-settings.hpp"


struct CameraEndpoint {
  std::string device_node;
  std::string bus;
  bool support_h264 = false;
  bool support_h265 = false;
  bool support_mjpeg = false;
  bool support_raw = false;
  std::vector<std::string> formats;
  // Consti10: cleanup- an endpoint that supports nothing, what the heck should
  // we do with that ;)
  [[nodiscard]] bool supports_anything() const {
    return (support_h264 || support_h265 || support_mjpeg || support_raw);
  }
};

static constexpr auto DEFAULT_BITRATE_KBITS = 5000;

// Return true if the bitrate is considered sane, false otherwise
static bool check_bitrate_sane(const int bitrateKBits) {
  if (bitrateKBits <= 100 || bitrateKBits > (1024 * 1024 * 50)) {
    return false;
  }
  return true;
}

// User-selectable camera options
// These values are settings that can change dynamically at run time
// (non-deterministic)
struct CameraSettings {
  // The video format selected by the user. If the user sets a video format that
  // isn't supported (for example, he might select h264|1920x1080@120 but the
  // camera can only do 60fps) The stream should default to the first available
  // video format. If no video format is available, it should default to
  // h264|640x480@30.
  VideoFormat userSelectedVideoFormat{VideoCodec::H264, 640, 480, 30};
  // All these are for the future, and probably implemented on a best effort
  // approach- e.g. changing them does not neccessarly mean the camera supports
  // changing them, and they are too many to do it in a "check if supported"
  // manner.
  //--
  // The bitrate the generated stream should have. Note that not all cameras /
  // encoder support a constant bitrate, and not all encoders support all
  // bitrates, especially really low ones.
  int bitrateKBits = DEFAULT_BITRATE_KBITS;
  // Only for network cameras (CameraTypeIP) URL in the rtp:// ... or similar
  // form
  std::string url;
  // enable/disable recording to file
  bool enableAirRecordingToFile = false;
  // todo they are simple for the most part, but rn not implemented yet.
  /*std::string brightness;
  std::string contrast;
  std::string sharpness;
  std::string rotate;
  std::string wdr;
  std::string denoise;
  std::string thermal_palette;
  std::string thermal_span;*/
  [[nodiscard]] nlohmann::json to_json()const{
    nlohmann::json j{{"userSelectedVideoFormat",userSelectedVideoFormat.to_json()},{"bitrateKBits",bitrateKBits},
                     {"url",url},"enableAirRecordingToFile",enableAirRecordingToFile};
    return j;
  }
  static CameraSettings from_json(const nlohmann::json& j){
    CameraSettings ret;
    nlohmann::json tmp;
    j.at("userSelectedVideoFormat").get_to(tmp);
    ret.userSelectedVideoFormat=VideoFormat::from_json(tmp);
    j.at("bitrateKBits").get_to(ret.bitrateKBits);
    j.at("url").get_to(ret.url);
    j.at("enableAirRecordingToFile").get_to(ret.enableAirRecordingToFile);
    return ret;
  }
};

struct Camera {
  CameraType type = CameraType::Unknown;
  std::string name = "unknown";
  std::string vendor = "unknown";
  std::string vid;
  std::string pid;
  // for USB this is the bus number, for CSI it's the connector number
  std::string bus;
  // Unique index of this camera, should start at 0. The index number depends on
  // the order the cameras were picked up during the discovery step.
  int index = 0;
  // All the endpoints supported by this camera.
  std::vector<CameraEndpoint> endpoints;
  /**
   * For logging, create a quick name string that gives developers enough info
   * such that they can figure out what this camera is.
   * @return verbose string.
   */
  [[nodiscard]] std::string debugName() const {
    std::stringstream ss;
    ss << name << "|" << camera_type_to_string(type);
    return ss.str();
  }
  [[nodiscard]] std::string to_string() const {
    std::stringstream ss;
    ss << "Camera" << index << "{" << camera_type_to_string(type) << ""
       << "}";
    return ss.str();
  }
};

static const std::string VIDEO_SETTINGS_DIRECTORY=std::string(BASE_PATH)+std::string("video/");

class CameraHolder{
 public:
  explicit CameraHolder(Camera camera):_camera(std::move(camera)){
    if(!OHDFilesystemUtil::exists(VIDEO_SETTINGS_DIRECTORY.c_str())){
      OHDFilesystemUtil::create_directory(VIDEO_SETTINGS_DIRECTORY);
    }
    const auto last_settings_opt=read_last_settings();
    if(last_settings_opt.has_value()){
      _settings=std::make_unique<CameraSettings>(last_settings_opt.value());
      std::cout<<"Found settings\n";
    }else{
      std::cout<<"Creating default settings\n";
      // create default settings and persist them for the next reboot
      _settings=std::make_unique<CameraSettings>();
      persist_settings();
    }
  }
  [[nodiscard]] const Camera& get_camera()const{
    return _camera;
  }
  [[nodiscard]] CameraSettings& get_settings()const{
    assert(_settings);
    return *_settings;
  }
 private:
  // Camera info is immutable
  const Camera _camera;
  std::mutex _settings_mutex;
  std::unique_ptr<CameraSettings> _settings;
  [[nodiscard]] std::string create_uniqe_hash()const{
    return _camera.name;
  }
  [[nodiscard]] std::string create_unique_filename()const{
    return VIDEO_SETTINGS_DIRECTORY+create_uniqe_hash();
  }
  // write settings locally for persistence
  void persist_settings()const{
    assert(_settings);
    const auto filename=create_unique_filename();
    const auto tmp=_settings->to_json();
    // and write them locally for persistence
    std::ofstream t(filename);
    t << tmp.dump(4);
    t.close();
  }
  // read last settings, if they are available
  [[nodiscard]] std::optional<CameraSettings> read_last_settings()const{
    const auto filename=create_unique_filename();
    if(!OHDFilesystemUtil::exists(filename.c_str())){
      return std::nullopt;
    }
    std::ifstream f(filename);
    nlohmann::json j;
    f >> j;
    return CameraSettings::from_json(j);
  }
};

using DiscoveredCameraList = std::vector<Camera>;


static nlohmann::json cameras_to_json(const DiscoveredCameraList &cameras) {
  nlohmann::json j;
  for (const auto &camera : cameras) {
    try {
      nlohmann::json endpoints = nlohmann::json::array();
      int endpoint_index = 0;
      for (const auto &_endpoint : camera.endpoints) {
        // Now this is for safety, the code by stephen was buggy in this regard
        // Aka why the heck should a camera have endpoints that are not even
        // related to it ???!! If this assertion fails, we need to check the
        // discovery step.
        assert(camera.bus == _endpoint.bus);
        // also, a camera without a endpoint - what the heck should that be
        if (camera.endpoints.empty()) {
          std::cerr << "to json Warning Camera without endpoints\n";
        }
        endpoints[endpoint_index] = {{"device_node", _endpoint.device_node},
                                     {"support_h264", _endpoint.support_h264},
                                     {"support_h265", _endpoint.support_h265},
                                     {"support_mjpeg", _endpoint.support_mjpeg},
                                     {"support_raw", _endpoint.support_raw},
                                     {"formats", _endpoint.formats}};
        endpoint_index++;
      }
      nlohmann::json _camera = {
          {"type", camera_type_to_string(camera.type)},
          {"name", camera.name},
          {"vendor", camera.vendor},
          {"vid", camera.vid},
          {"pid", camera.pid},
          {"bus", camera.bus},
          {"index", camera.index},
          {"endpoints", endpoints}
      };
      std::stringstream message;
      message << "Detected camera: " << camera.name << std::endl;
      ohd_log(STATUS_LEVEL::INFO, message.str());
      j.push_back(_camera);
    } catch (std::exception &ex) {
      std::cerr << "exception: " << ex.what() << std::endl;
    }
  }
  return j;
}

static constexpr auto CAMERA_MANIFEST_FILENAME = "/tmp/camera_manifest";

static void write_camera_manifest(const DiscoveredCameraList &cameras) {
  auto manifest = cameras_to_json(cameras);
  std::ofstream _t(CAMERA_MANIFEST_FILENAME);
  _t << manifest.dump(4);
  _t.close();
}

static Camera createDummyCamera() {
  Camera camera;
  camera.name = "DummyCamera";
  camera.index = 0;
  camera.vendor = "dummy";
  camera.type = CameraType::Dummy;
  return camera;
}

static std::shared_ptr<CameraHolder> createDummyCamera2(){
  return std::make_shared<CameraHolder>(createDummyCamera());
}

#endif
