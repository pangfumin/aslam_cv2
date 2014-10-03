#include <aslam/pipeline/visual-pipeline-brisk.h>

#include <aslam/frames/visual-frame.h>
#include <aslam/pipeline/undistorter.h>
#include <brisk/brisk.h>
#include <glog/logging.h>

namespace aslam {

BriskVisualPipeline::BriskVisualPipeline() {
  // Just for serialization. Not meant to be used.
}

BriskVisualPipeline::BriskVisualPipeline(const Camera::ConstPtr& camera, bool copy_images,
                                         size_t octaves, double uniformity_radius,
                                         double absolute_threshold, size_t max_number_of_keypoints,
                                         bool rotation_invariant, bool scale_invariant)
    : VisualPipeline(camera, camera, copy_images) {
  initializeBrisk(octaves, uniformity_radius, absolute_threshold, max_number_of_keypoints,
                  rotation_invariant, scale_invariant);
}

BriskVisualPipeline::BriskVisualPipeline(std::unique_ptr<Undistorter>& preprocessing,
                                         bool copy_images, size_t octaves, double uniformity_radius,
                                         double absolute_threshold, size_t max_number_of_keypoints,
                                         bool rotation_invariant, bool scale_invariant)
    : VisualPipeline(preprocessing, copy_images) {
  initializeBrisk(octaves, uniformity_radius, absolute_threshold, max_number_of_keypoints,
                  rotation_invariant, scale_invariant);
}

BriskVisualPipeline::~BriskVisualPipeline() { }

void BriskVisualPipeline::initializeBrisk(size_t octaves,
                                          double uniformity_radius,
                                          double absolute_threshold,
                                          size_t max_number_of_keypoints,
                                          bool rotation_invariant,
                                          bool scale_invariant) {
  octaves_ = octaves;
  uniformity_radius_ = uniformity_radius;
  absolute_threshold_ = absolute_threshold;
  max_number_of_keypoints_ = max_number_of_keypoints;
  rotation_invariant_ = rotation_invariant;
  scale_invariant_ = scale_invariant;
#if __arm__
  // \TODO(slynen): Currently no Harris on ARM. Adapt if we port it to ARM.
  static const int AstThreshold = 70;
  detector_.reset(new brisk::BriskFeatureDetector(AstThreshold) );
#else
  detector_.reset(
      new brisk::ScaleSpaceFeatureDetector<brisk::HarrisScoreCalculator>(
          octaves_, uniformity_radius_, absolute_threshold_, max_number_of_keypoints_)
  );
#endif
  extractor_.reset(new brisk::BriskDescriptorExtractor(rotation_invariant_,
                                                       scale_invariant_));
}

void BriskVisualPipeline::processFrameImpl(const cv::Mat& image,
                                           VisualFrame* frame) const {
  CHECK_NOTNULL(frame);
  // Now we use the image from the frame. It might be undistorted.
  std::vector <cv::KeyPoint> keypoints;
  detector_->detect(image, keypoints);

  cv::Mat descriptors;
  if(!keypoints.empty()) {
    extractor_->compute(image, keypoints, descriptors);
  } else {
    descriptors = cv::Mat(0,0,CV_8UC1);
    LOG(WARNING) << "Frame produced no keypoints:\n" << *frame;
  }
  // Note: It is important that
  //       (a) this happens after the descriptor extractor as the extractor
  //           may remove keypoints; and
  //       (b) the values are set even if there are no keypoints as downstream
  //           code may rely on the keypoints being set.
  CHECK_EQ(descriptors.type(), CV_8UC1);
  CHECK(descriptors.isContinuous());
  frame->setDescriptors(
      // Switch cols/rows as Eigen is col-major and cv::Mat is row-major
      Eigen::Map<VisualFrame::DescriptorsT>(descriptors.data,
                                            descriptors.cols,
                                            descriptors.rows)
  );

  Eigen::Matrix2Xd ikeypoints(2, keypoints.size());
  Eigen::VectorXd  scales(keypoints.size());
  Eigen::VectorXd  orientations(keypoints.size());
  Eigen::VectorXd  scores(keypoints.size());
  // \TODO(ptf) Who knows a good formula for uncertainty based on octave?
  //            See https://github.com/ethz-asl/aslam_cv2/issues/73
  for(size_t i = 0; i < keypoints.size(); ++i) {
    const cv::KeyPoint& kp = keypoints[i];
    ikeypoints(0,i)  = kp.pt.x;
    ikeypoints(1,i)  = kp.pt.y;
    scales[i]        = kp.size;
    orientations[i]  = kp.angle;
    scores[i]        = kp.response;
  }
  frame->swapKeypointMeasurements(&ikeypoints);
  frame->swapKeypointScores(&scores);
  frame->swapKeypointOrientations(&orientations);
  frame->swapKeypointScales(&scales);
  frame->setDescriptorSizeBytes(extractor_->descriptorSize());
}

}  // namespace aslam
